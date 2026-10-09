/** Production frozen-join controls with independent fact-ID/cents expectations. */
import { join, resolve } from "node:path";
import { readFileSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import {
  Database,
  assert,
  digest,
  json,
  temporary,
  emitReport,
  ROOT,
  sqlString,
} from "./artifact_helpers.ts";
interface Field {
  name: string;
  datatype?: string;
  expression: { dialects: { dialect: string; expression: string }[] };
}
interface Core {
  name: string;
  datasets: {
    name: string;
    source: string;
    primary_key: string[];
    fields: Field[];
  }[];
  relationships: {
    name: string;
    from: string;
    to: string;
    from_columns: string[];
    to_columns: string[];
  }[];
  metrics: Field[];
}
const FIX = join(ROOT, "test/fixtures/joined-population");
const artifact = resolve(process.argv[2]!);
const fixtureSQL = readFileSync(
  join(ROOT, "test/fixtures/safe-join-research/rows.sql"),
  "utf8",
);
const expr = (sql: string) => ({
  dialects: [{ dialect: "ANSI_SQL", expression: sql }],
});
if (process.env.OSSIE_JOIN_CLOSE_CHILD === "1") {
  const db = await Database.open();
  await db.loadArtifact(artifact);
  await db.run(fixtureSQL);
  await db.run("CALL ossie_bundle_load(?)", [join(FIX, "bundle.json")]);
  const prepared = await db.connection.prepare(
    "SELECT * FROM ossie_objects('ValidUser',[])",
  );
  await prepared.runAndReadAll();
  prepared.destroySync();
  db.close();
  process.stdout.write("joined close ok\n");
  process.exit(0);
}
await temporary("ossie-frozen-join-", async (directory) => {
  const db = await Database.open();
  const version = await db.loadArtifact(artifact);
  let refusals = 0;
  const checked: string[] = [];
  const manifest = join(FIX, "bundle.json");
  const load = (path = manifest) =>
    db.one("SELECT * FROM ossie_bundle_load(?)", [path]);
  const total = (concept = "User") =>
    db.one("SELECT * FROM ossie_concept_query(?, ?, [], [])", [
      concept,
      ["record_count", "value_count", "amount_sum", "amount_avg"],
    ]);
  const ids = () =>
    db.rows(
      "SELECT entity_id,record_id FROM ossie_objects('ValidUser',[]) ORDER BY entity_id",
    );
  const baselineDocs = () =>
    [
      json<Core>(join(FIX, "core.json")),
      json<any>(join(FIX, "ontology.json")),
      json<any>(join(FIX, "mapping.json")),
    ] as const;
  function bundle(
    modify: (docs: ReturnType<typeof baselineDocs>, policy: any) => void,
  ) {
    const docs = baselineDocs();
    const policy = {
      fact: "fact",
      dimension: "dimension",
      relationship: "fact_dimension",
      type: "INNER",
      missing: "reject",
    };
    modify(docs, policy);
    const output: any = {
      profile: "fact-dimension-population-v1",
      join: policy,
    };
    for (const [i, kind] of ["core", "ontology", "mapping"].entries()) {
      const path = join(directory, kind + ".json");
      writeFileSync(path, JSON.stringify(docs[i], null, 2) + "\n");
      output[kind] = { path, name: docs[i]!.name, sha256: digest(path) };
    }
    const path = join(directory, "bundle.json");
    writeFileSync(path, JSON.stringify(output));
    return path;
  }
  async function reset() {
    await db.run(
      "DROP TABLE IF EXISTS r6_fact; DROP TABLE IF EXISTS r6_dimension;",
    );
    await db.run(fixtureSQL);
  }
  async function refuse(
    name: string,
    mutation: string,
    reason: RegExp,
    path = manifest,
  ) {
    const before = await db.rows(
      "SELECT snapshot_id,content_sha256 FROM ossie_concepts() ORDER BY concept",
    );
    if (mutation) await db.run(mutation);
    await assert.rejects(() => load(path), reason, name);
    assert.deepEqual(
      await db.rows(
        "SELECT snapshot_id,content_sha256 FROM ossie_concepts() ORDER BY concept",
      ),
      before,
      name + " keeps bundle",
    );
    assert.deepEqual(
      await total(),
      [6, 5, "55.01", 11.002],
      name + " old values",
    );
    refusals++;
    checked.push(name);
    await reset();
  }
  try {
    await reset();
    const first = await load();
    assert.equal(first[0], "fact-dimension-population-v1");
    assert.equal(first[3], 6);
    assert.deepEqual(await total(), [6, 5, "55.01", 11.002]);
    assert.deepEqual(
      await db.rows(
        'SELECT entity_id,record_id,"fact.dim_id","dimension.id" FROM ossie_objects(\'User\',[]) ORDER BY entity_id',
      ),
      [
        ["f1", "f1", 1, 1],
        ["f2", "f2", 1, 1],
        ["f3", "f3", 2, 2],
        ["f4", "f4", 2, 2],
        ["f5", "f5", 3, 3],
        ["f6", "f6", 3, 3],
      ],
    );
    assert.deepEqual(await total("ValidUser"), [4, 4, "24.98", 6.245]);
    assert.deepEqual(
      await ids(),
      ["f1", "f2", "f5", "f6"].map((id) => [id, id]),
    );
    assert.deepEqual(
      await db.rows(
        "SELECT * FROM ossie_concept_query('User',['record_count','amount_sum'],['dimension.label'],[]) ORDER BY 1",
      ),
      [
        ["North", 4, "24.98"],
        ["South", 2, "30.03"],
      ],
    );
    assert.deepEqual(
      await db.one(
        "SELECT * FROM ossie_concept_query('User',['eligible_amount'],[],[])",
      ),
      ["24.98"],
    );
    assert.deepEqual(
      await db.rows(
        "SELECT entity_id FROM ossie_objects('User',['dimension.label = ''South''']) ORDER BY entity_id",
      ),
      [["f3"], ["f4"]],
    );
    const meta = await db.one(
      "SELECT profile,execution_target,executed FROM ossie_concept_compile('User',['amount_sum'],[],[])",
    );
    assert.equal(meta[0], first[0]);
    assert.equal(meta[2], false);
    assert.match(String(meta[1]), /INNER\/reject/);
    const provenance = JSON.parse(String(meta[1]));
    assert.equal(provenance.metric_grain, "fact");
    assert.deepEqual(
      provenance.inputs.map((x: any) => x.rows),
      [6, 4],
    );
    assert(
      provenance.logical_fields.some(
        (x: any) => x.dataset === "dimension" && x.field === "label",
      ),
    );
    await assert.rejects(
      () =>
        db.run(
          "SELECT * FROM ossie_objects('User',['fact._ossie_field_0 = ''f1'''])",
        ),
      /unknown original/,
    );
    const prepared = await db.connection.prepare(
      "SELECT * FROM ossie_concept_query('User',['amount_sum'],[],[])",
    );
    await db.run(
      "UPDATE r6_fact SET amount=999 WHERE id='f1'; UPDATE r6_dimension SET label='Changed',eligible=false WHERE id=1;",
    );
    assert.deepEqual(await total(), [6, 5, "55.01", 11.002]);
    assert.deepEqual(
      await ids(),
      ["f1", "f2", "f5", "f6"].map((id) => [id, id]),
    );
    const second = await load();
    assert.notEqual(first[2], second[2]);
    assert.deepEqual((await prepared.runAndReadAll()).getRowsJson(), [
      ["55.01"],
    ]);
    prepared.destroySync();
    assert.deepEqual(await total("ValidUser"), [2, 2, "-5.05", -2.525]);
    await db.run("DROP TABLE r6_fact; DROP TABLE r6_dimension");
    assert.equal((await ids()).length, 2);
    assert.equal((await total())[0], 6);
    await reset();
    await load();
    const unusedBefore = (await load())[2];
    await db.run(
      "UPDATE r6_dimension SET label='Unreferenced change' WHERE id=4",
    );
    const unusedAfter = (await load())[2];
    assert.notEqual(unusedBefore, unusedAfter);
    assert.deepEqual(await total(), [6, 5, "55.01", 11.002]);
    await reset();
    await load();
    for (const [name, sql, reason] of [
      [
        "referenced duplicate",
        "INSERT INTO r6_dimension SELECT * FROM r6_dimension WHERE id=1",
        /dimension batch/,
      ],
      [
        "unused duplicate",
        "INSERT INTO r6_dimension SELECT * FROM r6_dimension WHERE id=4",
        /dimension batch/,
      ],
      [
        "NULL dimension",
        "INSERT INTO r6_dimension VALUES(NULL,'bad',true)",
        /dimension batch/,
      ],
      [
        "missing FK",
        "DELETE FROM r6_dimension WHERE id=3",
        /every fact foreign key/,
      ],
      [
        "NULL FK",
        "UPDATE r6_fact SET dim_id=NULL WHERE id='f1'",
        /nonnull fact foreign key/,
      ],
      [
        "out of scope FK",
        "UPDATE r6_fact SET dim_id=99 WHERE id='f3'",
        /every fact foreign key/,
      ],
      [
        "duplicate fact",
        "INSERT INTO r6_fact SELECT * FROM r6_fact WHERE id='f1'",
        /fact batch/,
      ],
      [
        "NULL identity",
        "UPDATE r6_fact SET id=NULL WHERE id='f1'",
        /fact batch/,
      ],
      [
        "key coercion",
        "ALTER TABLE r6_dimension ALTER id TYPE VARCHAR",
        /native INTEGER/,
      ],
      [
        "empty fact bad dimension",
        "DELETE FROM r6_fact; INSERT INTO r6_dimension VALUES(4,'bad',false)",
        /dimension batch/,
      ],
      ["empty dimension", "DELETE FROM r6_dimension", /every fact foreign key/],
      [
        "too many unused dimensions",
        "INSERT INTO r6_dimension SELECT i,'unused',true FROM range(5,10006) t(i)",
        /10000 rows/,
      ],
      [
        "joined output budget",
        "UPDATE r6_dimension SET label=repeat('x',3000000) WHERE id=1; INSERT INTO r6_fact SELECT 'large'||i,1,0,true FROM range(10) t(i)",
        /joined batch exceeds 16 MiB/,
      ],
      [
        "input bytes including unused",
        "UPDATE r6_dimension SET label=repeat('x',17000000) WHERE id=4",
        /batch exceeds 16 MiB/,
      ],
      [
        "different attached database",
        "ATTACH ':memory:' AS other; CREATE TABLE other.r6_dimension AS SELECT * FROM r6_dimension",
        /same underlying database/,
      ],
    ] as const) {
      const path =
        name === "different attached database"
          ? bundle(([core]) => {
              core.datasets[1]!.source = "other.main.r6_dimension";
            })
          : manifest;
      await refuse(name, sql, reason, path);
    }
    for (const [name, change, reason] of [
      [
        "dimension grain",
        (c: Core) => {
          c.metrics.push({
            name: "bad",
            expression: expr("COUNT(dimension.id)"),
          });
        },
        /designated/,
      ],
      [
        "hidden aggregate",
        (c: Core) => {
          c.datasets[0]!.fields[2]!.expression = expr("SUM(amount)");
        },
        /aggregate|function/,
      ],
      [
        "unknown aggregate",
        (c: Core) => {
          c.metrics[0]!.expression = expr(
            "SUM(fact.amount)+approx_count_distinct(dimension.id)",
          );
        },
        /function/,
      ],
      [
        "nested aggregate",
        (c: Core) => {
          c.metrics[0]!.expression = expr("SUM(SUM(fact.amount))");
        },
        /unsupported function or aggregate/,
      ],
      [
        "grainless count",
        (c: Core) => {
          c.metrics[1]!.expression = expr("COUNT(*)");
        },
        /unsupported function or aggregate/,
      ],
      [
        "reverse relation",
        (c: Core) => {
          c.relationships[0]!.from = "dimension";
          c.relationships[0]!.to = "fact";
        },
        /forward/,
      ],
    ] as const)
      await refuse(
        name,
        "",
        reason,
        bundle(([core]) => change(core)),
      );
    await refuse(
      "LEFT policy",
      "",
      /INNER\/reject/,
      bundle((_, policy) => {
        policy.type = "LEFT";
      }),
    );
    await refuse(
      "dimension identity",
      "",
      /plain declared single-column primary key/,
      bundle(([, , mapping]) => {
        mapping.concept_mappings[0].object_mappings[0].referent_mappings[0].expression =
          "dimension.label";
        mapping.concept_mappings[0].link_mappings[0].object_mapping.referent_mappings[0].expression =
          "dimension.label";
        mapping.concept_mappings[0].link_mappings[0].children[0].object_mapping.expression =
          "dimension.label";
      }),
    );
    await refuse(
      "same native source table",
      "",
      /two distinct native source tables/,
      bundle(([core]) => {
        core.datasets[1]!.source = core.datasets[0]!.source;
      }),
    );
    // Field names matching generated candidates cannot confuse physical and logical resolution.
    const unusual = bundle(([core, , mapping]) => {
      const f = core.datasets[0]!;
      f.fields[2]!.name = "_ossie_column_0";
      core.metrics.forEach((m) =>
        m.expression.dialects.forEach((d) => {
          d.expression = d.expression.replaceAll(
            "fact.amount",
            "fact._ossie_column_0",
          );
        }),
      );
      mapping.concept_mappings[0].link_mappings[0].children[1].object_mapping.expression =
        "fact._ossie_column_0";
      core.datasets.reverse();
    });
    await load(unusual);
    assert.deepEqual(await total(), [6, 5, "55.01", 11.002]);
    await load();
    await db.run("UPDATE r6_dimension SET label=NULL WHERE id=2");
    await load();
    assert.deepEqual(
      await db.rows(
        "SELECT * FROM ossie_concept_query('User',['record_count'],['dimension.label'],[]) ORDER BY 1",
      ),
      [
        ["North", 4],
        [null, 2],
      ],
    );
    await db.run("DELETE FROM r6_fact;DELETE FROM r6_dimension");
    await load();
    assert.deepEqual(await total(), [0, 0, null, null]);
    assert.deepEqual(await ids(), []);
    assert.deepEqual(
      await db.rows(
        "SELECT * FROM ossie_concept_query('User',['record_count'],['dimension.label'],[])",
      ),
      [],
    );
    await reset();
    await load();
    await db.run("BEGIN");
    await assert.rejects(() => load(), /outside an explicit transaction/);
    await db.run("ROLLBACK");
    const beforeCancel = await db.rows(
      "SELECT snapshot_id,content_sha256 FROM ossie_concepts() ORDER BY concept",
    );
    await db.run(
      "DELETE FROM r6_fact; INSERT INTO r6_fact SELECT 'cancel'||i,1,1,true FROM range(9000) t(i); UPDATE r6_dimension SET label=repeat('x',1024) WHERE id=1",
    );
    const cancelled = load();
    const timer = setTimeout(() => db.connection.interrupt(), 2);
    try {
      await assert.rejects(() => cancelled, /interrupt/i);
    } finally {
      clearTimeout(timer);
    }
    assert.deepEqual(
      await db.rows(
        "SELECT snapshot_id,content_sha256 FROM ossie_concepts() ORDER BY concept",
      ),
      beforeCancel,
    );
    assert.deepEqual(await total(), [6, 5, "55.01", 11.002]);
    await reset();
    const quoted = bundle(([core, , mapping], policy) => {
      core.datasets[0]!.name = 'Fact "Sales';
      core.datasets[1]!.name = 'Dim"Region';
      policy.fact = core.datasets[0]!.name;
      policy.dimension = core.datasets[1]!.name;
      const q = (name: string) => '"' + name.replaceAll('"', '""') + '"';
      core.relationships[0]!.from = policy.fact;
      core.relationships[0]!.to = policy.dimension;
      core.relationships[0]!.from_columns = ['key"a'];
      core.relationships[0]!.to_columns = ['key"a'];
      core.datasets[1]!.primary_key = ['key"a'];
      core.datasets[0]!.fields[1]!.expression = expr(q('key"a'));
      core.datasets[1]!.fields[0]!.expression = expr(q('key"a'));
      core.metrics.forEach((m) =>
        m.expression.dialects.forEach((d) => {
          d.expression = d.expression
            .replaceAll("fact.", q(policy.fact) + ".")
            .replaceAll("dimension.", q(policy.dimension) + ".");
        }),
      );
      const change = (object: any) => {
        if (object && typeof object === "object")
          for (const [key, value] of Object.entries(object)) {
            if (key === "expression" && typeof value === "string")
              object[key] = value
                .replaceAll("fact.", q(policy.fact) + ".")
                .replaceAll("dimension.", q(policy.dimension) + ".");
            else change(value);
          }
      };
      change(mapping);
    });
    await db.run(
      'ALTER TABLE r6_fact RENAME dim_id TO "key""a"; ALTER TABLE r6_dimension RENAME id TO "key""a"',
    );
    await load(quoted);
    assert.deepEqual(await total(), [6, 5, "55.01", 11.002]);
    assert.deepEqual(
      await ids(),
      ["f1", "f2", "f5", "f6"].map((id) => [id, id]),
    );
    const quoteMeta = JSON.parse(
      String(
        (
          await db.one(
            "SELECT execution_target FROM ossie_concept_compile('User',['amount_sum'],[],[])",
          )
        )[0],
      ),
    );
    assert.equal(quoteMeta.metric_grain, 'Fact "Sales');
    assert(quoteMeta.physical_columns.some((x: any) => x.column === 'key"a'));
    await reset();
    await load();
    const computed = bundle(([core]) => {
      core.datasets[0]!.fields[2]!.expression = expr(
        "amount + CAST(0 AS DECIMAL(18,2))",
      );
      core.datasets[1]!.fields[1]!.expression = expr(
        "CASE WHEN eligible THEN label ELSE NULL END",
      );
      core.metrics.push({
        name: "ratio",
        expression: expr("SUM(fact.amount)/NULLIF(COUNT(fact.id),0)"),
      });
    });
    await load(computed);
    assert.deepEqual(await total(), [6, 5, "55.01", 11.002]);
    const ratio = (
      await db.one("SELECT * FROM ossie_concept_query('User',['ratio'],[],[])")
    )[0];
    assert(typeof ratio === "number");
    assert(Math.abs(ratio - 55.01 / 6) < 1e-12);
    assert.deepEqual(
      await db.rows(
        "SELECT * FROM ossie_concept_query('User',['record_count'],['dimension.label'],[]) ORDER BY 1",
      ),
      [
        ["North", 4],
        [null, 2],
      ],
    );
    await load();
    const close = spawnSync(
      process.execPath,
      [resolve(import.meta.path), artifact],
      {
        env: { ...process.env, OSSIE_JOIN_CLOSE_CHILD: "1" },
        timeout: 10000,
        encoding: "utf8",
      },
    );
    assert.equal(close.status, 0, close.stderr);
    assert.match(close.stdout, /joined close ok/);
    emitReport({
      profile: "fact-dimension-population-v1",
      artifact_sha256: digest(artifact),
      version,
      fixture_sha256: digest(join(FIX, "core.json")),
      source_sha256: Object.fromEntries(
        [
          "scripts/verify_joined_population_artifact.ts",
          "scripts/artifact_helpers.ts",
          "test/fixtures/safe-join-research/rows.sql",
          "test/fixtures/joined-population/core.json",
          "test/fixtures/joined-population/ontology.json",
          "test/fixtures/joined-population/mapping.json",
          "test/fixtures/joined-population/bundle.json",
        ].map((path) => [path, digest(join(ROOT, path))]),
      ),
      refusals,
      checked,
      totals: [6, 5, "55.01", 11.002],
      valid_ids: ["f1", "f2", "f5", "f6"],
      source_updates_frozen: true,
      prepared_reload_frozen: true,
      unused_dimension_hash: true,
      parent_alive_close: true,
      cancellation_preserves_bundle: true,
      quoted_sources_and_columns: true,
      computed_fields_and_ratio: true,
      limits: "canonical input/result; not peak memory",
      concurrent_capture: "not deterministically injected by this verifier",
    });
  } finally {
    db.close();
  }
});

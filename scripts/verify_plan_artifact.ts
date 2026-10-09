/** Inspectable production AST and immutable provenance against a real stock binding. */
import { join, resolve } from "node:path";
import { readFileSync, writeFileSync } from "node:fs";
import {
  Database,
  assert,
  digest,
  json,
  ROOT,
  emitReport,
  temporary,
} from "./artifact_helpers.ts";
interface Expression {
  selected_dialect: string;
  available_dialects: string[];
  declared_sql: string;
  parsed_lowered_sql: string;
}
interface Plan {
  schema_version: string;
  phase: string;
  executed: boolean;
  physical_plan: boolean;
  sql_replayable: boolean;
  request_kind: string;
  profile: string;
  engine: { dialect: string; version: string; source_id: string };
  definitions: {
    core: { name: string; version: string; sha256: string };
    ontology: { name: string; version: string; sha256: string };
    mapping: { name: string; version: string; sha256: string };
    manifest_sha256: string;
  };
  snapshot: {
    id: string;
    content_sha256: string;
    captured_population_rows: number;
    filtered_rows_computed: boolean;
  };
  concept: {
    name: string;
    formal_rule: {
      owner: string;
      applied: boolean;
      declared_sql: string;
      resolved_definition_sql: string;
      input_language: string;
    };
  };
  identity: {
    dataset: string;
    logical_field: string;
    entity_equals_record: boolean;
  };
  grain: { population_input_dataset: string; declared_primary_key: string[] };
  capture_join: null | {
    relationship: string;
    from_dataset: string;
    to_dataset: string;
    from_column: string;
    to_column: string;
    type: string;
    missing: string;
    execution_stage: string;
  };
  inputs: {
    dataset: string;
    source: string;
    captured_rows: number;
    sha256: string;
  }[];
  metrics: { name: string; expression: Expression }[];
  declared_fields: {
    dataset: string;
    name: string;
    is_time: boolean;
    is_time_explicit: boolean;
    expression: Expression;
  }[];
  filters: { requested_sql: string[]; effective_where_sql: string | null };
  time: { range_inferred: boolean };
  generated_ast: {
    from_sql: string;
    projections: { ordinal: number; alias: string; sql: string }[];
    group_by_sql: string[];
  };
  result_contract: {
    ordering: string;
    groups: string;
    query_truncation: boolean;
  };
  limitations: string[];
}
export async function verify(artifact: string) {
  const db = await Database.open(),
    checks: string[] = [],
    observations: unknown[] = [];
  const check = (label: string, run: () => void) => {
    run();
    checks.push(label);
  };
  async function explain(
    concept: string,
    metrics: string[],
    dimensions: string[] = [],
    filters: string[] = [],
    trace = false,
  ) {
    const [sql, text, snapshot, hash] = await db.one(
      `SELECT sql,plan_json,snapshot_id,content_sha256 FROM ossie_concept_compile(?,?,?,?${trace ? ",include_trace=>true" : ""})`,
      [concept, metrics, dimensions, filters],
    );
    const plan = JSON.parse(String(text)) as Plan;
    check("versioned actual AST envelope", () => {
      assert.equal(plan.schema_version, "ossie-population-plan-v1");
      assert.equal(plan.phase, "generated semantic SELECT AST");
      assert.equal(plan.executed, false);
      assert.equal(plan.physical_plan, false);
      assert.equal(plan.sql_replayable, false);
      assert.equal(plan.snapshot.id, snapshot);
      assert.equal(plan.snapshot.content_sha256, hash);
      assert.equal(plan.snapshot.filtered_rows_computed, false);
      assert.deepEqual(plan.engine, {
        dialect: "DUCKDB",
        version: "v1.5.6",
        source_id: "069cc9f9b5",
      });
    });
    check("statement parts and ordered output", () => {
      assert(String(sql).includes(plan.generated_ast.from_sql));
      if (plan.filters.effective_where_sql !== null)
        assert(
          String(sql).includes("WHERE " + plan.filters.effective_where_sql),
        );
      else assert(!String(sql).includes("WHERE"));
      assert.deepEqual(
        plan.generated_ast.projections.map((p) => p.ordinal),
        plan.generated_ast.projections.map((_, i) => i + 1),
      );
      for (const p of plan.generated_ast.projections)
        assert(String(sql).includes(p.sql));
      for (const g of plan.generated_ast.group_by_sql)
        assert(String(sql).includes(g));
      assert.deepEqual(plan.filters.requested_sql, filters);
      assert.equal(plan.time.range_inferred, false);
    });
    observations.push({ sql, plan });
    return { sql: String(sql), text: String(text), plan };
  }
  try {
    const version = await db.loadArtifact(artifact);
    await db.run(
      "CREATE TABLE dws_gaap_di(ds DATE, product VARCHAR, region VARCHAR, user_type VARCHAR, user_id VARCHAR, gaap_val DECIMAL(18,2), ytd_gaap DECIMAL(18,2)); COPY dws_gaap_di FROM 'docs/research/ossie-req-001/gaap-boundary.csv' (HEADER)",
    );
    await db.run("CALL ossie_bundle_load(?)", [
      join(ROOT, "test/fixtures/population/bundle-10.json"),
    ]);
    const root = await explain("User", []);
    check("root objects have no formal restriction", () => {
      assert.equal(root.plan.request_kind, "objects");
      assert.equal(root.plan.concept.formal_rule.applied, false);
      assert.equal(root.plan.filters.effective_where_sql, null);
      assert.equal(root.plan.snapshot.captured_population_rows, 11);
      assert.equal(root.plan.capture_join, null);
      assert.deepEqual(root.plan.metrics, []);
      assert.deepEqual(
        root.plan.inputs.map((x) => [x.dataset, x.source, x.captured_rows]),
        [["dws_gaap_di", "memory.main.dws_gaap_di", 11]],
      );
      assert.deepEqual(root.plan.grain.declared_primary_key, ["user_id"]);
      assert.equal(root.plan.identity.logical_field, "user_id");
    });
    const where = [
      "User.product = 'X'",
      "User.date < DATE '2026-03-02' OR User.date >= DATE '2026-03-31'",
    ];
    const grouped = await explain(
      "ValidUser",
      ["GAAP_SUM", "record_count", "gaap_sum"],
      ["dws_gaap_di.ds"],
      where,
      true,
    );
    check("derived rule, canonical metrics and disjoint time predicate", () => {
      assert.equal(grouped.plan.concept.formal_rule.applied, true);
      assert.equal(
        grouped.plan.concept.formal_rule.declared_sql,
        "User.ytd_gaap >= 10",
      );
      assert(grouped.plan.filters.effective_where_sql!.includes(" OR "));
      assert(!grouped.plan.filters.effective_where_sql!.includes("User."));
      assert.deepEqual(
        grouped.plan.metrics.map((m) => m.name),
        ["gaap_sum", "record_count", "gaap_sum"],
      );
      assert.deepEqual(grouped.plan.generated_ast.group_by_sql, [
        "dws_gaap_di.ds",
      ]);
      assert.equal(grouped.plan.generated_ast.projections.length, 7);
      assert.deepEqual(
        grouped.plan.generated_ast.projections.slice(-3).map((p) => p.alias),
        [
          "_ossie_snapshot_id",
          "_ossie_content_sha256",
          "_ossie_ontology_sha256",
        ],
      );
    });
    await assert.rejects(
      () =>
        db.rows(
          "SELECT * FROM ossie_concept_query('ValidUser',['GAAP_SUM','record_count','gaap_sum'],['dws_gaap_di.ds'],[],include_trace=>true)",
        ),
      /duplicate column name/,
    );
    checks.push(
      "duplicate diagnostic projections do not imply executable table schema",
    );
    const executable = await explain(
      "ValidUser",
      ["GAAP_SUM", "record_count"],
      ["dws_gaap_di.ds"],
      where,
      true,
    );
    const actual = await db.connection.runAndReadAll(
      "SELECT * FROM ossie_concept_query('ValidUser',['GAAP_SUM','record_count'],['dws_gaap_di.ds'],['User.product = ''X''','User.date < DATE ''2026-03-02'' OR User.date >= DATE ''2026-03-31'''],include_trace=>true)",
    );
    check("actual output matches plan ordinals", () =>
      assert.deepEqual(
        actual.columnNames(),
        executable.plan.generated_ast.projections.map((p) => p.alias),
      ),
    );
    // Native values compared independently; this does not replay diagnostic SQL.
    const results = await db.rows(
      "SELECT * FROM ossie_concept_query('ValidUser',['record_count','gaap_sum'],['dws_gaap_di.ds'],['User.product = ''X''','User.date < DATE ''2026-03-02'' OR User.date >= DATE ''2026-03-31''']) ORDER BY 1",
    );
    check("hand-derived disjoint groups", () =>
      assert.deepEqual(results, [
        ["2026-03-01", 2, "10.00"],
        ["2026-03-31", 1, "50.00"],
        ["2026-04-01", 1, "100.00"],
      ]),
    );
    const ds = grouped.plan.declared_fields.find((f) => f.name === "ds")!;
    check("definition dialect and default time role retained", () => {
      assert.equal(ds.is_time, true);
      assert.equal(ds.is_time_explicit, false);
      assert.equal(ds.expression.selected_dialect, "OSSIE_SQL_2026");
      assert.equal(ds.expression.declared_sql, "ds");
      assert.equal(
        grouped.plan.definitions.core.sha256,
        digest(
          join(
            ROOT,
            "docs/research/ossie-req-001/proposed-definitions/gaap.core.json",
          ),
        ),
      );
      assert.equal(grouped.plan.definitions.ontology.name, "gaap_population");
      assert.equal(grouped.plan.definitions.mapping.name, "gaap_binding");
      assert.equal(grouped.plan.definitions.mapping.version, "0.2.0.dev0");
    });
    await db.run(
      "PREPARE retained_plan AS SELECT plan_json FROM ossie_concept_compile('ValidUser',['gaap_sum'],[],[])",
    );
    const old = String((await db.one("EXECUTE retained_plan"))[0]);
    await db.run("CALL ossie_bundle_load(?)", [
      join(ROOT, "test/fixtures/population/bundle-20.json"),
    ]);
    const newer = await explain("ValidUser", ["gaap_sum"]);
    check("prepared plan pins old definition and snapshot", () => {
      assert.equal(
        JSON.parse(old).concept.formal_rule.declared_sql,
        "User.ytd_gaap >= 10",
      );
      assert.equal(
        newer.plan.concept.formal_rule.declared_sql,
        "User.ytd_gaap >= 20",
      );
      assert.notEqual(JSON.parse(old).snapshot.id, newer.plan.snapshot.id);
    });
    const oldAfterReload = (await db.one("EXECUTE retained_plan"))[0];
    check("prepared statement remains immutable across reload", () =>
      assert.equal(oldAfterReload, old),
    );
    await db.run("DEALLOCATE retained_plan");
    await assert.rejects(() =>
      db.run("CALL ossie_bundle_load(?)", [
        join(ROOT, "test/fixtures/population/missing.json"),
      ]),
    );
    const retained = await explain("ValidUser", ["gaap_sum"]);
    check("failed load keeps plan exactly", () =>
      assert.equal(retained.text, newer.text),
    );
    await temporary("ossie-plan-definitions-", async (directory) => {
      const original = json<
        Record<string, { name: string; path: string; sha256: string }>
      >(join(ROOT, "test/fixtures/population/bundle-10.json"));
      const manifest: Record<string, unknown> = {
        profile: "gaap-scalar-population-v1",
      };
      for (const kind of ["core", "ontology", "mapping"]) {
        const path = resolve(
          ROOT,
          "test/fixtures/population",
          original[kind].path,
        );
        const doc = json<{
          name: string;
          datasets?: {
            fields: { name: string; dimension?: { is_time: boolean } }[];
          }[];
          metrics?: {
            name: string;
            expression: { dialects: { dialect: string; expression: string }[] };
          }[];
        }>(path);
        if (kind === "core") {
          doc.datasets![0].fields.find(
            (field) => field.name === "ds",
          )!.dimension = { is_time: false };
          doc
            .metrics!.find((metric) => metric.name === "gaap_sum")!
            .expression.dialects.unshift({
              dialect: "ANSI_SQL",
              expression: "SUM(dws_gaap_di.gaap_val) + 1000",
            });
        }
        const destination = join(directory, kind + ".json");
        writeFileSync(destination, JSON.stringify(doc));
        manifest[kind] = {
          name: doc.name,
          path: destination,
          sha256: digest(destination),
        };
      }
      const path = join(directory, "bundle.json");
      writeFileSync(path, JSON.stringify(manifest));
      await db.run("CALL ossie_bundle_load(?)", [path]);
      const choice = await explain("ValidUser", ["gaap_sum"]);
      check(
        "selected dialect differs from unused variant; explicit time opt-out",
        () => {
          assert.deepEqual(
            choice.plan.metrics[0].expression.available_dialects,
            ["ANSI_SQL", "OSSIE_SQL_2026"],
          );
          assert.equal(
            choice.plan.metrics[0].expression.selected_dialect,
            "OSSIE_SQL_2026",
          );
          assert.equal(
            choice.plan.metrics[0].expression.declared_sql,
            "SUM(dws_gaap_di.gaap_val)",
          );
          assert(!choice.sql.includes("1000"));
          const field = choice.plan.declared_fields.find(
            (field) => field.name === "ds",
          )!;
          assert.equal(field.is_time, false);
          assert.equal(field.is_time_explicit, true);
        },
      );
      // A file change cannot replace the already loaded definition provenance.
      writeFileSync(join(directory, "core.json"), "{}");
      const immutable = await explain("ValidUser", ["gaap_sum"]);
      check("explanation never rereads modified definition files", () =>
        assert.equal(immutable.text, choice.text),
      );
    });
    await db.run("DELETE FROM dws_gaap_di");
    await db.run("CALL ossie_bundle_load(?)", [
      join(ROOT, "test/fixtures/population/bundle-20.json"),
    ]);
    const empty = await explain("ValidUser", ["record_count"]);
    check("empty capture makes no filtered-count claim", () => {
      assert.equal(empty.plan.snapshot.captured_population_rows, 0);
      assert.equal(empty.plan.snapshot.filtered_rows_computed, false);
      assert.equal(empty.plan.inputs[0].captured_rows, 0);
    });
    await db.run("DROP TABLE dws_gaap_di");
    await db.run(
      readFileSync(
        join(ROOT, "test/fixtures/population-expressions/rows.sql"),
        "utf8",
      ),
    );
    await db.run("CALL ossie_bundle_load(?)", [
      join(ROOT, "test/fixtures/population-expressions/bundle.json"),
    ]);
    const expressions = await explain("User", [
      "ratio_of_sums",
      "distinct_region",
      "sum_ratio",
    ]);
    check("NULLIF division/DISTINCT/computed field provenance", () => {
      assert(
        expressions.plan.metrics[0].expression.declared_sql.includes("NULLIF"),
      );
      assert(
        expressions.plan.generated_ast.projections[0].sql.includes("nullif"),
      );
      assert(
        expressions.plan.generated_ast.projections[1].sql.includes("DISTINCT"),
      );
      assert(
        expressions.plan.metrics[2].expression.declared_sql.includes(
          "dws_gaap_di.ratio",
        ),
      );
      assert(
        !expressions.plan.generated_ast.projections[2].sql.includes(".ratio"),
      );
      assert.equal(expressions.plan.filters.effective_where_sql, null);
    });
    await db.run(
      readFileSync(
        join(ROOT, "test/fixtures/safe-join-research/rows.sql"),
        "utf8",
      ),
    );
    await db.run("CALL ossie_bundle_load(?)", [
      join(ROOT, "test/fixtures/joined-population/bundle.json"),
    ]);
    const joined = await explain(
      "ValidUser",
      ["amount_sum"],
      ["dimension.label"],
      ["User.label = 'North'"],
    );
    check("validated join inputs and original identity grain", () => {
      assert.equal(joined.plan.profile, "fact-dimension-population-v1");
      assert.deepEqual(
        joined.plan.inputs.map((i) => [i.dataset, i.source, i.captured_rows]),
        [
          ["fact", "memory.main.r6_fact", 6],
          ["dimension", "memory.main.r6_dimension", 4],
        ],
      );
      assert.equal(joined.plan.grain.population_input_dataset, "fact");
      assert.deepEqual(joined.plan.grain.declared_primary_key, ["id"]);
      assert.equal(joined.plan.identity.dataset, "fact");
      assert.equal(joined.plan.identity.logical_field, "id");
      assert.equal(joined.plan.capture_join!.from_dataset, "fact");
      assert.equal(joined.plan.capture_join!.to_dataset, "dimension");
      assert.equal(joined.plan.capture_join!.type, "INNER");
      assert.equal(joined.plan.capture_join!.missing, "reject");
      assert(
        joined.plan.capture_join!.execution_stage.includes(
          "no live query join",
        ),
      );
      assert(!joined.plan.generated_ast.from_sql.includes("r6_dimension"));
      assert(!joined.sql.includes("JOIN"));
    });
    check(
      "original expressions and flattened final references are separate",
      () => {
        assert.equal(
          joined.plan.metrics[0].expression.selected_dialect,
          "ANSI_SQL",
        );
        assert.equal(
          joined.plan.metrics[0].expression.declared_sql,
          "SUM(fact.amount)",
        );
        assert(
          joined.plan.generated_ast.projections[1].sql.includes(
            "_ossie_column_",
          ),
        );
        assert(
          joined.plan.filters.effective_where_sql!.includes("_ossie_column_"),
        );
        assert.equal(
          joined.plan.generated_ast.projections[0].alias,
          "dimension.label",
        );
        assert.equal(joined.plan.snapshot.captured_population_rows, 6);
      },
    );
    const joinedRows = await db.rows(
      "SELECT * FROM ossie_concept_query('ValidUser',['amount_sum'],['dimension.label'],['User.label = ''North'''])",
    );
    check("independent joined query on actual population", () =>
      assert.deepEqual(joinedRows, [["North", "24.98"]]),
    );
    return {
      scope:
        "Generated semantic SELECT AST and immutable definition/capture provenance; not physical plans or full R7/P0",
      engine: version,
      artifact_sha256: digest(artifact),
      source_sha256: Object.fromEntries(
        [
          "src/population/population.cpp",
          "scripts/artifact_helpers.ts",
          "test/sql/ossie_population_plan.test",
          "docs/research/ossie-req-001/gaap-boundary.csv",
          ".github/workflows/MainDistributionPipeline.yml",
          "scripts/verify_plan_artifact.ts",
          "test/fixtures/population/bundle-10.json",
          "test/fixtures/population/bundle-20.json",
          "test/fixtures/population-expressions/core.json",
          "test/fixtures/population-expressions/rows.sql",
          "test/fixtures/joined-population/bundle.json",
          "test/fixtures/safe-join-research/rows.sql",
        ].map((p) => [p, digest(join(ROOT, p))]),
      ),
      checks,
      observations,
    };
  } finally {
    db.close();
  }
}
if (import.meta.main) {
  try {
    assert(process.argv[2], "artifact path required");
    emitReport(await verify(resolve(process.argv[2])));
  } catch (error) {
    console.error(String(error));
    process.exitCode = 1;
  }
}

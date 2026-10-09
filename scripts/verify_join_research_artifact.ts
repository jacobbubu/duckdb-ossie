/** R6 research: core live behavior and SQL preflight oracle, not joined population support. */
import { readFileSync, writeFileSync, readdirSync } from "node:fs";
import { join, resolve, relative } from "node:path";
import {
  Database,
  ROOT,
  assert,
  digest,
  json,
  temporary,
  emitReport,
  type Cell,
} from "./artifact_helpers.ts";
const FIX = join(ROOT, "test/fixtures/safe-join-research");
interface Core {
  name: string;
  datasets: {
    name: string;
    primary_key?: string[];
    unique_keys?: string[][];
  }[];
  relationships: {
    name: string;
    from: string;
    to: string;
    from_columns: string[];
    to_columns: string[];
  }[];
}
interface Oracle {
  facts: {
    id: string;
    dimension: number;
    cents: number | null;
    active: boolean;
  }[];
  dimensions: { id: number; label: string | null; eligible: boolean }[];
}
const oracle = json<Oracle>(join(FIX, "oracle.json"));
const sum = (facts: Oracle["facts"]) => {
  const values = facts.filter((f) => f.cents !== null);
  if (!values.length) return null;
  const cents = values.reduce((a, f) => a + BigInt(f.cents!), 0n);
  const abs = cents < 0n ? -cents : cents;
  return `${cents < 0n ? "-" : ""}${abs / 100n}.${String(abs % 100n).padStart(2, "0")}`;
};
export async function verify(artifact: string) {
  const con = await Database.open();
  try {
    const version = await con.loadArtifact(artifact);
    await con.run(readFileSync(join(FIX, "rows.sql"), "utf8"));
    const load = (path: string) => con.run("CALL ossie_load(?)", [path]);
    await load(join(FIX, "core.json"));
    const query = (dims: string[] = [], filters: string[] = []) =>
      con.rows("SELECT * FROM ossie_query(?,?,?) ORDER BY 1", [
        ["record_count", "amount_sum"],
        dims,
        filters,
      ]);
    const joined = () => query(["dimension.label"]);
    // This fixed-fixture research helper is SQL, not an extension validation API.
    async function preflight() {
      const types = await con.rows(
        "SELECT table_name,data_type FROM information_schema.columns WHERE table_schema='main' AND ((table_name='r6_fact' AND column_name='dim_id') OR (table_name='r6_dimension' AND column_name='id')) ORDER BY table_name",
      );
      const reasons: string[] = [];
      if (
        types.length !== 2 ||
        types[0][1] !== types[1][1] ||
        types[0][1] !== "INTEGER"
      )
        reasons.push("exact_integer_key_type");
      const checks: [string, string][] = [
        ["fact_identity_null", "SELECT COUNT(*) FROM r6_fact WHERE id IS NULL"],
        [
          "fact_identity_duplicate",
          "SELECT COUNT(*) FROM (SELECT id FROM r6_fact GROUP BY id HAVING COUNT(*)>1)",
        ],
        [
          "dimension_key_null",
          "SELECT COUNT(*) FROM r6_dimension WHERE id IS NULL",
        ],
        [
          "dimension_key_duplicate",
          "SELECT COUNT(*) FROM (SELECT id FROM r6_dimension GROUP BY id HAVING COUNT(*)>1)",
        ],
        [
          "fact_foreign_key_null",
          "SELECT COUNT(*) FROM r6_fact WHERE dim_id IS NULL",
        ],
      ];
      if (!reasons.includes("exact_integer_key_type"))
        checks.push([
          "fact_foreign_key_unmatched",
          "SELECT COUNT(*) FROM r6_fact f WHERE f.dim_id IS NOT NULL AND NOT EXISTS (SELECT 1 FROM r6_dimension d WHERE f.dim_id=d.id)",
        ]);
      for (const [name, sql] of checks)
        if (Number((await con.one(sql))[0]) > 0) reasons.push(name);
      return reasons;
    }
    assert.deepEqual(await preflight(), []);
    const all = oracle.facts;
    assert.deepEqual(await query(), [[all.length, sum(all)]]);
    const labels = [
      ...new Set(
        oracle.dimensions
          .filter((d) => all.some((f) => f.dimension === d.id))
          .map((d) => d.label),
      ),
    ].sort();
    const expectedGroups = labels.map((label) => {
      const ids = oracle.dimensions
        .filter((d) => d.label === label)
        .map((d) => d.id);
      const facts = all.filter((f) => ids.includes(f.dimension));
      return [label, facts.length, sum(facts)];
    });
    assert.deepEqual(await joined(), expectedGroups);
    const nonNull = all.filter((f) => f.cents !== null);
    const exactCents = nonNull.reduce(
      (total, f) => total + BigInt(f.cents!),
      0n,
    );
    const average = Number(exactCents) / 100 / nonNull.length;
    const values = await con.one("SELECT * FROM ossie_query(?,?,?)", [
      ["value_count", "amount_avg"],
      [],
      [],
    ]);
    assert.equal(values[0], nonNull.length);
    assert(Math.abs(Number(values[1]) - average) <= 1e-12);
    const valueGroups = await con.rows(
      "SELECT * FROM ossie_query(?,?,?) ORDER BY 1",
      [["value_count", "amount_avg"], ["dimension.label"], []],
    );
    assert.deepEqual(valueGroups, [
      ["North", 4, 6.245],
      ["South", 1, 30.03],
    ]);
    const filter = "fact.active AND dimension.eligible";
    const selected = all.filter(
      (f) =>
        f.active &&
        oracle.dimensions.find((d) => d.id === f.dimension)!.eligible,
    );
    assert.deepEqual(await query([], [filter]), [
      [selected.length, sum(selected)],
    ]);
    // Explicitly qualify shared physical 'id'; a core aggregate dimension provides fact-ID drilldown.
    assert.deepEqual(
      await con.rows('SELECT "fact.id" FROM ossie_query(?,?,?) ORDER BY 1', [
        ["record_count"],
        ["fact.id"],
        [filter],
      ]),
      selected.map((f) => [f.id]),
    );
    assert.deepEqual(
      await con.rows(
        "SELECT f.id FROM r6_fact f JOIN r6_dimension d ON f.dim_id=d.id WHERE f.active AND d.eligible ORDER BY f.id",
      ),
      selected.map((f) => [f.id]),
    );
    const aggregate = await con.one("SELECT * FROM ossie_query(?,?,?)", [
      ["record_count", "amount_sum"],
      [],
      ["dimension.eligible"],
    ]);
    assert.deepEqual(aggregate, [4, "24.98"]);
    const preflightCases: {
      name: string;
      research_reasons: string[];
      core_joined_rows: Cell[][];
      core_fact_only_rows: Cell[][];
      actual_fact_rows: Cell;
      actual_join_rows: Cell;
      fact_id_multiplicity: Cell[][];
    }[] = [];
    async function bad(
      name: string,
      sql: string,
      reasons: string[],
      expectedGroups: Cell[][],
      factOnly: Cell[][] = [[6, "55.01"]],
    ) {
      await con.run("BEGIN");
      try {
        await con.run(sql);
        const found = await preflight();
        assert.deepEqual(found, reasons, name);
        const actual = await joined();
        assert.deepEqual(
          actual,
          [...expectedGroups].sort((a, b) =>
            String(a[0]).localeCompare(String(b[0])),
          ),
          name,
        );
        const raw = await query();
        assert.deepEqual(raw, factOnly, name);
        preflightCases.push({
          name,
          research_reasons: found,
          core_joined_rows: actual,
          core_fact_only_rows: raw,
          actual_fact_rows: (await con.one("SELECT COUNT(*) FROM r6_fact"))[0],
          actual_join_rows: (
            await con.one(
              "SELECT COUNT(*) FROM r6_fact f JOIN r6_dimension d ON f.dim_id=d.id",
            )
          )[0],
          fact_id_multiplicity: await con.rows(
            "SELECT f.id, COUNT(*) FROM r6_fact f JOIN r6_dimension d ON f.dim_id=d.id GROUP BY f.id ORDER BY f.id",
          ),
        });
      } finally {
        await con.run("ROLLBACK");
      }
      assert.deepEqual(await preflight(), []);
    }
    await bad(
      "referenced_duplicate_dimension",
      "INSERT INTO r6_dimension VALUES(1,'North',true)",
      ["dimension_key_duplicate"],
      [
        ["South", 2, "30.03"],
        ["North", 6, "55.01"],
      ],
    );
    await bad(
      "unreferenced_duplicate_dimension",
      "INSERT INTO r6_dimension VALUES(4,'Unused',true)",
      ["dimension_key_duplicate"],
      expectedGroups,
    );
    await bad(
      "null_dimension_key",
      "INSERT INTO r6_dimension VALUES(NULL,'Unused',true)",
      ["dimension_key_null"],
      expectedGroups,
    );
    await bad(
      "missing_target",
      "DELETE FROM r6_dimension WHERE id=3",
      ["fact_foreign_key_unmatched"],
      [
        ["South", 2, "30.03"],
        ["North", 2, "30.03"],
      ],
    );
    await bad(
      "null_fact_foreign_key",
      "UPDATE r6_fact SET dim_id=NULL WHERE id='f1'",
      ["fact_foreign_key_null"],
      [
        ["South", 2, "30.03"],
        ["North", 3, "14.97"],
      ],
    );
    await bad(
      "unmatched_fact_outside_active_scope",
      "UPDATE r6_fact SET dim_id=999 WHERE id='f3'",
      ["fact_foreign_key_unmatched"],
      [
        ["North", 4, "24.98"],
        ["South", 1, null],
      ],
    );
    await bad(
      "duplicate_fact_identity",
      "INSERT INTO r6_fact VALUES('f1',1,10.01,true)",
      ["fact_identity_duplicate"],
      [
        ["South", 2, "30.03"],
        ["North", 5, "34.99"],
      ],
      [[7, "65.02"]],
    );
    await bad(
      "null_fact_identity",
      "INSERT INTO r6_fact VALUES(NULL,1,1,true)",
      ["fact_identity_null"],
      [
        ["South", 2, "30.03"],
        ["North", 4, "25.98"],
      ],
      [[6, "56.01"]],
    );
    await bad(
      "key_type_coercion",
      "ALTER TABLE r6_dimension ALTER COLUMN id TYPE VARCHAR; INSERT INTO r6_dimension VALUES('01','North',true)",
      ["exact_integer_key_type"],
      [
        ["South", 2, "30.03"],
        ["North", 6, "55.01"],
      ],
    );
    await bad(
      "empty_fact_bad_dimension",
      "DELETE FROM r6_fact; INSERT INTO r6_dimension VALUES(4,'Unused',true)",
      ["dimension_key_duplicate"],
      [],
      [[0, null]],
    );
    await bad(
      "empty_dimension_nonempty_fact",
      "DELETE FROM r6_dimension",
      ["fact_foreign_key_unmatched"],
      [],
    );
    await con.run("BEGIN");
    try {
      await con.run("DELETE FROM r6_fact; DELETE FROM r6_dimension");
      assert.deepEqual(await preflight(), []);
      assert.deepEqual(await query(), [[0, null]]);
      assert.deepEqual(await joined(), []);
    } finally {
      await con.run("ROLLBACK");
    }
    await con.run("BEGIN");
    try {
      await con.run("UPDATE r6_dimension SET label=NULL WHERE id=2");
      assert.deepEqual(await preflight(), []);
      assert.deepEqual(await joined(), [
        ["North", 4, "24.98"],
        [null, 2, "30.03"],
      ]);
    } finally {
      await con.run("ROLLBACK");
    }
    const plannerRejected: string[] = [];
    await temporary("ossie-r6-", async (directory) => {
      const baseline = json<Core>(join(FIX, "core.json"));
      async function refuse(
        name: string,
        model: Core,
        metrics: string[],
        dims: string[],
        reason: string,
      ) {
        const path = join(directory, name + ".json");
        writeFileSync(path, JSON.stringify(model));
        await load(path);
        await assert.rejects(
          con.one("SELECT ossie_compile(?,?,?)", [metrics, dims, []]),
          (error: Error) => {
            assert(error.message.includes(reason), `${name}: ${error.message}`);
            return true;
          },
        );
        plannerRejected.push(name);
      }
      const fanout = structuredClone(baseline);
      delete fanout.datasets[1].primary_key;
      await refuse(
        "declared_many_to_many",
        fanout,
        ["amount_sum"],
        ["dimension.label"],
        "repeats each",
      );
      const ambiguous = structuredClone(baseline);
      ambiguous.relationships.push({
        ...ambiguous.relationships[0],
        name: "second_path",
      });
      await refuse(
        "ambiguous_path",
        ambiguous,
        ["amount_sum"],
        ["dimension.label"],
        "more than one join path",
      );
      await refuse(
        "mixed_grain",
        baseline,
        ["amount_sum", "dimension_count"],
        [],
        "different grains",
      );
      await refuse(
        "reverse_fanout",
        baseline,
        ["dimension_count"],
        ["fact.id"],
        "repeats each",
      );
      const unique = structuredClone(baseline);
      delete unique.datasets[1].primary_key;
      unique.datasets[1].unique_keys = [["id"]];
      const path = join(directory, "unique.json");
      writeFileSync(path, JSON.stringify(unique));
      await load(path);
      assert.deepEqual(await joined(), expectedGroups);
    });
    await load(join(FIX, "core.json"));
    assert.deepEqual(
      await con.one("SELECT * FROM ossie_query(?,?,?)", [
        ["dimension_count"],
        [],
        [],
      ]),
      [4],
    );
    const dimensionCompiled = await con.one("SELECT ossie_compile(?,?,?)", [
      ["dimension_count"],
      [],
      [],
    ]);
    assert(String(dimensionCompiled[0]).includes("r6_dimension"));
    assert.deepEqual(await con.one("SELECT COUNT(*) FROM r6_dimension"), [4]);
    assert.deepEqual(
      await con.one(
        "SELECT COUNT(d.id) FROM r6_fact f JOIN r6_dimension d ON f.dim_id=d.id",
      ),
      [6],
    );
    const compiled = await con.one("SELECT ossie_compile(?,?,?)", [
      ["record_count", "amount_sum"],
      ["dimension.label"],
      [],
    ]);
    assert(String(compiled[0]).includes("INNER JOIN"));
    // Host snapshot experiment: not a production population capture implementation.
    const writer = await con.instance.connect();
    try {
      await con.run("BEGIN");
      try {
        assert.deepEqual(
          await con.rows("SELECT id FROM r6_fact ORDER BY id"),
          all.map((f) => [f.id]),
        );
        await writer.run("UPDATE r6_dimension SET label='Changed' WHERE id=1");
        assert.deepEqual(await joined(), expectedGroups);
        assert.deepEqual(
          await con.rows("SELECT label FROM r6_dimension WHERE id=1"),
          [["North"]],
        );
      } finally {
        await con.run("COMMIT");
      }
      assert.deepEqual(
        await con.rows("SELECT label FROM r6_dimension WHERE id=1"),
        [["Changed"]],
      );
      assert.deepEqual(await joined(), [
        ["Changed", 2, "30.03"],
        ["North", 2, "-5.05"],
        ["South", 2, "30.03"],
      ]);
      await writer.run("UPDATE r6_dimension SET label='North' WHERE id=1");
    } finally {
      writer.disconnectSync();
    }
    const populationManifest = await temporary(
      "ossie-r6-pop-",
      async (directory) => {
        const items: Record<string, unknown> = {
          profile: "gaap-scalar-population-v1",
        };
        for (const [kind, path] of [
          ["core", join(FIX, "core.json")],
          [
            "ontology",
            join(ROOT, "test/fixtures/population-dates/ontology.json"),
          ],
          [
            "mapping",
            join(ROOT, "test/fixtures/population-dates/mapping.json"),
          ],
        ]) {
          const data = readFileSync(path);
          const target = join(directory, kind + ".json");
          writeFileSync(target, data);
          const doc = JSON.parse(data.toString());
          items[kind] = {
            path: kind + ".json",
            name: doc.name,
            sha256: digest(target),
          };
        }
        const path = join(directory, "bundle.json");
        writeFileSync(path, JSON.stringify(items));
        await assert.rejects(
          con.one("CALL ossie_bundle_load(?)", [path]),
          (error: Error) => {
            assert(
              error.message.includes("exactly one dataset and no core joins"),
            );
            return true;
          },
        );
        return true;
      },
    );
    const report = {
      scope:
        "R6 core live-query and research SQL preflight controls; production joined population NOT implemented",
      runtime: `Bun ${Bun.version}`,
      duckdb_version: version[0],
      duckdb_source_id: version[1],
      artifact_sha256: digest(resolve(artifact)),
      script_sha256: digest(import.meta.path),
      helper_sha256: digest(join(ROOT, "scripts/artifact_helpers.ts")),
      fact_rows: 6,
      dimension_rows: 4,
      independent_oracle: "hand-defined associations and bigint cents",
      non_null_measure_count: nonNull.length,
      average_control: values[1],
      average_tolerance: 1e-12,
      dimension_grain_count_vs_naive_flattened_count: [4, 6],
      dimension_grain_compiles_independently: dimensionCompiled[0],
      fact_total: await query(),
      joined_groups: await joined(),
      selected_fact_ids: selected.map((f) => f.id),
      compiled_sql: compiled[0],
      preflight_cases: preflightCases,
      planner_rejections: plannerRejected,
      declared_unique_key_supported: true,
      unused_dimension_row_produces_no_fact: true,
      null_nonkey_attribute_allowed: true,
      empty_sources_checked: true,
      host_same_database_transaction_snapshot_checked: true,
      population_v1_multitable_still_refused: populationManifest,
      inputs_sha256: Object.fromEntries(
        readdirSync(FIX)
          .sort()
          .map((n) => [relative(ROOT, join(FIX, n)), digest(join(FIX, n))]),
      ),
      implementation_sha256: Object.fromEntries(
        [
          "src/compile/compile.cpp",
          "src/model/graph.cpp",
          "src/model/validate.cpp",
          "src/population/population.cpp",
        ].map((n) => [n, digest(join(ROOT, n))]),
      ),
    };
    emitReport(report);
    return report;
  } finally {
    con.close();
  }
}
if (import.meta.main) {
  assert(process.argv[2], "artifact required");
  await verify(process.argv[2]);
}

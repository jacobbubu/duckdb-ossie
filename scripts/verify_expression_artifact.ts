/** Synthetic, independently hand-derived R3 controls in a stock 1.5.6 client. */
import { join, resolve, relative } from "node:path";
import { readFileSync, writeFileSync, readdirSync } from "node:fs";
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
const FIX = join(ROOT, "test/fixtures/population-expressions");
interface Expression {
  dialects: { expression: string; dialect: string }[];
}
interface Core {
  name: string;
  datasets: {
    fields: { name: string; datatype?: string; expression: Expression }[];
  }[];
  metrics: { name: string; datatype?: string; expression: Expression }[];
}
interface Ontology {
  name: string;
  ontology: { derived_by?: string[] }[];
}
type Docs = [Core, Ontology, { name: string }];
function definitions(): Docs {
  return [
    json<Core>(join(FIX, "core.json")),
    json<Ontology>(join(FIX, "ontology.json")),
    json<{ name: string }>(join(FIX, "mapping.json")),
  ];
}
function files(directory: string, docs: Docs) {
  const entries: Record<string, unknown> = {};
  for (const [index, kind] of ["core", "ontology", "mapping"].entries()) {
    const path = join(directory, kind + ".json");
    writeFileSync(path, JSON.stringify(docs[index], null, 2) + "\n");
    entries[kind] = {
      path: kind + ".json",
      name: docs[index].name,
      sha256: digest(path),
    };
  }
  const path = join(directory, "bundle.json");
  writeFileSync(
    path,
    JSON.stringify(
      { profile: "gaap-scalar-population-v1", ...entries },
      null,
      2,
    ) + "\n",
  );
  return path;
}
function near(actual: Cell, expected: number) {
  assert.equal(typeof actual, "number");
  assert(Number.isFinite(Number(actual)));
  assert(
    Math.abs(Number(actual) - expected) <= 1e-12,
    `${actual} != ${expected}`,
  );
}
export async function verify(artifact: string) {
  const con = await Database.open();
  try {
    const version = await con.loadArtifact(artifact);
    const host = await con.one(
      "SELECT 1/2, typeof(1::DECIMAL(18,2)/2::DECIMAL(18,2)), isinf(1.0/0.0), isnan(0.0/0.0), NULLIF(NULL,0), NULLIF(1,NULL), 1/NULLIF(0,0)",
    );
    assert.deepEqual(host, [0.5, "DOUBLE", true, true, null, 1, null]);
    await con.run(readFileSync(join(FIX, "rows.sql"), "utf8"));
    const load = (path: string) => con.one("CALL ossie_bundle_load(?)", [path]);
    const baseline = await load(join(FIX, "bundle.json"));
    const ids = async (concept = "ValidUser", filters: string[] = []) =>
      (
        await con.rows(
          "SELECT entity_id FROM ossie_objects(?,?) ORDER BY entity_id",
          [concept, filters],
        )
      ).map((row) => String(row[0]));
    assert.deepEqual(await ids(), ["fraction", "positive"]);
    const ratios = await con.rows(
      'SELECT entity_id, "dws_gaap_di.ratio" FROM ossie_objects(?,?) ORDER BY entity_id',
      ["User", []],
    );
    const expected: Record<string, number | null> = {
      positive: 5,
      negative: -2,
      fraction: 1 / 3,
      negative_denominator: -1,
      zero_denominator: null,
      zero_zero: null,
      null_numerator: null,
      null_denominator: null,
    };
    assert.equal(ratios.length, 8);
    for (const [id, value] of ratios) {
      if (expected[String(id)] === null) assert.equal(value, null);
      else near(value, expected[String(id)]!);
    }
    const requested = [
      "record_count",
      "gaap_value_count",
      "gaap_sum",
      "ratio_of_sums",
      "sum_ratio",
      "count_ratio",
      "distinct_region",
      "nullif_sums",
    ];
    const query = (concept = "ValidUser", filters: string[] = []) =>
      con.one("SELECT * FROM ossie_concept_query(?,?,?,?)", [
        concept,
        requested,
        [],
        filters,
      ]);
    const positive = await query();
    assert.deepEqual(positive.slice(0, 3), [2, 2, "11.00"]);
    near(positive[3], 11 / 5);
    near(positive[4], 16 / 3);
    assert.deepEqual(positive.slice(5), [2, 2, "11.00"]);
    const all = await query("User");
    assert.deepEqual(all.slice(0, 3), [8, 7, "14.00"]);
    near(all[3], 14 / 11);
    near(all[4], 7 / 3);
    assert.deepEqual(all.slice(5), [4, 3, "14.00"]);
    const cancelled = await query("User", [
      "User.id = 'positive' OR User.id = 'negative_denominator'",
    ]);
    assert.equal(cancelled[3], null);
    near(cancelled[4], 4);
    const zeros = await query("User", [
      "User.id = 'zero_denominator' OR User.id = 'zero_zero'",
    ]);
    assert.deepEqual(zeros, [2, 2, "2.00", null, null, 0, 0, "2.00"]);
    const empty = await query("User", ["User.id = 'absent'"]);
    assert.deepEqual(empty, [0, 0, null, null, null, 0, 0, null]);
    assert.deepEqual(await ids("User", ["NULLIF(User.ratio,-2) IS NULL"]), [
      "negative",
      "null_denominator",
      "null_numerator",
      "zero_denominator",
      "zero_zero",
    ]);
    assert.deepEqual(
      await ids("User", ["User.ratio / NULLIF(User.ytd_gaap, 0.00) > 0"]),
      ["fraction", "negative_denominator", "positive"],
    );
    const compiled = await con.one(
      "SELECT * FROM ossie_concept_compile(?,?,?,?)",
      ["ValidUser", requested, [], []],
    );
    assert.equal(compiled[2], baseline[1]);
    assert.equal(compiled[3], baseline[2]);
    assert.equal(compiled.at(-1), false);
    const typed = await con.one(
      "SELECT typeof(\"dws_gaap_di.ratio\"), typeof(\"dws_gaap_di.gaap_val\") FROM ossie_objects('User', ['User.id = ''positive'''])",
    );
    assert.deepEqual(typed, ["DOUBLE", "DECIMAL(18,2)"]);
    const beforeMacro = await query("User");
    await con.run("CREATE MACRO nullif(a,b) AS 999");
    assert.deepEqual(await ids(), ["fraction", "positive"]);
    assert.deepEqual(await query("User"), beforeMacro);
    assert.equal((await load(join(FIX, "bundle.json")))[2], baseline[2]);
    await con.run("DROP MACRO nullif");
    const rejected: string[] = [];
    await temporary("ossie-r3-", async (directory) => {
      async function reject(
        name: string,
        expression: string,
        where: "field" | "metric" | "rule" = "field",
        reason = "division requires",
        datatype?: string,
      ) {
        const docs = definitions();
        if (where === "field")
          docs[0].datasets[0].fields.at(-1)!.expression.dialects[0].expression =
            expression;
        else if (where === "metric")
          docs[0].metrics.find(
            (metric) => metric.name === "ratio_of_sums",
          )!.expression.dialects[0].expression = expression;
        else docs[1].ontology[1].derived_by = [expression];
        if (datatype && where === "metric")
          docs[0].metrics.find(
            (metric) => metric.name === "ratio_of_sums",
          )!.datatype = datatype;
        else if (datatype)
          docs[0].datasets[0].fields.at(-1)!.datatype = datatype;
        const snapshot = await con.one(
          "SELECT snapshot_id FROM ossie_concepts() LIMIT 1",
        );
        await assert.rejects(load(files(directory, docs)), (error: Error) => {
          assert(error.message.includes(reason), `${name}: ${error.message}`);
          assert(
            error.message.includes(
              "missing_capability=bounded population profile",
            ),
          );
          return true;
        });
        assert.deepEqual(
          await con.one("SELECT snapshot_id FROM ossie_concepts() LIMIT 1"),
          snapshot,
        );
        rejected.push(name);
      }
      for (const [name, expr] of Object.entries({
        bare: "gaap_val / ytd_gaap",
        nonzero: "gaap_val / NULLIF(ytd_gaap,1)",
        tiny_nonzero: "gaap_val / NULLIF(ytd_gaap,0.000000000000000001)",
        maximum_scale_nonzero: `gaap_val / NULLIF(ytd_gaap,0.${"0".repeat(37)}1)`,
        string_zero: "gaap_val / NULLIF(ytd_gaap,'0')",
        boolean_zero: "gaap_val / NULLIF(ytd_gaap,FALSE)",
        null_zero: "gaap_val / NULLIF(ytd_gaap,NULL)",
        computed_zero: "gaap_val / NULLIF(ytd_gaap,0+0)",
        cast_zero: "gaap_val / NULLIF(ytd_gaap,CAST(0 AS DECIMAL(18,2)))",
        case_guard:
          "gaap_val / CASE WHEN ytd_gaap=0 THEN NULL ELSE ytd_gaap END",
        wrapped_guard: "gaap_val / CAST(NULLIF(ytd_gaap,0) AS DOUBLE)",
        nested_bare: "gaap_val / NULLIF(ytd_gaap / gaap_val,0)",
      }))
        await reject(name, expr);
      await reject(
        "metric_bare",
        "SUM(dws_gaap_di.gaap_val) / SUM(dws_gaap_di.ytd_gaap)",
        "metric",
      );
      await reject("rule_bare", "User.ytd_gaap / 2 > 0", "rule");
      for (const [name, expr] of Object.entries({
        qualified: "main.nullif(gaap_val,0)",
        unknown: "mystery(gaap_val,0)",
        integer_division: "gaap_val // NULLIF(ytd_gaap,0)",
        modulo: "gaap_val % NULLIF(ytd_gaap,0)",
      }))
        await reject(name, expr, "field", "unsupported function");
      await reject(
        "arity",
        "NULLIF(gaap_val)",
        "field",
        "unparseable expression",
      );
      await reject(
        "no_aggregate",
        "NULLIF(dws_gaap_di.gaap_val,0)",
        "metric",
        "requires SUM, COUNT or AVG",
      );
      await reject(
        "ungrouped_column",
        "SUM(dws_gaap_di.gaap_val) + dws_gaap_di.ytd_gaap",
        "metric",
        "GROUP BY",
      );
      await reject(
        "string_dividend",
        "region / NULLIF(ytd_gaap,0)",
        "field",
        "No function matches",
      );
      await reject(
        "rule_nullifzero_alias",
        "NULLIFZERO(User.ytd_gaap)>0",
        "rule",
        "unsupported function",
      );
      await reject(
        "nested_aggregate",
        "SUM(NULLIF(AVG(dws_gaap_di.gaap_val),0))",
        "metric",
        "unsupported function",
      );
      await assert.rejects(
        ids("User", ["User.ytd_gaap / 2 > 0"]),
        /division requires/,
      );
      rejected.push("request_bare");
      await assert.rejects(
        ids("User", ["NULLIF(User.ytd_gaap,0)"]),
        /BOOLEAN type/,
      );
      rejected.push("request_numeric_truthiness");
      await reject(
        "metric_ratio_declared_decimal",
        "SUM(dws_gaap_di.gaap_val) / NULLIF(SUM(dws_gaap_di.ytd_gaap),0)",
        "metric",
        "metric datatype incompatible",
        "Decimal",
      );
      // Aggregate detection must descend through outer functions/operators.
      for (const [expression, expected] of [
        ["SUM(dws_gaap_di.gaap_val) * 2", 22],
        ["2 * SUM(dws_gaap_di.gaap_val)", 22],
        ["NULLIF(SUM(dws_gaap_di.gaap_val),0)", 11],
      ] as const) {
        const docs = definitions();
        const metric = docs[0].metrics.find(
          (metric) => metric.name === "ratio_of_sums",
        )!;
        metric.expression.dialects[0].expression = expression;
        delete metric.datatype;
        await load(files(directory, docs));
        assert.equal(Number((await query())[3]), expected);
      }
      const alias = definitions();
      alias[0].datasets[0].fields.at(-1)!.expression.dialects[0].expression =
        "gaap_val / NULLIFZERO(ytd_gaap)";
      await load(files(directory, alias));
      assert.deepEqual(await ids(), ["fraction", "positive"]);
      const directRule = definitions();
      directRule[1].ontology[1].derived_by = [
        "User.ytd_gaap / NULLIF(User.ytd_gaap,0) > 0",
      ];
      await load(files(directory, directRule));
      assert.deepEqual(await ids(), [
        "fraction",
        "negative",
        "negative_denominator",
        "null_numerator",
        "positive",
      ]);
      const overflow = definitions();
      overflow[0].datasets[0].fields.at(-1)!.expression.dialects[0].expression =
        "1e308 / NULLIF(1e-308,0)";
      await load(files(directory, overflow));
      const infinite = await con.one(
        "SELECT \"dws_gaap_di.ratio\" FROM ossie_objects('User',['User.id = ''positive'''])",
      );
      assert.equal(infinite[0], Number.POSITIVE_INFINITY);
      // Do not serialize Infinity as JSON null and call it a NULL result.
      await load(join(FIX, "bundle.json"));
    });
    return {
      scope:
        "R3 synthetic NULLIF and directly guarded division; not full R3 or safe joins",
      runtime: `Bun ${Bun.version}`,
      duckdb_version: version[0],
      duckdb_source_id: version[1],
      artifact_sha256: digest(artifact),
      script_sha256: digest(import.meta.path),
      host_boundary: {
        integer_division_half: host[0],
        decimal_division_type: host[1],
        division_zero_infinity: host[2],
        zero_zero_nan: host[3],
      },
      rows: 8,
      positive_membership: ["fraction", "positive"],
      row_and_aggregate_ratios_hand_derived: true,
      aggregate_zero_by_cancellation: true,
      zero_null_empty: true,
      count_distinct_excludes_null: true,
      core_nullifzero_lowering_checked: true,
      direct_rule_division_checked: true,
      recursive_aggregate_detection_checked: true,
      guarded_overflow:
        "native positive Infinity; zero guard does not ensure finite results",
      native_ratio_type: typed[0],
      macro_shadowing_prevented: true,
      failed_load_preserves_snapshot: true,
      rejected_controls: rejected,
      inputs_sha256: Object.fromEntries(
        readdirSync(FIX)
          .sort()
          .map((name) => [
            relative(ROOT, join(FIX, name)),
            digest(join(FIX, name)),
          ]),
      ),
      implementation_sha256: {
        "src/population/population.cpp": digest(
          join(ROOT, "src/population/population.cpp"),
        ),
      },
      helper_sha256: digest(join(ROOT, "scripts/artifact_helpers.ts")),
    };
  } finally {
    con.close();
  }
}
if (import.meta.main) emitReport(await verify(resolve(process.argv[2])));

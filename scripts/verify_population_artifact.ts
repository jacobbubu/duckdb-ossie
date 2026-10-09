/** Independent bigint-cents CSV oracle against a real stock-client artifact. */
import { join, relative, resolve } from "node:path";
import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import {
  Database,
  assert,
  digest,
  json,
  temporary,
  emitReport,
  ROOT,
  type Cell,
} from "./artifact_helpers.ts";
import { filesUnder } from "./check_architecture.ts";
import { populationError } from "./population_error.ts";
const EVIDENCE = join(ROOT, "docs/research/ossie-req-001"),
  DEFINITIONS = join(EVIDENCE, "proposed-definitions");
const FILTERS = [
  "User.product = 'X'",
  "User.date >= DATE '2026-03-01' AND User.date < DATE '2026-04-01'",
];
const METRICS = ["record_count", "gaap_value_count", "gaap_sum", "mean_gaap"];
const TABLE =
  "CREATE TABLE dws_gaap_di(ds DATE, product VARCHAR, region VARCHAR, user_type VARCHAR, user_id VARCHAR, gaap_val DECIMAL(18,2), ytd_gaap DECIMAL(18,2))";
interface Expression {
  dialects: { dialect: string; expression: string }[];
}
interface Descriptive {
  description?: string;
  ai_context?: unknown;
  custom_extensions?: unknown;
}
interface Field extends Descriptive {
  name: string;
  datatype?: string;
  label?: string;
  expression: Expression;
}
interface Core extends Descriptive {
  name: string;
  version: string;
  datasets: { name: string; source: string; fields: Field[] }[];
  metrics: Field[];
}
interface Relation {
  name: string;
  requires?: string[];
  multiplicity: string;
}
interface Concept {
  concept: string;
  type: string;
  requires?: string[];
  extends?: string[];
  identify_by?: string[];
  derived_by?: string[];
  relationships?: Relation[];
}
interface Ontology extends Descriptive {
  name: string;
  version: string;
  requires?: string[];
  ontology: Concept[];
}
interface ObjectMapping {
  concept?: string;
  expression?: string;
  referent_mappings?: { relationship: string; expression: string }[];
}
interface Mapping extends Descriptive {
  name: string;
  ontology_ref: { name: string };
  concept_mappings: {
    concept: string;
    link_mappings: {
      object_mapping: ObjectMapping;
      children: { object_mapping: ObjectMapping; relationship: string }[];
    }[];
  }[];
}
type Docs = [Core, Ontology, Mapping];
interface Manifest {
  profile: string;
  core: { path: string; name: string; sha256: string };
  ontology: { path: string; name: string; sha256: string };
  mapping: { path: string; name: string; sha256: string };
}
interface SourceRow {
  ds: string;
  product: string;
  region: string;
  user_type: string;
  user_id: string;
  gaap_val: bigint | null;
  ytd_gaap: bigint | null;
}
interface Baseline {
  fixture_sha256: string;
  cases: Record<string, { record_count: number }>;
}
interface Boundary {
  fixture_sha256: string;
  cases: Record<
    string,
    {
      object_ids: string[];
      daily: [string, number, number, number | null, number | null][];
    }
  >;
}
export function cents(value: string): bigint {
  const match = /^(-?)(\d+)(?:\.(\d{1,2}))?$/.exec(value);
  assert(match, `not an exact two-decimal value: ${value}`);
  return (
    (match[1] ? -1n : 1n) *
    (BigInt(match[2]) * 100n + BigInt((match[3] || "").padEnd(2, "0")))
  );
}
function sourceRows(path: string): SourceRow[] {
  // Immutable fixtures use plain comma-separated fields, without quoting/embedded newlines.
  const lines = readFileSync(path, "utf8").trimEnd().split(/\r?\n/),
    header = lines.shift()!.split(",");
  assert.deepEqual(header, [
    "ds",
    "product",
    "region",
    "user_type",
    "user_id",
    "gaap_val",
    "ytd_gaap",
  ]);
  return lines.map((line) => {
    assert(
      !line.includes('"'),
      "fixture dialect changed: CSV parser review required",
    );
    const columns = line.split(",");
    assert.equal(columns.length, header.length);
    const [ds, product, region, user_type, user_id, gaap, ytd] = columns;
    return {
      ds,
      product,
      region,
      user_type,
      user_id,
      gaap_val: gaap ? cents(gaap) : null,
      ytd_gaap: ytd ? cents(ytd) : null,
    };
  });
}
type Aggregate = [number, number, bigint | null, number | null];
function aggregate(selected: SourceRow[]): Aggregate {
  const values = selected
    .map((row) => row.gaap_val)
    .filter((value): value is bigint => value !== null);
  const sum = values.length
    ? values.reduce((sum, value) => sum + value, 0n)
    : null;
  if (sum !== null)
    assert(
      Number.isSafeInteger(Number(sum)),
      "oracle sum outside safe AVG range",
    );
  return [
    selected.length,
    values.length,
    sum,
    sum === null ? null : Number(sum) / 100 / values.length,
  ];
}
function same(actual: Cell[], expected: Aggregate) {
  assert.deepEqual(actual.slice(0, 2), expected.slice(0, 2));
  assert.equal(
    actual[2] === null ? null : cents(String(actual[2])),
    expected[2],
  );
  if (expected[3] === null) assert.equal(actual[3], null);
  else {
    assert.equal(typeof actual[3], "number");
    assert(
      Math.abs(Number(actual[3]) - expected[3]) <= 1e-10,
      JSON.stringify(actual),
    );
  }
}
function bundleFiles(directory: string, docs: Docs) {
  const entries: Record<
    string,
    { path: string; name: string; sha256: string }
  > = {};
  for (const [index, kind] of ["core", "ontology", "mapping"].entries()) {
    const path = join(directory, kind + ".json");
    writeFileSync(path, JSON.stringify(docs[index], null, 2) + "\n");
    entries[kind] = {
      path: kind + ".json",
      name: docs[index].name,
      sha256: digest(path),
    };
  }
  const manifest = join(directory, "bundle.json");
  writeFileSync(
    manifest,
    JSON.stringify(
      { profile: "gaap-scalar-population-v1", ...entries },
      null,
      2,
    ) + "\n",
  );
  return manifest;
}
const load = (con: Database, path: string) =>
  con.one("CALL ossie_bundle_load(?)", [path]);
async function objects(
  con: Database,
  filters = FILTERS,
  concept = "ValidUser",
) {
  return (
    await con.rows("SELECT entity_id FROM ossie_objects(?, ?)", [
      concept,
      filters,
    ])
  )
    .map((row) => String(row[0]))
    .sort();
}
const metric = (con: Database, filters = FILTERS, dimensions: string[] = []) =>
  con.rows("SELECT * FROM ossie_concept_query(?, ?, ?, ?)", [
    "ValidUser",
    METRICS,
    dimensions,
    filters,
  ]);
function groups(selected: SourceRow[], keys: (keyof SourceRow)[]) {
  const result = new Map<string, SourceRow[]>();
  for (const row of selected) {
    const key = JSON.stringify(keys.map((key) => row[key]));
    if (!result.has(key)) result.set(key, []);
    result.get(key)!.push(row);
  }
  return result;
}
export async function verify(artifact: string) {
  const con = await Database.open();
  try {
    await con.loadArtifact(artifact);
    await con.run(TABLE);
    const baseline = json<Baseline>(join(EVIDENCE, "baseline-results.json")),
      original = join(EVIDENCE, "gaap-original-475.csv");
    assert.equal(digest(original), baseline.fixture_sha256);
    await con.run(
      "INSERT INTO dws_gaap_di SELECT * FROM read_csv(?,header=true)",
      [original],
    );
    const independent = sourceRows(original),
      cases: Record<string, Record<string, unknown>> = {},
      contentHashes: Cell[] = [];
    for (const threshold of [10, 20]) {
      const snapshot = await load(
        con,
        join(ROOT, `test/fixtures/population/bundle-${threshold}.json`),
      );
      contentHashes.push(snapshot[2]);
      const selected = independent.filter(
        (row) =>
          row.product === "X" &&
          row.ytd_gaap !== null &&
          row.ytd_gaap >= BigInt(threshold) * 100n &&
          row.ds >= "2026-03-01" &&
          row.ds < "2026-04-01",
      );
      const ids = await objects(con);
      assert.deepEqual(ids, selected.map((row) => row.user_id).sort());
      assert.deepEqual(
        ids,
        json<string[]>(
          join(EVIDENCE, `baseline-objects-threshold-${threshold}.json`),
        ),
      );
      const totals = (await metric(con))[0]!;
      same(totals, aggregate(selected));
      assert.equal(ids.length, baseline.cases[threshold].record_count);
      const grouped = groups(selected, ["ds"]),
        daily = await metric(con, FILTERS, ["dws_gaap_di.ds"]);
      assert.equal(daily.length, 15);
      assert.equal(daily.length, grouped.size);
      for (const row of daily)
        same(row.slice(1), aggregate(grouped.get(JSON.stringify([row[0]]))!));
      const groupedDrill = groups(selected, ["ds", "region", "user_type"]),
        drill = await metric(con, FILTERS, [
          "dws_gaap_di.ds",
          "dws_gaap_di.region",
          "dws_gaap_di.user_type",
        ]);
      assert.equal(drill.length, groupedDrill.size);
      const drilldownObjects: {
        dimensions: Cell[];
        filters: string[];
        object_ids: string[];
      }[] = [];
      for (const result of drill) {
        const key = result.slice(0, 3),
          selectedGroup = groupedDrill.get(JSON.stringify(key))!;
        same(result.slice(3), aggregate(selectedGroup));
        const narrowed = [
          ...FILTERS,
          `User.date = DATE '${key[0]}'`,
          `User.region = '${key[1]}'`,
          `User.user_type = '${key[2]}'`,
        ];
        const childIds = await objects(con, narrowed);
        assert.deepEqual(
          childIds,
          selectedGroup.map((row) => row.user_id).sort(),
        );
        drilldownObjects.push({
          dimensions: key,
          filters: narrowed,
          object_ids: childIds,
        });
      }
      const compiled = await con.one(
        "SELECT * FROM ossie_concept_compile(?, ?, ?, ?)",
        ["ValidUser", METRICS, [], FILTERS],
      );
      assert.deepEqual(compiled.slice(2, 4), snapshot.slice(1, 3));
      assert.equal(compiled.at(-1), false);
      const traced = await con.one(
        "SELECT * FROM ossie_concept_query('ValidUser', ?, [], ?, include_trace=>true)",
        [METRICS, FILTERS],
      );
      assert.deepEqual(traced.slice(-3), [
        snapshot[1],
        snapshot[2],
        compiled[5],
      ]);
      cases[threshold] = {
        query: {
          concept: "ValidUser",
          metrics: METRICS,
          filters: FILTERS,
          daily_dimensions: ["dws_gaap_di.ds"],
          drill_dimensions: [
            "dws_gaap_di.ds",
            "dws_gaap_di.region",
            "dws_gaap_di.user_type",
          ],
        },
        object_ids: ids,
        aggregate: totals,
        daily_results: daily,
        grouped_results: drill,
        drilldown_objects: drilldownObjects,
        actual_plan: JSON.parse(String(compiled[9])),
        traced_aggregate: traced,
        objects: ids.length,
        sum: totals[2],
        daily_groups: daily.length,
        drill_groups: drill.length,
        snapshot_id: snapshot[1],
        content_sha256: snapshot[2],
        sql: compiled[0],
        ontology_sha256: compiled[5],
      };
    }
    const manifests = [10, 20].map((threshold) =>
      json<Manifest>(
        join(ROOT, `test/fixtures/population/bundle-${threshold}.json`),
      ),
    );
    assert.equal(manifests[0]!.core.sha256, manifests[1]!.core.sha256);
    assert.equal(manifests[0]!.mapping.sha256, manifests[1]!.mapping.sha256);
    const ontologies = [10, 20].map((threshold) =>
      json<Ontology>(
        join(DEFINITIONS, `gaap.ontology.threshold-${threshold}.json`),
      ),
    );
    for (const [index, threshold] of [10, 20].entries()) {
      const child = ontologies[index]!.ontology.find(
        (c) => c.concept === "ValidUser",
      )!;
      assert.deepEqual(child.derived_by, [`User.ytd_gaap >= ${threshold}`]);
      delete child.derived_by;
    }
    assert.deepEqual(ontologies[0], ontologies[1]);
    assert.deepEqual(cases["10"]!.query, cases["20"]!.query);
    assert.equal(contentHashes[0], contentHashes[1]);
    assert.notEqual(cases["10"].ontology_sha256, cases["20"].ontology_sha256);
    const boundary = join(EVIDENCE, "gaap-boundary.csv"),
      controls = json<Boundary>(join(EVIDENCE, "boundary-results.json")),
      boundaryCases: Record<string, unknown> = {};
    assert.equal(digest(boundary), controls.fixture_sha256);
    await con.run("DELETE FROM dws_gaap_di");
    await con.run(
      "INSERT INTO dws_gaap_di SELECT * FROM read_csv(?,header=true)",
      [boundary],
    );
    for (const threshold of [10, 20]) {
      const boundarySnapshot = await load(
        con,
        join(ROOT, `test/fixtures/population/bundle-${threshold}.json`),
      );
      const boundaryIds = await objects(con);
      assert.deepEqual(boundaryIds, controls.cases[threshold].object_ids);
      const daily = (await metric(con, FILTERS, ["dws_gaap_di.ds"])).sort(
          (a, b) => String(a[0]).localeCompare(String(b[0])),
        ),
        expectedDaily = controls.cases[threshold].daily;
      assert.equal(daily.length, expectedDaily.length);
      boundaryCases[threshold] = {
        snapshot: boundarySnapshot,
        object_ids: boundaryIds,
        daily_results: daily,
        independent_expected: controls.cases[threshold],
      };
      daily.forEach((actual, index) => {
        const expected = expectedDaily[index];
        assert.equal(actual[0], expected[0]);
        same(actual.slice(1), [
          expected[1],
          expected[2],
          expected[3] === null ? null : cents(String(expected[3])),
          expected[4],
        ]);
      });
    }
    const emptyResults = {
      filters: ["User.product = 'absent'"],
      objects: await objects(con, ["User.product = 'absent'"]),
      scalar: await metric(con, ["User.product = 'absent'"]),
      grouped: await metric(
        con,
        ["User.product = 'absent'"],
        ["dws_gaap_di.ds"],
      ),
    };
    assert.deepEqual(emptyResults.objects, []);
    assert.deepEqual(emptyResults.scalar, [[0, 0, null, null]]);
    assert.deepEqual(emptyResults.grouped, []);
    const old = await load(
      con,
      join(ROOT, "test/fixtures/population/bundle-10.json"),
    );
    await con.run(
      "PREPARE frozen_trace AS SELECT * FROM ossie_concept_query('ValidUser',['record_count'],[],[],include_trace=>true)",
    );
    const newer = await load(
        con,
        join(ROOT, "test/fixtures/population/bundle-20.json"),
      ),
      prepared = await con.one("EXECUTE frozen_trace");
    assert.equal(prepared[0], 7);
    assert.equal(prepared.at(-3), old[1]);
    assert.notEqual(prepared.at(-3), newer[1]);
    await con.run("DEALLOCATE frozen_trace");
    await con.run(
      "INSERT INTO dws_gaap_di VALUES (DATE '2026-03-01','X','North','student','added',30,30)",
    );
    assert.equal((await objects(con, [], "User")).length, 11);
    await load(con, join(ROOT, "test/fixtures/population/bundle-20.json"));
    assert.equal((await objects(con, [], "User")).length, 12);
    await con.run("DELETE FROM dws_gaap_di WHERE user_id='added'");
    const baseDocs: Docs = [
        json<Core>(join(DEFINITIONS, "gaap.core.json")),
        json<Ontology>(join(DEFINITIONS, "gaap.ontology.threshold-10.json")),
        json<Mapping>(join(DEFINITIONS, "gaap.mapping.json")),
      ],
      rejected: string[] = [];
    async function rejection(
      name: string,
      path: string,
      reason: string,
      rollback = false,
    ) {
      // During an explicit transaction, read the old snapshot before the load aborts it.
      const before = await con.one(
        "SELECT snapshot_id FROM ossie_concepts() LIMIT 1",
      );
      await assert.rejects(load(con, path), (error: Error) => {
        assert(error.message.includes(reason), `${name}: ${error.message}`);
        for (const label of ["object=", "stage=", "missing_capability="])
          assert(error.message.includes(label), error.message);
        return true;
      });
      if (rollback) await con.run("ROLLBACK");
      assert.deepEqual(
        await con.one("SELECT snapshot_id FROM ossie_concepts() LIMIT 1"),
        before,
      );
      rejected.push(name);
    }
    await temporary("ossie-population-controls-", async (directory) => {
      const unchanged = await load(
          con,
          join(ROOT, "test/fixtures/population/bundle-10.json"),
        ),
        beforeIds = await objects(con, []),
        beforeValues = (await metric(con, []))[0],
        descriptive = structuredClone(baseDocs);
      for (const document of descriptive)
        document.description =
          "synthetic editorial change without a rule change";
      descriptive[0].ai_context = {
        instructions: "Synthetic alternative wording",
        examples: ["example"],
      };
      descriptive[0].custom_extensions = [
        {
          vendor: "quality-control",
          data: JSON.stringify({ unit: "CNY", scale: 1000 }),
        },
      ];
      for (const field of descriptive[0].datasets[0].fields) {
        field.description = "synthetic field description";
        field.label = "synthetic display label";
        field.ai_context = "synthetic vocabulary";
      }
      descriptive[0].metrics[0].description = "synthetic metric description";
      descriptive[0].metrics[0].custom_extensions = [
        { vendor: "quality-control", data: JSON.stringify({ unit: "CNY" }) },
      ];
      await assert.rejects(
        load(con, bundleFiles(directory, descriptive)),
        (error: Error) => {
          const failure = populationError(error.message);
          assert.equal(failure?.code, "LOAD_FAILED");
          assert.equal(failure?.stage, "load");
          assert(failure?.reason.includes('unknown field "vendor"'));
          return true;
        },
      );
      assert.deepEqual(await objects(con, []), beforeIds);
      assert.deepEqual((await metric(con, []))[0], beforeValues);
      rejected.push("legacy malformed vendor extension retains population");
      descriptive[0].custom_extensions = [
        {
          vendor_name: "quality-control",
          data: JSON.stringify({ unit: "CNY", scale: 1000 }),
        },
      ];
      descriptive[0].metrics[0].custom_extensions = [
        {
          vendor_name: "quality-control",
          data: JSON.stringify({ unit: "CNY" }),
        },
      ];
      const modified = await load(con, bundleFiles(directory, descriptive));
      assert.equal(modified[2], unchanged[2]);
      assert.deepEqual(await objects(con, []), beforeIds);
      assert.deepEqual((await metric(con, []))[0], beforeValues);
      await load(con, join(ROOT, "test/fixtures/population/bundle-10.json"));
      const mutations: Record<
        string,
        (c: Core, o: Ontology, m: Mapping) => void
      > = {
        root_requires: (_c, o) => {
          o.ontology[0].requires = ["TRUE"];
        },
        derived_requires: (_c, o) => {
          o.ontology[1].requires = ["TRUE"];
        },
        identity_requires: (_c, o) => {
          o.ontology[0].relationships![0].requires = ["TRUE"];
        },
        attribute_requires: (_c, o) => {
          o.ontology[0].relationships!.at(-1)!.requires = ["TRUE"];
        },
        value_type_requires: (_c, o) => {
          o.ontology.push({
            concept: "Decimal",
            type: "ValueType",
            requires: ["TRUE"],
          });
        },
        ontology_requires: (_c, o) => {
          o.requires = ["TRUE"];
        },
        multiple_inheritance: (_c, o) => {
          o.ontology[1].extends = ["User", "Other"];
        },
        derived_identity_override: (_c, o) => {
          o.ontology[1].identify_by = ["id"];
        },
        derived_relation_override: (_c, o) => {
          o.ontology[1].relationships = o.ontology[0].relationships;
        },
        derived_mapping: (_c, _o, m) => {
          m.concept_mappings.push({
            ...structuredClone(m.concept_mappings[0]),
            concept: "ValidUser",
          });
        },
        link_identity_override: (_c, _o, m) => {
          m.concept_mappings[0].link_mappings[0].object_mapping.referent_mappings![0].expression =
            "dws_gaap_di.region";
        },
        duplicate_binding: (_c, _o, m) => {
          const children = m.concept_mappings[0].link_mappings[0].children;
          children.push(structuredClone(children[0]));
        },
        bad_target: (_c, _o, m) => {
          m.concept_mappings[0].link_mappings[0].children[0].object_mapping.concept =
            "Decimal";
        },
        reference_name_mismatch: (_c, _o, m) => {
          m.ontology_ref.name = "other";
        },
        nul_core_field_expression: (c) => {
          c.datasets[0].fields.at(-1)!.expression.dialects[0].expression =
            "ytd_gaap\0 + 999";
        },
        nul_rule: (_c, o) => {
          o.ontology[1].derived_by = ["User.ytd_gaap >= 10\0 OR TRUE"];
        },
        unknown_version: (_c, o) => {
          o.version = "0.2.0";
        },
        unknown_rule_function: (_c, o) => {
          o.ontology[1].derived_by = ["random() > 0"];
        },
        aggregate_rule: (_c, o) => {
          o.ontology[1].derived_by = ["SUM(User.ytd_gaap) > 0"];
        },
        subquery_rule: (_c, o) => {
          o.ontology[1].derived_by = ["EXISTS(SELECT 1)"];
        },
        implicit_boolean_rule: (_c, o) => {
          o.ontology[1].derived_by = ["User.ytd_gaap"];
        },
        implicit_boolean_conjunction: (_c, o) => {
          o.ontology[1].derived_by = ["User.ytd_gaap AND TRUE"];
        },
        implicit_boolean_case: (_c, o) => {
          o.ontology[1].derived_by = [
            "CASE WHEN User.ytd_gaap THEN TRUE ELSE FALSE END",
          ];
        },
        hidden_aggregate_field: (c) => {
          c.datasets[0].fields.at(-1)!.expression.dialects[0].expression =
            "SUM(gaap_val)";
        },
        hidden_window_field: (c) => {
          c.datasets[0].fields.at(-1)!.expression.dialects[0].expression =
            "SUM(gaap_val) OVER ()";
        },
        hidden_subquery_field: (c) => {
          c.datasets[0].fields.at(-1)!.expression.dialects[0].expression =
            "(SELECT MAX(gaap_val) FROM elsewhere)";
        },
        hidden_unknown_field: (c) => {
          c.datasets[0].fields.at(-1)!.expression.dialects[0].expression =
            "random()";
        },
        metric_unknown_function: (c) => {
          c.metrics[0].expression.dialects[0].expression =
            "mystery(dws_gaap_di.gaap_val)";
        },
        metric_nested_aggregate: (c) => {
          c.metrics[0].expression.dialects[0].expression =
            "SUM(AVG(dws_gaap_di.gaap_val))";
        },
        metric_qualified_function: (c) => {
          c.metrics[0].expression.dialects[0].expression =
            "main.avg(dws_gaap_di.gaap_val)";
        },
        multi_dataset: (c) => {
          c.datasets.push({
            ...structuredClone(c.datasets[0]),
            name: "another",
          });
        },
        metric_without_aggregate: (c) => {
          c.metrics[0].expression.dialects[0].expression =
            "dws_gaap_di.gaap_val";
        },
        metric_type_mismatch: (c) => {
          c.metrics[0].datatype = "Decimal";
        },
        reserved_metric_case: (c) => {
          c.metrics[0].name = "_OSSIE_snapshot_id";
        },
        physical_type_mismatch: (c) => {
          c.datasets[0].fields[0].datatype = "String";
        },
        nonunique_attribute: (_c, o) => {
          o.ontology[0].relationships![2].multiplicity = "OneToOne";
        },
        missing_physical_column: (c) => {
          c.datasets[0].fields.at(-1)!.expression.dialects[0].expression =
            "missing_column";
        },
      };
      for (const [name, mutate] of Object.entries(mutations)) {
        const docs = structuredClone(baseDocs);
        mutate(...docs);
        await rejection(name, bundleFiles(directory, docs), "ossie_population");
      }
      let path = bundleFiles(directory, baseDocs),
        manifest = json<Manifest>(path);
      manifest.core.sha256 = "0".repeat(64);
      writeFileSync(path, JSON.stringify(manifest));
      await rejection("sha256_mismatch", path, "SHA-256 mismatch");
      path = bundleFiles(directory, baseDocs);
      manifest = json<Manifest>(path);
      manifest.core.name = "wrong_name";
      writeFileSync(path, JSON.stringify(manifest));
      await rejection(
        "manifest_name_mismatch",
        path,
        "manifest document name mismatch",
      );
      path = bundleFiles(directory, baseDocs);
      await con.run(
        "INSERT INTO dws_gaap_di SELECT * FROM dws_gaap_di WHERE user_id='e01'",
      );
      await rejection("duplicate_identical_row", path, "entire batch violates");
      await con.run("DELETE FROM dws_gaap_di WHERE user_id='e01'");
      await con.run(
        "INSERT INTO dws_gaap_di VALUES (DATE '2026-03-01','X','North','student','e01',10,10)",
      );
      await con.run(
        "INSERT INTO dws_gaap_di VALUES (DATE '2026-06-01','Y','North','student','e01',10,0)",
      );
      await rejection(
        "duplicate_id_outside_filter",
        path,
        "entire batch violates",
      );
      await con.run("DELETE FROM dws_gaap_di WHERE ds=DATE '2026-06-01'");
      await con.run(
        "INSERT INTO dws_gaap_di VALUES (DATE '2026-06-01','Y','North','student',NULL,10,0)",
      );
      await rejection("null_id_outside_filter", path, "entire batch violates");
      await con.run("DELETE FROM dws_gaap_di WHERE user_id IS NULL");
      await con.run("BEGIN TRANSACTION");
      await rejection(
        "explicit_transaction_load",
        path,
        "outside an explicit transaction",
        true,
      );
      await con.run("CREATE VIEW fact_view AS SELECT * FROM dws_gaap_di");
      let docs = structuredClone(baseDocs);
      docs[0].datasets[0].source = "memory.main.fact_view";
      await rejection(
        "view_source",
        bundleFiles(directory, docs),
        "native tables",
      );
      await con.run(
        "CREATE TEMP TABLE temp_facts AS SELECT * FROM dws_gaap_di",
      );
      docs[0].datasets[0].source = "temp.main.temp_facts";
      await rejection(
        "temp_source",
        bundleFiles(directory, docs),
        "ossie_population",
      );
      await con.run(
        "CREATE TABLE oversized AS SELECT ds,product,region,user_type,CAST(i AS VARCHAR) user_id,gaap_val,ytd_gaap FROM dws_gaap_di CROSS JOIN range(10001) r(i) LIMIT 10001",
      );
      docs[0].datasets[0].source = "memory.main.oversized";
      await rejection(
        "row_limit_no_truncation",
        bundleFiles(directory, docs),
        "exceeds 10000",
      );
      docs = structuredClone(baseDocs);
      docs[0].datasets[0].fields.at(-1)!.expression.dialects[0].expression =
        "ytd_gaap + 1";
      await con.run("UPDATE dws_gaap_di SET ytd_gaap=8.5 WHERE user_id='e02'");
      const once = await load(con, bundleFiles(directory, docs));
      assert(!(await objects(con, [])).includes("e02"));
      const compiled = await con.one(
        "SELECT sql FROM ossie_concept_compile('ValidUser',['record_count'],[],[])",
      );
      assert.equal(String(compiled[0]).split("+ 1").length - 1, 1);
      assert.equal(once[3], 11);
      await con.run(
        "UPDATE dws_gaap_di SET region='O''Brien' WHERE user_id='e01'",
      );
      await load(con, join(ROOT, "test/fixtures/population/bundle-10.json"));
      assert.deepEqual(
        await con.one(
          "SELECT \"dws_gaap_di.ds\", \"dws_gaap_di.gaap_val\", \"dws_gaap_di.region\" FROM ossie_objects('User', ['User.id = ''e01'''])",
        ),
        ["2026-03-01", "10.00", "O'Brien"],
      );
      assert.deepEqual(
        await con.one(
          "SELECT \"dws_gaap_di.gaap_val\" FROM ossie_objects('User',['User.id = ''e04'''])",
        ),
        [null],
      );
    });
    assert.equal(rejected.length, 47);
    await assert.rejects(
      con.run("SELECT * FROM system.main._ossie_snapshot_rows()"),
      /internal frozen AST dependency required/,
    );
    const beforeMetric = (await metric(con, []))[0];
    await con.run("CREATE MACRO avg(x) AS 999");
    assert.deepEqual((await metric(con, []))[0], beforeMetric);
    await con.run("DROP MACRO avg");
    const beforeCancel = await con.one(
      "SELECT snapshot_id FROM ossie_concepts() LIMIT 1",
    );
    await con.run(
      "CREATE TABLE cancel_facts AS SELECT DATE '2026-03-01' ds, 'X' product, repeat('r',1024) region, 'student' user_type, CAST(i AS VARCHAR) user_id, 10::DECIMAL(18,2) gaap_val, 10::DECIMAL(18,2) ytd_gaap FROM range(10000) r(i)",
    );
    let interrupts = 0;
    await temporary("ossie-cancel-", async (directory) => {
      const docs = structuredClone(baseDocs);
      docs[0].datasets[0].source = "memory.main.cancel_facts";
      const cancelPath = bundleFiles(directory, docs);
      const pending = load(con, cancelPath);
      const timer = setInterval(() => {
        interrupts++;
        con.connection.interrupt();
      }, 1);
      try {
        await assert.rejects(pending, /interrupt/i);
      } finally {
        clearInterval(timer);
      }
      assert(interrupts > 0, "timer never ran during native load");
      assert.deepEqual(
        await con.one("SELECT snapshot_id FROM ossie_concepts() LIMIT 1"),
        beforeCancel,
      );
    });
    await con.run("DROP TABLE cancel_facts");
    const expected = (await objects(con, [])).length;
    // Settle every started task before disconnecting. Each connection has one
    // outstanding query; independent connections still execute concurrently.
    const opened = await Promise.allSettled([
      con.instance.connect(),
      con.instance.connect(),
    ]);
    const connections = opened.flatMap((result) =>
      result.status === "fulfilled" ? [result.value] : [],
    );
    const failures: unknown[] = opened.flatMap((result) =>
      result.status === "rejected" ? [result.reason] : [],
    );
    let results: unknown[] = [];
    if (!failures.length) {
      const settled = await Promise.allSettled(
        connections.map(async (connection) => {
          const cursor = new Database(con.instance, connection);
          const values = [];
          for (let i = 0; i < 2; i++)
            values.push(
              (
                await cursor.one(
                  "SELECT count(*) FROM ossie_objects('ValidUser', [])",
                )
              )[0],
            );
          return values;
        }),
      );
      failures.push(
        ...settled.flatMap((result) =>
          result.status === "rejected" ? [result.reason] : [],
        ),
      );
      results = settled.flatMap((result) =>
        result.status === "fulfilled" ? result.value : [],
      );
    }
    for (const connection of connections) {
      try {
        connection.disconnectSync();
      } catch (error) {
        failures.push(error);
      }
    }
    if (failures.length) throw failures[0];
    assert.deepEqual(results, Array(4).fill(expected));
    await temporary("ossie-lifetime-", async (directory) => {
      const databasePath = join(directory, "frozen_lifetime.duckdb"),
        closing = await Database.open(databasePath);
      try {
        await closing.loadArtifact(artifact);
        await closing.run(TABLE);
        await closing.run(
          "INSERT INTO dws_gaap_di SELECT * FROM read_csv(?,header=true)",
          [boundary],
        );
        const docs = structuredClone(baseDocs);
        docs[0].datasets[0].source = "frozen_lifetime.main.dws_gaap_di";
        await load(closing, bundleFiles(directory, docs));
      } finally {
        closing.close();
      }
      // Parent stays alive while the separate stock binding reopens the same file.
      const reopened = spawnSync(
        process.execPath,
        [
          join(ROOT, "scripts/verify_population_artifact.ts"),
          "--reopen",
          databasePath,
        ],
        { cwd: ROOT, encoding: "utf8", timeout: 10000 },
      );
      assert.equal(reopened.status, 0, reopened.stderr);
    });
    const inputPaths = [
      original,
      boundary,
      join(EVIDENCE, "baseline-results.json"),
      join(EVIDENCE, "boundary-results.json"),
      ...readdirSync(DEFINITIONS)
        .filter((name) => name.endsWith(".json"))
        .sort()
        .map((name) => join(DEFINITIONS, name)),
      ...readdirSync(join(ROOT, "test/fixtures/population"))
        .filter((name) => name.endsWith(".json"))
        .sort()
        .map((name) => join(ROOT, "test/fixtures/population", name)),
      join(ROOT, "test/sql/ossie_population.test"),
      join(ROOT, "scripts/population_error.ts"),
    ];
    return {
      scope:
        "real single-table population execution; not full P0/A1-A8 acceptance",
      runtime: `Bun ${Bun.version}`,
      client: "@duckdb/node-api 1.5.6-r.1",
      duckdb_version: "1.5.6",
      duckdb_source_id: "069cc9f9b5",
      artifact_loaded_in_stock_node_bindings: true,
      artifact_sha256: digest(artifact),
      script_sha256: digest(import.meta.path),
      helper_sha256: digest(join(ROOT, "scripts/artifact_helpers.ts")),
      inputs_sha256: Object.fromEntries(
        inputPaths.map((path) => [relative(ROOT, path), digest(path)]),
      ),
      cases,
      boundary_cases: boundaryCases,
      empty_results: emptyResults,
      acceptance_evidence: {
        cases: ["A1", "A2", "A3", "A4"],
        full_object_and_group_results_saved: true,
        only_formal_derived_rule_changes: true,
        metric_mapping_and_request_template_unchanged: true,
        unit: "USD",
        unit_source: "original workbook metric!E2; correspondence review",
        decimal_sum_oracle: "independent bigint cents from immutable CSV",
        average_absolute_tolerance: 1e-10,
        query_order:
          "unspecified; object IDs sorted in verifier for comparison only",
        grouped_means_are_not_additive: true,
        autonomous_agent_verified: false,
      },
      numeric_oracle:
        "CSV parsed independently into bigint cents; exact SUM and count; DOUBLE AVG tolerance 1e-10",
      same_capture_content_for_thresholds: true,
      prepared_query_retains_original_snapshot: true,
      explicit_reload_observes_source_changes: true,
      failed_load_preserves_bundle: true,
      computed_field_expanded_once: true,
      typed_null_date_decimal_quoted_text_preserved: true,
      parallel_queries_threads_1: true,
      interrupted_load_preserves_bundle: true,
      native_interrupt_calls: interrupts,
      engine_macro_shadowing_prevented: true,
      diagnostic_sql_cannot_access_frozen_batch: true,
      descriptive_metadata_does_not_change_values: true,
      database_close_releases_native_file_lock: true,
      implementation_sources_sha256: Object.fromEntries(
        [
          join(ROOT, "CMakeLists.txt"),
          ...filesUnder(join(ROOT, "src")).filter((path) =>
            /\.(cpp|hpp)$/.test(path),
          ),
        ].map((path) => [relative(ROOT, path), digest(path)]),
      ),
      rejected_controls: rejected,
    };
  } finally {
    con.close();
  }
}
if (import.meta.main) {
  if (process.argv[2] === "--reopen") {
    const con = await Database.open(process.argv[3]);
    try {
      assert.deepEqual(await con.one("SELECT count(*) FROM dws_gaap_di"), [11]);
    } finally {
      con.close();
    }
  } else emitReport(await verify(resolve(process.argv[2])));
}

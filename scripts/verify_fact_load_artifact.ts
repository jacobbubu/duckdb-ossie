/** Production two-fact load/profile boundaries; frozen execution is checked by the native seam separately. */
import { readFileSync, writeFileSync, mkdirSync, existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { Database, ROOT, assert, digest } from "./artifact_helpers.ts";
import { fixedSchemaValidator } from "./check_fixed_schema.ts";
import { populationError } from "./population_error.ts";
const [artifactArg, outArg] = process.argv.slice(2);
assert(
  artifactArg && outArg && process.argv.length === 4,
  "ARTIFACT NEW_OUTPUT_DIRECTORY",
);
const artifact = resolve(artifactArg),
  out = resolve(outArg);
assert(!existsSync(out), "fresh output directory required");
mkdirSync(out, { recursive: true });
const fixture = join(ROOT, "test/fixtures/population-fact-program");
const original = Object.fromEntries(
  ["core", "ontology", "mapping"].map((kind) => [
    kind,
    JSON.parse(readFileSync(join(fixture, kind + ".json"), "utf8")),
  ]),
);
const originalManifest = JSON.parse(
  readFileSync(join(fixture, "bundle.json"), "utf8"),
);
const validate = fixedSchemaValidator(),
  db = await Database.open();
const checks: string[] = [],
  refusals: unknown[] = [];
let count = 0;
async function bundle(documents: Record<string, any>, manifest: any) {
  const n = ++count;
  const value = structuredClone(manifest);
  for (const kind of ["core", "ontology", "mapping"]) {
    const path = join(out, `${kind}-${n}.json`);
    writeFileSync(path, JSON.stringify(documents[kind], null, 2) + "\n");
    value[kind] = { name: documents[kind].name, path, sha256: digest(path) };
  }
  const path = join(out, `bundle-${n}.json`);
  writeFileSync(path, JSON.stringify(value, null, 2) + "\n");
  return path;
}
async function reject(
  name: string,
  mutate: (docs: Record<string, any>, manifest: any) => void,
) {
  const docs = structuredClone(original),
    manifest = structuredClone(originalManifest);
  mutate(docs, manifest);
  const path = await bundle(docs, manifest);
  let caught = false;
  try {
    await db.run("CALL ossie_bundle_load(?)", [path]);
  } catch (error) {
    const envelope = populationError(
      error instanceof Error ? error.message : String(error),
    );
    assert(envelope, "population error envelope required");
    assert(envelope.schema_version === "ossie-population-error-v1");
    refusals.push({ name, error: envelope });
    caught = true;
  }
  assert(caught, "expected refusal: " + name);
}
try {
  await db.run(`LOAD '${artifact.replaceAll("'", "''")}'`);
  await db.run(readFileSync(join(fixture, "rows.sql"), "utf8"));
  for (const kind of ["core", "ontology", "mapping"])
    assert(
      validate(kind as "core" | "ontology" | "mapping", original[kind])
        .accepted,
      "fixed schema: " + kind,
    );
  checks.push("fixed core/ontology/mapping schema accepted");
  const load = await db.rows("CALL ossie_bundle_load(?)", [
    join(fixture, "bundle.json"),
  ]);
  assert.equal(load[0]?.[0], "multi-fact-user-program-v1");
  assert.equal(load[0]?.[3], 18);
  const hash = load[0]?.[2];
  checks.push(
    "production artifact publishes two-fact parent with18 captured rows",
  );
  await reject(
    "unknown bare metric reference",
    (d) =>
      (d.core.metrics[0].expression.dialects[0].expression = "missing-expense"),
  );
  await reject(
    "metric cycle",
    (d) => (d.core.metrics[1].expression.dialects[0].expression = "balance"),
  );
  await reject(
    "window omitted from classification",
    (_, m) => (m.program.windows = ["previous"]),
  );
  await reject("non-window classified as window", (_, m) =>
    m.program.windows.push("income"),
  );
  await reject("case duplicate window", (_, m) =>
    m.program.windows.push("PREVIOUS"),
  );
  await reject("unknown classified window", (_, m) =>
    m.program.windows.push("missing"),
  );
  await reject(
    "ordinary metric depends on window",
    (d) =>
      (d.core.metrics[0].expression.dialects[0].expression =
        "previous-expense"),
  );
  await reject(
    "missing product from full physical key",
    (d) => (d.core.datasets[1].primary_key = ["who", "day"]),
  );
  await reject(
    "swapped physical key order",
    (d) => (d.core.datasets[0].primary_key = ["day", "who"]),
  );
  await reject(
    "computed source role field",
    (d) =>
      (d.core.datasets[0].fields[0].expression.dialects[0].expression =
        "upper(who)"),
  );
  await reject(
    "currency mismatch",
    (_, m) => (m.program.facts[1].currency = "EUR"),
  );
  await reject(
    "missing explicit unit",
    (_, m) => delete m.program.facts[0].currency,
  );
  await reject(
    "root concept override",
    (_, m) => (m.program.facts[0].concept = "Other"),
  );
  await reject(
    "same dataset twice",
    (_, m) => (m.program.facts[1].dataset = "revenue"),
  );
  await reject(
    "same underlying native table twice",
    (d) => (d.core.datasets[1].source = "memory.main.revenue"),
  );
  await db.run(
    "ATTACH ':memory:' AS foreign_db; CREATE TABLE foreign_db.cost AS SELECT * FROM cost",
  );
  await reject(
    "different underlying source databases",
    (d) => (d.core.datasets[1].source = "foreign_db.main.cost"),
  );
  await db.run("DETACH foreign_db");
  await reject(
    "deep expression before core lowering",
    (d) =>
      (d.core.metrics[0].expression.dialects[0].expression = Array.from(
        { length: 34 },
        () => "income",
      ).join(" + ")),
  );
  await reject(
    "nested User identity override",
    (d) =>
      (d.mapping.concept_mappings[1].link_mappings[0].children[0].object_mapping.referent_mappings[0].relationship =
        "other_id"),
  );
  await reject(
    "root object versus link identity mismatch",
    (d) =>
      (d.mapping.concept_mappings[0].object_mappings[0].referent_mappings[0].referent_mappings[0].expression =
        "revenue.amount"),
  );
  await reject("missing mapped amount link", (d) =>
    d.mapping.concept_mappings[0].link_mappings[0].children.pop(),
  );
  await reject(
    "wrong ontology User role",
    (d) => (d.ontology.ontology[1].relationships[0].roles[0].concept = "Other"),
  );
  await reject("unbound extra ontology relationship", (d) =>
    d.ontology.ontology[1].relationships.push({
      ...d.ontology.ontology[1].relationships[2],
      name: "other_amount",
    }),
  );
  await reject(
    "incomplete ontology identity",
    (d) => (d.ontology.ontology[2].identify_by = ["user", "date"]),
  );
  await reject(
    "metric declared Float versus bound Decimal",
    (d) => (d.core.metrics[0].datatype = "Float"),
  );
  await reject(
    "window declared Integer versus actual Decimal",
    (d) => (d.core.metrics[7].datatype = "Integer"),
  );
  await reject(
    "ANSI-only declaration",
    (d) => (d.core.metrics[1].expression.dialects[0].dialect = "ANSI_SQL"),
  );
  await reject(
    "oversized expression before core parse",
    (d) =>
      (d.core.metrics[0].expression.dialects[0].expression = " ".repeat(16385)),
  );
  await reject(
    "raw cross-fact aggregate leaf",
    (d) =>
      (d.core.metrics[0].expression.dialects[0].expression =
        "SUM(revenue.amount-cost.amount)"),
  );
  const after = await db.rows("CALL ossie_bundle_load(?)", [
    join(fixture, "bundle.json"),
  ]);
  assert.equal(after[0]?.[2], hash);
  checks.push(
    "valid reload after refused definitions reproduces original paired hash",
  );
  let old = false;
  try {
    await db.rows("SELECT * FROM ossie_load(?)", [join(fixture, "core.json")]);
  } catch {
    old = true;
  }
  assert(old);
  checks.push("default old core entry still refuses bare metric dependencies");
  writeFileSync(
    join(out, "verification.json"),
    JSON.stringify(
      {
        passed: true,
        scope:
          "production artifact typed dual capture and strict profile definitions; no public objects/MCP or frozen execution claim",
        artifact_sha256: digest(artifact),
        checks,
        refusals,
        source_sha256: Object.fromEntries(
          [
            "scripts/verify_fact_load_artifact.ts",
            "src/population/population.cpp",
            "src/model/parser.cpp",
            "src/model/validate.cpp",
          ].map((p) => [p, digest(join(ROOT, p))]),
        ),
        real_model_requests: 0,
      },
      null,
      2,
    ) + "\n",
  );
  console.log(
    `PASS fact load ${checks.length} checks/${refusals.length} refusals`,
  );
} finally {
  db.close();
}

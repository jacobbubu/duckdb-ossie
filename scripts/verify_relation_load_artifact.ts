/** Actual frozen relation-profile load/constraints; no object-interface claim. */
import { mkdirSync, readFileSync, writeFileSync, existsSync } from "node:fs";
import { resolve, join, relative } from "node:path";
import { Database, ROOT, assert, digest } from "./artifact_helpers.ts";
import { populationError } from "./population_error.ts";
import { fixedSchemaValidator } from "./check_fixed_schema.ts";
const [artifactArg, directoryArg] = process.argv.slice(2);
assert(
  artifactArg && directoryArg && process.argv.length === 4,
  "ARTIFACT NEW_DIR required",
);
const artifact = resolve(artifactArg),
  directory = resolve(directoryArg);
assert(!existsSync(directory), "new output directory required");
mkdirSync(directory, { recursive: true });
const fixture = join(ROOT, "test/fixtures/population-observation");
const csv = join(
  ROOT,
  "docs/research/ossie-req-001/multi-period-research/every-day/observations.csv",
);
const base = JSON.parse(
  readFileSync(
    join(
      ROOT,
      "docs/research/ossie-req-001/p1-program-research/candidate-ontology.json",
    ),
    "utf8",
  ),
);
const db = await Database.open();
const checks: string[] = [],
  refusals: unknown[] = [];
let variant = 0;
async function manifest(
  ontology: any,
  profile = "observation-relation-program-v1",
) {
  assert(
    fixedSchemaValidator()("ontology", ontology).accepted,
    "fixed ontology schema",
  );
  const name = `ontology-${variant++}.json`;
  writeFileSync(
    join(directory, name),
    JSON.stringify(ontology, null, 2) + "\n",
  );
  const bundle = JSON.parse(
    readFileSync(join(fixture, "bundle-10.json"), "utf8"),
  );
  bundle.profile = profile;
  for (const kind of ["core", "mapping"])
    bundle[kind].path = relative(directory, join(fixture, bundle[kind].path));
  bundle.ontology.path = name;
  bundle.ontology.sha256 = digest(join(directory, name));
  const p = join(directory, `bundle-${variant}.json`);
  writeFileSync(p, JSON.stringify(bundle, null, 2) + "\n");
  return p;
}
const root = (o: any) =>
  o.ontology.find((c: any) => c.concept === "DailyObservation");
const relation = (o: any, n: string) =>
  root(o).relationships.find((r: any) => r.name === n);
async function program() {
  return JSON.parse(
    String(
      (await db.one("SELECT program_json FROM ossie_relation_program()"))[0],
    ),
  );
}
async function refuse(o: any, code: string, label: string, profile?: string) {
  const before = await program();
  let error: unknown;
  try {
    await db.run("CALL ossie_bundle_load(?)", [await manifest(o, profile)]);
  } catch (e) {
    error = e;
  }
  assert(error, label);
  const decoded = populationError(String(error));
  assert.equal(decoded?.code, code, label);
  assert.equal(
    (await program()).snapshot_id,
    before.snapshot_id,
    label + " retains snapshot",
  );
  refusals.push({ label, error: decoded, old_snapshot_retained: true });
}
try {
  const engine = await db.loadArtifact(artifact);
  await db.run(
    "CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER)",
  );
  await db.run(
    "INSERT INTO observations SELECT * FROM read_csv(?,header=true)",
    [csv],
  );
  await db.run("CALL ossie_bundle_load(?)", [join(fixture, "bundle-10.json")]);
  await db.run(
    "PREPARE old_objects AS SELECT count(*) FROM ossie_objects('QualifiedObservation', [])",
  );
  await db.run("CALL ossie_bundle_load(?)", [await manifest(base)]);
  assert.equal((await db.one("EXECUTE old_objects"))[0], 18);
  checks.push(
    "prepared old observation query retains old profile after relation publication",
  );
  const initial = await program();
  assert.equal(initial.profile, "observation-relation-program-v1");
  assert.equal(initial.captured_rows, "23");
  assert.deepEqual(initial.root_identity_bound_types, [
    "VARCHAR",
    "DATE",
    "VARCHAR",
  ]);
  assert.equal(initial.source_types_bound, true);
  assert.equal(initial.load_constraints_validated, true);
  assert.equal(initial.query_executed, false);
  assert.equal(initial.objects_queryable, false);
  assert.deepEqual(
    initial.relations.map((r: any) => r.name),
    ["qualified_user", "audited_user"],
  );
  assert.deepEqual(initial.relations[1].dependencies, ["qualified_user"]);
  checks.push(
    "actual source-bound relation DAG loaded, full constraints validated, discovery not query execution",
  );
  await db.run(
    "INSERT INTO observations VALUES('bad','BAD',DATE '2026-02-01','X',NULL,-1)",
  );
  await refuse(
    base,
    "CONSTRAINT_VIOLATION",
    "root requires sees violation excluded by qualification",
  );
  await db.run("DELETE FROM observations WHERE user_id='BAD'");
  const linkBad = structuredClone(base);
  relation(linkBad, "qualified_user").requires = [
    "DailyObservation.classification_value IS NULL OR DailyObservation.classification_value >= 20",
  ];
  await refuse(
    linkBad,
    "CONSTRAINT_VIOLATION",
    "requires validates actual qualified link population",
  );
  const linkGood = structuredClone(base);
  relation(linkGood, "qualified_user").requires = [
    "DailyObservation.classification_value IS NOT NULL AND DailyObservation.classification_value >= 10",
  ];
  await db.run("CALL ossie_bundle_load(?)", [await manifest(linkGood)]);
  assert.equal((await program()).relations[0].link_constraints.length, 1);
  checks.push(
    "link constraints can be valid while root still includes low/NULL qualification values",
  );
  const empty = structuredClone(linkBad);
  relation(empty, "qualified_user").derived_by = [
    "DailyObservation.classification_value >= 1000 AND DailyObservation.user(User)",
  ];
  await db.run("CALL ossie_bundle_load(?)", [await manifest(empty)]);
  checks.push(
    "empty actual link population satisfies link requires vacuously; no existence claim",
  );
  const cycle = structuredClone(base);
  relation(cycle, "qualified_user").derived_by = [
    "DailyObservation.audited_user(User)",
  ];
  await refuse(cycle, "RULE_REJECTED", "cycle refused before publication");
  const role = structuredClone(base);
  relation(role, "qualified_user").derived_by = [
    "DailyObservation.user(Other)",
  ];
  await refuse(role, "RULE_REJECTED", "unbound User role refused");
  const unknown = structuredClone(base);
  relation(unknown, "qualified_user").derived_by = [
    "DailyObservation.missing(User)",
  ];
  await refuse(unknown, "RULE_REJECTED", "unknown dependency refused");
  const duplicate = structuredClone(base);
  relation(duplicate, "qualified_user").name = "product";
  await refuse(
    duplicate,
    "UNSUPPORTED_CAPABILITY",
    "derived name cannot shadow a mapped noninteger relationship",
  );
  await refuse(
    base,
    "UNSUPPORTED_CAPABILITY",
    "old profile continues refusing derived relations",
    "daily-observation-population-v1",
  );
  const total = structuredClone(base);
  root(total).requires = ["DailyObservation.classification_value >= 0"];
  await refuse(total, "RULE_REJECTED", "unguarded nullable constraint refused");
  await db.run(
    "ALTER TABLE observations ALTER classification_value TYPE VARCHAR",
  );
  await refuse(
    base,
    "INVALID_REQUEST",
    "actual captured scalar type mismatch refused before program binding",
  );
  await db.run(
    "ALTER TABLE observations ALTER classification_value TYPE INTEGER",
  );
  await db.run(
    "INSERT INTO observations SELECT 'duplicate',user_id,ds,product,amount_usd,classification_value FROM observations LIMIT 1",
  );
  await refuse(
    base,
    "IDENTITY_VIOLATION",
    "full source tuple duplicate rejected before constraints",
  );
  await db.run("DELETE FROM observations WHERE observation_id='duplicate'");
  const preparedSnapshot = await program();
  await db.run(
    "PREPARE old_program AS SELECT program_json FROM ossie_relation_program()",
  );
  await db.run("CALL ossie_bundle_load(?)", [await manifest(linkGood)]);
  assert.notEqual((await program()).snapshot_id, preparedSnapshot.snapshot_id);
  assert.deepEqual(
    JSON.parse(String((await db.one("EXECUTE old_program"))[0])),
    preparedSnapshot,
  );
  checks.push(
    "prepared discovery retains its bound program across successful reload",
  );
  const beforeDrop = await program();
  await db.run("DROP TABLE observations");
  assert.deepEqual(await program(), beforeDrop);
  checks.push(
    "source deletion does not change frozen discovery or prior validation",
  );
  for (const sql of [
    "SELECT * FROM ossie_concepts_v2()",
    "SELECT * FROM ossie_population_capabilities()",
    "SELECT * FROM ossie_objects('DailyObservation',[])",
    "SELECT * FROM ossie_daily_users_json('QualifiedObservation','{}')",
  ]) {
    let error: unknown;
    try {
      await db.rows(sql);
    } catch (e) {
      error = e;
    }
    assert.equal(
      populationError(String(error))?.code,
      "UNSUPPORTED_CAPABILITY",
    );
    refusals.push({
      label: "new profile old query surface explicitly refused",
      sql,
    });
  }
  checks.push(
    "pending old query/capability surfaces explicitly refuse new profile",
  );
  const report = {
    passed: true,
    scope: "relation program load and discovery only, not full P1 or objects",
    engine,
    artifact_sha256: digest(artifact),
    checks,
    refusals,
    initial_program: initial,
    final_program: await program(),
    source_sha256: digest(csv),
    real_model_calls: 0,
  };
  writeFileSync(
    join(directory, "verification.json"),
    JSON.stringify(report, null, 2) + "\n",
  );
  console.log(
    `PASS relation load ${checks.length} checks ${refusals.length} refusals`,
  );
} finally {
  db.close();
}

/** Independent full-link oracle for the actual frozen relation API. */
import {
  existsSync,
  mkdirSync,
  readFileSync,
  writeFileSync,
  realpathSync,
} from "node:fs";
import { join, relative, resolve } from "node:path";
import {
  Database,
  ROOT,
  assert,
  digest,
  type Parameter,
} from "./artifact_helpers.ts";
import { populationError } from "./population_error.ts";
const [artifactArg, outputArg] = process.argv.slice(2);
assert(
  artifactArg && outputArg && process.argv.length === 4,
  "ARTIFACT NEW_DIRECTORY",
);
const artifact = resolve(artifactArg),
  requestedDirectory = resolve(outputArg);
assert(!existsSync(requestedDirectory), "fresh evidence directory required");
mkdirSync(requestedDirectory, { recursive: true });
const directory = realpathSync(requestedDirectory);
const fixture = join(ROOT, "test/fixtures/population-observation");
const ontology = JSON.parse(
  readFileSync(
    join(ROOT, "test/fixtures/population-relation-program/ontology.json"),
    "utf8",
  ),
);
const csv = join(
  ROOT,
  "docs/research/ossie-req-001/multi-period-research/every-day/observations.csv",
);
const observations = readFileSync(csv, "utf8")
  .trim()
  .split("\n")
  .slice(1)
  .map((r) => r.split(","));
const compare = (a: string[], b: string[]) => {
  for (let i = 0; i < 4; i++) {
    const order = Buffer.compare(Buffer.from(a[i]), Buffer.from(b[i]));
    if (order) return order;
  }
  return 0;
};
const expected = (threshold: number) =>
  observations
    .filter((r) => r[5] !== "" && Number(r[5]) >= threshold)
    .map((r) => [r[1], r[2], r[3], r[1]])
    .sort(compare);
const db = await Database.open();
const checks: string[] = [],
  refusals: unknown[] = [],
  cases: unknown[] = [];
let serial = 0;
const root = (o: any) =>
  o.ontology.find((c: any) => c.concept === "DailyObservation");
const relation = (o: any, n: string) =>
  root(o).relationships.find((r: any) => r.name === n);
async function load(o: any) {
  const file = `ontology-${serial++}.json`;
  writeFileSync(join(directory, file), JSON.stringify(o, null, 2) + "\n");
  const m = JSON.parse(readFileSync(join(fixture, "bundle-10.json"), "utf8"));
  m.profile = "observation-relation-program-v1";
  for (const k of ["core", "mapping"])
    m[k].path = relative(directory, join(fixture, m[k].path));
  m.ontology.path = file;
  m.ontology.sha256 = digest(join(directory, file));
  const path = join(directory, `bundle-${serial}.json`);
  writeFileSync(path, JSON.stringify(m, null, 2) + "\n");
  await db.run("CALL ossie_bundle_load(?)", [path]);
}
async function json(functionName: string, name: string) {
  return JSON.parse(
    String((await db.one(`SELECT * FROM ${functionName}(?)`, [name]))[0]),
  );
}
async function check(name: string, threshold: number, label: string) {
  const result = await json("ossie_relation_objects_json", name),
    plan = await json("ossie_relation_compile", name);
  assert.deepEqual(result.links, expected(threshold));
  assert.equal(result.link_count, String(expected(threshold).length));
  assert.equal(result.executed, true);
  assert.equal(result.members_computed, true);
  assert.equal(result.target_identity_relationship, "id");
  assert.equal(result.target_identity_field, "user_id");
  assert.equal(result.target_identity_bound_type, "VARCHAR");
  assert.deepEqual(result.root_identity_bound_types, [
    "VARCHAR",
    "DATE",
    "VARCHAR",
  ]);
  assert.equal(plan.executed, false);
  assert.equal(plan.members_computed, false);
  assert.equal(plan.full_link_projection_types_bound, true);
  assert(!("links" in plan));
  assert(!("link_count" in plan));
  for (const k of [
    "relationship",
    "bound_predicate",
    "scope",
    "derived_by",
    "dependencies",
    "snapshot_id",
    "content_sha256",
    "core_sha256",
    "ontology_sha256",
    "mapping_sha256",
    "manifest_sha256",
  ]) {
    assert.deepEqual(result[k], plan[k], k);
  }
  assert.equal(result.paging, false);
  assert.equal(result.truncated, false);
  cases.push({ label, result, plan, independent_links: expected(threshold) });
  checks.push(label);
}
async function refusal(
  sql: string,
  args: Parameter[],
  code: string,
  label: string,
) {
  let failed: unknown;
  try {
    await db.rows(sql, args);
  } catch (e) {
    failed = e;
  }
  assert(failed, label);
  const error = populationError(String(failed));
  assert.equal(error?.code, code, label);
  refusals.push({ label, error });
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
  await load(ontology);
  await check(
    "qualified_user",
    10,
    "full18 source-root/target links match independent oracle",
  );
  await check(
    "AUDITED_USER",
    10,
    "dependent18 links, case-normalized lookup and sameUserdate/product preservation",
  );
  const oldResult = await json("ossie_relation_objects_json", "audited_user"),
    oldPlan = await json("ossie_relation_compile", "audited_user");
  await db.run(
    "PREPARE old_links AS SELECT result_json FROM ossie_relation_objects_json('audited_user')",
  );
  await db.run(
    "PREPARE old_plan AS SELECT plan_json FROM ossie_relation_compile('audited_user')",
  );
  const twenty = structuredClone(ontology);
  relation(twenty, "qualified_user").derived_by = [
    "DailyObservation.classification_value >= 20 AND DailyObservation.user(User)",
  ];
  await load(twenty);
  await check("audited_user", 20, "upstream rule20 propagates complete7 links");
  assert.deepEqual(
    JSON.parse(String((await db.one("EXECUTE old_links"))[0])),
    oldResult,
  );
  assert.deepEqual(
    JSON.parse(String((await db.one("EXECUTE old_plan"))[0])),
    oldPlan,
  );
  checks.push(
    "prepared old result and plan retain rule/snapshot after successful reload",
  );
  const union = structuredClone(ontology);
  relation(union, "qualified_user").derived_by.push(
    "DailyObservation.classification_value >= 3 AND DailyObservation.user(User)",
  );
  await load(union);
  await check(
    "audited_user",
    3,
    "downstream consumes upstream union, duplicate derivations do not duplicate links and old qualification10 not imposed",
  );
  const renamed = structuredClone(union);
  relation(renamed, "qualified_user").name = "eligible_user";
  relation(renamed, "audited_user").derived_by = [
    "DailyObservation.eligible_user(User)",
  ];
  relation(renamed, "audited_user").name = "reviewed_user";
  await load(renamed);
  await check(
    "reviewed_user",
    3,
    "renamed declared dependency remains executable",
  );
  await refusal(
    "SELECT * FROM ossie_relation_objects_json(?)",
    ["missing"],
    "INVALID_REQUEST",
    "unknown relationship refused",
  );
  await refusal(
    "SELECT * FROM ossie_relation_compile(NULL)",
    [],
    "INVALID_REQUEST",
    "NULL relationship refused",
  );
  const beforeDrop = await json("ossie_relation_objects_json", "reviewed_user");
  await db.run("DROP TABLE observations");
  assert.deepEqual(
    await json("ossie_relation_objects_json", "reviewed_user"),
    beforeDrop,
  );
  assert.equal(
    (await json("ossie_relation_compile", "reviewed_user")).snapshot_id,
    beforeDrop.snapshot_id,
  );
  checks.push("objects and compile remain source-independent after drop");
  await db.run(
    "CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER)",
  );
  const strange = [
    ["é", "2026-01-01", "x"],
    ["a\u0000", "2026-02-01", "p\n"],
    ["A", "2026-01-01", '"\\'],
    ["a", "2026-01-01", "x"],
  ];
  for (let i = 0; i < strange.length; i++)
    await db.run("INSERT INTO observations VALUES(?,?,?,?,NULL,10)", [
      String(i),
      ...strange[i],
    ]);
  await db.run("SET GLOBAL default_collation='nocase'");
  await load(ontology);
  const raw = await json("ossie_relation_objects_json", "audited_user");
  assert.deepEqual(raw.links, strange.map((r) => [...r, r[0]]).sort(compare));
  checks.push(
    "raw UTF8/case/NUL/control/quote identities sorted without caller nocase merging",
  );
  await db.run("DELETE FROM observations");
  await db.run(
    "INSERT INTO observations VALUES('future','D',DATE '10000-01-01','X',NULL,10),('bc','D',DATE '0001-01-01 (BC)','X',NULL,10),('present','D',DATE '2026-01-01','X',NULL,10)",
  );
  await load(ontology);
  const wideDates = await json("ossie_relation_objects_json", "audited_user");
  assert.deepEqual(wideDates.links, [
    ["D", "0001-01-01 (BC)", "X", "D"],
    ["D", "2026-01-01", "X", "D"],
    ["D", "10000-01-01", "X", "D"],
  ]);
  checks.push(
    "finite BC and year10000 DATE identities preserve typed chronological order",
  );
  await db.run("DELETE FROM observations");
  await load(ontology);
  const empty = await json("ossie_relation_objects_json", "audited_user");
  assert.deepEqual(empty.links, []);
  assert.equal(empty.link_count, "0");
  checks.push("empty complete result retains metadata");
  await db.run(
    "INSERT INTO observations SELECT 'o'||i::VARCHAR, 'U'||lpad(i::VARCHAR,5,'0'), DATE '2026-01-01','X',NULL,10 FROM range(10000) r(i)",
  );
  await load(ontology);
  const full = await json("ossie_relation_objects_json", "audited_user");
  const fullExpected = Array.from({ length: 10000 }, (_, i) => {
    const id = `U${String(i).padStart(5, "0")}`;
    return [id, "2026-01-01", "X", id];
  });
  assert.deepEqual(full.links, fullExpected);
  assert.equal(full.link_count, "10000");
  checks.push(
    "10000 complete root/target links returned without paging or truncation",
  );
  await db.run("DELETE FROM observations");
  // Product is represented once; tune its raw ASCII bytes against complete output metadata.
  await db.run(
    "INSERT INTO observations VALUES('padding','P',DATE '2026-01-01','p',NULL,10)",
  );
  await load(ontology);
  const baseline = String(
    (
      await db.one(
        "SELECT result_json FROM ossie_relation_objects_json('audited_user')",
      )
    )[0],
  );
  // Reload increments the snapshot sequence; its decimal width is also output metadata.
  const sequence = String(JSON.parse(baseline).snapshot_id).split(":").at(-1)!;
  const sequenceGrowth = String(BigInt(sequence) + 1n).length - sequence.length;
  const length = 1048576 - Buffer.byteLength(baseline) + 1 - sequenceGrowth;
  await db.run("UPDATE observations SET product=?", ["p".repeat(length)]);
  await load(ontology);
  const exact = String(
    (
      await db.one(
        "SELECT result_json FROM ossie_relation_objects_json('audited_user')",
      )
    )[0],
  );
  assert.equal(Buffer.byteLength(exact), 1048576);
  checks.push("exact1MiB full result includes metadata and escaped links");
  await db.run("UPDATE observations SET product=product || 'p'");
  await load(ontology);
  const boundPlan = await json("ossie_relation_compile", "audited_user");
  assert.equal(boundPlan.members_computed, false);
  await refusal(
    "SELECT * FROM ossie_relation_objects_json('audited_user')",
    [],
    "RESOURCE_LIMIT_EXCEEDED",
    "1MiB plus1 refused while compile remains available",
  );
  await db.run("UPDATE observations SET product=?", ["\n".repeat(600000)]);
  await load(ontology);
  assert.equal(
    (await json("ossie_relation_compile", "audited_user")).members_computed,
    false,
  );
  await refusal(
    "SELECT * FROM ossie_relation_objects_json('audited_user')",
    [],
    "RESOURCE_LIMIT_EXCEEDED",
    "escaped UTF8 JSON expansion refused despite raw value below1MiB",
  );
  await db.run("CALL ossie_bundle_load(?)", [join(fixture, "bundle-10.json")]);
  await refusal(
    "SELECT * FROM ossie_relation_compile('audited_user')",
    [],
    "UNSUPPORTED_CAPABILITY",
    "old profile relation operation refusal",
  );
  const report = {
    passed: true,
    scope: "relation objects/compile SQL only, not complete P1 or MCP",
    engine,
    artifact_sha256: digest(artifact),
    checks,
    refusals,
    cases,
    input_sha256: digest(csv),
    exact_result_bytes: Buffer.byteLength(exact),
    real_model_calls: 0,
  };
  writeFileSync(
    join(directory, "verification.json"),
    JSON.stringify(report, null, 2) + "\n",
  );
  console.log(
    `PASS relation objects ${checks.length} checks ${refusals.length} refusals`,
  );
} finally {
  db.close();
}

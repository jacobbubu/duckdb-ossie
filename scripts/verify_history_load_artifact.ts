/** Real typed double capture/index and failed replacement controls; links checked separately. */
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { Database, ROOT, digest, assert } from "./artifact_helpers.ts";
import { populationError } from "./population_error.ts";
const [artifactArg, outArg] = process.argv.slice(2);
assert(artifactArg && outArg && process.argv.length === 4);
const artifact = resolve(artifactArg),
  out = resolve(outArg);
assert(!existsSync(out), "fresh directory required");
mkdirSync(out, { recursive: true });
const fixture = join(ROOT, "test/fixtures/population-history-program");
const db = await Database.open();
const checks: string[] = [],
  refusals: unknown[] = [];
let serial = 0;
const read = (n: string) => JSON.parse(readFileSync(join(fixture, n), "utf8"));
async function load(definitions?: {
  core?: any;
  ontology?: any;
  mapping?: any;
  program?: any;
}) {
  if (!definitions) {
    await db.run("CALL ossie_bundle_load(?)", [join(fixture, "bundle.json")]);
    return;
  }
  const manifest = read("bundle.json");
  for (const kind of ["core", "ontology", "mapping"] as const) {
    const file = join(out, `${kind}-${serial}.json`);
    writeFileSync(
      file,
      JSON.stringify(definitions[kind] ?? read(kind + ".json"), null, 2) + "\n",
    );
    manifest[kind].path = file;
    manifest[kind].sha256 = digest(file);
  }
  if (definitions.program) manifest.program = definitions.program;
  const file = join(out, `bundle-${serial++}.json`);
  writeFileSync(file, JSON.stringify(manifest, null, 2) + "\n");
  await db.run("CALL ossie_bundle_load(?)", [file]);
}
async function program() {
  const [raw] = await db.one("SELECT * FROM ossie_history_program()");
  return JSON.parse(String(raw));
}
async function refuse(
  label: string,
  action: () => Promise<unknown>,
  code: string,
) {
  const before = await program();
  try {
    await action();
  } catch (error) {
    assert.equal(populationError(String(error))?.code, code, label);
    assert.deepEqual(
      await program(),
      before,
      "failed replacement retained " + label,
    );
    refusals.push({ label, code });
    return;
  }
  throw Error("unexpectedly accepted " + label);
}
try {
  const engine = await db.loadArtifact(artifact);
  await db.run(
    "CREATE TABLE observations(user_id VARCHAR,ds DATE,product VARCHAR)",
  );
  await db.run(
    "CREATE TABLE history(record_id VARCHAR,user_id VARCHAR,team_id VARCHAR,starts DATE,ends DATE)",
  );
  await db.run(
    "INSERT INTO observations VALUES ('u',DATE '2024-02-28','X'),('u',DATE '2024-02-29','Y')",
  );
  await db.run(
    "INSERT INTO history VALUES ('a','u','TeamA',DATE '2024-02-28',DATE '2024-03-01'),('b','u','TeamA',DATE '2024-02-29',DATE '2024-03-02'),('c','u','TeamB',DATE '2024-03-02',DATE '2024-03-03')",
  );
  await load();
  const initial = await program();
  assert(
    initial.query_executed === false &&
      initial.query_supported === true &&
      initial.compile_supported === true &&
      initial.mcp_registered === false,
  );
  assert.equal(initial.total_source_rows, "5");
  assert.equal(initial.history_record_count, "3");
  assert.equal(initial.index_segment_count, "2");
  assert.deepEqual(
    initial.sources.map((x: any) => x.rows),
    ["2", "3"],
  );
  assert.deepEqual(
    initial.sources[1].roles.map((x: any) => [x.role, x.bound_type]),
    [
      ["record_id", "VARCHAR"],
      ["user", "VARCHAR"],
      ["team", "VARCHAR"],
      ["starts", "DATE"],
      ["ends", "DATE"],
    ],
  );
  checks.push(
    "actual named source roles/types, original source rows and merged index counts, nonexecuting honest program",
  );
  await db.run(
    "INSERT INTO history VALUES ('d','without-observations','TeamA',DATE '2024-01-01',DATE '2024-03-01'),('e','without-observations','TeamB',DATE '2024-02-01',DATE '2024-04-01')",
  );
  await refuse(
    "whole-history conflict for User with no observations",
    () => load(),
    "CONSTRAINT_VIOLATION",
  );
  await db.run("DELETE FROM history WHERE record_id IN ('d','e')");
  await db.run("INSERT INTO history SELECT * FROM history WHERE record_id='a'");
  await refuse(
    "identical duplicate global record_id",
    () => load(),
    "IDENTITY_VIOLATION",
  );
  await db.run("DELETE FROM history WHERE record_id='a'");
  await db.run(
    "INSERT INTO history VALUES ('a','u','TeamA',DATE '2024-02-28',DATE '2024-03-01')",
  );
  await db.run(
    "INSERT INTO history VALUES ('bad','u','TeamA',DATE '2024-02-28',DATE '2024-02-28')",
  );
  await refuse("empty interval", () => load(), "CONSTRAINT_VIOLATION");
  await db.run("DELETE FROM history WHERE record_id='bad'");
  await db.run("INSERT INTO observations SELECT * FROM observations LIMIT 1");
  await refuse(
    "duplicate complete observation tuple",
    () => load(),
    "IDENTITY_VIOLATION",
  );
  await db.run("DELETE FROM observations");
  await db.run("INSERT INTO observations VALUES ('u',DATE '2024-02-28','X')");
  const wrong = read("mapping.json");
  wrong.concept_mappings[1].link_mappings[0].children.find(
    (r: any) => r.relationship === "team",
  ).object_mapping.concept = "User";
  await refuse(
    "typed target differs despite same scalar string identity",
    () => load({ mapping: wrong }),
    "MAPPING_INVALID",
  );
  const ignored = read("ontology.json");
  ignored.ontology[2].relationships.find(
    (r: any) => r.name === "team_at_observation",
  ).derived_by = ["TRUE"];
  await refuse(
    "unexecuted ontology rule not ignored",
    () => load({ ontology: ignored }),
    "UNSUPPORTED_CAPABILITY",
  );
  const other = read("bundle.json").program;
  other.operator = "as_of_guess";
  await refuse(
    "undeclared operator",
    () => load({ program: other }),
    "UNSUPPORTED_CAPABILITY",
  );
  const builtin = JSON.parse(
    JSON.stringify(read("ontology.json")).replaceAll('"Team"', '"String"'),
  );
  await refuse(
    "builtin scalar cannot be redeclared as target EntityType",
    () => load({ ontology: builtin }),
    "INVALID_REQUEST",
  );
  await db.run("DROP TABLE history");
  await refuse(
    "second source missing preserves old whole parent",
    () => load(),
    "SNAPSHOT_REJECTED",
  );
  await db.run(
    "CREATE TABLE history(record_id VARCHAR,user_id VARCHAR,team_id VARCHAR,starts INTEGER,ends DATE)",
  );
  await refuse(
    "actual date type wrong before tuple reads",
    () => load(),
    "INVALID_REQUEST",
  );
  await db.run("DROP TABLE history");
  await db.run(
    "CREATE TABLE history(record_id VARCHAR,user_id VARCHAR,team_id VARCHAR,starts DATE,ends DATE)",
  );
  await db.run("DELETE FROM observations");
  await load();
  const empty = await program();
  assert(empty.total_source_rows === "0" && empty.index_segment_count === "0");
  assert.notEqual(empty.snapshot_id, initial.snapshot_id);
  checks.push("empty sources loaded after failures with new parent binding");
  await db.run(
    "INSERT INTO history VALUES ('minimum','u','TeamA',DATE '1970-01-01'+(-2147483648)::INTEGER,DATE '0001-01-01 (BC)')",
  );
  await load();
  assert.equal((await program()).history_record_count, "1");
  checks.push(
    "full finite DATE source including INT32_MIN, beyond JS calendar",
  );
  await refuse(
    "legacy generic capabilities refused on unsupported new profile",
    () => db.one("SELECT * FROM ossie_population_capabilities()"),
    "UNSUPPORTED_CAPABILITY",
  );
  await db.run("DELETE FROM history");
  await db.run(
    "INSERT INTO observations SELECT 'u'||i,DATE '2024-01-01','X' FROM range(10000) t(i)",
  );
  await db.run(
    "INSERT INTO history SELECT 'r'||i,'orphan','T',DATE '2024-01-01',DATE '2024-02-01' FROM range(10000) t(i)",
  );
  await load();
  const full = await program();
  assert.equal(full.total_source_rows, "20000");
  assert.equal(full.history_record_count, "10000");
  assert.equal(full.index_segment_count, "1");
  assert.deepEqual(
    full.sources.map((s: any) => s.rows),
    ["10000", "10000"],
  );
  checks.push(
    "complete10000 rows in each source, total20000; raw records distinct from merged index",
  );
  await db.run(
    "INSERT INTO observations VALUES ('extra',DATE '2024-01-01','X')",
  );
  await refuse(
    "first source10001 rows no truncation",
    () => load(),
    "SNAPSHOT_REJECTED",
  );
  await db.run("DELETE FROM observations WHERE user_id='extra'");
  await db.run(
    "INSERT INTO history VALUES ('extra','orphan','T',DATE '2024-01-01',DATE '2024-02-01')",
  );
  await refuse(
    "second source10001 rows no partial publication",
    () => load(),
    "SNAPSHOT_REJECTED",
  );
  writeFileSync(
    join(out, "verification.json"),
    JSON.stringify(
      {
        passed: true,
        scope:
          "historical typed double capture/index and program only; query/compile/MCP/lifecycle cancellation still required",
        engine,
        artifact_sha256: digest(artifact),
        checks,
        refusals,
        initial,
        empty,
        source_sha256: {
          "scripts/verify_history_load_artifact.ts": digest(
            join(ROOT, "scripts/verify_history_load_artifact.ts"),
          ),
        },
        real_model_requests: 0,
      },
      null,
      2,
    ) + "\n",
  );
  console.log(
    `PASS history load ${checks.length} controls/${refusals.length} refusals; no historical link execution claim`,
  );
} finally {
  await db.close();
}

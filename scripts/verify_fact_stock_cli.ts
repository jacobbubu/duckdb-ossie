/** Stock CLI loads the actual artifact and executes all five dedicated functions. */
import { readFileSync, writeFileSync, mkdirSync, existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { ROOT, assert, digest, sqlString } from "./artifact_helpers.ts";
const [cliArg, artifactArg, outArg] = process.argv.slice(2);
assert(
  cliArg && artifactArg && outArg && process.argv.length === 5,
  "CLI ARTIFACT NEW_OUT",
);
const cli = resolve(cliArg),
  artifact = resolve(artifactArg),
  out = resolve(outArg);
assert(!existsSync(out), "fresh output directory required");
mkdirSync(out, { recursive: true });
const version = Bun.spawn([cli, "--version"], {
  stdout: "pipe",
  stderr: "pipe",
});
const [versionText, versionError, versionCode] = await Promise.all([
  new Response(version.stdout).text(),
  new Response(version.stderr).text(),
  version.exited,
]);
assert.equal(versionCode, 0, versionError);
assert(/^v1\.5\.6\b/.test(versionText), "fixed stock CLI version required");
const fixture = join(ROOT, "test/fixtures/population-fact-program");
const users = {
  domain: "common",
  input_scope: "entire_capture",
  metrics: ["balance"],
  cohort: { operation: "all" },
};
const window = {
  input_scope: "entire_capture",
  windows: ["previous", "rolling"],
  display: {
    mode: "date_range",
    start_date: "2026-02-01",
    end_date: "2026-02-01",
  },
};
let sql =
  "LOAD " +
  sqlString(artifact) +
  ";" +
  readFileSync(join(fixture, "rows.sql"), "utf8") +
  "CALL ossie_bundle_load(" +
  sqlString(join(fixture, "bundle.json")) +
  ");SELECT * FROM ossie_fact_program();";
for (const [fn, request] of [
  ["ossie_fact_users_json", users],
  ["ossie_fact_users_compile", users],
  ["ossie_fact_windows_json", window],
  ["ossie_fact_windows_compile", window],
] as const)
  sql +=
    "SELECT * FROM " + fn + "(" + sqlString(JSON.stringify(request)) + ");";
writeFileSync(join(out, "request.sql"), sql);
const cliProcess = Bun.spawn(
  [cli, "-unsigned", "-json", "-batch", ":memory:"],
  {
    stdin: "pipe",
    stdout: "pipe",
    stderr: "pipe",
  },
);
cliProcess.stdin.write(sql);
cliProcess.stdin.end();
const [stdout, stderr, code] = await Promise.all([
  new Response(cliProcess.stdout).text(),
  new Response(cliProcess.stderr).text(),
  cliProcess.exited,
]);
writeFileSync(join(out, "stdout.log"), stdout);
writeFileSync(join(out, "stderr.log"), stderr);
assert.equal(code, 0, stderr);
const carriers = stdout
  .trim()
  .split("\n")
  .map((line) => JSON.parse(line));
assert.equal(carriers.length, 6);
assert.equal(carriers[0].length, 1);
assert.equal(carriers[0][0].records, 18);
const parsed = carriers.slice(1).map((rows: any[]) => {
  assert.equal(rows.length, 1);
  const values = Object.values(rows[0]);
  assert.equal(values.length, 1);
  assert.equal(typeof values[0], "string");
  return JSON.parse(values[0] as string);
});
assert.deepEqual(
  parsed.map((p) => p.schema_version),
  [
    "ossie-fact-program-v1",
    "ossie-fact-users-result-v1",
    "ossie-fact-users-plan-v1",
    "ossie-fact-windows-result-v1",
    "ossie-fact-windows-plan-v1",
  ],
);
assert.equal(parsed[0].query_executed, false);
assert.equal(parsed[0].mcp_registered, false);
assert.deepEqual(parsed[1].rows, [
  ["A", "290.00"],
  ["B", "50.00"],
  ["C", null],
]);
assert.equal(parsed[1].common_user_count, "3");
assert.deepEqual(parsed[3].rows, [
  ["A", "2026-02-01", "200.00", "250.00"],
  ["B", "2026-02-01", "80.00", "80.00"],
  ["C", "2026-02-01", null, null],
  ["D", "2026-02-01", null, "40.00"],
]);
for (const p of [parsed[2], parsed[4]]) {
  assert.equal(p.executed, false);
  assert.equal(p.counts_computed, false);
  assert.equal(p.sql_replayable, false);
  assert(!("rows" in p));
  assert(!("common_user_count" in p));
}
for (const p of parsed) assert.equal(p.snapshot_id, carriers[0][0].snapshot_id);
writeFileSync(
  join(out, "verification.json"),
  JSON.stringify(
    {
      passed: true,
      scope:
        "stock DuckDB1.5.6 five dedicated function schemas and complete User/window oracles; no MCP or real Agent",
      artifact_sha256: digest(artifact),
      cli_sha256: digest(cli),
      cli_version: versionText.trim(),
      source_sha256: digest(join(ROOT, "scripts/verify_fact_stock_cli.ts")),
      schema_versions: parsed.map((p) => p.schema_version),
      shared_snapshot_id: parsed[0].snapshot_id,
      request_sha256: digest(join(out, "request.sql")),
      stdout_sha256: digest(join(out, "stdout.log")),
      stderr_sha256: digest(join(out, "stderr.log")),
      real_model_requests: 0,
    },
    null,
    2,
  ) + "\n",
);
console.log(
  "PASS stock five dedicated functions and complete User/window oracles",
);

/** Reproduce artifact acceptance into a caller-owned new directory; retain failures. */
import {
  mkdirSync,
  existsSync,
  readdirSync,
  writeFileSync,
  openSync,
  closeSync,
} from "node:fs";
import { join, resolve } from "node:path";
import { assert, digest, ROOT } from "./artifact_helpers.ts";
import { VERIFIERS } from "./audit_acceptance_evidence.ts";
import { verifyAcceptance } from "./verify_p0_acceptance.ts";

const [artifactArg, outputArg] = process.argv.slice(2);
assert(
  artifactArg && outputArg && process.argv.length === 4,
  "Usage: bun scripts/run_p0_acceptance.ts ARTIFACT NEW_OUTPUT_DIRECTORY",
);
const artifact = resolve(artifactArg),
  directory = resolve(outputArg);
assert(existsSync(artifact), "artifact does not exist");
assert(
  !existsSync(directory) || readdirSync(directory).length === 0,
  "output directory must be new or empty; previous evidence is preserved",
);
assert(Bun.which("duckdb"), "stock DuckDB 1.5.6 CLI must be on PATH");
mkdirSync(directory, { recursive: true });
const logs: Record<string, string> = {};
async function run(name: string, command: string[]) {
  process.stdout.write(`${name}\n`);
  const logPath = join(directory, `${name}.log`);
  const fd = openSync(logPath, "w");
  let exit: number;
  try {
    const child = Bun.spawn(command, {
      cwd: ROOT,
      env: { ...process.env, OSSIE_EXTENSION: artifact },
      stdout: fd,
      stderr: fd,
    });
    exit = await child.exited;
  } finally {
    closeSync(fd);
  }
  logs[logPath] = digest(logPath);
  assert.equal(exit, 0, `${name} failed; diagnostic retained in ${logPath}`);
}
try {
  await run("cli-dependencies", [
    "duckdb",
    "-bail",
    "-c",
    "INSTALL duckdb_mcp FROM community; INSTALL tpcds;",
  ]);
  for (const tag of VERIFIERS) {
    const script =
      tag === "correspondence"
        ? "scripts/verify_core_correspondence.ts"
        : `scripts/verify_${tag}_artifact.ts`;
    await run(tag, [
      process.execPath,
      script,
      artifact,
      "--output",
      join(directory, `p0-final-${tag}-artifact-results.json`),
    ]);
  }
  await run("mcp", [
    process.execPath,
    "scripts/verify_population_mcp.ts",
    artifact,
    "--output",
    join(directory, "p0-final-mcp-results.json"),
  ]);
  await run("replay", [
    process.execPath,
    "scripts/verify_population_artifact.ts",
    artifact,
    "--output",
    join(directory, "p0-replay-population-artifact-results.json"),
  ]);
  await run("core-mcp", [process.execPath, "scripts/mcp_check.ts"]);
  await run("stock-cli", ["bash", "scripts/full_functionality_check.sh"]);
  const acceptance = verifyAcceptance(artifact, directory);
  writeFileSync(
    join(directory, "acceptance.json"),
    JSON.stringify(
      {
        ...acceptance,
        runner_sha256: digest(import.meta.path),
        run_log_sha256: logs,
        native_quality_gate:
          "run make quality-local test separately; not claimed by this artifact runner",
      },
      null,
      2,
    ) + "\n",
  );
  process.stdout.write(
    `PASS scoped A1-A8: ${join(directory, "acceptance.json")}\n`,
  );
} catch (error) {
  writeFileSync(
    join(directory, "failure.json"),
    JSON.stringify(
      {
        status: "FAIL",
        artifact_sha256: digest(artifact),
        reason: String(error),
        run_log_sha256: logs,
      },
      null,
      2,
    ) + "\n",
  );
  throw error;
}

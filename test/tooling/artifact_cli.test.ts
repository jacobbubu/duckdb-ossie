/** The CLI smoke test must fail before querying if the real loading path is unavailable. */
import { test, expect } from "bun:test";
import { mkdirSync, writeFileSync, symlinkSync } from "node:fs";
import { join, relative } from "node:path";
import { spawnSync } from "node:child_process";
import { ROOT, temporary } from "../../scripts/artifact_helpers.ts";

async function probe(options: {
  cli: boolean;
  artifact: boolean;
  relativePath?: boolean;
}) {
  return temporary("ossie-cli-control-", async (directory) => {
    const bin = join(directory, "bin");
    mkdirSync(bin);
    for (const name of ["dirname", "basename", "mktemp", "rm", "sed"]) {
      const path = Bun.which(name);
      if (!path) throw new Error(`required fixture utility missing: ${name}`);
      symlinkSync(path, join(bin, name));
    }
    if (options.cli) {
      writeFileSync(
        join(bin, "duckdb"),
        "#!/bin/sh\nprintf 'controlled artifact load rejected\\n' >&2\nprintf '%s\\n' \"$@\" >&2\nexit 7\n",
        { mode: 0o755 },
      );
    }
    const artifact = join(directory, "artifact.duckdb_extension");
    if (options.artifact)
      writeFileSync(artifact, "deliberately invalid artifact");
    return spawnSync(
      "/bin/bash",
      [join(ROOT, "scripts/full_functionality_check.sh")],
      {
        cwd: ROOT,
        env: {
          ...process.env,
          PATH: bin,
          OSSIE_EXTENSION: options.relativePath
            ? relative(ROOT, artifact)
            : artifact,
        },
        encoding: "utf8",
        timeout: 5000,
      },
    );
  });
}

test("missing stock CLI fails instead of using build-tree engine", async () => {
  const result = await probe({ cli: false, artifact: true });
  expect(result.status).toBe(2);
  expect(result.stderr).toContain("stock duckdb CLI is required");
  expect(result.stdout).not.toContain("FUNCTIONALITY CHECK GREEN");
});

test("missing artifact fails before invoking the CLI", async () => {
  const result = await probe({ cli: true, artifact: false });
  expect(result.status).toBe(2);
  expect(result.stderr).toContain("extension artifact missing");
  expect(result.stderr).not.toContain("controlled artifact load rejected");
});

test("artifact loading failure is fatal and retains its diagnostic", async () => {
  const result = await probe({ cli: true, artifact: true });
  expect(result.status).toBe(1);
  expect(result.stderr).toContain("controlled artifact load rejected");
  expect(result.stderr).toContain(
    "stock CLI failed to load extension artifact",
  );
  expect(result.stdout).not.toContain("FUNCTIONALITY CHECK GREEN");
});

test("relative artifact paths are normalized before stock CLI loading", async () => {
  const result = await probe({ cli: true, artifact: true, relativePath: true });
  expect(result.status).toBe(1);
  expect(result.stderr).toContain("LOAD '/");
  expect(result.stderr).not.toContain("LOAD '../");
});

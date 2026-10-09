/** Meaningful negative controls use isolated trees, never edits to the repository. */
import { test, expect } from "bun:test";
import {
  mkdtempSync,
  mkdirSync,
  writeFileSync,
  rmSync,
  readFileSync,
} from "node:fs";
import { join, dirname } from "node:path";
import { tmpdir } from "node:os";
import { spawnSync } from "node:child_process";
import { ROOT, check } from "../../scripts/check_architecture.ts";
function tree(files: Record<string, string>, reason?: string) {
  const root = mkdtempSync(join(tmpdir(), "ossie-architecture-"));
  try {
    for (const [name, text] of Object.entries(files)) {
      const path = join(root, name);
      mkdirSync(dirname(path), { recursive: true });
      writeFileSync(path, text);
    }
    const result = spawnSync(
      process.execPath,
      [join(ROOT, "scripts/check_architecture.ts"), "--root", root],
      { encoding: "utf8" },
    );
    if (reason) {
      expect(result.status).not.toBe(0);
      expect(result.stderr).toContain(reason);
    } else
      expect({ status: result.status, error: result.stderr }).toEqual({
        status: 0,
        error: "",
      });
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
}
test("direct forbidden", () =>
  tree(
    {
      "src/compile/compile.cpp": '#include "ossie/state.hpp"\n',
      "src/include/ossie/state.hpp": "",
    },
    "forbidden state/functions",
  ));
test("indirect forbidden", () =>
  tree(
    {
      "src/include/ossie/compile.hpp": "#include <ossie/helper.hpp>\n",
      "src/include/ossie/helper.hpp": '#include "state.hpp"\n',
      "src/include/ossie/state.hpp": "",
    },
    "compile.hpp -> src/include/ossie/helper.hpp -> src/include/ossie/state.hpp",
  ));
test("model forbidden", () =>
  tree(
    {
      "src/model/validate.cpp": '#include "ossie/functions.hpp"\n',
      "src/include/ossie/functions.hpp": "",
    },
    "forbidden state/functions",
  ));
test("header cycle", () =>
  tree(
    {
      "src/include/ossie/a.hpp": '#include "b.hpp"\n',
      "src/include/ossie/b.hpp": '#include "a.hpp"\n',
    },
    "header cycle",
  ));
test("relative AST and comments", () =>
  tree({
    "src/compile/c.cpp":
      '// #include "ossie/state.hpp"\n/*\n#include "bad.hpp"\n*/\n#include "../include/ossie/model.hpp"\n#include <duckdb/parser/parser.hpp>\n',
    "src/include/ossie/model.hpp": '#include "duckdb.hpp"\n',
  }));
test("missing quoted reference", () =>
  tree(
    { "src/model/validate.cpp": '#include "ossie/missing.hpp"\n' },
    "unresolved project include",
  ));
test("missing angle reference", () =>
  tree(
    { "src/model/validate.cpp": "#include <missing.hpp>\n" },
    "unresolved project include",
  ));
test("indirect runtime", () =>
  tree(
    {
      "src/compile/c.cpp": '#include "ossie/helper.hpp"\n',
      "src/include/ossie/helper.hpp": '#include "duckdb/main/connection.hpp"\n',
    },
    "helper.hpp -> duckdb/main/connection.hpp",
  ));
test("nonliteral reference", () =>
  tree({ "src/compile/c.cpp": "#include MY_HEADER\n" }, "nonliteral include"));
test("zero inputs", () => tree({}, "zero project"));
test("unscanned source", () =>
  tree(
    {
      "src/compile/c.cpp": "",
      "CMakeLists.txt":
        "set(EXTENSION_SOURCES src/compile/c.cpp src/missing.cpp)",
    },
    "unscanned=",
  ));
test("undeclared source", () =>
  tree(
    {
      "src/compile/c.cpp": "",
      "CMakeLists.txt": "set(EXTENSION_SOURCES src/other.cpp)",
    },
    "unlisted=",
  ));
test("empty compiled inventory", () =>
  tree(
    {
      "src/include/ossie/model.hpp": "",
      "CMakeLists.txt": "set(EXTENSION_SOURCES )",
    },
    "empty compiled source inventory",
  ));
test("compiler IO", () =>
  tree(
    { "src/compile/c.cpp": '#include "duckdb/main/client_context.hpp"\n' },
    "forbidden runtime include",
  ));
test("precise parser adapter", () => {
  tree({
    "src/model/parser.cpp":
      '#include "duckdb/main/client_context.hpp"\n#include "duckdb/common/file_system.hpp"\n',
  });
  tree(
    { "src/model/parser.cpp": '#include "duckdb/main/connection.hpp"\n' },
    "forbidden runtime include",
  );
});
test("parser exception cannot permit state", () =>
  tree(
    {
      "src/model/parser.cpp": '#include "ossie/state.hpp"\n',
      "src/include/ossie/state.hpp": "",
    },
    "forbidden state/functions",
  ));
test("script formatting failure and recovery", () => {
  const root = mkdtempSync(join(tmpdir(), "ossie-format-"));
  try {
    const script = join(root, "control.ts");
    writeFileSync(script, "const value=  {a:1}\n");
    const prettier = join(ROOT, "node_modules/prettier/bin/prettier.cjs");
    const run = (mode: string) =>
      spawnSync(process.execPath, [prettier, mode, script], {
        encoding: "utf8",
      });
    const bad = run("--check");
    expect(bad.status).not.toBe(0);
    expect(bad.stdout + bad.stderr).toContain("control.ts");
    expect(run("--write").status).toBe(0);
    expect(run("--check").status).toBe(0);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
test("state and metric-grain runners required before SQL", () => {
  const result = spawnSync("make", ["-n", "test_release_internal"], {
    cwd: ROOT,
    encoding: "utf8",
  });
  expect(result.status).toBe(0);
  expect(result.stdout).toContain("--target ossie_state_test");
  expect(result.stdout).toContain('ossie_state_test "');
  expect(result.stdout.indexOf("ossie_state_test")).toBeLessThan(
    result.stdout.indexOf("test/unittest"),
  );
  expect(result.stdout).toContain("--target ossie_metric_grain_test");
  expect(result.stdout).toContain('ossie_metric_grain_test "');
  expect(result.stdout.indexOf("ossie_metric_grain_test")).toBeLessThan(
    result.stdout.indexOf("test/unittest"),
  );
});
test("actual CI gate fails closed for every dependency", () => {
  const text = readFileSync(
      join(ROOT, ".github/workflows/MainDistributionPipeline.yml"),
      "utf8",
    ),
    section = text.split("  ci-gate:")[1].split("\n  code-quality-check:")[0];
  expect(section).toMatch(/if:\s*always\(\)/);
  const jobs = /needs: \[([^\]]+)\]/
    .exec(section)![1]
    .split(",")
    .map((name) => name.trim());
  expect(jobs).toContain("project-controls");
  expect(jobs).toContain("artifact-load");
  const lines = section.split("run: |\n")[1].trimEnd().split("\n"),
    indent = Math.min(
      ...lines
        .filter((line) => line.trim())
        .map((line) => /^ */.exec(line)![0].length),
    ),
    script = lines.map((line) => line.slice(indent)).join("\n");
  const execute = (status: Record<string, string>) =>
    spawnSync(
      "bash",
      [
        "-c",
        script.replace(
          /\$\{\{\s*needs\.([\w-]+)\.result\s*\}\}/g,
          (_all, job: string) => status[job],
        ),
      ],
      { encoding: "utf8" },
    ).status;
  expect(execute(Object.fromEntries(jobs.map((job) => [job, "success"])))).toBe(
    0,
  );
  for (const job of jobs)
    for (const failed of ["failure", "skipped", "cancelled"])
      expect(
        execute({
          ...Object.fromEntries(jobs.map((job) => [job, "success"])),
          [job]: failed,
        }),
      ).not.toBe(0);
});
test("current project graph scans nonzero inventory", () => {
  expect(check().source_files).toBeGreaterThan(0);
});

test("strict typecheck rejects a runtime-valid type error and recovers", () => {
  const root = mkdtempSync(join(tmpdir(), "ossie-typecheck-"));
  try {
    const source = join(root, "control.ts");
    writeFileSync(source, 'const value: number = "bad";\n');
    const cmd = [
      join(ROOT, "node_modules/typescript/bin/tsc"),
      "--noEmit",
      "--strict",
      "--skipLibCheck",
      "--target",
      "ES2022",
      source,
    ];
    const invalid = spawnSync(process.execPath, cmd, { encoding: "utf8" });
    expect(invalid.status).not.toBe(0);
    expect(invalid.stdout + invalid.stderr).toContain("TS2322");
    writeFileSync(source, "const value: number = 1;\n");
    expect(spawnSync(process.execPath, cmd, { encoding: "utf8" }).status).toBe(
      0,
    );
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test("CI uses frozen Bun tooling and the same artifact for all five verifiers", () => {
  const workflow = readFileSync(
    join(ROOT, ".github/workflows/MainDistributionPipeline.yml"),
    "utf8",
  );
  const artifact = workflow.split("  artifact-load:")[1].split("  ci-gate:")[0];
  for (const name of [
    "loader",
    "population",
    "expression",
    "date",
    "join_research",
  ])
    expect(artifact).toContain(
      `bun scripts/verify_${name}_artifact.ts downloaded/ossie.duckdb_extension --output quality-reports/${name}.json`,
    );
  expect(artifact).toContain("bun-version: '1.3.11'");
  expect(artifact).toContain("bun install --frozen-lockfile");
  const controls = workflow.split("  project-controls:")[1];
  expect(controls).toContain(
    "make script-format-check tooling-typecheck architecture-check quality-control-tests",
  );
  expect(controls).toContain("bun install --frozen-lockfile");
  expect(workflow).not.toContain("python3 scripts/");
});

test("stock artifact CLI and all verifiers consume the downloaded artifact", () => {
  const workflow = readFileSync(
    join(ROOT, ".github/workflows/MainDistributionPipeline.yml"),
    "utf8",
  );
  const artifact = workflow.split("  artifact-load:")[1].split("  ci-gate:")[0];
  expect(artifact).toContain("uses: actions/download-artifact@v4");
  expect(artifact).toContain("name: ossie-v1.5.6-extension-${{ matrix.arch }}");
  expect(artifact).toContain("path: downloaded");
  expect(artifact).toContain(
    "OSSIE_EXTENSION: downloaded/ossie.duckdb_extension",
  );
  expect(artifact).toContain("run: ./scripts/full_functionality_check.sh");
});

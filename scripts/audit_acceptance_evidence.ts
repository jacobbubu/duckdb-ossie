/** Verify claim-specific source bindings; hash checks are not functional tests. */
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { assert, digest, emitReport, json, ROOT } from "./artifact_helpers.ts";

export const EVIDENCE = "docs/research/ossie-req-001";
export const VERIFIERS = [
  "loader",
  "population",
  "expression",
  "date",
  "join_research",
  "joined_population",
  "plan",
  "discovery",
  "error",
  "typed_population",
  "correspondence",
] as const;
export function auditEvidence(
  artifact: string,
  prefix = "p0-final",
  reportDirectory = join(ROOT, EVIDENCE),
) {
  const artifactHash = digest(artifact);
  const checked: { path: string; sha256: string; carrier: string }[] = [];
  function check(path: string, expected: string, carrier: string) {
    assert.equal(digest(resolve(ROOT, path)), expected, `${carrier}: ${path}`);
    checked.push({ path, sha256: expected, carrier });
  }
  const manifestPath = join(EVIDENCE, "sources/manifest.json");
  const manifest = json<{
    spec_commit: string;
    files: { saved_path?: string; source_path: string; sha256: string }[];
  }>(join(ROOT, manifestPath));
  for (const file of manifest.files)
    check(file.saved_path || file.source_path, file.sha256, manifestPath);
  const reports: Record<string, string> = {};
  for (const tag of [...VERIFIERS, "mcp"] as const) {
    const path = join(
      reportDirectory,
      `${prefix}-${tag}${tag === "mcp" ? "" : "-artifact"}-results.json`,
    );
    const report = json<Record<string, unknown>>(resolve(ROOT, path));
    assert.equal(report.artifact_sha256, artifactHash, path);
    const script =
      tag === "correspondence"
        ? "scripts/verify_core_correspondence.ts"
        : tag === "mcp"
          ? "scripts/verify_population_mcp.ts"
          : `scripts/verify_${tag}_artifact.ts`;
    const single: Record<string, string> = {
      script_sha256: script,
      verifier_sha256: script,
      verification_script_sha256: script,
      helper_sha256: "scripts/artifact_helpers.ts",
      verification_helper_sha256: "scripts/artifact_helpers.ts",
      population_source_sha256: "src/population/population.cpp",
      fixture_sha256: "test/fixtures/joined-population/core.json",
    };
    if (typeof report.cli_sha256 === "string") {
      const cli = Bun.which("duckdb");
      assert(
        cli,
        "stock DuckDB CLI must be on PATH to verify MCP source binding",
      );
      check(cli, report.cli_sha256, path);
    }
    for (const [key, value] of Object.entries(report)) {
      if (!key.endsWith("_sha256")) continue;
      if (typeof value === "string" && single[key])
        check(single[key], value, path);
      else if (value && typeof value === "object" && !Array.isArray(value))
        for (const [source, hash] of Object.entries(value)) {
          assert.equal(typeof hash, "string", `${path}: ${key}.${source}`);
          check(source, hash as string, path);
        }
    }
    reports[path] = digest(resolve(ROOT, path));
  }
  const issuePath = "docs/ossie-execution-research.md";
  const issue = readFileSync(join(ROOT, issuePath), "utf8");
  const requirements = [...issue.matchAll(/^\| (R\d\.\d) \|/gm)].map(
    (m) => m[1]!,
  );
  assert.deepEqual(
    requirements,
    [4, 5, 6, 6, 7, 5, 5].flatMap((n, r) =>
      Array.from({ length: n }, (_, i) => `R${r + 1}.${i + 1}`),
    ),
  );
  const acceptance = [...issue.matchAll(/^\| (A[1-8]) \|/gm)].map((m) => m[1]!);
  assert.deepEqual(
    acceptance,
    Array.from({ length: 8 }, (_, i) => `A${i + 1}`),
  );
  const schema = json<any>(
    join(ROOT, EVIDENCE, "sources/apache-ossie/core-spec/ossie-schema.json"),
  );
  const inventory = Object.entries(schema.properties)
    .map(([field]) => `root.${field}`)
    .concat(
      Object.entries(schema.$defs).flatMap(([scope, definition]) =>
        Object.keys((definition as any).properties || {}).map(
          (field) => `${scope}.${field}`,
        ),
      ),
    );
  const rows = [
    ...readFileSync(
      join(ROOT, "docs/core-field-correspondence.md"),
      "utf8",
    ).matchAll(/^\| `([^`]+)` \|/gm),
  ].map((m) => m[1]!);
  assert.equal(rows.length, 50);
  assert.deepEqual(rows, inventory);
  return {
    scope:
      "documentary evidence audit; not a functional rerun or full P0 acceptance",
    artifact_sha256: artifactHash,
    spec_commit: manifest.spec_commit,
    verifier_sha256: digest(import.meta.path),
    requirements,
    acceptance,
    hash_checks: checked,
    reports,
    schema_property_rows: rows.length,
    documentation_sha256: Object.fromEntries(
      [
        issuePath,
        "ossie-duckdb-plan.md",
        "docs/core-field-correspondence.md",
        "docs/limitations.md",
      ].map((p) => [p, digest(join(ROOT, p))]),
    ),
    remaining: [
      "final rule-driven acceptance archive",
      "remote CI/Ubuntu tidy unrun; no autonomous Agent proof",
    ],
  };
}
if (import.meta.main) emitReport(auditEvidence(resolve(process.argv[2]!)));

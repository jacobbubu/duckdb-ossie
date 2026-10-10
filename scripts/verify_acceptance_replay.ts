/** Compare complete actual results from two independent stock-client processes. */
import { join, resolve } from "node:path";
import { assert, digest, emitReport, json, ROOT } from "./artifact_helpers.ts";
interface Case {
  query: unknown;
  object_ids: string[];
  aggregate: unknown[];
  daily_results: unknown[][];
  grouped_results: unknown[][];
  drilldown_objects: {
    dimensions: unknown[];
    filters: string[];
    object_ids: string[];
  }[];
  content_sha256: string;
  ontology_sha256: string;
  sql: string;
}
interface Report {
  artifact_sha256: string;
  script_sha256: string;
  helper_sha256: string;
  inputs_sha256: Record<string, string>;
  implementation_sources_sha256: Record<string, string>;
  cases: Record<string, Case>;
}
const sorted = (rows: unknown[]) =>
  rows.map((row) => JSON.stringify(row)).sort();
export function verifyReplay(
  artifact: string,
  reportDirectory = join(ROOT, "docs/research/ossie-req-001"),
) {
  const paths = ["p0-final", "p0-replay"].map((prefix) =>
    join(reportDirectory, `${prefix}-population-artifact-results.json`),
  );
  const reports = paths.map((p) => json<Report>(resolve(ROOT, p)));
  for (const report of reports) {
    assert.equal(report.artifact_sha256, digest(artifact));
    assert.equal(
      report.script_sha256,
      digest(join(ROOT, "scripts/verify_population_artifact.ts")),
    );
    assert.equal(
      report.helper_sha256,
      digest(join(ROOT, "scripts/artifact_helpers.ts")),
    );
    for (const map of [
      report.inputs_sha256,
      report.implementation_sources_sha256,
    ])
      for (const [path, hash] of Object.entries(map))
        assert.equal(digest(join(ROOT, path)), hash, path);
  }
  const checks: string[] = [];
  for (const threshold of ["10", "20"]) {
    const [first, second] = reports.map((r) => r.cases[threshold]!);
    assert(first && second);
    for (const key of [
      "query",
      "object_ids",
      "aggregate",
      "content_sha256",
      "ontology_sha256",
      "sql",
    ] as const)
      assert.deepEqual(first[key], second[key], `${threshold}: ${key}`);
    for (const key of [
      "daily_results",
      "grouped_results",
      "drilldown_objects",
    ] as const)
      assert.deepEqual(
        sorted(first[key]),
        sorted(second[key]),
        `${threshold}: ${key}`,
      );
    checks.push(
      `threshold ${threshold}: complete objects, aggregates, daily/groups/drilldown, SQL and content/definition hashes reproduced`,
    );
  }
  return {
    scope:
      "A7 fixed-definition replay in separate processes with separately imported same immutable fixture; no live SQL-text replay or cross-reload atomicity promise",
    artifact_sha256: digest(artifact),
    verifier_sha256: digest(import.meta.path),
    helper_sha256: digest(join(ROOT, "scripts/artifact_helpers.ts")),
    reports_sha256: Object.fromEntries(
      paths.map((p) => [p, digest(resolve(ROOT, p))]),
    ),
    checks,
    result_order: "unspecified SQL row order; complete row multisets compared",
    limitations: [
      "fixed same engine and input; not universal determinism",
      "native AVG is DOUBLE; repeated values compared exactly for this fixture",
      "this replay alone does not establish Agent autonomy or remote CI outcomes",
    ],
  };
}
if (import.meta.main) emitReport(verifyReplay(resolve(process.argv[2]!)));

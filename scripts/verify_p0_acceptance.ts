/** Final rule-driven acceptance index; scoped assertions over actual verifier results. */
import { join, resolve } from "node:path";
import { assert, digest, emitReport, json, ROOT } from "./artifact_helpers.ts";
import { auditEvidence, EVIDENCE } from "./audit_acceptance_evidence.ts";
import { verifyReplay } from "./verify_acceptance_replay.ts";

export function verifyAcceptance(
  artifact: string,
  directory = join(ROOT, EVIDENCE),
) {
  const audit = auditEvidence(artifact, "p0-final", directory);
  const replay = verifyReplay(artifact, directory);
  const report = (tag: string) =>
    json<any>(join(directory, `p0-final-${tag}-artifact-results.json`));
  const p = report("population"),
    j = report("joined_population"),
    x = report("expression"),
    date = report("date"),
    plan = report("plan"),
    discovery = report("discovery"),
    error = report("error"),
    fields = report("correspondence"),
    typed = report("typed_population"),
    coreJoin = report("join_research"),
    loader = report("loader"),
    mcp = json<any>(join(directory, "p0-final-mcp-results.json"));
  assert(p.acceptance_evidence.only_formal_derived_rule_changes);
  assert(p.acceptance_evidence.metric_mapping_and_request_template_unchanged);
  assert.equal(p.acceptance_evidence.unit, "USD");
  for (const [threshold, count, sum, groups] of [
    ["10", 166, "2265.86", 101],
    ["20", 63, "1472.73", 45],
  ] as const) {
    const c = p.cases[threshold];
    assert.equal(c.object_ids.length, count);
    assert.deepEqual(c.aggregate.slice(0, 3), [count, count, sum]);
    assert.equal(c.daily_results.length, 15);
    assert.equal(c.grouped_results.length, groups);
    assert.equal(c.drilldown_objects.length, groups);
    assert.equal(c.actual_plan.executed, false);
    assert.equal(c.actual_plan.physical_plan, false);
    assert(c.sql.includes(`ytd_gaap >= ${threshold}`));
    assert.equal(c.content_sha256, p.cases["10"].content_sha256);
  }
  const march10 = p.cases["10"].daily_results.find(
    (r: unknown[]) => r[0] === "2026-03-10",
  );
  assert.deepEqual(march10.slice(0, 4), ["2026-03-10", 37, 37, "1697.80"]);
  assert(Math.abs(march10[4] - 1697.8 / 37) <= 1e-10);
  assert.deepEqual(p.empty_results.objects, []);
  assert.deepEqual(p.empty_results.grouped, []);
  assert.deepEqual(p.empty_results.scalar, [[0, 0, null, null]]);
  assert.equal(Object.keys(p.boundary_cases).length, 2);
  assert.deepEqual(j.totals, [6, 5, "55.01", 11.002]);
  assert.deepEqual(j.valid_ids, ["f1", "f2", "f5", "f6"]);
  assert.equal(j.refusals, 24);
  assert.equal(j.checked.length, 24);
  for (const name of [
    "ambiguous_path",
    "mixed_grain",
    "reverse_fanout",
    "declared_many_to_many",
  ])
    assert(coreJoin.planner_rejections.includes(name));
  assert(
    x.zero_null_empty &&
      x.failed_load_preserves_snapshot &&
      date.failed_load_preserves_snapshot,
  );
  for (const key of [
    "prepared_query_retains_original_snapshot",
    "explicit_reload_observes_source_changes",
    "failed_load_preserves_bundle",
    "interrupted_load_preserves_bundle",
  ])
    assert.equal(p[key], true, key);
  assert.equal(plan.checks.length, 36);
  assert.equal(discovery.checks.length, 18);
  assert.equal(error.checks.length, 54);
  assert.equal(fields.observations.length, 365);
  assert.equal(fields.observations.filter((r: any) => r.accepted).length, 35);
  assert.equal(typed.checks.length, 24);
  assert.equal(mcp.checks.length, 330);
  assert.equal(mcp.refusals.length, 106);
  assert(loader.artifact_loaded_in_stock_node_bindings);
  const carrier = (tag: string) =>
    join(
      directory,
      `p0-final-${tag}${tag === "mcp" ? "" : "-artifact"}-results.json`,
    );
  const cases = [
    [
      "A1",
      ["population", "mcp"],
      "original 475-row daily oracle and March10 45.89 USD",
    ],
    [
      "A2",
      ["population", "mcp"],
      "formal threshold10/20 only; same metric/mapping/request; SQL/population/results change",
    ],
    [
      "A3",
      ["population", "mcp"],
      "complete object IDs, COUNT/SUM/AVG and same frozen population",
    ],
    [
      "A4",
      ["population", "mcp"],
      "complete daily/groups/drilldown; inherited filters and rules",
    ],
    [
      "A5",
      ["population", "expression", "date", "joined_population"],
      "threshold equality, NULL, empty/sparse and missing-relation rejection",
    ],
    [
      "A6",
      ["joined_population", "join_research"],
      "hand-derived safe fact grain and production key/FK/grain/policy refusals",
    ],
    [
      "A7",
      ["population", "plan", "discovery", "typed_population", "mcp"],
      "actual AST/definition/capture provenance, independent-process replay and scoped typed transport",
    ],
    [
      "A8",
      ["loader", "correspondence", "expression"],
      "user-adjusted new-format only; exact dialect selection and unsupported constructs refused",
    ],
  ] as const;
  return {
    scope:
      "adjusted A1-A8 local rule-driven acceptance for the two reviewed bounded profiles",
    conclusion: "PASS within declared scope and deviations",
    artifact_sha256: digest(artifact),
    project_api_version: "2.0.0",
    spec_commit: audit.spec_commit,
    spec_version: "0.2.0.dev0",
    engine_version: "1.5.6",
    verifier_sha256: digest(import.meta.path),
    verifier_dependencies_sha256: Object.fromEntries(
      [
        "scripts/audit_acceptance_evidence.ts",
        "scripts/source_evidence.ts",
        "scripts/verify_acceptance_replay.ts",
        "scripts/artifact_helpers.ts",
      ].map((path) => [path, digest(join(ROOT, path))]),
    ),
    acceptance: cases.map(([id, evidence, behavior]) => ({
      id,
      status: "PASS within scope",
      behavior,
      evidence: evidence.map((tag) => ({
        path: carrier(tag),
        sha256: digest(carrier(tag)),
      })),
    })),
    audit,
    replay,
    remaining: [
      "remote CI not run; native tests and clang-tidy are separate quality evidence",
      "real autonomous Agent not tested",
      "pagination future research only",
      "P1/calendar/multi-period/general ontology/mixed grain/windows remain outside this acceptance",
    ],
    limitations: [
      "reviewed YTD classification does not establish active/paid status or automatic NL conflict detection",
      "quoted identifiers use DuckDB ASCII case semantics",
      "compile is diagnostic semantic AST, not physical/final binder proof or ordinary SQL replay",
      "separate calls across reload are not atomic; service maintains execution reference",
      "Float approximate; typed numeric transport does not prove arbitrary client arithmetic",
      "10000-row response tests are not universal output or peak-memory bounds",
      "local acceptance does not authorize push/merge/publication",
    ],
  };
}
if (import.meta.main) emitReport(verifyAcceptance(resolve(process.argv[2]!)));

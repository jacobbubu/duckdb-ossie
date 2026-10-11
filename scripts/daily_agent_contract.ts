/** Frozen daily rehearsal contract; expected memberships are review-only, never model context. */
import assert from "node:assert/strict";
import { ROOT } from "./artifact_helpers.ts";
import { join } from "node:path";
import {
  dailyResult,
  dailyPlan,
  sameDailyBinding,
  DAILY_TRACE,
  type DailyEnvelope,
} from "./daily_result.ts";
import { dailyContent } from "./daily_mcp.ts";
import { LIMITS, type JsonObject } from "./agent_loop.ts";
export const DAILY_SYSTEM = `Use only discovered daily tools and resources. Do not replace every-calendar-day qualification with existential interval counts or add daily counts. All scope belongs in request_json; send concept and request_json as Strings only. Discover the current qualified concept and rule; do not assume business truth, data completeness or historical rules. Compile explains but does not compute members. For an answered result you must execute population_daily_users for the exact literal dates/products, preserve ordered raw member IDs and integer-text member_count, required_day_count and qualification_rule. Return one JSON object with exactly status="answered", request, members, member_count, required_day_count, qualification_rule, evidence_ids, limitations. Cite the actual executed tool evidence ID. Do not claim an answer from a compile-only result. If explicitly asked to explain only without executing, return exactly status="not_executed", reason, evidence_ids, limitations, with no computed members/count; cite compile evidence. Do not call a supported but unexecuted query unsupported. No arbitrary SQL/import/reload.`;
export const DAILY_SCENES = [
  {
    id: "all",
    threshold: 10,
    start: "2026-01-31",
    end: "2026-02-01",
    products: null,
    expected: ["B", "E", "F", "H", "K"],
    question:
      "请列出2026-01-31至2026-02-01（含首尾）每天都有至少一次正式合格观察的全部User，全部产品范围；给出成员、人数和实际规则。",
  },
  {
    id: "product-X",
    threshold: 10,
    start: "2026-01-31",
    end: "2026-02-01",
    products: ["X"],
    expected: ["B", "F", "H", "K"],
    question:
      "请列出2026-01-31至2026-02-01（含首尾）产品X范围内每天有正式合格观察的全部User、人数和实际规则。",
  },
  {
    id: "missing-day",
    threshold: 10,
    start: "2026-01-31",
    end: "2026-02-02",
    products: null,
    expected: [],
    question:
      "请列出2026-01-31至2026-02-02（含首尾）每天都有正式合格观察的全部User，全部产品范围；没有观察的日期也必须满足，给出成员和人数。",
  },
  {
    id: "changed-rule",
    threshold: 20,
    start: "2026-01-31",
    end: "2026-02-01",
    products: null,
    expected: ["B", "K"],
    question:
      "请列出2026-01-31至2026-02-01（含首尾）每天都有至少一次正式合格观察的全部User，全部产品范围；给出成员、人数和实际规则。",
  },
  {
    id: "compile-only",
    threshold: 10,
    start: "2026-01-31",
    end: "2026-02-01",
    products: null,
    expected: [],
    question:
      "只解释2026-01-31至2026-02-01（含首尾）、全部产品的每天合格User集合处理过程，不执行查询，不给成员或人数；明确这次没有计算。",
  },
] as const;
export type DailyScene = (typeof DAILY_SCENES)[number];
export const dailySceneRequest = (scene: DailyScene) => ({
  start_date: scene.start,
  end_date: scene.end,
  products:
    scene.products === null
      ? { mode: "all" }
      : { mode: "subset", values: [...scene.products] },
});
export const DAILY_SOURCE_NAMES = [
  "examples/daily_user_server.sql",
  "docs/research/ossie-req-001/multi-period-research/every-day/observations.csv",
  ...[
    "core.json",
    "ontology.json",
    "ontology-20.json",
    "mapping.json",
    "bundle-10.json",
    "bundle-20.json",
  ].map((name) => "test/fixtures/population-observation/" + name),
  ...[
    "daily_agent_contract",
    "daily_agent_rehearsal",
    "daily_agent_replay",
    "daily_mcp",
    "daily_result",
    "agent_loop",
    "mcp_check",
    "artifact_helpers",
    "observation_mcp",
    "observation_evidence",
    "observation_agent_archive",
    "observation_agent_replay",
    "observation_agent_contract",
    "population_result",
    "population_error",
  ].map((name) => "scripts/" + name + ".ts"),
];
export const DAILY_SOURCES = DAILY_SOURCE_NAMES.map((name) => join(ROOT, name));
export const DAILY_BUDGET = {
  per_loop: LIMITS,
  discovery_boundary:
    "Separate RPC read timeouts; no whole discovery/process deadline guarantee",
  scenes: 5,
  maximum_simulated_completion_requests: 40,
  maximum_tool_calls: 60,
  monetary_ceiling: "not applicable: no model transport",
  input_token_ceiling: "not enforced",
};
export function auditDailyAnswer(
  content: string,
  events: JsonObject[],
  scene: DailyScene,
  discoveryBinding: Record<string, string>,
) {
  const answer: unknown = JSON.parse(content);
  assert(
    answer !== null && typeof answer === "object" && !Array.isArray(answer),
  );
  const value = answer as JsonObject;
  if (scene.id !== "compile-only")
    assert.deepEqual(
      Object.keys(value).sort(),
      [
        "status",
        "request",
        "members",
        "member_count",
        "required_day_count",
        "qualification_rule",
        "evidence_ids",
        "limitations",
      ].sort(),
    );
  if (scene.id !== "compile-only") assert.equal(value.status, "answered");
  const responses = events.filter((e) => e.kind === "tool_response");
  const requests = events.filter((e) => e.kind === "tool_request");
  const results: { id: string; envelope: DailyEnvelope }[] = [],
    plans: DailyEnvelope[] = [];
  for (const response of responses) {
    const request = requests.find(
      (e) => e.evidence_id === response.evidence_id,
    );
    assert(request);
    const args = request.arguments as Record<string, unknown>;
    assert.equal(args.concept, "QualifiedObservation");
    assert.deepEqual(
      JSON.parse(String(args.request_json)),
      dailySceneRequest(scene),
      "wrong tool scope",
    );
    const reply = response.result as Parameters<typeof dailyContent>[0];
    const decoded =
      request.name === "population_daily_users"
        ? dailyResult(dailyContent(reply))
        : dailyPlan(dailyContent(reply));
    assert.deepEqual(decoded.request, dailySceneRequest(scene));
    assert.deepEqual(decoded.qualification_rule, {
      concept: "QualifiedObservation",
      declared_sql: `DailyObservation.classification_value >= ${scene.threshold}`,
    });
    for (const key of DAILY_TRACE)
      assert.equal(decoded[key], discoveryBinding[key]);
    if (request.name === "population_daily_users")
      results.push({ id: String(response.evidence_id), envelope: decoded });
    else {
      assert.equal(request.name, "population_daily_compile");
      plans.push(decoded);
    }
  }
  if (scene.id === "compile-only") {
    assert.deepEqual(
      Object.keys(value).sort(),
      ["status", "reason", "evidence_ids", "limitations"].sort(),
    );
    assert.equal(value.status, "not_executed");
    assert(typeof value.reason === "string" && value.reason.length > 0);
    assert(
      results.length === 0 && plans.length > 0,
      "compile-only must not execute members",
    );
    assert(Array.isArray(value.evidence_ids) && value.evidence_ids.length > 0);
    for (const id of value.evidence_ids) {
      assert(typeof id === "string");
      assert(
        responses.some(
          (r) =>
            r.evidence_id === id &&
            requests.find((q) => q.evidence_id === id)?.name ===
              "population_daily_compile",
        ),
      );
    }
    assert(Array.isArray(value.limitations) && value.limitations.length > 0);
    return {
      passed: true,
      status: "not_executed",
      scope: "supported query deliberately not calculated; no membership claim",
    };
  }
  assert(
    results.length > 0,
    "answered membership requires executed daily_users evidence; compile is insufficient",
  );
  assert(Array.isArray(value.evidence_ids) && value.evidence_ids.length > 0);
  const cited = value.evidence_ids.map((id) => {
    assert(typeof id === "string");
    const result = results.find((r) => r.id === id);
    assert(
      result,
      "citation must identify a successful executed daily_users result",
    );
    return result;
  });
  assert(new Set(value.evidence_ids).size === value.evidence_ids.length);
  for (const result of cited) {
    for (const plan of plans) sameDailyBinding(plan, result.envelope);
    for (const key of [
      "request",
      "members",
      "member_count",
      "required_day_count",
      "qualification_rule",
    ])
      assert.deepEqual(
        value[key],
        result.envelope[key],
        "answer differs from executed evidence: " + key,
      );
  }
  // Hand-reviewed membership, independent of tool output; never supplied to the scripted completion.
  assert.deepEqual(
    value.members,
    [...scene.expected],
    "hand membership oracle",
  );
  assert.equal(value.member_count, String(scene.expected.length));
  assert(
    Array.isArray(value.limitations) &&
      value.limitations.length > 0 &&
      value.limitations.every((v) => typeof v === "string" && v.length > 0),
  );
  return {
    passed: true,
    executed_evidence_ids: value.evidence_ids,
    members: value.members,
    member_count: value.member_count,
    scope:
      "synthetic scripted integration; not model autonomy or business acceptance",
  };
}

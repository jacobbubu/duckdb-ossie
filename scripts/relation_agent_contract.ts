/** Relation simulation contract: independent link oracle stays outside provider context. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "./artifact_helpers.ts";
import { LIMITS, type JsonObject } from "./agent_loop.ts";
import { relationContent } from "./relation_mcp.ts";
import {
  relationPlan,
  relationResult,
  relationDiscovery,
  sameRelationBinding,
  compareRelationLinks,
  type RelationEnvelope,
} from "./relation_result.ts";
export const RELATION_SYSTEM = `Use only discovered relationship tools and resources. Discover canonical relation, rules, dependencies, identity and snapshot first. Only relationship String is allowed; never add date/products/limit. Entire captured root scope with selected TRUE predicate. Return full source User/date/product to target User links, do not reduce to distinct users or substitute a count. Preserve raw identities and finite DuckDB DATE texts including (BC) and wide years. Compile binds/explains but does not calculate links or count. For execution return one JSON object with exactly status="answered", relationship, root_concept, target_concept, root_identity_fields, links, link_count, derived_by, dependencies, scope, snapshot_id, evidence_ids, limitations; copy these fields from actual executed evidence and cite its evidence ID. Do not infer business truth or missing-source completeness. If asked explain only, do not execute; return exactly status="not_executed", reason, evidence_ids, limitations, cite compile evidence, no links/count. Supported unexecuted query is not unsupported. No arbitrary SQL/import/reload, generic metrics or historic state claims.`;
export const RELATION_SCENES = [
  {
    id: "direct",
    threshold: 10,
    relationship: "qualified_user",
    setup: "extended",
    question:
      "返回当前qualified_user的完整来源观察到目标User关系及条数，保留完整User/date/product身份，不按User去重。给出实际规则和执行证据。",
  },
  {
    id: "dependent",
    threshold: 10,
    relationship: "audited_user",
    setup: "extended",
    question:
      "返回当前audited_user的全部完整关系及条数，给出实际规则、依赖和执行证据。",
  },
  {
    id: "changed-rule",
    threshold: 20,
    relationship: "audited_user",
    setup: "extended",
    question:
      "返回当前audited_user的全部完整关系及条数，给出实际规则、依赖和执行证据。",
  },
  {
    id: "empty",
    threshold: 10,
    relationship: "audited_user",
    setup: "empty",
    question:
      "执行当前audited_user完整关系查询，给出完整links和条数；空来源不代表全部现实用户不存在。",
  },
  {
    id: "wide-date",
    threshold: 10,
    relationship: "audited_user",
    setup: "wide",
    question:
      "返回当前audited_user的完整关系及条数，保留所有有限DATE身份及实际排序，不限制四位年份或删除纪元前日期。",
  },
  {
    id: "compile-only",
    threshold: 10,
    relationship: "audited_user",
    setup: "extended",
    question:
      "只解释当前audited_user的完整关系查询，不执行，不给links或条数；明确没有计算，而非该能力不支持。",
  },
] as const;
export type RelationScene = (typeof RELATION_SCENES)[number];
export const RELATION_SOURCE_NAMES = [
  "examples/relation_server.sql",
  "docs/research/ossie-req-001/multi-period-research/every-day/observations.csv",
  "test/fixtures/population-relation-program/ontology.json",
  "test/fixtures/population-relation-program/bundle.json",
  "test/fixtures/population-observation/core.json",
  "test/fixtures/population-observation/mapping.json",
  ...[
    "relation_agent_contract",
    "relation_agent_rehearsal",
    "relation_agent_replay",
    "relation_mcp",
    "relation_result",
    "daily_mcp",
    "agent_loop",
    "agent_deadline",
    "mcp_check",
    "artifact_helpers",
    "observation_mcp",
    "observation_evidence",
    "observation_agent_archive",
    "observation_agent_replay",
    "observation_agent_contract",
    "population_result",
    "population_error",
  ].map((n) => `scripts/${n}.ts`),
];
export const RELATION_BUDGET = {
  per_loop: LIMITS,
  discovery_boundary:
    "Separate RPC timeouts; no whole discovery/process deadline guarantee",
  scenes: 6,
  maximum_simulated_completion_requests: 48,
  maximum_tool_calls: 72,
  monetary_ceiling: "not applicable: no model transport",
  input_token_ceiling: "not enforced",
};
export const RELATION_ANSWER_FIELDS = [
  "relationship",
  "root_concept",
  "target_concept",
  "root_identity_fields",
  "links",
  "link_count",
  "derived_by",
  "dependencies",
  "scope",
  "snapshot_id",
];
export function relationOracle(scene: RelationScene): string[][] {
  if (scene.setup === "empty") return [];
  if (scene.setup === "wide")
    return [
      "5877642-06-25 (BC)",
      "0001-01-01 (BC)",
      "2026-01-01",
      "10000-01-01",
      "5881580-07-10",
    ].map((date) => ["D", date, "X", "D"]);
  return readFileSync(
    join(
      ROOT,
      "docs/research/ossie-req-001/multi-period-research/every-day/observations.csv",
    ),
    "utf8",
  )
    .trim()
    .split("\n")
    .slice(1)
    .map((r) => r.split(","))
    .filter((r) => r[5] !== "" && Number(r[5]) >= scene.threshold)
    .map((r) => [r[1], r[2], r[3], r[1]])
    .sort(compareRelationLinks);
}
export function auditRelationAnswer(
  content: string,
  events: JsonObject[],
  scene: RelationScene,
  program: Record<string, unknown>,
) {
  const answer: unknown = JSON.parse(content);
  assert(
    answer !== null && typeof answer === "object" && !Array.isArray(answer),
  );
  const value = answer as JsonObject;
  const responses = events.filter((e) => e.kind === "tool_response"),
    requests = events.filter((e) => e.kind === "tool_request");
  const results: { id: string; envelope: RelationEnvelope }[] = [],
    plans: RelationEnvelope[] = [];
  for (const response of responses) {
    const request = requests.find(
      (e) => e.evidence_id === response.evidence_id,
    );
    assert(request);
    assert.deepEqual(
      request.arguments,
      { relationship: scene.relationship },
      "exact requested semantic object",
    );
    const reply = response.result as Parameters<typeof relationContent>[0];
    assert(
      request.name === "population_relation_objects" ||
        request.name === "population_relation_compile",
    );
    const decoded =
      request.name === "population_relation_objects"
        ? relationResult(relationContent(reply))
        : relationPlan(relationContent(reply));
    relationDiscovery(scene.relationship, program, decoded);
    if (request.name === "population_relation_objects")
      results.push({ id: String(response.evidence_id), envelope: decoded });
    else plans.push(decoded);
  }
  assert(
    Array.isArray(value.evidence_ids) &&
      value.evidence_ids.length > 0 &&
      value.evidence_ids.every((id) => typeof id === "string"),
  );
  assert(new Set(value.evidence_ids).size === value.evidence_ids.length);
  assert(
    Array.isArray(value.limitations) &&
      value.limitations.length > 0 &&
      value.limitations.every((v) => typeof v === "string" && v.length > 0),
  );
  if (scene.id === "compile-only") {
    assert.deepEqual(
      Object.keys(value).sort(),
      ["status", "reason", "evidence_ids", "limitations"].sort(),
    );
    assert(
      value.status === "not_executed" &&
        typeof value.reason === "string" &&
        value.reason.length > 0,
    );
    assert(
      results.length === 0 && plans.length > 0,
      "compile-only cannot calculate links",
    );
    for (const id of value.evidence_ids)
      assert(
        responses.some(
          (r) =>
            r.evidence_id === id &&
            requests.find((q) => q.evidence_id === id)?.name ===
              "population_relation_compile",
        ),
      );
    return {
      passed: true,
      status: "not_executed",
      scope: "supported query deliberately not executed; no membership claim",
    };
  }
  assert.deepEqual(
    Object.keys(value).sort(),
    ["status", ...RELATION_ANSWER_FIELDS, "evidence_ids", "limitations"].sort(),
  );
  assert.equal(value.status, "answered");
  assert(results.length > 0, "execution evidence required");
  for (const id of value.evidence_ids) {
    const result = results.find((r) => r.id === id);
    assert(result, "citation must name actual executed complete link result");
    for (const plan of plans) sameRelationBinding(plan, result.envelope);
    for (const k of RELATION_ANSWER_FIELDS)
      assert.deepEqual(
        value[k],
        result.envelope[k],
        "answer differs from executed evidence:" + k,
      );
  }
  const expected = relationOracle(scene);
  assert.deepEqual(value.links, expected, "independent complete link oracle");
  assert.equal(value.link_count, String(expected.length));
  return {
    passed: true,
    executed_evidence_ids: value.evidence_ids,
    link_count: value.link_count,
    member_oracle: "complete root/target links, not distinct User",
    scope:
      "scripted integration only, not model autonomy or business acceptance",
  };
}

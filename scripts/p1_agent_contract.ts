import { STATE_SCOPE_NOTICE } from "./p1_agent_safety.ts";
import { p1JSON as factJSON } from "./p1_agent_json.ts";
/** Task evaluation is separate from runtime argument validation and model context. */
import assert from "node:assert/strict";
import { readFileSync, existsSync } from "node:fs";
import { join, resolve, relative } from "node:path";
import { ROOT, digest } from "./artifact_helpers.ts";
import type { JsonObject, Tool } from "./agent_loop.ts";
import { factValue, factProgram, type FactProgram } from "./fact_result.ts";
import {
  stateRequest,
  stateResult,
  statePlan,
  sameStateBinding,
  object,
  exact,
  stateProgram,
} from "./daily_state_result.ts";
import {
  historyRequest,
  historyResult,
  historyPlan,
  sameHistoryBinding,
  historyProgram,
} from "./history_result.ts";
import { binding } from "./observation_evidence.ts";
import { factContent } from "./fact_mcp.ts";
export type Scene = "facts" | "states" | "history";
export const SCENES: Scene[] = ["facts", "states", "history"];
export const RESEARCH = "docs/research/ossie-req-001/p1-agent-live-research/";
export type AgentLimits = {
  rounds: number;
  calls: number;
  milliseconds: number;
  outputTokens: number;
  inputBytes: number;
};
export const LIMITS: Readonly<AgentLimits> = Object.freeze({
  rounds: 4,
  calls: 3,
  milliseconds: 180000,
  outputTokens: 4096,
  inputBytes: 131072,
});
export const INBOUND_BYTES = 2097152;
export type Discovery = {
  tools: Tool[];
  rows: Record<string, JsonObject[]>;
  program: JsonObject;
  binding: JsonObject;
};
export function prompts() {
  return factJSON(
    readFileSync(join(ROOT, RESEARCH + "prompts.json"), "utf8"),
  ) as JsonObject;
}
export function context(scene: Scene, d: Discovery) {
  const p = prompts();
  const item = (p.scenes as JsonObject[]).find((v) => v.id === scene)!;
  return {
    system:
      String(p.system) + (scene === "states" ? "\n" + STATE_SCOPE_NOTICE : ""),
    question: String(item.question),
    tools: d.tools,
    resources: d.rows,
  };
}
export function validateArguments(
  name: string,
  raw: string,
  tools: Tool[],
): JsonObject {
  const tool = tools.find((t) => t.name === name);
  assert(tool, "undiscovered tool");
  const a = factJSON(raw, 131072);
  object(a);
  exact(a, Object.keys(tool.inputSchema.properties as object));
  assert(
    Object.values(a).every((v) => typeof v === "string"),
    "String arguments required",
  );
  assert(typeof a.request_json === "string");
  const request = factJSON(a.request_json, 16384);
  object(request);
  if (name.startsWith("population_daily_states")) stateRequest(request);
  else if (name.startsWith("population_history_links")) historyRequest(request);
  else {
    const window = name.startsWith("population_fact_windows");
    exact(
      request,
      window
        ? ["input_scope", "windows", "display"]
        : ["domain", "input_scope", "metrics", "cohort"],
    );
    assert(request.input_scope === "entire_capture");
    if (!window) {
      assert(request.domain === "common");
      validateCohort(request.cohort, 0, { n: 0 });
    } else {
      object(request.display);
      exact(
        request.display,
        request.display.mode === "all"
          ? ["mode"]
          : ["mode", "start_date", "end_date"],
      );
    }
    const names = request[window ? "windows" : "metrics"];
    assert(
      Array.isArray(names) &&
        names.length > 0 &&
        names.every((v) => typeof v === "string"),
    );
  }
  return a;
}
function validateCohort(v: unknown, depth: number, counter: { n: number }) {
  object(v);
  assert(depth < 16 && ++counter.n <= 64);
  if (v.operation === "all") exact(v, ["operation"]);
  else if (v.operation === "predicate") {
    exact(v, ["operation", "predicate"]);
    assert(typeof v.predicate === "string");
  } else {
    exact(v, ["operation", "children"]);
    assert(
      ["intersection", "union", "difference"].includes(String(v.operation)),
    );
    assert(Array.isArray(v.children) && v.children.length === 2);
    v.children.forEach((c) => validateCohort(c, depth + 1, counter));
  }
}
export function decode(
  name: string,
  args: JsonObject,
  reply: unknown,
  d: Discovery,
): JsonObject {
  const request = factJSON(String(args.request_json), 16384) as JsonObject;
  const content = factContent(reply as Parameters<typeof factContent>[0]);
  const plan = name.endsWith("_compile");
  if (name.startsWith("population_fact_"))
    return factValue(
      content,
      request,
      d.program as FactProgram,
      name.includes("windows"),
      plan,
    );
  if (name.startsWith("population_daily_states")) {
    const r = plan ? statePlan(content) : stateResult(content);
    assert(
      String(args.concept).toLowerCase() ===
        String(r.qualification_concept).toLowerCase(),
      "concept/result correspondence mismatch",
    );
    sameStateBinding(r, d.program);
    assert.deepEqual(r.request, stateRequest(request));
    return r;
  }
  const r = plan ? historyPlan(content) : historyResult(content);
  assert(
    String(args.relationship).toLowerCase() ===
      String(r.relationship).toLowerCase(),
    "relationship/result correspondence mismatch",
  );
  sameHistoryBinding(r, d.program);
  assert.deepEqual(r.request, historyRequest(request));
  return r;
}
export function fixtureDefinition(scene: Scene, d: Discovery) {
  const fixture =
    scene === "history"
      ? "test/fixtures/population-history-program/"
      : scene === "states"
        ? "test/fixtures/population-observation/"
        : "test/fixtures/population-fact-program/";
  for (const role of ["core", "ontology", "mapping", "manifest"])
    assert.equal(
      d.program[role + "_sha256"],
      digest(
        join(
          ROOT,
          fixture +
            (role === "manifest"
              ? scene === "states"
                ? "bundle-10"
                : "bundle"
              : role) +
            ".json",
        ),
      ),
      "formal fixture definition mismatch",
    );
}
export function evaluate(
  scene: Scene,
  answerText: string,
  events: JsonObject[],
  d: Discovery,
) {
  const a = factJSON(answerText);
  object(a);
  assert(a.scene === scene);
  assert(
    Array.isArray(a.evidence_ids) &&
      a.evidence_ids.every((v) => typeof v === "string"),
  );
  assert(
    Array.isArray(a.limitations) &&
      a.limitations.length > 0 &&
      a.limitations.every((v) => typeof v === "string" && v.length > 0),
  );
  if (a.status === "failed") {
    exact(a, ["status", "scene", "reason", "evidence_ids", "limitations"]);
    assert(typeof a.reason === "string" && a.reason.length > 0);
    for (const id of a.evidence_ids)
      assert(events.some((e) => e.evidence_id === id));
    return { status: "task_failed" as const };
  }
  exact(a, [
    "status",
    "scene",
    "result",
    "definition",
    "evidence_ids",
    "limitations",
  ]);
  assert(
    a.status === "answered" &&
      typeof a.definition === "string" &&
      a.definition.length > 0,
  );
  assert(a.evidence_ids.length === 1);
  const id = a.evidence_ids[0];
  const calls = events.filter(
    (e) => e.kind === "tool_request" && e.evidence_id === id,
  );
  const replies = events.filter(
    (e) => e.kind === "tool_response" && e.evidence_id === id,
  );
  assert(calls.length === 1 && replies.length === 1);
  const call = calls[0];
  assert(
    !String(call.name).endsWith("_compile"),
    "compile is not execution evidence",
  );
  const args = validateArguments(
    String(call.name),
    String(call.arguments_json),
    d.tools,
  );
  const result = decode(String(call.name), args, replies[0].result, d);
  assert.deepEqual(a.result, result, "answer differs from actual execution");
  // Hidden task/oracle is read only after the loop has produced the final answer.
  const expected = factJSON(
    readFileSync(join(ROOT, RESEARCH + "independent-answers.json"), "utf8"),
  ) as Record<string, JsonObject>;
  const task =
    expected[
      scene === "facts" ? "fact" : scene === "states" ? "state" : "history"
    ];
  const r = factJSON(String(args.request_json)) as JsonObject;
  if (scene === "facts") {
    assert(String(call.name) === "population_fact_users");
    const canonical = structuredClone(r);
    canonical.metrics = (canonical.metrics as string[])
      .map((v) => v.toLowerCase())
      .sort();
    const gold = structuredClone(task.request as JsonObject);
    gold.metrics = (gold.metrics as string[]).sort();
    // Narrow supported normalization; never accept cost>80 just because members happen to match.
    object(canonical.cohort);
    const children = canonical.cohort.children as JsonObject[];
    assert(Array.isArray(children));
    if (typeof children[1]?.predicate === "string")
      children[1].predicate = children[1].predicate
        .replace(/\s/g, "")
        .toLowerCase()
        .replace(/^expense>50(?:\.0+)?$/, "expense>50");
    assert.deepEqual(canonical, gold, "task scope/threshold differs");
    const columns = result.columns as JsonObject[];
    const wanted = ["income", "expense", "balance"].map((n) =>
      columns.findIndex((c) => String(c.name).toLowerCase() === n),
    );
    assert(wanted.every((n) => n >= 0));
    const actual = (result.rows as unknown[][]).map((row) => [
      row[0],
      ...wanted.map((i) => row[i]),
    ]);
    const goldData = task.expected as JsonObject;
    assert.deepEqual(actual, goldData.rows, "independent fact oracle mismatch");
    assert(
      result.common_user_count === goldData.common_user_count &&
        result.selected_user_count === goldData.selected_user_count,
    );
  } else {
    assert.deepEqual(r, task.request, "task scope differs");
    if (scene === "states") {
      const g = task.expected as JsonObject;
      assert.deepEqual(result.states, g.states);
      assert.deepEqual(result.comparisons, g.comparisons);
      for (const k of [
        "user_count",
        "state_count",
        "comparison_count",
        "combined_object_count",
        "state_totals",
      ])
        assert.deepEqual(result[k], g[k]);
      assert(result.changed_comparison_count === g.changed_count);
    } else {
      assert.deepEqual(result.links, task.expected_links);
      assert(result.link_count === task.link_count);
    }
  }
  return {
    status: "mechanical_pass" as const,
    semantic_review: "pending",
    limitation:
      "A matching full answer is not a review of free-form explanation.",
  };
}
export function sourceHashes() {
  const found = new Set<string>();
  function visit(p: string) {
    p = resolve(p);
    const name = relative(ROOT, p);
    if (found.has(name)) return;
    assert(p.startsWith(ROOT + "/") && existsSync(p));
    found.add(name);
    const text = readFileSync(p, "utf8");
    if (p.endsWith(".ts"))
      for (const m of text.matchAll(/from\s+["'](\.\.?\/[^"']+\.ts)["']/g))
        visit(resolve(p, "..", m[1]));
  }
  for (const p of [
    "scripts/p1_agent.ts",
    "scripts/p1_agent_run.ts",
    "scripts/verify_p1_agent_transport.ts",
    "scripts/p1_agent_simulation.ts",
    "scripts/p1_agent_contract.ts",
    "scripts/p1_agent_replay.ts",
    "scripts/p1_agent_public.ts",
    "scripts/verify_p1_agent_offline.ts",
    "package.json",
    "bun.lock",
    RESEARCH + "prompts.json",
    RESEARCH + "independent-answers.json",
    "docs/research/ossie-req-001/p1-time-accepted/acceptance.json",
  ])
    visit(join(ROOT, p));
  const inventory = JSON.parse(
    readFileSync(join(ROOT, RESEARCH + "source-hashes.json"), "utf8"),
  );
  for (const p of Object.keys(inventory.source_sha256)) visit(join(ROOT, p));
  return Object.fromEntries(
    [...found].sort().map((p) => [p, digest(join(ROOT, p))]),
  );
}

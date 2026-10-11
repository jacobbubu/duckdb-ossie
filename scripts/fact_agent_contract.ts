/** Bounded scripted integration contract; independent oracle is never provider context. */
import assert from "node:assert/strict";
import { join } from "node:path";
import { ROOT, digest } from "./artifact_helpers.ts";
import { LIMITS, type JsonObject } from "./agent_loop.ts";
import { factContent, type FactSetup } from "./fact_mcp.ts";
import { factValue, factJSON, type FactProgram } from "./fact_result.ts";
import { factInputs, factUserOracle, factWindowOracle } from "./fact_oracle.ts";
const users = (
  metrics: string[],
  cohort: JsonObject = { operation: "all" },
) => ({ domain: "common", input_scope: "entire_capture", metrics, cohort });
const window = (display: JsonObject = { mode: "all" }) => ({
  input_scope: "entire_capture",
  windows: ["previous", "rolling"],
  display,
});
const day = {
  mode: "date_range",
  start_date: "2026-02-01",
  end_date: "2026-02-01",
};
export const FACT_LIMITATIONS = [
  "Scripted integration from actual tool evidence, not model autonomy or business acceptance; no production completeness or history/state inference.",
];
export const FACT_UNEXECUTED_REASON =
  "Only the logical window plan was inspected; rows, values and counts were not calculated.";
export function factFixtureBinding(program: FactProgram) {
  for (const role of ["core", "ontology", "mapping", "manifest"]) {
    const file = role === "manifest" ? "bundle" : role;
    assert.equal(
      program[`${role}_sha256`],
      digest(join(ROOT, `test/fixtures/population-fact-program/${file}.json`)),
      "fixture definition binding mismatch: " + role,
    );
  }
}
export const FACT_SYSTEM = `Use only discovered fact tools/resources. Discover source grain, canonical declarations, units, scope, budgets and six snapshot bindings first. Send only request_json String. User metrics reduce each fact independently before common User combination; NULL money is not zero, COUNT(User) and COUNT(amount) have different denominators. Cohorts are TRUE member sets; difference removes RHS TRUE only. Windows use whole captured single-source User/date observations before display DATE filtering; ROWS is not calendar days, LAG includes NULL, noncommon Users remain. Preserve exact decimal/integer Strings, NULL, raw identities and full finite DuckDB DATE text including BC/wide years. Compile binds/explains but does not execute or compute members/counts. For execution return one JSON object with exactly status="answered", kind="users" or "windows", result (the complete actual executed inner envelope unchanged), evidence_ids, limitations. Cite actual successful execution evidence, never compile-only, capture counts or invented members. For explain-only do not execute and return exactly status="not_executed", reason, evidence_ids, limitations; no result or numbers. Do not claim business truth or missing-source completeness, unsupported state/history inference, arbitrary SQL, input filters, import/reload or paging.`;
export const FACT_SCENES: readonly {
  id: string;
  setup: FactSetup;
  kind: "users" | "windows";
  request: JsonObject;
  question: string;
  compileOnly?: boolean;
}[] = [
  {
    id: "separate-grains",
    setup: "original",
    kind: "users",
    request: users([
      "balance",
      "income",
      "expense",
      "revenue_rows",
      "revenue_values",
      "cost_rows",
      "cost_values",
    ]),
    question:
      "执行全部共同User的balance/income/expense及两来源行数、非NULL金额数，保留完整金额、NULL与共同/筛选分母。先各自归约，不能明细连接重复累计。",
  },
  {
    id: "difference-unknown",
    setup: "original",
    kind: "users",
    request: users(["balance"], {
      operation: "difference",
      children: [
        { operation: "all" },
        { operation: "predicate", predicate: "expense>50" },
      ],
    }),
    question:
      "从全部共同User中排除expense>50为TRUE的成员，执行balance查询，给出完整成员与独立共同分母；不能把右侧UNKNOWN当TRUE。",
  },
  {
    id: "empty-cohort",
    setup: "original",
    kind: "users",
    request: users(["balance"], {
      operation: "predicate",
      predicate: "balance>999999",
    }),
    question:
      "执行共同User中balance>999999的balance查询。完整返回空集也须保留筛选前共同分母，不把空结果当共同来源为空。",
  },
  {
    id: "displayed-window",
    setup: "original",
    kind: "windows",
    request: window(day),
    question:
      "执行previous/rolling两个声明窗口，只展示2026-02-01观察行。先计算整份来源再筛展示日期，保留全部来源User，不限共同人群。",
  },
  {
    id: "empty-sources",
    setup: "empty",
    kind: "users",
    request: users(["balance", "revenue_rows", "cost_rows"]),
    question:
      "执行全部共同User的balance和两源行数。来源为空只说明本次捕获范围，不推断现实中不存在用户。",
  },
  {
    id: "wide-window",
    setup: "wide",
    kind: "windows",
    request: window(),
    question:
      "执行previous/rolling，展示全部来源观察行；保留BC、超四位年份和全部有限DATE身份及时间排序。",
  },
  {
    id: "raw-identity",
    setup: "raw",
    kind: "users",
    request: users(["balance"]),
    question:
      "执行全部共同User的balance，保留大小写、Unicode和控制字符原始身份，不规范化或去重不同原始字节。",
  },
  {
    id: "compile-only",
    setup: "original",
    kind: "windows",
    request: window(day),
    compileOnly: true,
    question:
      "只解释previous/rolling窗口和2026-02-01展示范围，不执行，不给出成员、金额或条数；这属于支持的查询未执行。",
  },
];
export type FactScene = (typeof FACT_SCENES)[number];
export const FACT_BUDGET = {
  per_loop: LIMITS,
  discovery_boundary:
    "Separate RPC timeouts; no whole discovery/process deadline guarantee",
  scenes: 8,
  maximum_simulated_completion_requests: 64,
  maximum_tool_calls: 96,
  monetary_ceiling: "not applicable: no model transport",
  input_token_ceiling: "not enforced",
};
export const FACT_SOURCE_NAMES = [
  "examples/fact_server.sql",
  ...[
    "core.json",
    "ontology.json",
    "mapping.json",
    "bundle.json",
    "rows.sql",
  ].map((n) => `test/fixtures/population-fact-program/${n}`),
  ...[
    "fact_agent_contract",
    "fact_agent_rehearsal",
    "fact_agent_replay",
    "fact_mcp",
    "fact_result",
    "fact_oracle",
    "daily_mcp",
    "relation_result",
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
  ].map((n) => `scripts/${n}.ts`),
];
export function decodeFactCall(
  name: string,
  args: JsonObject,
  reply: Parameters<typeof factContent>[0],
  scene: FactScene,
  program: FactProgram,
) {
  const query = `population_fact_${scene.kind}`;
  assert(
    name === query || name === query + "_compile",
    "scene tool kind mismatch",
  );
  assert.deepEqual(Object.keys(args), ["request_json"]);
  assert(typeof args.request_json === "string");
  assert.deepEqual(
    factJSON(args.request_json, 16384),
    scene.request,
    "scene request scope mismatch",
  );
  return factValue(
    factContent(reply),
    scene.request,
    program,
    scene.kind === "windows",
    name.endsWith("_compile"),
  );
}
export function auditFactAnswer(
  content: string,
  events: JsonObject[],
  scene: FactScene,
  program: FactProgram,
) {
  factFixtureBinding(program);
  const answer = factJSON(content);
  assert(
    answer !== null && typeof answer === "object" && !Array.isArray(answer),
  );
  const value = answer as JsonObject;
  const requests = events.filter((e) => e.kind === "tool_request"),
    responses = events.filter((e) => e.kind === "tool_response");
  assert.equal(
    requests.length,
    responses.length,
    "paired tool evidence required",
  );
  for (const list of [requests, responses])
    assert(
      new Set(list.map((e) => e.evidence_id)).size === list.length,
      "duplicate evidence identity",
    );
  const query = `population_fact_${scene.kind}`,
    results: { id: string; value: ReturnType<typeof factValue> }[] = [],
    plans: { id: string; value: ReturnType<typeof factValue> }[] = [];
  for (const response of responses) {
    const request = requests.find(
      (e) => e.evidence_id === response.evidence_id,
    );
    assert(request);
    assert(
      request.name === query || request.name === query + "_compile",
      "scene tool kind mismatch",
    );
    const plan = request.name === query + "_compile";
    const decoded = decodeFactCall(
      String(request.name),
      request.arguments as JsonObject,
      response.result as Parameters<typeof factContent>[0],
      scene,
      program,
    );
    (plan ? plans : results).push({
      id: String(response.evidence_id),
      value: decoded,
    });
  }
  assert(
    Array.isArray(value.evidence_ids) &&
      value.evidence_ids.length > 0 &&
      value.evidence_ids.every((id) => typeof id === "string" && id.length > 0),
  );
  assert(new Set(value.evidence_ids).size === value.evidence_ids.length);
  assert(
    Array.isArray(value.limitations) &&
      value.limitations.length > 0 &&
      value.limitations.every((v) => typeof v === "string" && v.length > 0),
  );
  assert.deepEqual(
    value.limitations,
    FACT_LIMITATIONS,
    "scripted limitation declaration mismatch",
  );
  if (scene.compileOnly) {
    assert.deepEqual(
      Object.keys(value).sort(),
      ["status", "reason", "evidence_ids", "limitations"].sort(),
    );
    assert(
      value.status === "not_executed" &&
        typeof value.reason === "string" &&
        value.reason.length > 0,
    );
    assert.equal(
      value.reason,
      FACT_UNEXECUTED_REASON,
      "scripted compile-only explanation mismatch",
    );
    assert(
      results.length === 0 && plans.length > 0,
      "compile-only cannot execute",
    );
    for (const id of value.evidence_ids)
      assert(
        plans.some((p) => p.id === id),
        "compile evidence required",
      );
    return {
      passed: true,
      status: "not_executed",
      scope: "supported query deliberately unexecuted; no rows or count claim",
    };
  }
  assert.deepEqual(
    Object.keys(value).sort(),
    ["status", "kind", "result", "evidence_ids", "limitations"].sort(),
  );
  assert.equal(value.status, "answered");
  assert.equal(value.kind, scene.kind);
  assert(results.length > 0, "execution evidence required");
  for (const id of value.evidence_ids) {
    const result = results.find((r) => r.id === id);
    assert(result, "citation must name actual executed complete fact result");
    assert.deepEqual(
      value.result,
      result.value,
      "answer differs from executed fact evidence",
    );
  }
  const actual = value.result as ReturnType<typeof factValue>;
  const inputs = factInputs(scene.setup);
  if (scene.kind === "users") {
    const choose =
      scene.id === "empty-cohort"
        ? () => false
        : scene.id === "difference-unknown"
          ? (v: Record<string, bigint | null>) =>
              !(v.expense !== null && v.expense > 5000n)
          : () => true;
    const expected = factUserOracle(
      inputs,
      scene.request.metrics as string[],
      choose,
    );
    assert.deepEqual(
      actual.rows,
      expected.rows,
      "independent complete fact User/money oracle",
    );
    assert.equal(
      actual.common_user_count,
      expected.common_user_count,
      "independent common denominator oracle",
    );
    assert.equal(actual.selected_user_count, expected.selected_user_count);
  } else
    assert.deepEqual(
      actual.rows,
      factWindowOracle(
        inputs,
        scene.request.windows as string[],
        scene.request.display as JsonObject,
      ),
      "independent whole-capture window then display oracle",
    );
  return {
    passed: true,
    executed_evidence_ids: value.evidence_ids,
    row_count: actual.row_count,
    kind: scene.kind,
    scope:
      "scripted exact tool evidence and independent complete oracle; not model autonomy or business acceptance",
  };
}

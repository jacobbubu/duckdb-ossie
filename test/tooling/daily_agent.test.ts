import { test, expect } from "bun:test";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "../../scripts/artifact_helpers.ts";
import {
  DAILY_SCENES,
  dailySceneRequest,
  auditDailyAnswer,
  type DailyScene,
} from "../../scripts/daily_agent_contract.ts";
import { DAILY_TRACE } from "../../scripts/daily_result.ts";
import {
  toolArguments,
  type Tool,
  type JsonObject,
} from "../../scripts/agent_loop.ts";
const report = JSON.parse(
  readFileSync(
    join(ROOT, "docs/research/ossie-req-001/p1-daily-mcp-controls/mcp.json"),
    "utf8",
  ),
);
function fixture(scene: DailyScene, caseId = "all-two-days") {
  const result = structuredClone(
    report.sessions.find((s: any) => s.case === caseId).result,
  );
  const args = {
    concept: "QualifiedObservation",
    request_json: JSON.stringify(dailySceneRequest(scene)),
  };
  const binding = Object.fromEntries(
    DAILY_TRACE.map((key) => [key, result[key]]),
  );
  const answer: JsonObject = {
    status: "answered",
    ...Object.fromEntries(
      [
        "request",
        "members",
        "member_count",
        "required_day_count",
        "qualification_rule",
      ].map((key) => [key, result[key]]),
    ),
    evidence_ids: ["tool-2"],
    limitations: ["synthetic unit fixture"],
  };
  const events: JsonObject[] = [
    {
      kind: "tool_request",
      evidence_id: "tool-2",
      name: "population_daily_users",
      arguments: args,
    },
    {
      kind: "tool_response",
      evidence_id: "tool-2",
      result: {
        result: {
          content: [
            {
              type: "text",
              text: JSON.stringify([{ result_json: JSON.stringify(result) }]),
            },
          ],
        },
      },
    },
  ];
  return { result, args, binding, answer, events };
}
test("daily audit compares full membership to independent hand oracle even when answer and tool agree", () => {
  const scene = DAILY_SCENES[0],
    f = fixture(scene);
  expect(
    auditDailyAnswer(JSON.stringify(f.answer), f.events, scene, f.binding)
      .passed,
  ).toBe(true);
  f.result.members[0] = "A";
  f.answer.members = f.result.members;
  (f.events[1].result as any).result.content[0].text = JSON.stringify([
    { result_json: JSON.stringify(f.result) },
  ]);
  expect(() =>
    auditDailyAnswer(JSON.stringify(f.answer), f.events, scene, f.binding),
  ).toThrow(/hand membership oracle/);
});
test("daily audit rejects matching empty answers from wrong request scope", () => {
  const scene = DAILY_SCENES[2],
    f = fixture(scene, "global-missing-day");
  expect(
    auditDailyAnswer(JSON.stringify(f.answer), f.events, scene, f.binding)
      .passed,
  ).toBe(true);
  f.args.request_json = JSON.stringify({
    ...dailySceneRequest(scene),
    products: { mode: "subset", values: ["Z"] },
  });
  expect(() =>
    auditDailyAnswer(JSON.stringify(f.answer), f.events, scene, f.binding),
  ).toThrow(/wrong tool scope/);
});
test("daily audit rejects wrong rule with unchanged members/count and stale trace", () => {
  const scene = DAILY_SCENES[0],
    f = fixture(scene);
  f.result.qualification_rule.declared_sql =
    "DailyObservation.classification_value >= 11";
  f.answer.qualification_rule = f.result.qualification_rule;
  (f.events[1].result as any).result.content[0].text = JSON.stringify([
    { result_json: JSON.stringify(f.result) },
  ]);
  expect(() =>
    auditDailyAnswer(JSON.stringify(f.answer), f.events, scene, f.binding),
  ).toThrow();
  const stale = fixture(scene);
  stale.binding.snapshot_id = "old";
  expect(() =>
    auditDailyAnswer(
      JSON.stringify(stale.answer),
      stale.events,
      scene,
      stale.binding,
    ),
  ).toThrow();
});
test("compile-only cannot support even the correct oracle number or an unrelated citation", () => {
  const scene = DAILY_SCENES[0],
    f = fixture(scene);
  const plan = report.sessions.find((s: any) => s.case === "all-two-days").plan;
  f.events[0].name = "population_daily_compile";
  (f.events[1].result as any).result.content[0].text = JSON.stringify([
    { plan_json: JSON.stringify(plan) },
  ]);
  expect(() =>
    auditDailyAnswer(JSON.stringify(f.answer), f.events, scene, f.binding),
  ).toThrow(/compile is insufficient/);
  const citation = fixture(scene);
  citation.answer.evidence_ids = ["tool-999"];
  expect(() =>
    auditDailyAnswer(
      JSON.stringify(citation.answer),
      citation.events,
      scene,
      citation.binding,
    ),
  ).toThrow(/citation/);
});
test("shared Agent dispatcher rejects publisher coercions and unknown outer scope before RPC", () => {
  const tools: Tool[] = [
    {
      name: "population_daily_users",
      inputSchema: {
        properties: {
          concept: { type: "string" },
          request_json: { type: "string" },
        },
        required: ["concept", "request_json"],
      },
    },
  ];
  for (const args of [
    { concept: "QualifiedObservation", request_json: {} },
    { concept: "QualifiedObservation", request_json: "{}", filters: [] },
    { concept: "QualifiedObservation" },
  ])
    expect(() =>
      toolArguments(
        {
          id: "call",
          type: "function",
          function: {
            name: "population_daily_users",
            arguments: JSON.stringify(args),
          },
        },
        tools,
      ),
    ).toThrow();
});
test("daily rehearsal/replay expose no live mode or provider environment and use injected transport", () => {
  const source =
    readFileSync(join(ROOT, "scripts/daily_agent_rehearsal.ts"), "utf8") +
    readFileSync(join(ROOT, "scripts/daily_agent_replay.ts"), "utf8");
  expect(source).not.toMatch(/process\.env|liveConfiguration|fetch\s*\(/);
  expect(source).toContain('"simulated"');
  expect(source).toContain("async () =>");
});

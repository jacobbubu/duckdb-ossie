import { test, expect } from "bun:test";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "../../scripts/artifact_helpers.ts";
import {
  dailyPlan,
  dailyResult,
  sameDailyBinding,
} from "../../scripts/daily_result.ts";
// Real native/MCP envelopes anchor fixtures; mutations probe hostile/partial client data.
const report = JSON.parse(
  readFileSync(
    join(ROOT, "docs/research/ossie-req-001/p1-daily-mcp-controls/mcp.json"),
    "utf8",
  ),
);
const base = report.sessions[0];
const carrier = (field: string, value: unknown) =>
  JSON.stringify([{ [field]: JSON.stringify(value) }]);
const copy = <T>(value: T): T => structuredClone(value);
test("daily decoders preserve all twelve real scope/rule/hash/member envelopes", () => {
  for (const scene of report.sessions) {
    const plan = dailyPlan(carrier("plan_json", scene.plan));
    const result = dailyResult(carrier("result_json", scene.result));
    sameDailyBinding(plan, result);
    expect(result.member_count).toBe(String(result.members.length));
  }
});
test("daily members retain raw Unicode/case/control IDs and integer-text count", () => {
  const raw = ["A", "a", "汉", "😀", "\u0000r", 'quote"\\'];
  raw.sort((a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b)));
  const value = {
    ...base.result,
    members: raw,
    member_count: String(raw.length),
  };
  expect(dailyResult(carrier("result_json", value)).members).toEqual(raw);
  for (const members of [["A", "A"], ["a", "A"], [""], [3], ["\ud800"]])
    expect(() =>
      dailyResult(
        carrier("result_json", {
          ...value,
          members,
          member_count: String(members.length),
        }),
      ),
    ).toThrow();
});
test("daily decoder refuses partial/truncated/count-number and unproven execution metadata", () => {
  for (const mutation of [
    { complete: false },
    { truncated: true },
    { paging: true },
    { member_count: base.result.members.length },
    { member_count: "01" },
    { executed: false },
    { schema_version: "ossie-population-result-v1" },
    { count_only: true },
    { content_sha256: "missing" },
  ])
    expect(() =>
      dailyResult(carrier("result_json", { ...base.result, ...mutation })),
    ).toThrow();
});
test("daily normalized scope rejects invalid calendars, extra filters, wrong days and product identity loss", () => {
  for (const mutation of [
    { request: { ...base.result.request, start_date: "2026-02-30" } },
    { request: { ...base.result.request, start_date: "0000-01-01" } },
    { request: { ...base.result.request, filters: [] } },
    {
      request: {
        ...base.result.request,
        products: { mode: "all", values: [] },
      },
    },
    {
      request: {
        ...base.result.request,
        products: { mode: "subset", values: ["X", "X"] },
      },
    },
    {
      request: {
        ...base.result.request,
        products: { mode: "subset", values: [1] },
      },
    },
    { required_day_count: "1" },
  ])
    expect(() =>
      dailyResult(carrier("result_json", { ...base.result, ...mutation })),
    ).toThrow();
});
test("daily plan refuses execution/physical claims, missing reduction, guessed types and mismatched binding", () => {
  for (const mutation of [
    { executed: true },
    { physical_plan: true },
    { members_computed: true },
    { sql_replayable: true },
    { plan_json_utf8_byte_limit: 2000000 },
    { operators: base.plan.operators.slice(0, -1) },
    { members: [] },
  ])
    expect(() =>
      dailyPlan(carrier("plan_json", { ...base.plan, ...mutation })),
    ).toThrow();
  const types = copy(base.plan.bound_identity);
  types[1].bound_type = "TIMESTAMP";
  expect(() =>
    dailyPlan(carrier("plan_json", { ...base.plan, bound_identity: types })),
  ).toThrow();
  const plan = dailyPlan(carrier("plan_json", base.plan));
  for (const key of ["snapshot_id", "concept", "request", "qualification_rule"])
    expect(() =>
      sameDailyBinding(plan, { ...base.result, [key]: "different" }),
    ).toThrow();
});
test("daily carrier refuses foreign format, multiple envelopes and oversize bytes", () => {
  expect(() => dailyResult("[]")).toThrow();
  expect(() => dailyPlan(carrier("result_json", base.plan))).toThrow();
  expect(() =>
    dailyResult(
      JSON.stringify([
        { result_json: JSON.stringify(base.result), other: "ignored?" },
      ]),
    ),
  ).toThrow();
  expect(() =>
    dailyResult(
      JSON.stringify([
        { result_json: JSON.stringify(base.result) },
        { result_json: JSON.stringify(base.result) },
      ]),
    ),
  ).toThrow();
  expect(() =>
    dailyResult(
      carrier("result_json", { ...base.result, concept: "x".repeat(1048576) }),
    ),
  ).toThrow();
});

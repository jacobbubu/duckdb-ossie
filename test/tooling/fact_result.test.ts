import { test, expect } from "bun:test";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "../../scripts/artifact_helpers.ts";
import { factJSON, factProgram, factValue } from "../../scripts/fact_result.ts";
const report = JSON.parse(
  readFileSync(
    join(
      ROOT,
      "docs/research/ossie-req-001/p1-fact-mcp-controls/conformance.json",
    ),
    "utf8",
  ),
);
const carrier = (field: string, value: unknown) =>
  JSON.stringify([{ [field]: JSON.stringify(value) }]);
const scene = report.sessions[0],
  base = scene.cases[0];
const program = () => factProgram(carrier("program_json", scene.program));
const decode = (
  value: Record<string, unknown>,
  c = base,
  p = program(),
  plan = false,
) =>
  factValue(
    carrier(plan ? "plan_json" : "result_json", value),
    c.request,
    p,
    "windows" in c.request,
    plan,
  );
test("fact native MCP examples retain exact money, NULL, raw identities, finite dates and empty denominators", () => {
  for (const s of report.sessions) {
    const p = factProgram(carrier("program_json", s.program));
    for (const c of s.cases) {
      const result = decode(c.result, c, p),
        plan = decode(c.plan, c, p, true);
      expect(result.rows).toEqual(c.independent.rows);
      expect(plan.executed).toBe(false);
      if (!("windows" in c.request))
        expect(result.common_user_count).toBe(c.independent.common_user_count);
    }
  }
});
test("fact result refuses partial, duplicate, unordered, wrongly encoded and overbound cells", () => {
  for (const mutate of [
    (v: any) => v.rows.pop(),
    (v: any) => v.rows.reverse(),
    (v: any) => {
      v.rows.push(v.rows.at(-1));
      v.row_count = v.selected_user_count = String(v.rows.length);
    },
    (v: any) => (v.rows[0][1] = 290),
    (v: any) => (v.rows[0][1] = "2.90e2"),
    (v: any) => (v.rows[0][1] = "0290.00"),
    (v: any) => (v.rows[0][1] = "-0.00"),
    (v: any) => (v.rows[0][1] = "1".repeat(37) + ".00"),
    (v: any) => (v.rows[0][0] = "\ud800"),
    (v: any) => (v.rows[0][4] = null),
    (v: any) => (v.rows[0][4] = "9"),
    (v: any) => (v.row_count = 3),
    (v: any) => (v.common_user_count = "9999"),
    (v: any) => (v.paging = true),
    (v: any) => (v.truncated = true),
    (v: any) => (v.columns[1].unit = "currency:EUR:scale2"),
    (v: any) => (v.extra = 1),
    (v: any) => (v.content_sha256 = "f".repeat(64)),
    (v: any) => (v.selected[0] = "expense"),
    (v: any) => (v.user_concept = "Other"),
  ]) {
    const v = structuredClone(base.result);
    mutate(v);
    expect(() => decode(v)).toThrow();
  }
});
test("fact compile cannot claim results and must match discovery plus exact request", () => {
  for (const mutate of [
    (v: any) => (v.rows = []),
    (v: any) => (v.row_count = "0"),
    (v: any) => (v.common_user_count = "3"),
    (v: any) => (v.executed = true),
    (v: any) => (v.members_computed = true),
    (v: any) => (v.counts_computed = true),
    (v: any) => (v.physical_plan = true),
    (v: any) => (v.sql_replayable = true),
    (v: any) => v.operators.reverse(),
  ]) {
    const v = structuredClone(base.plan);
    mutate(v);
    expect(() => decode(v, base, program(), true)).toThrow();
  }
  for (const request of [
    { ...base.request, domain: "all" },
    { ...base.request, limit: 1 },
    { ...base.request, cohort: undefined },
    {
      ...base.request,
      cohort: { operation: "difference", children: [{ operation: "all" }] },
    },
    { ...base.request, metrics: ["balance", "BALANCE"] },
  ]) {
    const c = { ...base, request },
      v = { ...structuredClone(base.plan), request };
    expect(() => decode(v, c, program(), true)).toThrow();
  }
});
test("fact discovery refuses inconsistent currencies, grains, declarations and budgets", () => {
  for (const mutate of [
    (v: any) => (v.sources[0].currency = "EUR"),
    (v: any) => (v.metrics[0].unit = "currency:EUR:scale2"),
    (v: any) => (v.metrics[3].unit = "count:Other:user_id"),
    (v: any) => (v.windows[0].dataset = "cost"),
    (v: any) => (v.windows[0].bound_catalog = "memory"),
    (v: any) => v.sources[0].identity_fields.reverse(),
    (v: any) => (v.sources[1].dataset = v.sources[0].dataset),
    (v: any) => (v.limits.result_rows = 10001),
    (v: any) => (v.mcp_registered = true),
    (v: any) => v.metrics.push(v.metrics[0]),
    (v: any) => (v.sources[0].fields[0].extra = true),
  ]) {
    const v = structuredClone(scene.program);
    mutate(v);
    expect(() => factProgram(carrier("program_json", v))).toThrow();
  }
});
test("fact empty window results and plans still validate display dates and schema", () => {
  const c = scene.cases.find(
    (c: any) => c.name.startsWith("windows/") && c.result.row_count === "0",
  );
  for (const display of [
    { mode: "date_range", start_date: "2026-02-29", end_date: "2026-03-01" },
    { mode: "date_range", start_date: "2026-02-02", end_date: "2026-02-01" },
    { mode: "all", start_date: "2026-01-01" },
    { mode: "date_range", start_date: "infinity", end_date: "infinity" },
  ]) {
    const request = { ...c.request, display };
    for (const plan of [false, true]) {
      const v = { ...structuredClone(plan ? c.plan : c.result), request };
      expect(() => decode(v, { ...c, request }, program(), plan)).toThrow();
    }
  }
  const w = scene.cases.find(
    (c: any) =>
      c.name.startsWith("windows/") &&
      c.request.display.mode === "date_range" &&
      c.result.row_count !== "0",
  );
  const v = structuredClone(w.result);
  v.rows[0][1] = "2026-01-31";
  expect(() => decode(v, w)).toThrow();
});
test("fact duplicate decoded JSON keys and complete carrier budgets are enforced", () => {
  for (const raw of [
    '{"a":1,"a":2}',
    '{"a":1,"\\u0061":2}',
    '{"x":{"a":1,"a":2}}',
  ])
    expect(() => factJSON(raw)).toThrow();
  const valid = JSON.stringify(JSON.stringify(base.result));
  expect(() =>
    factValue(
      `[{"result_json":"bad","result_json":${valid}}]`,
      base.request,
      program(),
      false,
      false,
    ),
  ).toThrow();
  for (const raw of [
    "[]",
    JSON.stringify([{ result_json: base.result }]),
    JSON.stringify([{ result_json: JSON.stringify(base.result), extra: 1 }]),
    carrier("result_json", " ".repeat(1048577)),
  ])
    expect(() =>
      factValue(raw, base.request, program(), false, false),
    ).toThrow();
  expect(() => factJSON(" ".repeat(1048577))).toThrow();
  expect(factJSON('{"a":[true,false,null,-1.25e2,"a\\\"b"]}')).toEqual({
    a: [true, false, null, -125, 'a"b'],
  });
});
test("fact precision and signed exact money retain all representable cells without JS Number", () => {
  const v = structuredClone(base.result);
  for (const money of ["-12.34", "0.00", "9".repeat(36) + ".99"]) {
    v.rows[0][1] = money;
    expect(decode(v).rows?.[0][1]).toBe(money);
  }
});
test("fact structural acceptance cannot prove money or membership truth", () => {
  const v = structuredClone(base.result);
  v.rows[0][1] = "291.00";
  const accepted = decode(v);
  expect(accepted.rows).not.toEqual(base.independent.rows);
  const c = scene.cases.find((c: any) => c.name === "union"),
    wrong = structuredClone(c.result);
  wrong.rows[0][0] = "Z";
  wrong.rows.sort((a: string[], b: string[]) =>
    Buffer.compare(Buffer.from(a[0]), Buffer.from(b[0])),
  );
  expect(decode(wrong, c).rows).not.toEqual(c.independent.rows);
});

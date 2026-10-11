/** Dedicated state carriers: validate full rectangular calendars and all adjacent comparisons. */
import assert from "node:assert/strict";
import { factJSON as strictJSON } from "./fact_result.ts";
import {
  dailyRequest,
  DAILY_TRACE,
  type DailyRequest,
} from "./daily_result.ts";
export type StateRequest = DailyRequest & {
  user_domain: "captured_users";
  transitions: "within_request_days";
};
export type State = "TRUE" | "FALSE" | "UNKNOWN";
export type StateRow = [string, string, State];
export type Comparison = [string, string, string, State, State, boolean];
export type StateEnvelope = Record<string, unknown> & {
  request: StateRequest;
  required_day_count: string;
  qualification_concept: string;
  declared_rule: string;
  user_concept: string;
};
export type StateResult = StateEnvelope & {
  states: StateRow[];
  comparisons: Comparison[];
  state_totals: Record<State, string> & { unit: "User/day" };
};
export function object(v: unknown): asserts v is Record<string, unknown> {
  assert(
    v !== null && typeof v === "object" && !Array.isArray(v),
    "object required",
  );
}
export function exact(v: Record<string, unknown>, names: string[]) {
  assert.deepEqual(
    Object.keys(v).sort(),
    [...names].sort(),
    "unexpected/missing fields",
  );
}
function text(v: unknown): asserts v is string {
  assert(typeof v === "string" && v.length > 0);
  assert(
    !/[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]/.test(
      v,
    ),
    "unpaired surrogate",
  );
}
export function stateRequest(v: unknown): StateRequest {
  object(v);
  exact(v, [
    "start_date",
    "end_date",
    "products",
    "user_domain",
    "transitions",
  ]);
  assert(
    v.user_domain === "captured_users" &&
      v.transitions === "within_request_days",
  );
  const dates = dailyRequest({
    start_date: v.start_date,
    end_date: v.end_date,
    products: v.products,
  });
  return {
    ...dates,
    user_domain: "captured_users",
    transitions: "within_request_days",
  };
}
export function stateDays(r: DailyRequest) {
  const days: string[] = [];
  for (
    let t = Date.parse(r.start_date), end = Date.parse(r.end_date);
    t <= end;
    t += 86400000
  )
    days.push(new Date(t).toISOString().slice(0, 10));
  return days;
}
export const STATE_METADATA = [
  "state_api_version",
  "profile",
  "user_concept",
  "qualification_concept",
  "declared_rule",
  "rule_scope",
  "user_domain",
  "transitions",
  "state_semantics",
  "carry_forward",
  ...DAILY_TRACE,
];
const SCOPE = [...STATE_METADATA, "request", "required_day_count"];
export const STATE_GATE =
  "captured_User_count <= 10000/(2*days-1) before allocation";
export const STATE_OPERATORS = [
  "frozen full-root User/date/product/rule BOOLEAN projection without qualification WHERE",
  "collect whole-capture raw User domain before scope",
  "explicit inclusive date and product filter",
  "preserve rule TRUE/FALSE/NULL; aggregate per User/natural day",
  "UNKNOWN for missing days; no carry",
  "all within-request adjacent comparisons including unchanged",
  "raw UTF-8 User and ascending natural-day order",
  "complete bounded JSON",
];
function metadata(v: Record<string, unknown>) {
  assert(
    v.state_api_version === "1.0.0" &&
      v.profile === "daily-observation-population-v1",
  );
  for (const k of [
    "user_concept",
    "qualification_concept",
    "declared_rule",
    "snapshot_id",
  ])
    text(v[k]);
  for (const k of DAILY_TRACE.slice(1))
    assert(typeof v[k] === "string" && /^[a-f0-9]{64}$/.test(v[k] as string));
  assert(
    v.rule_scope ===
      "current frozen rule evaluated on captured observations; no historical rule inference",
  );
  assert(
    v.user_domain === "captured_users" &&
      v.transitions === "within_request_days" &&
      v.carry_forward === false,
  );
  assert(
    v.state_semantics ===
      "any TRUE -> TRUE; nonempty all FALSE -> FALSE; otherwise UNKNOWN",
  );
}
function count(v: unknown, max = 10000) {
  assert(typeof v === "string" && /^(0|[1-9]\d*)$/.test(v));
  const n = Number(v);
  assert(Number.isSafeInteger(n) && n <= max);
  return n;
}
function carrier(content: string, field: string) {
  const rows: unknown = strictJSON(content, 8 * 1048576);
  assert(Array.isArray(rows) && rows.length === 1);
  object(rows[0]);
  exact(rows[0], [field]);
  assert(typeof rows[0][field] === "string");
  const raw = rows[0][field] as string;
  assert(Buffer.byteLength(raw) <= 1048576, "internal response budget");
  const v: unknown = strictJSON(raw);
  object(v);
  metadata(v);
  return v;
}
function scoped(content: string, field: string): StateEnvelope {
  const v = carrier(content, field);
  const r = stateRequest(v.request);
  assert(v.required_day_count === String(stateDays(r).length));
  return v as StateEnvelope;
}
export function stateResult(content: string): StateResult {
  const v = scoped(content, "result_json");
  exact(v, [
    ...SCOPE,
    "schema_version",
    "executed",
    "complete",
    "paging",
    "truncated",
    "user_count",
    "state_count",
    "comparison_count",
    "combined_object_count",
    "ordering",
    "state_columns",
    "states",
    "comparison_columns",
    "comparisons",
    "state_totals",
    "changed_comparison_count",
  ]);
  assert(
    v.schema_version === "ossie-daily-states-result-v1" &&
      v.executed === true &&
      v.complete === true &&
      v.paging === false &&
      v.truncated === false,
  );
  assert(v.ordering === "raw UTF-8 User ascending, natural day ascending");
  assert.deepEqual(v.state_columns, ["user_id", "date", "state"]);
  assert.deepEqual(v.comparison_columns, [
    "user_id",
    "from_date",
    "to_date",
    "from_state",
    "to_state",
    "changed",
  ]);
  assert(Array.isArray(v.states) && Array.isArray(v.comparisons));
  const days = stateDays(v.request),
    users = count(v.user_count),
    states = v.states as unknown[],
    pairs = v.comparisons as unknown[];
  assert(
    count(v.state_count) === states.length &&
      states.length === users * days.length,
  );
  assert(
    count(v.comparison_count) === pairs.length &&
      pairs.length === users * (days.length - 1),
  );
  assert(count(v.combined_object_count) === states.length + pairs.length);
  const totals = { TRUE: 0, FALSE: 0, UNKNOWN: 0 },
    expected: Comparison[] = [];
  let previous: string | undefined;
  for (let u = 0; u < users; u++) {
    let id = "";
    const labels: State[] = [];
    for (let d = 0; d < days.length; d++) {
      const row = states[u * days.length + d];
      assert(Array.isArray(row) && row.length === 3);
      text(row[0]);
      if (d === 0) {
        id = row[0];
        if (previous !== undefined)
          assert(
            Buffer.compare(Buffer.from(previous), Buffer.from(id)) < 0,
            "User order/uniqueness",
          );
        previous = id;
      }
      assert(
        row[0] === id && row[1] === days[d],
        "complete rectangular User/day calendar",
      );
      assert(["TRUE", "FALSE", "UNKNOWN"].includes(row[2]));
      const label = row[2] as State;
      totals[label]++;
      labels.push(label);
    }
    for (let d = 1; d < days.length; d++)
      expected.push([
        id,
        days[d - 1],
        days[d],
        labels[d - 1],
        labels[d],
        labels[d - 1] !== labels[d],
      ]);
  }
  assert.deepEqual(
    pairs,
    expected,
    "complete ordered adjacent pairs and changed labels",
  );
  object(v.state_totals);
  exact(v.state_totals, ["unit", "TRUE", "FALSE", "UNKNOWN"]);
  assert(v.state_totals.unit === "User/day");
  for (const k of ["TRUE", "FALSE", "UNKNOWN"] as const)
    assert(count(v.state_totals[k]) === totals[k]);
  assert(
    count(v.changed_comparison_count) === expected.filter((p) => p[5]).length,
  );
  return v as StateResult;
}
export function statePlan(content: string): StateEnvelope {
  const v = scoped(content, "plan_json");
  exact(v, [
    ...SCOPE,
    "schema_version",
    "executed",
    "states_computed",
    "comparisons_computed",
    "physical_plan",
    "sql_replayable",
    "input_projection_binding",
    "captured_user_count",
    "projection_sql",
    "input_types",
    "operators",
    "limits",
    "cardinality_gate",
    "limitations",
  ]);
  assert(v.schema_version === "ossie-daily-states-plan-v1");
  for (const flag of [
    "executed",
    "states_computed",
    "comparisons_computed",
    "physical_plan",
    "sql_replayable",
  ])
    assert(v[flag] === false);
  assert(v.input_projection_binding === true);
  text(v.projection_sql);
  assert.deepEqual(v.input_types, ["VARCHAR", "DATE", "VARCHAR", "BOOLEAN"]);
  assert.deepEqual(v.operators, STATE_OPERATORS);
  assert.deepEqual(v.limits, {
    combined_objects: 10000,
    complete_json_utf8_bytes: 1048576,
  });
  assert(v.cardinality_gate === STATE_GATE);
  assert(
    count(v.captured_user_count) * (2 * stateDays(v.request).length - 1) <=
      10000,
  );
  assert.deepEqual(v.limitations, [
    "Compile does not scan or compute states or comparisons",
    "Load User count is capture metadata, not a query count",
    "Projection SQL does not include C++ reduction and is not independently replayable",
    "Compile does not guarantee encoded execution result size",
  ]);
  return v;
}
export function stateProgram(content: string) {
  const v = carrier(content, "program_json");
  exact(v, [
    ...STATE_METADATA,
    "schema_version",
    "query_executed",
    "mcp_registered",
    "captured_user_count",
    "captured_observation_rows",
    "functions",
    "state_identity",
    "comparison_identity",
    "status_values",
    "includes_unchanged_comparisons",
    "limits",
    "cardinality_gate",
    "refused",
    "registered_user_domain",
  ]);
  assert(
    v.schema_version === "ossie-daily-states-program-v1" &&
      v.query_executed === false &&
      v.mcp_registered === false &&
      v.registered_user_domain === false &&
      v.includes_unchanged_comparisons === true,
  );
  count(v.captured_user_count);
  count(v.captured_observation_rows);
  assert.deepEqual(v.functions, [
    "ossie_daily_states_json",
    "ossie_daily_states_compile",
    "ossie_daily_states_program",
  ]);
  assert.deepEqual(v.state_identity, ["User", "DATE"]);
  assert.deepEqual(v.comparison_identity, ["User", "from_DATE", "to_DATE"]);
  assert.deepEqual(v.status_values, ["TRUE", "FALSE", "UNKNOWN"]);
  assert.deepEqual(v.limits, {
    days: 366,
    combined_objects: 10000,
    request_utf8_bytes: 16384,
    complete_json_utf8_bytes: 1048576,
    subset_products: 64,
    product_utf8_bytes: 256,
  });
  assert(v.cardinality_gate === STATE_GATE);
  assert.deepEqual(v.refused, [
    "other user domains",
    "request-external predecessor",
    "carry-forward",
    "all-products qualification",
    "historical validity inference",
    "paging or truncation",
  ]);
  return v;
}
export function sameStateBinding(
  a: Record<string, unknown>,
  b: Record<string, unknown>,
) {
  for (const k of STATE_METADATA) assert.deepEqual(a[k], b[k], k);
  if (a.request && b.request) {
    assert.deepEqual(a.request, b.request);
    assert.equal(a.required_day_count, b.required_day_count);
  }
}

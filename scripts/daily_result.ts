/** Separate daily schemas: preserve raw identities and validate scope/provenance/counts. */
import assert from "node:assert/strict";
export const DAILY_TRACE = [
  "snapshot_id",
  "content_sha256",
  "core_sha256",
  "ontology_sha256",
  "mapping_sha256",
  "manifest_sha256",
] as const;
export interface DailyRequest {
  start_date: string;
  end_date: string;
  products: { mode: "all" } | { mode: "subset"; values: string[] };
}
export type DailyEnvelope = Record<string, unknown> & {
  request: DailyRequest;
  required_day_count: string;
  qualification_rule: { concept: string; declared_sql: string };
  concept: string;
  user_concept: string;
};
export type DailyResult = DailyEnvelope & {
  members: string[];
  member_count: string;
};
function object(value: unknown): asserts value is Record<string, unknown> {
  assert(
    value !== null && typeof value === "object" && !Array.isArray(value),
    "object required",
  );
}
function keys(value: Record<string, unknown>, expected: string[]) {
  assert.deepEqual(
    Object.keys(value).sort(),
    [...expected].sort(),
    "unexpected/missing fields",
  );
}
function text(value: unknown): asserts value is string {
  assert(
    typeof value === "string" && value.length > 0,
    "nonempty raw string required",
  );
  assert(
    !/[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]/.test(
      value,
    ),
    "unpaired UTF-16 surrogate cannot represent raw UTF-8 identity",
  );
}
function sorted(values: unknown): asserts values is string[] {
  assert(Array.isArray(values));
  for (let i = 0; i < values.length; i++) {
    text(values[i]);
    if (i)
      assert(
        Buffer.compare(Buffer.from(values[i - 1]), Buffer.from(values[i])) < 0,
        "raw identities must be unique and UTF-8 byte ascending",
      );
  }
}
function day(value: unknown) {
  assert(
    typeof value === "string" && /^(?!0000)\d{4}-\d{2}-\d{2}$/.test(value),
  );
  const epoch = Date.parse(value);
  assert(
    Number.isFinite(epoch) &&
      new Date(epoch).toISOString().slice(0, 10) === value,
    "invalid Gregorian DATE",
  );
  return epoch / 86400000;
}
export function dailyRequest(value: unknown): DailyRequest {
  object(value);
  keys(value, ["start_date", "end_date", "products"]);
  const days = day(value.end_date) - day(value.start_date) + 1;
  assert(days >= 1 && days <= 366, "invalid calendar range");
  object(value.products);
  if (value.products.mode === "all") keys(value.products, ["mode"]);
  else {
    assert(value.products.mode === "subset");
    keys(value.products, ["mode", "values"]);
    sorted(value.products.values);
    assert(
      value.products.values.length > 0 && value.products.values.length <= 64,
    );
    assert(value.products.values.every((v) => Buffer.byteLength(v) <= 256));
  }
  return value as unknown as DailyRequest;
}
const COMMON = [
  "schema_version",
  "request_kind",
  "executed",
  "concept",
  "user_concept",
  "request",
  "required_day_count",
  "eligibility_semantics",
  "qualification_rule",
  "profile",
  ...DAILY_TRACE,
];
function envelope(
  content: string,
  field: "result_json" | "plan_json",
): DailyEnvelope {
  const carrier: unknown = JSON.parse(content);
  assert(Array.isArray(carrier) && carrier.length === 1);
  object(carrier[0]);
  keys(carrier[0], [field]);
  assert(typeof carrier[0][field] === "string");
  const raw = carrier[0][field] as string;
  assert(Buffer.byteLength(raw) <= 1048576, "internal envelope exceeds budget");
  const value: unknown = JSON.parse(raw);
  object(value);
  assert(
    value.request_kind === "daily_users" &&
      value.profile === "daily-observation-population-v1",
  );
  text(value.concept);
  text(value.user_concept);
  text(value.snapshot_id);
  for (const key of DAILY_TRACE.slice(1))
    assert(
      typeof value[key] === "string" &&
        /^[a-f0-9]{64}$/.test(value[key] as string),
      "invalid hash",
    );
  const request = dailyRequest(value.request);
  assert(
    value.required_day_count ===
      String(day(request.end_date) - day(request.start_date) + 1),
  );
  assert(
    value.eligibility_semantics ===
      "every_calendar_day_exists_qualifying_observation",
  );
  object(value.qualification_rule);
  keys(value.qualification_rule, ["concept", "declared_sql"]);
  assert(value.qualification_rule.concept === value.concept);
  text(value.qualification_rule.declared_sql);
  return value as DailyEnvelope;
}
export function dailyResult(content: string): DailyResult {
  const value = envelope(content, "result_json");
  keys(value, [
    ...COMMON,
    "member_count",
    "paging",
    "truncated",
    "complete",
    "ordering",
    "members",
  ]);
  assert(
    value.schema_version === "ossie-daily-user-set-result-v1" &&
      value.executed === true,
  );
  assert(
    value.paging === false &&
      value.truncated === false &&
      value.complete === true &&
      value.ordering === "raw UTF-8 byte ascending",
  );
  sorted(value.members);
  assert(value.members.length <= 10000);
  assert(
    value.member_count === String(value.members.length),
    "exact count/member mismatch",
  );
  return value as DailyResult;
}
export function dailyPlan(content: string): DailyEnvelope {
  const value = envelope(content, "plan_json");
  keys(value, [
    ...COMMON,
    "physical_plan",
    "final_output_binding",
    "input_projection_binding",
    "sql_replayable",
    "members_computed",
    "phase",
    "qualified_projection_sql",
    "bound_identity",
    "operators",
    "limitations",
    "plan_json_utf8_byte_limit",
  ]);
  assert(value.schema_version === "ossie-daily-user-set-plan-v1");
  for (const flag of [
    "executed",
    "physical_plan",
    "final_output_binding",
    "sql_replayable",
    "members_computed",
  ])
    assert(value[flag] === false, flag);
  assert(
    value.input_projection_binding === true &&
      value.plan_json_utf8_byte_limit === 1048576,
  );
  text(value.phase);
  text(value.qualified_projection_sql);
  assert(
    Array.isArray(value.bound_identity) && value.bound_identity.length === 3,
  );
  value.bound_identity.forEach((entry, i) => {
    object(entry);
    keys(entry, ["field", "role", "bound_type"]);
    text(entry.field);
    assert(
      entry.role === ["user", "date", "product"][i] &&
        entry.bound_type === ["VARCHAR", "DATE", "VARCHAR"][i],
    );
  });
  assert.deepEqual(value.operators, [
    {
      operator: "qualified_observation_projection",
      executor: "DuckDB AST",
      qualification: "TRUE only",
    },
    { operator: "inclusive_date_filter", executor: "C++", identity_ordinal: 2 },
    {
      operator: "explicit_product_scope",
      executor: "C++",
      identity_ordinal: 3,
    },
    {
      operator: "distinct_user_date",
      executor: "C++",
      identity_ordinals: [1, 2],
    },
    {
      operator: "date_count_equals_required_calendar_days",
      executor: "C++",
      required_count: value.required_day_count,
    },
    {
      operator: "complete_members_order",
      comparison: "raw UTF-8 byte ascending",
    },
    {
      operator: "bounded_complete_JSON",
      byte_limit: 1048576,
      truncated: false,
    },
  ]);
  assert(Array.isArray(value.limitations) && value.limitations.length > 0);
  value.limitations.forEach(text);
  return value;
}
export function sameDailyBinding(plan: DailyEnvelope, result: DailyEnvelope) {
  for (const key of [
    "concept",
    "user_concept",
    "request",
    "required_day_count",
    "eligibility_semantics",
    "qualification_rule",
    "profile",
    ...DAILY_TRACE,
  ])
    assert.deepEqual(
      plan[key],
      result[key],
      "daily request/binding mismatch: " + key,
    );
}

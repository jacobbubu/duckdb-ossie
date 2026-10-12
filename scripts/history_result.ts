/** Strict full historical carriers. Structure/scope correspondence is not proof of membership. */
import assert from "node:assert/strict";
import { factJSON } from "./fact_result.ts";
import { relationDate, compareRelationLinks } from "./relation_result.ts";
import { BINDING_KEYS } from "./observation_evidence.ts";
import { object, exact } from "./daily_state_result.ts";
export type HistoryRequest = {
  dates:
    | { mode: "all" }
    | { mode: "range"; start_date: string; end_date: string };
  products: { mode: "all" } | { mode: "subset"; values: string[] };
};
export type HistoryValue = Record<string, unknown> & {
  request: HistoryRequest;
  relationship: string;
  links?: string[][];
};
function text(v: unknown): asserts v is string {
  assert(typeof v === "string" && v.length > 0);
  assert(
    !/[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]/.test(
      v,
    ),
    "unpaired surrogate",
  );
}
function count(v: unknown, max = 10000) {
  assert(typeof v === "string" && /^(0|[1-9]\d*)$/.test(v));
  const n = Number(v);
  assert(Number.isSafeInteger(n) && n <= max);
  return n;
}
function strings(v: unknown, n: number) {
  assert(Array.isArray(v) && v.length === n);
  for (const x of v) text(x);
  assert(new Set(v.map((x: string) => x.toLowerCase())).size === n);
  return v as string[];
}
export function historyRequest(v: unknown): HistoryRequest {
  object(v);
  exact(v, ["dates", "products"]);
  object(v.dates);
  object(v.products);
  if (v.dates.mode === "all") exact(v.dates, ["mode"]);
  else {
    exact(v.dates, ["mode", "start_date", "end_date"]);
    assert(v.dates.mode === "range");
    assert(relationDate(v.dates.start_date) <= relationDate(v.dates.end_date));
  }
  if (v.products.mode === "all") exact(v.products, ["mode"]);
  else {
    exact(v.products, ["mode", "values"]);
    assert(v.products.mode === "subset" && Array.isArray(v.products.values));
    const values = v.products.values;
    assert(
      values.length > 0 &&
        values.length <= 64 &&
        new Set(values).size === values.length,
    );
    for (const p of values) {
      text(p);
      assert(Buffer.byteLength(p) <= 256);
    }
    assert.deepEqual(
      values,
      [...values].sort((a, b) =>
        Buffer.compare(Buffer.from(a), Buffer.from(b)),
      ),
      "normalized product order",
    );
  }
  assert(Buffer.byteLength(JSON.stringify(v)) <= 16384);
  return v as HistoryRequest;
}
export const HISTORY_COMMON = [
  "api_version",
  "profile",
  "operator",
  "root_concept",
  "relationship",
  "user_concept",
  "target_concept",
  "target_identity_relationship",
  ...BINDING_KEYS,
];
const META = [
  ...HISTORY_COMMON,
  "target_identity_bound_type",
  "observation_rows",
  "history_record_count",
  "index_segment_count",
  "root_identity_relationships",
  "root_identity_fields",
  "link_role_concepts",
  "validity",
  "scope_semantics",
  "source_identity",
];
export const HISTORY_OPERATORS = [
  "frozen full observation projection",
  "explicit observation date/product scope",
  "immutable per-User interval binary lookup",
  "start inclusive and end exclusive; gap no link",
  "complete Observation to typed target set",
  "raw User/product/target bytes and DATE value ordering",
  "complete bounded JSON",
];
export const HISTORY_LIMITATIONS = [
  "Compile binds projection but never scans or matches links",
  "Input and index counts are load metadata",
  "Rendered projection excludes C++ scope/index matching and cannot replay independently",
  "Compile does not guarantee execution completion or encoded result size",
];
export const HISTORY_QUERY_LIMITS = {
  links: 10000,
  request_utf8_bytes: 16384,
  subset_products: 64,
  product_utf8_bytes: 256,
  complete_json_utf8_bytes: 1048576,
};
function common(v: Record<string, unknown>) {
  assert(
    v.api_version === "1.0.0" &&
      v.profile === "historical-observation-relation-v1" &&
      v.operator === "history_at_observation" &&
      v.target_identity_relationship === "id",
  );
  for (const k of [
    "root_concept",
    "relationship",
    "user_concept",
    "target_concept",
    "snapshot_id",
  ])
    text(v[k]);
  for (const k of BINDING_KEYS.slice(1))
    assert(typeof v[k] === "string" && /^[a-f0-9]{64}$/.test(v[k] as string));
  assert(
    new Set(
      [v.root_concept, v.user_concept, v.target_concept].map((x) =>
        String(x).toLowerCase(),
      ),
    ).size === 3,
  );
}
function carrier(content: string, field: string) {
  const rows = factJSON(content, 8 * 1048576);
  assert(Array.isArray(rows) && rows.length === 1);
  object(rows[0]);
  exact(rows[0], [field]);
  assert(typeof rows[0][field] === "string");
  const v = factJSON(rows[0][field]);
  object(v);
  common(v);
  return v;
}
function metadata(v: Record<string, unknown>) {
  assert(v.target_identity_bound_type === "VARCHAR");
  const obs = count(v.observation_rows),
    history = count(v.history_record_count),
    segments = count(v.index_segment_count);
  assert(
    segments <= history && (history === 0 ? segments === 0 : segments > 0),
  );
  assert.deepEqual(v.root_identity_relationships, ["user", "date", "product"]);
  strings(v.root_identity_fields, 3);
  assert.deepEqual(v.link_role_concepts, [
    v.user_concept,
    "Date",
    "String",
    v.target_concept,
  ]);
  assert(
    v.validity ===
      "finite DATE start<=observation_date<end; gaps produce no link",
  );
  assert(
    v.scope_semantics ===
      "request filters captured observations only; full history validated at load; all observations without qualification rule",
  );
  assert(
    v.source_identity ===
      "record_id identifies source witnesses only, not output links",
  );
  return obs;
}
export function historyResult(
  content: string,
): HistoryValue & { links: string[][] } {
  const v = carrier(content, "result_json"),
    obs = metadata(v),
    r = historyRequest(v.request);
  exact(v, [
    ...META,
    "schema_version",
    "executed",
    "request",
    "link_count",
    "complete",
    "paging",
    "truncated",
    "link_columns",
    "link_types",
    "link_encodings",
    "ordering",
    "links",
  ]);
  assert(
    v.schema_version === "ossie-history-links-result-v1" &&
      v.executed === true &&
      v.complete === true &&
      v.paging === false &&
      v.truncated === false,
  );
  assert.deepEqual(v.link_columns, ["user_id", "date", "product", "target_id"]);
  assert.deepEqual(v.link_types, ["VARCHAR", "DATE", "VARCHAR", "VARCHAR"]);
  assert.deepEqual(v.link_encodings, [
    "raw-UTF8",
    "finite-date-text",
    "raw-UTF8",
    "raw-UTF8",
  ]);
  assert(
    v.ordering ===
      "raw User bytes, DATE value, raw product bytes, raw target bytes ascending",
  );
  assert(
    Array.isArray(v.links) &&
      count(v.link_count) === v.links.length &&
      v.links.length <= obs,
  );
  let previous: string[] | undefined;
  for (const row of v.links) {
    assert(Array.isArray(row) && row.length === 4);
    row.forEach(text);
    const day = relationDate(row[1]);
    if (r.dates.mode === "range")
      assert(
        day >= relationDate(r.dates.start_date) &&
          day <= relationDate(r.dates.end_date),
      );
    if (r.products.mode === "subset")
      assert(r.products.values.includes(row[2]));
    if (previous) {
      assert(
        compareRelationLinks(previous, row) < 0,
        "complete unique sorted links",
      );
      assert(
        !(
          previous[0] === row[0] &&
          previous[1] === row[1] &&
          previous[2] === row[2]
        ),
        "functional Observation to target",
      );
    }
    previous = row;
  }
  return v as HistoryValue & { links: string[][] };
}
export function historyPlan(content: string): HistoryValue {
  const v = carrier(content, "plan_json");
  metadata(v);
  historyRequest(v.request);
  exact(v, [
    ...META,
    "schema_version",
    "executed",
    "request",
    "links_computed",
    "physical_plan",
    "sql_replayable",
    "input_projection_binding",
    "input_types",
    "projection_sql",
    "operators",
    "limits",
    "limitations",
  ]);
  assert(v.schema_version === "ossie-history-links-plan-v1");
  for (const flag of [
    "executed",
    "links_computed",
    "physical_plan",
    "sql_replayable",
  ])
    assert(v[flag] === false);
  assert(v.input_projection_binding === true);
  text(v.projection_sql);
  assert.deepEqual(v.input_types, ["VARCHAR", "DATE", "VARCHAR"]);
  assert.deepEqual(v.operators, HISTORY_OPERATORS);
  assert.deepEqual(v.limitations, HISTORY_LIMITATIONS);
  assert.deepEqual(v.limits, {
    links: 10000,
    request_utf8_bytes: 16384,
    complete_json_utf8_bytes: 1048576,
  });
  return v as HistoryValue;
}
export function historyProgram(content: string) {
  const v = carrier(content, "program_json");
  exact(v, [
    ...HISTORY_COMMON,
    "schema_version",
    "query_executed",
    "query_supported",
    "compile_supported",
    "mcp_registered",
    "total_source_rows",
    "history_record_count",
    "index_segment_count",
    "history_User_count",
    "functions",
    "query_limits",
    "request_dates",
    "request_products",
    "scope_semantics",
    "sources",
    "limits",
    "validity",
    "conflict",
    "source_identity",
    "same_target_overlap",
    "link_identity",
    "load_metadata_not_query_counts",
  ]);
  assert(
    v.schema_version === "ossie-history-program-v1" &&
      v.query_executed === false &&
      v.query_supported === true &&
      v.compile_supported === true &&
      v.mcp_registered === false &&
      v.load_metadata_not_query_counts === true,
  );
  assert.deepEqual(v.functions, [
    "ossie_history_program",
    "ossie_history_links_json",
    "ossie_history_links_compile",
  ]);
  assert.deepEqual(v.query_limits, HISTORY_QUERY_LIMITS);
  assert(
    v.request_dates ===
      "explicit all or inclusive finite canonical DuckDB DATE range; no calendar expansion" &&
      v.request_products === "explicit all or nonempty raw subset",
  );
  assert(
    v.scope_semantics ===
      "filters observations only, all without qualification; full history checked at load",
  );
  assert.deepEqual(v.limits, {
    rows_per_source: 10000,
    combined_source_rows: 20000,
    physical_columns_per_source: 256,
    combined_canonical_bytes: 16777216,
    index_segments: 10000,
    complete_json_utf8_bytes: 1048576,
  });
  assert(
    v.validity === "finite DuckDB DATE [start,end), start<end" &&
      v.conflict ===
        "whole-history same User different-target overlap rejects entire load" &&
      v.source_identity === "global raw record_id, duplicates rejected" &&
      v.same_target_overlap ===
        "distinct sources retained; derived coverage merged without gaps",
  );
  assert.deepEqual(v.link_identity, ["User", "DATE", "product", "target_id"]);
  const records = count(v.history_record_count),
    segments = count(v.index_segment_count),
    users = count(v.history_User_count);
  assert(
    segments <= records &&
      users <= segments &&
      (records === 0
        ? segments === 0 && users === 0
        : segments > 0 && users > 0),
  );
  assert(Array.isArray(v.sources) && v.sources.length === 2);
  const sources = v.sources;
  let total = 0;
  for (let i = 0; i < 2; i++) {
    const source = sources[i];
    object(source);
    exact(source, [
      "dataset",
      "concept",
      "source",
      "content_sha256",
      "rows",
      "roles",
    ]);
    for (const k of ["dataset", "concept", "source"]) text(source[k]);
    assert(
      typeof source.content_sha256 === "string" &&
        /^[a-f0-9]{64}$/.test(source.content_sha256),
    );
    const rows = count(source.rows);
    total += rows;
    if (i === 1) assert(rows === records);
    else assert(source.concept === v.root_concept);
    const roles =
        i === 0
          ? ["user", "date", "product"]
          : ["record_id", "user", "team", "starts", "ends"],
      types =
        i === 0
          ? ["VARCHAR", "DATE", "VARCHAR"]
          : ["VARCHAR", "VARCHAR", "VARCHAR", "DATE", "DATE"];
    assert(Array.isArray(source.roles) && source.roles.length === roles.length);
    for (let j = 0; j < roles.length; j++) {
      const role: unknown = source.roles[j];
      object(role);
      exact(role, ["role", "field", "physical_column", "bound_type"]);
      assert(role.role === roles[j] && role.bound_type === types[j]);
      text(role.field);
      text(role.physical_column);
    }
    strings(
      source.roles.map((x) => x.field),
      roles.length,
    );
    strings(
      source.roles.map((x) => x.physical_column),
      roles.length,
    );
  }
  assert(count(v.total_source_rows, 20000) === total);
  assert(sources[0].dataset.toLowerCase() !== sources[1].dataset.toLowerCase());
  assert(
    new Set(
      [
        v.root_concept,
        v.user_concept,
        v.target_concept,
        sources[1].concept,
      ].map((x) => String(x).toLowerCase()),
    ).size === 4,
  );
  return v;
}
export function sameHistoryBinding(
  a: Record<string, unknown>,
  b: Record<string, unknown>,
) {
  for (const k of [
    ...HISTORY_COMMON,
    "history_record_count",
    "index_segment_count",
  ])
    assert.deepEqual(a[k], b[k], k);
  if (a.request && b.request) assert.deepEqual(a.request, b.request);
  if (a.observation_rows && b.observation_rows)
    assert.equal(a.observation_rows, b.observation_rows);
  for (const x of [a, b])
    if (x.sources && (a.observation_rows || b.observation_rows))
      assert.equal(
        (x.sources as Record<string, unknown>[])[0].rows,
        a.observation_rows ?? b.observation_rows,
      );
}

/** Complete bounded carrier checks plus request/discovery correspondence; not a membership oracle. */
import assert from "node:assert/strict";
import { BINDING_KEYS, binding } from "./observation_evidence.ts";
import { relationDate } from "./relation_result.ts";
type ObjectValue = Record<string, unknown>;
export type FactDeclaration = ObjectValue & {
  name: string;
  bound_type: string;
  unit: string;
  dataset?: string;
};
export type FactSource = ObjectValue & {
  dataset: string;
  concept: string;
  captured_rows: string;
  identity_fields: string[];
};
export type FactProgram = ObjectValue & {
  metrics: FactDeclaration[];
  windows: FactDeclaration[];
  sources: FactSource[];
};
export type FactValue = ObjectValue & {
  rows?: (string | null)[][];
  columns: ObjectValue[];
  selected: string[];
};
function object(v: unknown): asserts v is ObjectValue {
  assert(v !== null && typeof v === "object" && !Array.isArray(v));
}
function keys(v: ObjectValue, wanted: readonly string[]) {
  assert.deepEqual(
    Object.keys(v).sort(),
    [...wanted].sort(),
    "unexpected/missing fields",
  );
}
function text(v: unknown): asserts v is string {
  assert(typeof v === "string" && v.length > 0);
  assert(
    !/[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]/.test(
      v,
    ),
    "unpaired UTF16 surrogate",
  );
}
/** JSON.parse supplies grammar checking; token walk additionally rejects decoded duplicate keys. */
export function factJSON(raw: string, byteLimit = 1048576): unknown {
  assert(Buffer.byteLength(raw) <= byteLimit);
  const value: unknown = JSON.parse(raw);
  const tokens =
    raw.match(
      /"(?:\\.|[^"\\])*"|[{}\[\],:]|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?|true|false|null/g,
    ) ?? [];
  let i = 0;
  function walk(depth: number): void {
    assert(depth <= 64, "JSON nesting budget");
    const token = tokens[i++];
    if (token === "{") {
      const seen = new Set<string>();
      if (tokens[i] !== "}")
        do {
          const key: unknown = JSON.parse(tokens[i++]);
          assert(
            typeof key === "string" && !seen.has(key),
            "duplicate JSON key",
          );
          seen.add(key);
          assert(tokens[i++] === ":");
          walk(depth + 1);
          if (tokens[i] !== ",") break;
          i++;
        } while (true);
      assert(tokens[i++] === "}");
    } else if (token === "[") {
      if (tokens[i] !== "]")
        do {
          walk(depth + 1);
          if (tokens[i] !== ",") break;
          i++;
        } while (true);
      assert(tokens[i++] === "]");
    } else assert(token !== undefined);
  }
  walk(0);
  assert(i === tokens.length);
  return value;
}
function carrier(content: string, field: string): ObjectValue {
  // The upstream array/string wrapper is larger than the bounded inner JSON.
  assert(
    Buffer.byteLength(content) <= 8 * 1048576,
    "local carrier guard, not native transport limit",
  );
  const outer: unknown = factJSON(content, 8 * 1048576);
  assert(Array.isArray(outer) && outer.length === 1);
  object(outer[0]);
  keys(outer[0], [field]);
  const raw = outer[0][field];
  assert(typeof raw === "string");
  const v = factJSON(raw);
  object(v);
  return v;
}
function provenance(v: ObjectValue) {
  assert(v.profile === "multi-fact-user-program-v1");
  binding(v);
  for (const k of BINDING_KEYS.slice(1))
    assert(typeof v[k] === "string" && /^[a-f0-9]{64}$/.test(v[k] as string));
  assert(
    new RegExp(`^${v.content_sha256}:[1-9]\\d*$`).test(String(v.snapshot_id)),
  );
  text(v.user_concept);
  text(v.user_identity_relationship);
}
const IDENTITY = [
  "profile",
  ...BINDING_KEYS,
  "user_concept",
  "user_identity_relationship",
];
function integer(v: unknown, maximum = 10000n) {
  assert(typeof v === "string" && /^(0|[1-9]\d*)$/.test(v));
  assert(BigInt(v) <= maximum);
  return BigInt(v);
}
function decimal(v: unknown, precision: number) {
  assert(typeof v === "string" && /^-?(0|[1-9]\d*)\.\d{2}$/.test(v));
  assert(v !== "-0.00", "noncanonical negative zero");
  const cents = BigInt(v.replace(".", ""));
  assert(
    cents > -(10n ** BigInt(precision)) && cents < 10n ** BigInt(precision),
  );
}
export const FACT_LIMITS = {
  sources: 2,
  rows_per_source: 10000,
  physical_columns_per_source: 256,
  combined_canonical_bytes: 16777216,
  regular_metrics: 32,
  window_metrics: 16,
  name_ascii_bytes: 128,
  rule_bytes: 16384,
  total_rule_bytes: 131072,
  expression_depth: 32,
  expression_nodes: 1024,
  expanded_program_nodes: 32768,
  metric_dependency_depth: 16,
  cohort_nodes: 64,
  cohort_depth: 16,
  request_json_bytes: 16384,
  complete_json_bytes: 1048576,
  result_rows: 10000,
};
export function factProgram(content: string): FactProgram {
  const v = carrier(content, "program_json");
  keys(v, [
    "schema_version",
    ...IDENTITY,
    "project_api_version",
    "query_executed",
    "counts_computed",
    "mcp_registered",
    "functions",
    "sources",
    "metrics",
    "windows",
    "domain",
    "cohort_semantics",
    "count_semantics",
    "input_scope",
    "window_semantics",
    "supported_aggregates",
    "refused",
    "limits",
    "paging",
    "truncated",
    "peak_memory_bound",
    "hash_set_normalized",
  ]);
  provenance(v);
  assert(
    v.schema_version === "ossie-fact-program-v1" &&
      v.project_api_version === "2.2.0",
  );
  for (const k of [
    "query_executed",
    "counts_computed",
    "mcp_registered",
    "paging",
    "truncated",
    "peak_memory_bound",
    "hash_set_normalized",
  ])
    assert.equal(v[k], false);
  assert.deepEqual(v.functions, [
    "ossie_fact_users_json",
    "ossie_fact_users_compile",
    "ossie_fact_windows_json",
    "ossie_fact_windows_compile",
    "ossie_fact_program",
  ]);
  assert.deepEqual(v.limits, FACT_LIMITS);
  assert(v.input_scope === "entire_capture");
  assert.equal(
    v.domain,
    "common User by row presence in both separately reduced sources",
  );
  assert.equal(
    v.cohort_semantics,
    "TRUE atomic member sets; binary intersection, union and ordered difference within common",
  );
  assert.equal(
    v.count_semantics,
    "COUNT(User) source rows; COUNT(amount) nonNULL values; common_user_count before cohort, selected_user_count after cohort",
  );
  assert.equal(
    v.window_semantics,
    "single User/date fact; explicit User partition and DATE ASC NULLS LAST; whole capture computation before display DATE filtering; LAG1 includes NULL, ROWS0..366 observes rows not calendar days",
  );
  assert.deepEqual(v.supported_aggregates, [
    "SUM(mapped amount)",
    "COUNT(mapped User or amount)",
    "same-currency SUM components +/-",
  ]);
  assert.deepEqual(v.refused, [
    "raw fact-to-fact fan-out",
    "AVG/COUNT(*) in declarations",
    "cross-fact aggregate leaves",
    "ratios or mixed measure units",
    "input/date/product filtering",
    "recursive cohorts or dependencies",
    "mixed window fact grains",
    "calendar filling or history/state inference",
    "ordinary rendered SQL replay",
  ]);
  assert(Array.isArray(v.sources) && v.sources.length === 2);
  const datasets = new Set<string>();
  for (const s of v.sources) {
    object(s);
    keys(s, [
      "dataset",
      "concept",
      "user_concept",
      "target_identity_relationship",
      "source",
      "currency",
      "content_sha256",
      "captured_rows",
      "identity_fields",
      "user_field",
      "date_field",
      "product_field",
      "amount_field",
      "fields",
    ]);
    for (const k of [
      "dataset",
      "concept",
      "source",
      "currency",
      "user_field",
      "date_field",
      "amount_field",
    ])
      text(s[k]);
    assert(!datasets.has(String(s.dataset).toLowerCase()));
    datasets.add(String(s.dataset).toLowerCase());
    assert.equal(s.user_concept, v.user_concept);
    assert.equal(s.target_identity_relationship, v.user_identity_relationship);
    assert(
      /^[A-Z]{3}$/.test(String(s.currency)) &&
        /^[a-f0-9]{64}$/.test(String(s.content_sha256)),
    );
    integer(s.captured_rows);
    assert(typeof s.product_field === "string");
    assert.deepEqual(s.identity_fields, [
      s.user_field,
      s.date_field,
      ...(s.product_field ? [s.product_field] : []),
    ]);
    assert(
      Array.isArray(s.fields) && s.fields.length === (s.product_field ? 4 : 3),
    );
    const expected = new Map<unknown, string>([
      [s.user_field, "VARCHAR"],
      [s.date_field, "DATE"],
      [s.amount_field, "DECIMAL(18,2)"],
    ]);
    if (s.product_field) expected.set(s.product_field, "VARCHAR");
    assert.equal(expected.size, s.fields.length);
    for (const f of s.fields) {
      object(f);
      keys(f, ["name", "physical_column", "bound_type"]);
      text(f.physical_column);
      assert(expected.has(f.name));
      assert.equal(f.bound_type, expected.get(f.name));
      expected.delete(f.name);
    }
  }
  const names = new Set<string>();
  const sources = v.sources as FactSource[];
  assert.equal(sources[0].currency, sources[1].currency);
  assert.equal(
    new Set(sources.map((s) => String(s.concept).toLowerCase())).size,
    2,
  );
  assert.equal(
    sources.filter((s) => s.product_field === "").length,
    1,
    "two distinct fact grains",
  );
  const currencyUnit = `currency:${sources[0].currency}:scale2`;
  let ruleBytes = 0;
  for (const kind of ["metrics", "windows"] as const) {
    const declarations = v[kind];
    assert(
      Array.isArray(declarations) &&
        declarations.length <= (kind === "metrics" ? 32 : 16),
    );
    for (const d of declarations) {
      object(d);
      keys(d, [
        "name",
        "original_definition",
        "unit",
        "bound_type",
        ...(kind === "windows"
          ? ["dataset", "bound_expression", "bound_catalog", "bound_schema"]
          : []),
      ]);
      text(d.name);
      assert(/^[a-zA-Z_][a-zA-Z0-9_]{0,127}$/.test(d.name));
      assert(!names.has(d.name.toLowerCase()));
      names.add(d.name.toLowerCase());
      text(d.original_definition);
      assert(Buffer.byteLength(d.original_definition) <= 16384);
      ruleBytes += Buffer.byteLength(d.original_definition);
      assert(ruleBytes <= 131072);
      text(d.unit);
      if (d.bound_type === "BIGINT") {
        assert(
          kind === "metrics" &&
            sources.some((s) =>
              [s.user_field, s.amount_field].some(
                (f) => d.unit === `count:${s.concept}:${f}`,
              ),
            ),
        );
      } else {
        assert(
          d.bound_type === "DECIMAL(38,2)" ||
            (kind === "windows" && d.bound_type === "DECIMAL(18,2)"),
        );
        assert.equal(d.unit, currencyUnit);
      }
      if (kind === "windows") {
        assert(
          sources.some(
            (s) => s.dataset === d.dataset && s.product_field === "",
          ),
        );
        text(d.bound_expression);
        assert.equal(d.bound_catalog, "system");
        assert.equal(d.bound_schema, "main");
      }
    }
  }
  return v as FactProgram;
}
const COMMON = [
  "schema_version",
  ...IDENTITY,
  "request",
  "selected",
  "input_scope",
  "columns",
  "paging",
  "truncated",
  "json_byte_limit",
  "ordering",
  "executed",
  "members_computed",
  "counts_computed",
];
const USER = ["domain", "domain_semantics", "cohort_semantics"];
const WINDOW = [
  "dataset",
  "source_concept",
  "source_captured_rows",
  "source_identity_fields",
  "output_identity_fields",
  "display_stage",
];
export function factCorrespondence(
  request: ObjectValue,
  program: FactProgram,
  v: FactValue,
  window: boolean,
) {
  keys(
    request,
    window
      ? ["input_scope", "windows", "display"]
      : ["domain", "input_scope", "metrics", "cohort"],
  );
  assert(request.input_scope === "entire_capture");
  assert(Buffer.byteLength(JSON.stringify(request)) <= 16384);
  if (window) {
    object(request.display);
    if (request.display.mode === "all") keys(request.display, ["mode"]);
    else {
      keys(request.display, ["mode", "start_date", "end_date"]);
      assert(request.display.mode === "date_range");
      assert(
        relationDate(request.display.start_date) <=
          relationDate(request.display.end_date),
      );
    }
  } else {
    assert(request.domain === "common");
    let nodes = 0;
    function cohort(v: unknown, depth: number): void {
      object(v);
      assert(++nodes <= 64 && depth <= 16);
      if (v.operation === "all") keys(v, ["operation"]);
      else if (v.operation === "predicate") {
        keys(v, ["operation", "predicate"]);
        text(v.predicate);
        assert(
          !v.predicate.includes("\0") &&
            Buffer.byteLength(v.predicate) <= 16384,
        );
      } else {
        keys(v, ["operation", "children"]);
        assert(
          ["intersection", "union", "difference"].includes(String(v.operation)),
        );
        assert(Array.isArray(v.children) && v.children.length === 2);
        v.children.forEach((c) => cohort(c, depth + 1));
      }
    }
    cohort(request.cohort, 1);
  }
  for (const k of IDENTITY) assert.deepEqual(v[k], program[k], k);
  assert.deepEqual(v.request, request, "echoed request mismatch");
  const requested = request[window ? "windows" : "metrics"];
  assert(Array.isArray(requested) && requested.length > 0);
  const declarations = window ? program.windows : program.metrics;
  const selected = requested.map((name) => {
    text(name);
    const d = declarations.find(
      (d) => d.name.toLowerCase() === name.toLowerCase(),
    );
    assert(d, "requested declaration missing");
    return d;
  });
  assert(new Set(selected.map((d) => d.name)).size === selected.length);
  assert.deepEqual(
    v.selected,
    selected.map((d) => d.name),
  );
  const expected: ObjectValue[] = [
    { ordinal: 1, name: "user_id", bound_type: "VARCHAR", encoding: "string" },
  ];
  if (window)
    expected.push({
      ordinal: 2,
      name: "ds",
      bound_type: "DATE",
      encoding: "date-text",
    });
  for (const d of selected)
    expected.push({
      ordinal: expected.length + 1,
      name: d.name,
      bound_type: d.bound_type,
      encoding: d.bound_type === "BIGINT" ? "integer-text" : "decimal-text",
      unit: d.unit,
    });
  assert.deepEqual(v.columns, expected, "projection/schema/units mismatch");
  if (window) {
    const source = program.sources.find(
      (s) => s.dataset === selected[0].dataset,
    );
    assert(source);
    assert(selected.every((d) => d.dataset === source.dataset));
    assert.equal(v.dataset, source.dataset);
    assert.equal(v.source_concept, source.concept);
    assert.equal(v.source_captured_rows, source.captured_rows);
    assert.deepEqual(v.source_identity_fields, source.identity_fields);
  }
}
export function factValue(
  content: string,
  request: ObjectValue,
  program: FactProgram,
  window: boolean,
  plan: boolean,
): FactValue {
  const v = carrier(content, plan ? "plan_json" : "result_json") as FactValue;
  keys(v, [
    ...COMMON,
    ...(window ? WINDOW : USER),
    ...(plan
      ? [
          "physical_plan",
          "sql_replayable",
          "projection_sql",
          "operators",
          ...(!window ? ["common_count_sql"] : []),
        ]
      : [
          "row_count",
          "rows",
          ...(!window ? ["common_user_count", "selected_user_count"] : []),
        ]),
  ]);
  provenance(v);
  assert.equal(
    v.schema_version,
    `ossie-fact-${window ? "windows" : "users"}-${plan ? "plan" : "result"}-v1`,
  );
  assert(
    v.input_scope === "entire_capture" &&
      v.paging === false &&
      v.truncated === false &&
      v.json_byte_limit === 1048576,
  );
  assert.equal(
    v.ordering,
    window
      ? "raw User UTF8 bytes ASC then DATE chronological ASC"
      : "raw User UTF8 bytes ASC",
  );
  for (const k of ["executed", "members_computed", "counts_computed"])
    assert.equal(v[k], !plan);
  if (window) {
    assert.deepEqual(v.output_identity_fields, ["user_id", "ds"]);
    assert.equal(v.display_stage, "after whole capture window computation");
  } else {
    assert.equal(v.domain, "common");
    assert.equal(
      v.domain_semantics,
      "row presence in both facts before cohort",
    );
    assert.equal(
      v.cohort_semantics,
      "TRUE member sets; difference excludes RHS TRUE only",
    );
  }
  factCorrespondence(request, program, v, window);
  if (plan) {
    assert(v.physical_plan === false && v.sql_replayable === false);
    text(v.projection_sql);
    if (!window) text(v.common_count_sql);
    assert.deepEqual(
      v.operators,
      window
        ? [
            "frozen single-source User/date projection",
            "whole capture window computation",
            "display DATE filtering",
            "raw identity sorting",
            "complete bounded encoding",
          ]
        : [
            "separate source User aggregation",
            "row-presence common domain",
            "same-unit metric combination",
            "TRUE cohort set filtering",
            "independent common domain count",
            "raw User sorting",
            "complete bounded encoding",
          ],
    );
    return v;
  }
  assert(Array.isArray(v.rows) && v.rows.length <= 10000);
  assert.equal(integer(v.row_count), BigInt(v.rows.length));
  if (!window) {
    assert.equal(integer(v.selected_user_count), BigInt(v.rows.length));
    assert(integer(v.common_user_count) >= BigInt(v.rows.length));
  } else assert(BigInt(v.rows.length) <= integer(v.source_captured_rows));
  let previous: (string | null)[] | undefined;
  for (const row of v.rows) {
    assert(Array.isArray(row) && row.length === v.columns.length);
    text(row[0]);
    if (window) relationDate(row[1]);
    for (let i = window ? 2 : 1; i < row.length; i++) {
      const type = v.columns[i].bound_type;
      if (row[i] === null) {
        assert(type !== "BIGINT", "COUNT cannot be NULL");
        continue;
      }
      if (type === "BIGINT") {
        const source = program.sources.find((s) =>
          [s.user_field, s.amount_field].some(
            (f) => v.columns[i].unit === `count:${s.concept}:${f}`,
          ),
        );
        assert(source);
        integer(row[i], integer(source.captured_rows));
      } else decimal(row[i], type === "DECIMAL(18,2)" ? 18 : 38);
    }
    if (previous) {
      const order = Buffer.compare(
        Buffer.from(previous[0]!),
        Buffer.from(row[0]!),
      );
      assert(
        order < 0 ||
          (window &&
            order === 0 &&
            relationDate(previous[1]) < relationDate(row[1])),
        "rows must be identity-unique and ordered",
      );
    }
    if (window) {
      object(request.display);
      if (request.display.mode === "date_range")
        assert(
          relationDate(row[1]) >= relationDate(request.display.start_date) &&
            relationDate(row[1]) <= relationDate(request.display.end_date),
          "outside requested display",
        );
    }
    previous = row;
  }
  if (!window) {
    const common = integer(v.common_user_count);
    assert(program.sources.every((s) => common <= integer(s.captured_rows)));
    object(request.cohort);
    if (request.cohort.operation === "all")
      assert.equal(
        common,
        BigInt(v.rows.length),
        "all cohort must return full common domain",
      );
  } else {
    object(request.display);
    if (request.display.mode === "all")
      assert.equal(
        integer(v.source_captured_rows),
        BigInt(v.rows.length),
        "all display must return full source grain",
      );
  }
  return v;
}

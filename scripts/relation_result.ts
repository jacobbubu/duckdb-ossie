/** Strict complete-link carrier validation; not a proof of membership truth. */
import assert from "node:assert/strict";
import { BINDING_KEYS, binding } from "./observation_evidence.ts";
export type RelationEnvelope = Record<string, unknown> & {
  relationship: string;
  derived_by: string[];
  dependencies: string[];
};
export type RelationResult = RelationEnvelope & {
  links: string[][];
  link_count: string;
};
function object(v: unknown): asserts v is Record<string, unknown> {
  assert(v !== null && typeof v === "object" && !Array.isArray(v));
}
function keys(v: Record<string, unknown>, wanted: readonly string[]) {
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
/** Proleptic Gregorian days since 1970, covering finite DuckDB DATE, not JS Date range. */
export function relationDate(v: unknown): number {
  text(v);
  const m = /^(\d{4,7})-(\d{2})-(\d{2})( \(BC\))?$/.exec(v);
  assert(m, "finite canonical DuckDB DATE required");
  const printed = Number(m[1]);
  assert(printed > 0 && String(printed).padStart(4, "0") === m[1]);
  let year = m[4] ? 1 - printed : printed;
  const month = Number(m[2]),
    day = Number(m[3]);
  const leap = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
  assert(
    month >= 1 &&
      month <= 12 &&
      day >= 1 &&
      day <=
        [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][month - 1],
  );
  year -= month <= 2 ? 1 : 0;
  const era = Math.floor(year / 400),
    yoe = year - era * 400;
  const doy =
    Math.floor((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5) + day - 1;
  const days =
    era * 146097 +
    yoe * 365 +
    Math.floor(yoe / 4) -
    Math.floor(yoe / 100) +
    doy -
    719468;
  assert(days > -2147483647 && days < 2147483647, "DATE infinity/out-of-range");
  return days;
}
export function compareRelationLinks(a: string[], b: string[]) {
  for (let i = 0; i < 4; i++) {
    const order =
      i === 1
        ? relationDate(a[i]) - relationDate(b[i])
        : Buffer.compare(Buffer.from(a[i]), Buffer.from(b[i]));
    if (order) return order;
  }
  return 0;
}
const COMMON = [
  "schema_version",
  "profile",
  "relationship",
  "root_concept",
  "target_concept",
  "target_identity_relationship",
  "target_identity_field",
  "target_identity_bound_type",
  ...BINDING_KEYS,
  "root_identity_fields",
  "root_identity_bound_types",
  "scope",
  "qualification",
  "load_constraints_validated",
  "dependencies",
  "derived_by",
  "bound_predicate",
  "ordering",
  "paging",
  "truncated",
  "json_byte_limit",
  "executed",
  "members_computed",
];
function envelope(content: string, field: string): RelationEnvelope {
  const carrier: unknown = JSON.parse(content);
  assert(Array.isArray(carrier) && carrier.length === 1);
  object(carrier[0]);
  keys(carrier[0], [field]);
  const raw = carrier[0][field];
  assert(typeof raw === "string" && Buffer.byteLength(raw) <= 1048576);
  const v: unknown = JSON.parse(raw);
  object(v);
  assert(v.profile === "observation-relation-program-v1");
  for (const k of [
    "relationship",
    "root_concept",
    "target_concept",
    "target_identity_relationship",
    "target_identity_field",
    "bound_predicate",
  ])
    text(v[k]);
  assert(v.target_identity_bound_type === "VARCHAR");
  assert(
    Array.isArray(v.root_identity_fields) &&
      v.root_identity_fields.length === 3,
  );
  v.root_identity_fields.forEach(text);
  assert(new Set(v.root_identity_fields).size === 3);
  assert.deepEqual(v.root_identity_bound_types, ["VARCHAR", "DATE", "VARCHAR"]);
  binding(v);
  for (const k of BINDING_KEYS.slice(1))
    assert(typeof v[k] === "string" && /^[a-f0-9]{64}$/.test(v[k] as string));
  assert(
    v.snapshot_id ===
      `${v.content_sha256}:${String(v.snapshot_id).split(":").at(-1)}` &&
      /^[1-9]\d*$/.test(String(v.snapshot_id).split(":").at(-1)!),
  );
  assert(
    v.scope ===
      "entire captured root population; selected relationship predicate only" &&
      v.qualification === "TRUE only" &&
      v.load_constraints_validated === true,
  );
  for (const k of ["dependencies", "derived_by"]) {
    assert(Array.isArray(v[k]));
    (v[k] as unknown[]).forEach(text);
  }
  assert((v.derived_by as unknown[]).length > 0);
  assert(
    v.ordering ===
      "source User raw UTF-8 bytes, DATE chronological, product raw UTF-8 bytes, target User raw UTF-8 bytes",
  );
  assert(
    v.paging === false &&
      v.truncated === false &&
      v.json_byte_limit === 1048576,
  );
  return v as RelationEnvelope;
}
export function relationResult(content: string): RelationResult {
  const v = envelope(content, "result_json");
  keys(v, [...COMMON, "links", "link_count"]);
  assert(
    v.schema_version === "ossie-relation-result-v1" &&
      v.executed === true &&
      v.members_computed === true,
  );
  assert(Array.isArray(v.links) && v.links.length <= 10000);
  for (let i = 0; i < v.links.length; i++) {
    const link: unknown = v.links[i];
    assert(Array.isArray(link) && link.length === 4);
    link.forEach(text);
    relationDate(link[1]);
    assert(link[0] === link[3], "current profile targets same mapped User");
    if (i)
      assert(
        compareRelationLinks(v.links[i - 1] as string[], link) < 0,
        "complete links unique and ordered",
      );
  }
  assert(v.link_count === String(v.links.length), "full link/count mismatch");
  return v as RelationResult;
}
export function relationPlan(content: string): RelationEnvelope {
  const v = envelope(content, "plan_json");
  keys(v, [
    ...COMMON,
    "full_link_projection_types_bound",
    "physical_plan",
    "sql_replayable",
    "projection_sql",
    "operators",
  ]);
  assert(
    v.schema_version === "ossie-relation-plan-v1" &&
      v.executed === false &&
      v.members_computed === false &&
      v.full_link_projection_types_bound === true &&
      v.physical_plan === false &&
      v.sql_replayable === false,
  );
  text(v.projection_sql);
  assert(Array.isArray(v.operators) && v.operators.length === 4);
  v.operators.forEach(text);
  return v;
}
export function sameRelationBinding(
  plan: RelationEnvelope,
  result: RelationEnvelope,
) {
  for (const k of COMMON.filter(
    (k) => !["schema_version", "executed", "members_computed"].includes(k),
  ))
    assert.deepEqual(plan[k], result[k], k);
}

/** Compare requested semantic object to discovery, independent of plan/result agreement. */
export function relationDiscovery(
  requested: string,
  program: Record<string, unknown>,
  value: RelationEnvelope,
) {
  assert(Array.isArray(program.relations));
  const declaration: unknown = program.relations.find(
    (r: Record<string, unknown>) =>
      typeof r.name === "string" &&
      r.name.toLowerCase() === requested.toLowerCase(),
  );
  object(declaration);
  assert.equal(
    value.relationship,
    declaration.name,
    "requested relationship mismatch",
  );
  for (const k of ["derived_by", "dependencies", "bound_predicate"])
    assert.deepEqual(value[k], declaration[k], k);
  assert.equal(value.root_concept, program.root);
  assert.equal(value.target_concept, program.target_concept);
  for (const k of [
    "root_identity_fields",
    "root_identity_bound_types",
    ...BINDING_KEYS,
  ])
    assert.deepEqual(value[k], program[k], k);
}

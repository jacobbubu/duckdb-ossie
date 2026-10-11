import { test, expect } from "bun:test";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "../../scripts/artifact_helpers.ts";
import {
  relationDate,
  relationDiscovery,
  relationPlan,
  relationResult,
  sameRelationBinding,
} from "../../scripts/relation_result.ts";
const report = JSON.parse(
  readFileSync(
    join(
      ROOT,
      "docs/research/ossie-req-001/p1-relation-object-controls/gate-final/verification.json",
    ),
    "utf8",
  ),
);
const carrier = (field: string, value: unknown) =>
  JSON.stringify([{ [field]: JSON.stringify(value) }]);
const base = report.cases[0];
test("relation complete native envelopes retain full links and binding", () => {
  for (const c of report.cases) {
    const result = relationResult(carrier("result_json", c.result));
    const plan = relationPlan(carrier("plan_json", c.plan));
    sameRelationBinding(plan, result);
    expect(result.links).toEqual(c.independent_links);
  }
});
test("relation DATE supports proleptic Gregorian BC and large years without JS Date", () => {
  expect(relationDate("1970-01-01")).toBe(0);
  expect(relationDate("5877642-06-25 (BC)")).toBe(-2147483646);
  expect(relationDate("5881580-07-10")).toBe(2147483646);
  expect(() => relationDate("5877642-06-24 (BC)")).toThrow();
  expect(() => relationDate("5881580-07-11")).toThrow();
  expect(relationDate("0001-01-01 (BC)")).toBeLessThan(
    relationDate("0001-01-01"),
  );
  expect(relationDate("10000-01-01")).toBeGreaterThan(
    relationDate("9999-12-31"),
  );
  expect(relationDate("0401-02-29 (BC)")).toBeLessThan(
    relationDate("0401-03-01 (BC)"),
  );
  for (const bad of [
    "0000-01-01",
    "00001-01-01",
    "2026-02-29",
    "1900-02-29",
    "2026-01-01 BC",
    "infinity",
    "9999999-01-01",
    "2026-13-01",
    "2026-01-00",
  ])
    expect(() => relationDate(bad)).toThrow();
});
test("relation decoder refuses partial, reordered, duplicate, mismatched or foreign-target links", () => {
  for (const mutate of [
    (v: any) => v.links.pop(),
    (v: any) => v.links.reverse(),
    (v: any) => {
      v.links.push(v.links.at(-1));
      v.link_count = String(v.links.length);
    },
    (v: any) => (v.links[0][3] = "foreign"),
    (v: any) => (v.links[0][1] = "2026-02-30"),
    (v: any) => (v.links[0][0] = "\ud800"),
    (v: any) => v.links[0].push("extra"),
    (v: any) => (v.link_count = Number(v.link_count)),
    (v: any) => (v.truncated = true),
    (v: any) => (v.scope = "filtered"),
    (v: any) => (v.content_sha256 = "f".repeat(64)),
    (v: any) => (v.extra = 1),
  ]) {
    const v = structuredClone(base.result);
    mutate(v);
    expect(() => relationResult(carrier("result_json", v))).toThrow();
  }
});
test("relation compile cannot claim execution or smuggle computed links", () => {
  for (const mutate of [
    (v: any) => (v.members_computed = true),
    (v: any) => (v.executed = true),
    (v: any) => (v.links = []),
    (v: any) => (v.full_link_projection_types_bound = false),
  ]) {
    const v = structuredClone(base.plan);
    mutate(v);
    expect(() => relationPlan(carrier("plan_json", v))).toThrow();
  }
  const result = relationResult(carrier("result_json", base.result));
  const plan = relationPlan(carrier("plan_json", base.plan));
  for (const key of [
    "target_identity_field",
    "relationship",
    "derived_by",
    "dependencies",
    "ontology_sha256",
  ]) {
    const v = structuredClone(plan);
    v[key] = "different";
    expect(() => sameRelationBinding(v, result)).toThrow();
  }
});
test("relation carrier must be one bounded string JSON payload", () => {
  for (const content of [
    "[]",
    JSON.stringify([{ result_json: base.result }]),
    JSON.stringify([{ result_json: JSON.stringify(base.result), extra: 1 }]),
    JSON.stringify([{ result_json: " ".repeat(1048577) }]),
  ])
    expect(() => relationResult(content)).toThrow();
});

test("requested declaration and discovery reject jointly wrong semantic responses", () => {
  const scene = JSON.parse(
    readFileSync(
      join(
        ROOT,
        "docs/research/ossie-req-001/p1-relation-mcp-controls/final-mcp.json",
      ),
      "utf8",
    ),
  ).sessions[0];
  const program = scene.discovery.program;
  const qualified = relationResult(
    carrier("result_json", scene.cases[0].result),
  );
  relationDiscovery("QUALIFIED_USER", program, qualified);
  const audited = relationResult(carrier("result_json", scene.cases[1].result));
  expect(audited.links).toEqual(qualified.links);
  expect(() => relationDiscovery("qualified_user", program, audited)).toThrow();
  for (const key of [
    "root_concept",
    "target_concept",
    "root_identity_fields",
    "root_identity_bound_types",
  ]) {
    const result = structuredClone(qualified),
      plan = relationPlan(carrier("plan_json", scene.cases[0].plan));
    result[key] = plan[key] = "jointly forged";
    sameRelationBinding(plan, result);
    expect(() =>
      relationDiscovery("qualified_user", program, result),
    ).toThrow();
  }
  const reduced = structuredClone(base.result);
  reduced.links.pop();
  reduced.link_count = String(reduced.links.length);
  const accepted = relationResult(carrier("result_json", reduced));
  expect(accepted.links).not.toEqual(base.independent_links); // Schema is not member truth.
});

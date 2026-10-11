import { test, expect } from "bun:test";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "../../scripts/artifact_helpers.ts";
import {
  auditRelationAnswer,
  RELATION_SCENES,
  RELATION_ANSWER_FIELDS,
} from "../../scripts/relation_agent_contract.ts";
import type { JsonObject } from "../../scripts/agent_loop.ts";
const report = JSON.parse(
  readFileSync(
    join(
      ROOT,
      "docs/research/ossie-req-001/p1-relation-mcp-controls/gate-mcp.json",
    ),
    "utf8",
  ),
);
const cases: Record<string, any> = {
  direct: report.sessions[0],
  dependent: report.sessions[0],
  "changed-rule": report.sessions[1],
  empty: report.sessions[2],
  "wide-date": report.sessions[3],
  "compile-only": report.sessions[0],
};
function sample(scene: (typeof RELATION_SCENES)[number]) {
  const session = cases[scene.id];
  const c = session.cases.find(
    (v: any) => v.result.relationship === scene.relationship,
  );
  const name =
    scene.id === "compile-only"
      ? "population_relation_compile"
      : "population_relation_objects";
  const envelope = structuredClone(
    name.endsWith("objects") ? c.result : c.plan,
  );
  const reply = {
    result: {
      content: [
        {
          type: "text",
          text: JSON.stringify([
            {
              [name.endsWith("objects") ? "result_json" : "plan_json"]:
                JSON.stringify(envelope),
            },
          ]),
        },
      ],
    },
  };
  const events: JsonObject[] = [
    {
      kind: "tool_request",
      name,
      arguments: { relationship: scene.relationship },
      evidence_id: "tool-1",
    },
    { kind: "tool_response", evidence_id: "tool-1", result: reply },
  ];
  const answer: JsonObject =
    scene.id === "compile-only"
      ? {
          status: "not_executed",
          reason: "No execution",
          evidence_ids: ["tool-1"],
          limitations: ["synthetic only"],
        }
      : {
          status: "answered",
          ...Object.fromEntries(
            RELATION_ANSWER_FIELDS.map((k) => [k, envelope[k]]),
          ),
          evidence_ids: ["tool-1"],
          limitations: ["synthetic only"],
        };
  return { answer, events, program: session.discovery.program, envelope };
}
test("all six relation audits require actual semantic object and full independent links", () => {
  for (const scene of RELATION_SCENES) {
    const s = sample(scene);
    expect(
      auditRelationAnswer(JSON.stringify(s.answer), s.events, scene, s.program)
        .passed,
    ).toBe(true);
  }
});
test("answer cannot use count-only, distinct users, false trace or compile citations", () => {
  const scene = RELATION_SCENES[0];
  for (const mutate of [
    (v: any) => delete v.links,
    (v: any) => (v.links = v.links.map((r: any) => r[0])),
    (v: any) => (v.link_count = Number(v.link_count)),
    (v: any) => (v.snapshot_id = "old"),
    (v: any) => (v.evidence_ids = ["other-scene"]),
    (v: any) => (v.derived_by = ["other rule"]),
    (v: any) => (v.status = "unsupported"),
    (v: any) => (v.extra = 1),
  ]) {
    const s = sample(scene);
    mutate(s.answer);
    expect(() =>
      auditRelationAnswer(JSON.stringify(s.answer), s.events, scene, s.program),
    ).toThrow();
  }
  const s = sample(scene);
  s.events[0].name = "population_relation_compile";
  expect(() =>
    auditRelationAnswer(JSON.stringify(s.answer), s.events, scene, s.program),
  ).toThrow();
});
test("schema-valid synchronized count/member forgery still fails independent oracle", () => {
  const scene = RELATION_SCENES[0],
    s = sample(scene);
  s.envelope.links[0][0] = "0";
  s.envelope.links[0][3] = "0";
  s.answer.links = s.envelope.links;
  (s.events[1].result as any).result.content[0].text = JSON.stringify([
    { result_json: JSON.stringify(s.envelope) },
  ]);
  expect(() =>
    auditRelationAnswer(JSON.stringify(s.answer), s.events, scene, s.program),
  ).toThrow(/independent complete link oracle/);
});
test("compile-only cannot calculate members or claim unsupported", () => {
  const scene = RELATION_SCENES.at(-1)!;
  for (const mutate of [
    (v: any) => (v.link_count = "0"),
    (v: any) => (v.status = "unsupported"),
    (v: any) => (v.status = "answered"),
    (v: any) => (v.evidence_ids = ["alien"]),
  ]) {
    const s = sample(scene);
    mutate(s.answer);
    expect(() =>
      auditRelationAnswer(JSON.stringify(s.answer), s.events, scene, s.program),
    ).toThrow();
  }
});
test("same-members wrong declaration and hidden scope fail audit", () => {
  const scene = RELATION_SCENES[0];
  const s = sample(scene);
  (s.events[0].arguments as JsonObject).relationship = "audited_user";
  expect(() =>
    auditRelationAnswer(JSON.stringify(s.answer), s.events, scene, s.program),
  ).toThrow();
  const extra = sample(scene);
  (extra.events[0].arguments as JsonObject).limit = 1;
  expect(() =>
    auditRelationAnswer(
      JSON.stringify(extra.answer),
      extra.events,
      scene,
      extra.program,
    ),
  ).toThrow();
});

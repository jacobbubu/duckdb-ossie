import { test, expect } from "bun:test";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import Ajv2020 from "ajv/dist/2020.js";
import {
  FACT_CONTRACT_PATH,
  FACT_CONTRACT_URI,
  FACT_USER_TOOLS,
  factRequestContract,
  factContractToolDescriptions,
} from "../../scripts/fact_request_contract.ts";
import { ROOT } from "../../scripts/artifact_helpers.ts";
import { sourceHashes } from "../../scripts/p1_agent_contract.ts";
import { FACT_SOURCE_NAMES } from "../../scripts/fact_agent_contract.ts";
import type { Tool } from "../../scripts/agent_loop.ts";
const text = readFileSync(FACT_CONTRACT_PATH, "utf8");
const document = JSON.parse(text);
const carrier = (value = text) => JSON.stringify([{ contract_json: value }]);
const tools = (): Tool[] =>
  FACT_USER_TOOLS.map((name) => ({
    name,
    description: document.construction_notice,
    inputSchema: {
      type: "object",
      properties: { request_json: { type: "string" } },
      required: ["request_json"],
    },
  }));
test("one frozen carrier and both SQL descriptions share the reviewed contract", () => {
  expect(factRequestContract(carrier())).toEqual(document);
  expect(() => factContractToolDescriptions(tools())).not.toThrow();
  const server = readFileSync(join(ROOT, "examples/fact_server.sql"), "utf8");
  expect(server.split(document.construction_notice).length - 1).toBe(2);
  expect(document.applies_to).toEqual(FACT_USER_TOOLS);
  expect(server).toContain(FACT_CONTRACT_URI);
});
test("carrier ambiguity, duplicate fields and byte changes cannot replace the reviewed document", () => {
  for (const value of [
    JSON.stringify([{ contract_json: text }, { contract_json: text }]),
    JSON.stringify([{ contract_json: document }]),
    JSON.stringify([{ contract_json: text, extra: true }]),
    `[{"contract_json":${JSON.stringify(text)},"contract_json":${JSON.stringify(text)}}]`,
    carrier(
      text.replace(
        '"schema_version":',
        '"schema_version":"forged", "schema_version":',
      ),
    ),
    carrier(text + " "),
    carrier(JSON.stringify({ ...document, schema_version: "other" })),
  ])
    expect(() => factRequestContract(value)).toThrow();
});
test("missing compile grammar or changed applicability is refused during discovery", () => {
  const changed = tools();
  changed[1].description = "Bind a request; syntax omitted";
  expect(() => factContractToolDescriptions(changed)).toThrow();
  expect(() => factContractToolDescriptions(tools().slice(0, 1))).toThrow();
  expect(() =>
    factRequestContract(
      carrier(
        JSON.stringify({ ...document, applies_to: [FACT_USER_TOOLS[0]] }),
      ),
    ),
  ).toThrow();
});
test("hypothetical examples cover every cohort branch and preserve a single String boundary", () => {
  const validate = new Ajv2020({ strict: true }).compile(
    document.request_schema,
  );
  const examples = document.generic_examples.examples;
  expect(examples.map((e: { id: string }) => e.id)).toEqual([
    "all",
    "predicate",
    "intersection",
    "union",
    "difference",
    "nested",
  ]);
  for (const item of examples) {
    expect(validate(item.inner_request)).toBe(true);
    expect(typeof item.outer_arguments.request_json).toBe("string");
    expect(JSON.parse(item.outer_arguments.request_json)).toEqual(
      item.inner_request,
    );
  }
  expect(
    document.generic_examples.hypothetical_program.order_count.unit,
  ).toStartWith("count:");
  expect(document.generic_examples.use).toContain("not directly executable");
});
test("both rehearsal source inventories bind the separately loaded JSON contract", () => {
  expect(
    Object.hasOwn(sourceHashes(), "examples/fact_users_request_contract.json"),
  ).toBe(true);
  expect(FACT_SOURCE_NAMES).toContain(
    "examples/fact_users_request_contract.json",
  );
  expect(FACT_SOURCE_NAMES).toContain("scripts/fact_request_contract.ts");
  expect(FACT_SOURCE_NAMES).toContain("scripts/p1_agent_json.ts");
});
test("published structural schema rejects missing fields and malformed cohort branches", () => {
  const validate = new Ajv2020({ strict: true }).compile(
    document.request_schema,
  );
  const base = document.generic_examples.examples[0].inner_request;
  for (const cohort of [
    { operation: "predicate" },
    { operation: "predicate", predicate: {} },
    { operation: "all", predicate: "order_count>7" },
    { operation: "not", children: [{ operation: "all" }] },
    { operation: "union", children: [{ operation: "all" }] },
    {
      operation: "intersection",
      children: [
        { operation: "all" },
        { operation: "all" },
        { operation: "all" },
      ],
    },
  ])
    expect(validate({ ...base, cohort })).toBe(false);
});

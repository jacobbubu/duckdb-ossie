/** Static users-request discovery contract, separate from native snapshot declarations. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "./artifact_helpers.ts";
import { p1JSON } from "./p1_agent_json.ts";
import type { JsonObject, Tool } from "./agent_loop.ts";
export const FACT_CONTRACT_URI = "ossie://facts/request-contract";
export const FACT_CONTRACT_PATH = join(
  ROOT,
  "examples/fact_users_request_contract.json",
);
export const FACT_USER_TOOLS = [
  "population_fact_users",
  "population_fact_users_compile",
];
function trustedContract() {
  const text = readFileSync(FACT_CONTRACT_PATH, "utf8");
  const value = p1JSON(text, 65536) as JsonObject;
  assert(value && typeof value === "object" && !Array.isArray(value));
  assert.equal(value.schema_version, "ossie-fact-users-request-contract-v1");
  assert.deepEqual(value.applies_to, FACT_USER_TOOLS);
  assert(
    typeof value.construction_notice === "string" &&
      value.construction_notice.includes(FACT_CONTRACT_URI),
  );
  return { text, value };
}
export function factRequestContract(carrier: string) {
  const rows = p1JSON(carrier, 1048576) as JsonObject[];
  assert(Array.isArray(rows) && rows.length === 1);
  assert(rows[0] && typeof rows[0] === "object" && !Array.isArray(rows[0]));
  assert.deepEqual(Object.keys(rows[0]), ["contract_json"]);
  assert(typeof rows[0].contract_json === "string");
  const expected = trustedContract();
  const value = p1JSON(rows[0].contract_json, 65536);
  assert.deepEqual(
    value,
    expected.value,
    "discovered request contract differs from reviewed source",
  );
  assert.equal(
    rows[0].contract_json,
    expected.text,
    "request contract bytes differ from reviewed source",
  );
  return expected.value;
}
export function factContractToolDescriptions(tools: Tool[]) {
  const { value } = trustedContract();
  for (const name of FACT_USER_TOOLS) {
    const tool = tools.find((t) => t.name === name);
    assert(tool && typeof tool.description === "string");
    assert(
      tool.description.includes(String(value.construction_notice)),
      "users tool request grammar missing or drifted",
    );
  }
}

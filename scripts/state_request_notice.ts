/** One source for generated query/compile descriptions; no additional MCP resource. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT, sqlString } from "./artifact_helpers.ts";
import { p1JSON } from "./p1_agent_json.ts";
import { stateRequest } from "./daily_state_result.ts";
import type { Tool } from "./agent_loop.ts";

export const STATE_NOTICE_PATH = join(
  ROOT,
  "examples/state_request_notice.json",
);
export const STATE_NOTICE_TOOLS = [
  "population_daily_states",
  "population_daily_states_compile",
];
export function stateRequestNotice(
  raw = readFileSync(STATE_NOTICE_PATH, "utf8"),
) {
  const value = p1JSON(raw, 32768) as {
    schema_version: string;
    applies_to: string[];
    tool_specific_suffixes: Record<string, string>;
    construction_notice: string;
    generic_example_notice: string;
    generic_inner_example: unknown;
    generic_outer_example: { concept: string; request_json: string };
  };
  assert.equal(value.schema_version, "ossie-state-tool-notice-v1");
  assert.deepEqual(value.applies_to, STATE_NOTICE_TOOLS);
  assert.deepEqual(
    Object.keys(value.tool_specific_suffixes).sort(),
    STATE_NOTICE_TOOLS,
  );
  for (const name of STATE_NOTICE_TOOLS)
    assert(
      typeof value.tool_specific_suffixes[name] === "string" &&
        value.tool_specific_suffixes[name].length > 0,
    );
  assert(
    typeof value.construction_notice === "string" &&
      value.construction_notice.length > 0,
  );
  assert(
    typeof value.generic_example_notice === "string" &&
      value.generic_example_notice.length > 0,
  );
  stateRequest(value.generic_inner_example);
  assert.deepEqual(Object.keys(value.generic_outer_example).sort(), [
    "concept",
    "request_json",
  ]);
  assert.equal(value.generic_outer_example.concept, "EligibleVisit");
  assert(typeof value.generic_outer_example.request_json === "string");
  assert.deepEqual(
    p1JSON(value.generic_outer_example.request_json),
    value.generic_inner_example,
  );
  return value;
}
export function stateDescriptions() {
  const v = stateRequestNotice();
  const common =
    v.construction_notice +
    " Generic syntax example: " +
    v.generic_example_notice +
    " Inner object: " +
    JSON.stringify(v.generic_inner_example) +
    " Serialized outer arguments: " +
    JSON.stringify(v.generic_outer_example) +
    ".";
  return Object.fromEntries(
    STATE_NOTICE_TOOLS.map((name) => [
      name,
      common + " " + v.tool_specific_suffixes[name],
    ]),
  );
}
export function renderStateDescriptions(sql: string) {
  const descriptions = stateDescriptions(),
    seen: string[] = [];
  const result = sql.replace(
    /'(population_daily_states(?:_compile)?)',\n '(?:[^']|'')*',/g,
    (_, name: string) => {
      seen.push(name);
      return sqlString(name) + ",\n " + sqlString(descriptions[name]) + ",";
    },
  );
  assert.deepEqual(
    seen.sort(),
    STATE_NOTICE_TOOLS,
    "exactly two generated descriptions required",
  );
  return result;
}
export function checkStateDescriptions(tools: Tool[]) {
  const expected = stateDescriptions();
  assert.deepEqual(tools.map((t) => t.name).sort(), STATE_NOTICE_TOOLS);
  for (const tool of tools)
    assert.equal(
      tool.description,
      expected[tool.name],
      "state description differs from shared source",
    );
}
if (import.meta.main) {
  const args = process.argv.slice(2);
  assert(args.length === 1 && ["--check", "--write"].includes(args[0]));
  const path = join(ROOT, "examples/daily_state_server.sql"),
    source = readFileSync(path, "utf8");
  const rendered = renderStateDescriptions(source);
  if (args[0] === "--write") writeFileSync(path, rendered);
  else
    assert.equal(
      source,
      rendered,
      "regenerate state descriptions with --write",
    );
}

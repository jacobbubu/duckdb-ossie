/** One source for generated query/compile descriptions; no additional MCP resource. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT, sqlString } from "./artifact_helpers.ts";
import { p1JSON } from "./p1_agent_json.ts";
import { historyRequest } from "./history_result.ts";
import type { Tool } from "./agent_loop.ts";

export const HISTORY_NOTICE_PATH = join(
  ROOT,
  "examples/history_request_notice.json",
);
export const HISTORY_NOTICE_TOOLS = [
  "population_history_links",
  "population_history_links_compile",
];
export function historyRequestNotice(
  raw = readFileSync(HISTORY_NOTICE_PATH, "utf8"),
) {
  const value = p1JSON(raw, 32768) as {
    schema_version: string;
    applies_to: string[];
    base_descriptions: Record<string, string>;
    construction_notice: string;
    generic_example_notice: string;
    generic_inner_example: unknown;
    generic_outer_example: { relationship: string; request_json: string };
  };
  assert.equal(value.schema_version, "ossie-history-tool-notice-v1");
  assert.deepEqual(value.applies_to, HISTORY_NOTICE_TOOLS);
  assert.deepEqual(
    Object.keys(value.base_descriptions).sort(),
    HISTORY_NOTICE_TOOLS,
  );
  for (const name of HISTORY_NOTICE_TOOLS)
    assert(
      typeof value.base_descriptions[name] === "string" &&
        value.base_descriptions[name].length > 0,
    );
  assert(
    typeof value.construction_notice === "string" &&
      value.construction_notice.length > 0,
  );
  assert(
    typeof value.generic_example_notice === "string" &&
      value.generic_example_notice.length > 0,
  );
  historyRequest(value.generic_inner_example);
  assert.deepEqual(Object.keys(value.generic_outer_example).sort(), [
    "relationship",
    "request_json",
  ]);
  assert.equal(
    value.generic_outer_example.relationship,
    "membership_at_observation",
  );
  assert(typeof value.generic_outer_example.request_json === "string");
  assert.deepEqual(
    p1JSON(value.generic_outer_example.request_json),
    value.generic_inner_example,
  );
  return value;
}
export function historyDescriptions() {
  const v = historyRequestNotice();
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
    HISTORY_NOTICE_TOOLS.map((name) => [
      name,
      v.base_descriptions[name] + " " + common,
    ]),
  );
}
export function renderHistoryDescriptions(sql: string) {
  const descriptions = historyDescriptions(),
    seen: string[] = [];
  const result = sql.replace(
    /'(population_history_links(?:_compile)?)',\n '(?:[^']|'')*',/g,
    (_, name: string) => {
      seen.push(name);
      return sqlString(name) + ",\n " + sqlString(descriptions[name]) + ",";
    },
  );
  assert.deepEqual(
    seen.sort(),
    HISTORY_NOTICE_TOOLS,
    "exactly two generated descriptions required",
  );
  return result;
}
export function checkHistoryDescriptions(tools: Tool[]) {
  const expected = historyDescriptions();
  assert.deepEqual(tools.map((t) => t.name).sort(), HISTORY_NOTICE_TOOLS);
  for (const tool of tools)
    assert.equal(
      tool.description,
      expected[tool.name],
      "history description differs from shared source",
    );
}
if (import.meta.main) {
  const args = process.argv.slice(2);
  assert(args.length === 1 && ["--check", "--write"].includes(args[0]));
  const path = join(ROOT, "examples/history_server.sql"),
    source = readFileSync(path, "utf8");
  const rendered = renderHistoryDescriptions(source);
  if (args[0] === "--write") writeFileSync(path, rendered);
  else
    assert.equal(
      source,
      rendered,
      "regenerate history descriptions with --write",
    );
}

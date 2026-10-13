import { test, expect } from "bun:test";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "../../scripts/artifact_helpers.ts";
import {
  sourceHashes,
  validateArguments,
} from "../../scripts/p1_agent_contract.ts";
import { HISTORY_AGENT_SOURCES } from "../../scripts/history_agent.ts";
import {
  HISTORY_NOTICE_PATH,
  HISTORY_NOTICE_TOOLS,
  historyRequestNotice,
  historyDescriptions,
  renderHistoryDescriptions,
  checkHistoryDescriptions,
} from "../../scripts/history_request_notice.ts";
import type { Tool } from "../../scripts/agent_loop.ts";
const tools = (): Tool[] =>
  HISTORY_NOTICE_TOOLS.map((name) => ({
    name,
    description: historyDescriptions()[name],
    inputSchema: {
      type: "object",
      properties: {
        relationship: { type: "string" },
        request_json: { type: "string" },
      },
      required: ["relationship", "request_json"],
    },
  }));
test("discovery refuses missing compile grammar and independently changed descriptions", () => {
  expect(() => checkHistoryDescriptions(tools())).not.toThrow();
  const changed = tools();
  changed[1].description = "Same request as query";
  expect(() => checkHistoryDescriptions(changed)).toThrow();
  expect(() => checkHistoryDescriptions(tools().slice(0, 1))).toThrow();
  const sql = readFileSync(join(ROOT, "examples/history_server.sql"), "utf8");
  expect(renderHistoryDescriptions(sql)).toBe(sql);
  expect(
    renderHistoryDescriptions(
      sql.replace("finite canonical DuckDB DATE strings", "date strings"),
    ),
  ).toBe(sql);
  expect(() =>
    renderHistoryDescriptions(
      sql.replace("'population_history_links_compile',", "'other',"),
    ),
  ).toThrow();
});
test("shared source rejects ambiguous JSON and double encoding; generic String example covers both tools", () => {
  const raw = readFileSync(HISTORY_NOTICE_PATH, "utf8"),
    source = historyRequestNotice();
  expect(() =>
    historyRequestNotice(
      raw.replace(
        '"schema_version":',
        '"schema_version":"forged", "schema_version":',
      ),
    ),
  ).toThrow();
  expect(() =>
    historyRequestNotice(
      JSON.stringify({
        ...source,
        generic_outer_example: {
          ...source.generic_outer_example,
          request_json: JSON.stringify(
            source.generic_outer_example.request_json,
          ),
        },
      }),
    ),
  ).toThrow();
  for (const name of HISTORY_NOTICE_TOOLS) {
    expect(() =>
      validateArguments(
        name,
        JSON.stringify(source.generic_outer_example),
        tools(),
      ),
    ).not.toThrow();
    expect(() =>
      validateArguments(
        name,
        JSON.stringify({
          ...source.generic_outer_example,
          request_json: source.generic_inner_example,
        }),
        tools(),
      ),
    ).toThrow();
  }
  expect(source.generic_example_notice).toContain(
    "not executable against the current pilot",
  );
});
test("both replay inventories bind shared notice and its strict JSON parser", () => {
  for (const name of [
    "examples/history_request_notice.json",
    "scripts/history_request_notice.ts",
    "scripts/p1_agent_json.ts",
  ]) {
    expect(Object.hasOwn(sourceHashes(), name)).toBe(true);
    expect(HISTORY_AGENT_SOURCES).toContain(name);
  }
});

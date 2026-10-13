import { test, expect } from "bun:test";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "../../scripts/artifact_helpers.ts";
import {
  sourceHashes,
  validateArguments,
} from "../../scripts/p1_agent_contract.ts";
import { STATE_AGENT_SOURCES } from "../../scripts/daily_state_agent.ts";
import {
  STATE_NOTICE_PATH,
  STATE_NOTICE_TOOLS,
  stateRequestNotice,
  stateDescriptions,
  renderStateDescriptions,
  checkStateDescriptions,
} from "../../scripts/state_request_notice.ts";
import type { Tool } from "../../scripts/agent_loop.ts";
const tools = (): Tool[] =>
  STATE_NOTICE_TOOLS.map((name) => ({
    name,
    description: stateDescriptions()[name],
    inputSchema: {
      type: "object",
      properties: {
        concept: { type: "string" },
        request_json: { type: "string" },
      },
      required: ["concept", "request_json"],
    },
  }));
test("discovery refuses missing compile grammar and independently changed descriptions", () => {
  expect(() => checkStateDescriptions(tools())).not.toThrow();
  const changed = tools();
  changed[1].description = "Same request as query";
  expect(() => checkStateDescriptions(changed)).toThrow();
  expect(() => checkStateDescriptions(tools().slice(0, 1))).toThrow();
  const sql = readFileSync(
    join(ROOT, "examples/daily_state_server.sql"),
    "utf8",
  );
  expect(renderStateDescriptions(sql)).toBe(sql);
  expect(
    renderStateDescriptions(
      sql.replace(
        "String canonical Gregorian YYYY-MM-DD dates",
        "date strings",
      ),
    ),
  ).toBe(sql);
  expect(() =>
    renderStateDescriptions(
      sql.replace("'population_daily_states_compile',", "'other',"),
    ),
  ).toThrow();
});
test("shared source rejects ambiguous JSON and double encoding; generic String example covers both tools", () => {
  const raw = readFileSync(STATE_NOTICE_PATH, "utf8"),
    source = stateRequestNotice();
  expect(() =>
    stateRequestNotice(
      raw.replace(
        '"schema_version":',
        '"schema_version":"forged", "schema_version":',
      ),
    ),
  ).toThrow();
  expect(() =>
    stateRequestNotice(
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
  for (const name of STATE_NOTICE_TOOLS) {
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
    "examples/state_request_notice.json",
    "scripts/state_request_notice.ts",
    "scripts/p1_agent_json.ts",
  ]) {
    expect(Object.hasOwn(sourceHashes(), name)).toBe(true);
    expect(STATE_AGENT_SOURCES).toContain(name);
  }
});

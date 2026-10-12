import { test, expect } from "bun:test";
import { agentLoop, type Tool } from "../../scripts/agent_loop.ts";
import {
  validateArguments,
  context,
  LIMITS,
} from "../../scripts/p1_agent_contract.ts";
import { cliArguments } from "../../scripts/p1_agent.ts";
import {
  boundedResponse,
  abortable,
} from "../../scripts/p1_agent_simulation.ts";
const tools: Tool[] = [
  {
    name: "population_fact_users",
    inputSchema: {
      type: "object",
      properties: { request_json: { type: "string" } },
      required: ["request_json"],
    },
  },
];
const request = {
  domain: "common",
  input_scope: "entire_capture",
  metrics: ["balance"],
  cohort: { operation: "predicate", predicate: "expense>80" },
};
test("a valid wrong threshold is neither rewritten nor rejected against hidden task", () => {
  const raw = JSON.stringify({ request_json: JSON.stringify(request) });
  expect(validateArguments(tools[0].name, raw, tools).request_json).toBe(
    JSON.stringify(request),
  );
});
test("outer duplicate keys and unknown keys fail before dispatch", () => {
  expect(() =>
    validateArguments(
      tools[0].name,
      '{"request_json":"{}","request_json":"{}"}',
      tools,
    ),
  ).toThrow();
  expect(() =>
    validateArguments(
      tools[0].name,
      JSON.stringify({ request_json: JSON.stringify(request), extra: "" }),
      tools,
    ),
  ).toThrow();
});
test("nested request duplicate keys and non-String RPC parameters fail", () => {
  expect(() =>
    validateArguments(
      tools[0].name,
      JSON.stringify({ request_json: '{"domain":"common","domain":"common"}' }),
      tools,
    ),
  ).toThrow();
  expect(() =>
    validateArguments(
      tools[0].name,
      JSON.stringify({ request_json: request }),
      tools,
    ),
  ).toThrow();
});
test("context contains only fixed prompts and passed discovery, never evaluator data", () => {
  const rows = { "ossie://fixture": [{ declaration: "formal" }] };
  const c = context("facts", {
    tools,
    rows,
    program: { hidden_evaluator_data: "must-not-send" },
    binding: {},
  });
  expect(c.resources).toEqual(rows);
  expect(JSON.stringify(c)).not.toContain("hidden_evaluator_data");
  expect(c.question).toStartWith("本次scene=facts。");
});
test("CLI refuses live flags and implicit environment config", () => {
  expect(() => cliArguments(["--run"])).toThrow();
  expect(() => cliArguments(["--rehearse", "--env", ".env_test"])).toThrow();
});
test("stream bounds count UTF8 bytes and cancel on excess", async () => {
  let cancelled = false;
  const r = new Response(
    new ReadableStream({
      start(c) {
        c.enqueue(new TextEncoder().encode('"界'));
        c.enqueue(new TextEncoder().encode('界"'));
      },
      cancel() {
        cancelled = true;
      },
    }),
  );
  await expect(
    boundedResponse(r, 5, new AbortController().signal),
  ).rejects.toThrow("byte budget");
  expect(cancelled).toBe(true);
});
test("complete JSON inbound at exact cap succeeds", async () => {
  const text = '{"x":"界"}';
  const result = await boundedResponse(
    new Response(text),
    Buffer.byteLength(text),
    new AbortController().signal,
  );
  expect(await result.json()).toEqual({ x: "界" });
});
test("abort releases waiting work without awaiting an uncooperative producer", async () => {
  const controller = new AbortController();
  const pending = abortable(new Promise<string>(() => {}), controller.signal);
  controller.abort(new Error("bounded cancellation"));
  await expect(pending).rejects.toThrow("bounded cancellation");
});
test("serialized outbound limit applies after escaped tool output", async () => {
  let sent = 0;
  const result = agentLoop({
    model: "simulated",
    system: "s",
    question: "q",
    resources: {},
    tools,
    limits: { ...LIMITS, inputBytes: 2000 },
    record() {},
    complete: async () => {
      sent++;
      return {
        id: "r1",
        message: {
          role: "assistant",
          content: null,
          tool_calls: [
            {
              id: "c1",
              type: "function",
              function: {
                name: tools[0].name,
                arguments: JSON.stringify({
                  request_json: JSON.stringify(request),
                }),
              },
            },
          ],
        },
        finish_reason: "tool_calls",
      };
    },
    call: async () => '\\"'.repeat(1500),
  });
  await expect(result).rejects.toThrow("byte budget");
  expect(sent).toBe(1);
});

test("linear parser accepts a multi-megabyte JSON String and rejects decoded duplicate keys", async () => {
  const { p1JSON } = await import("../../scripts/p1_agent_json.ts");
  const text = JSON.stringify({ payload: "a".repeat(2097000) });
  expect((p1JSON(text, 2097152) as { payload: string }).payload.length).toBe(
    2097000,
  );
  expect(() => p1JSON('{"a":1,"\\u0061":2}')).toThrow("duplicate");
  expect(() => p1JSON('{"nested":{"x":1,"x":2}}')).toThrow("duplicate");
});
test("bounded provider JSON rejects duplicate envelope fields", async () => {
  await expect(
    boundedResponse(
      new Response('{"id":"one","id":"two"}'),
      1000,
      new AbortController().signal,
    ),
  ).rejects.toThrow("duplicate");
});

test("state operation notice preserves resources and does not alter other prompts", () => {
  const d = {
    tools,
    rows: { "ossie://population/capabilities": [{ sparse: true }] },
    program: {},
    binding: {},
  };
  const states = context("states", d);
  expect(states.system).toContain("每日状态查询以state-program");
  expect(states.system).toContain("不代表本次tools/list注册状态");
  expect(states.resources).toEqual(d.rows);
  expect(context("facts", d).system).not.toContain("操作适用范围");
});

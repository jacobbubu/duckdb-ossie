import { test, expect } from "bun:test";
import {
  agentLoop,
  responsesProvider,
  LIMITS,
  beforeDeadline,
  providerBaseURL,
  type Completion,
  type JsonObject,
  type Tool,
} from "../../scripts/agent_loop.ts";
import {
  checkAnswer,
  experimentConfiguration,
  experimentSummary,
} from "../../scripts/agent_acceptance.ts";
const tools: Tool[] = [
  {
    name: "population_query",
    description: "Discovered tool",
    inputSchema: {
      type: "object",
      properties: { concept: { type: "string" }, filters: { type: "string" } },
      required: ["concept", "filters"],
    },
  },
];
const toolReply = (
  name = "population_query",
  args = '{"concept":"ChosenByModel","filters":"[\\"x = 1\\"]"}',
  id = "call-1",
): Completion => ({
  id: `response-${id}`,
  message: {
    role: "assistant",
    content: null,
    tool_calls: [{ id, type: "function", function: { name, arguments: args } }],
  },
  finish_reason: "tool_calls",
});
const final: Completion = {
  id: "response-final",
  message: { role: "assistant", content: '{"status":"unsupported"}' },
  finish_reason: "stop",
};
test("model arguments and exact numeric MCP strings reach the next model request unchanged", async () => {
  const events: JsonObject[] = [];
  const requests: JsonObject[] = [];
  let args: JsonObject | undefined;
  const mcp = {
    jsonrpc: "2.0",
    result: { content: [{ text: '[{"amount":"9007199254740993.01"}]' }] },
  };
  const result = await agentLoop({
    model: "test",
    system: "instructions",
    question: "question",
    resources: { source: "live" },
    tools,
    record: (e) => events.push(e),
    complete: async (body) => {
      requests.push(structuredClone(body));
      return requests.length === 1 ? toolReply() : final;
    },
    call: async (_name, value) => {
      args = value;
      return mcp;
    },
  });
  expect(args).toEqual({ concept: "ChosenByModel", filters: '["x = 1"]' });
  const messages = requests[1].input as JsonObject[];
  expect(JSON.parse(String(messages.at(-1)?.output))).toEqual({
    evidence_id: "tool-1",
    mcp_response: mcp,
  });
  expect((requests[0].input as JsonObject[]).length).toBe(2);
  expect(requests[1].previous_response_id).toBe("response-call-1");
  expect(messages[0].call_id).toBe("call-1");
  expect(requests[0]).not.toHaveProperty("previous_response_id");
  expect(result.calls).toBe(1);
  expect(events.filter((e) => e.kind === "tool_request")).toHaveLength(1);
});
test("unknown tool or malformed arguments never reach MCP", async () => {
  for (const reply of [
    toolReply("reload"),
    toolReply("population_query", '{"concept":"X","filters":[]}'),
    toolReply("population_query", '{"concept":"X"}'),
    toolReply(
      "population_query",
      '{"concept":"X","filters":"[]","sql":"DROP TABLE t"}',
    ),
  ]) {
    let called = false;
    await expect(
      agentLoop({
        model: "test",
        system: "s",
        question: "q",
        resources: [],
        tools,
        record: () => {},
        complete: async () => reply,
        call: async () => {
          called = true;
          return {};
        },
      }),
    ).rejects.toThrow();
    expect(called).toBe(false);
  }
});
test("call and round budgets terminate without successful-call substitution", async () => {
  let calls = 0;
  let rounds = 0;
  await expect(
    agentLoop({
      model: "test",
      system: "s",
      question: "q",
      resources: [],
      tools,
      record: () => {},
      limits: { ...LIMITS, rounds: 2, calls: 1 },
      complete: async () =>
        toolReply(
          "population_query",
          '{"concept":"X","filters":"[]"}',
          `c-${++rounds}`,
        ),
      call: async () => {
        calls++;
        return { error: "original error" };
      },
    }),
  ).rejects.toThrow("tool call budget");
  expect(calls).toBe(1);
  expect(rounds).toBe(2);
  await expect(
    agentLoop({
      model: "test",
      system: "s",
      question: "q",
      resources: [],
      tools,
      record: () => {},
      limits: { ...LIMITS, rounds: 1 },
      complete: async () => toolReply(),
      call: async () => ({}),
    }),
  ).rejects.toThrow("round budget");
});
test("deadline and byte budget stop requests; provider length is a failure", async () => {
  await expect(
    agentLoop({
      model: "test",
      system: "s",
      question: "q",
      resources: [],
      tools,
      record: () => {},
      limits: { ...LIMITS, milliseconds: 10 },
      complete: async () => new Promise(() => {}),
      call: async () => ({}),
    }),
  ).rejects.toThrow("deadline");
  let contacted = false;
  await expect(
    agentLoop({
      model: "test",
      system: "s",
      question: "q",
      resources: [],
      tools,
      record: () => {},
      limits: { ...LIMITS, inputBytes: 1 },
      complete: async () => {
        contacted = true;
        return final;
      },
      call: async () => ({}),
    }),
  ).rejects.toThrow("byte budget");
  expect(contacted).toBe(false);
  await expect(
    agentLoop({
      model: "test",
      system: "s",
      question: "q",
      resources: [],
      tools,
      record: () => {},
      complete: async () => ({ ...final, finish_reason: "length" }),
      call: async () => ({}),
    }),
  ).rejects.toThrow("token budget");
});
test("provider forwards only model-visible response fields, refuses redirects and HTTP failure without retries", async () => {
  let count = 0;
  const fake = async (_url: unknown, options?: RequestInit) => {
    count++;
    expect(options?.redirect).toBe("error");
    expect((options?.headers as Record<string, string>).Authorization).toBe(
      "Bearer dummy-test-key",
    );
    return new Response(
      JSON.stringify({
        id: "r",
        model: "resolved",
        usage: { total_tokens: 3 },
        status: "completed",
        output: [
          { type: "reasoning", summary: [], private_field: "hidden" },
          {
            type: "message",
            role: "assistant",
            content: [{ type: "output_text", text: "answer" }],
          },
        ],
      }),
    );
  };
  const result = await responsesProvider(
    "https://example.test/v1/",
    "dummy-test-key",
    fake,
  )({}, new AbortController().signal);
  expect(result.message).toEqual({ role: "assistant", content: "answer" });
  expect(JSON.stringify(result)).not.toContain("private");
  expect(count).toBe(1);
  let failures = 0;
  await expect(
    responsesProvider("https://example.test/", "dummy-test-key", async () => {
      failures++;
      return new Response("private error", { status: 401 });
    })({}, new AbortController().signal),
  ).rejects.toThrow("HTTP 401");
  expect(failures).toBe(1);
});
test("boundary scene accepts zero calls but numerical hallucination is refused", () => {
  const answer = {
    status: "unsupported",
    scope: "missing",
    definition: "missing",
    evidence_ids: [],
    limitations: ["missing active/paid"],
    count: null,
    total: null,
    average: null,
  };
  expect(
    checkAnswer(JSON.stringify(answer), 10, [], {}, true).numerical_gate,
  ).toBe("not applicable");
  expect(() =>
    checkAnswer(
      JSON.stringify({ ...answer, average: "45.89" }),
      10,
      [],
      {},
      true,
    ),
  ).toThrow();
});
test("correct-looking final numbers without executed citations cannot pass", () => {
  const answer = {
    status: "answered",
    scope: "scope",
    definition: "rule",
    evidence_ids: ["ossie://population/concepts"],
    limitations: [],
    count: "37",
    total: "1697.80",
    average: "45.89",
    unit: "USD",
  };
  const baseline = {
    cases: {
      "10": {
        daily: [
          {
            ds: "2026-03-10",
            record_count: 37,
            gaap_sum: "1697.80",
            gaap_average: "45.88648648648649",
          },
        ],
      },
    },
  };
  expect(() =>
    checkAnswer(JSON.stringify(answer), 10, [], baseline, false),
  ).toThrow("cited executed");
});

test("credential-bearing URLs and unrelated ambient gateway/model overrides are rejected", () => {
  for (const url of [
    "https://key@example.test/v1/",
    "https://example.test/v1/?key=secret",
    "http://example.test/v1/",
  ]) {
    expect(() => providerBaseURL(url)).toThrow();
    expect(() => experimentConfiguration({ OPENAI_BASE_URL: url })).toThrow();
  }
  expect(() =>
    experimentConfiguration({ OPENAI_BASE_URL: "https://other.test/v1/" }),
  ).toThrow("user-selected gateway");
  expect(() =>
    experimentConfiguration({ OPENAI_MODEL: "unapproved-model" }),
  ).toThrow("user-selected model");
  expect(experimentConfiguration({}).model).toBe("configured-tool-capable-model");
});
test("shared scene deadline prevents setup from borrowing more time or starting after expiry", async () => {
  let started = false;
  await expect(
    beforeDeadline(Date.now() - 1, async () => {
      started = true;
      return 1;
    }),
  ).rejects.toThrow("deadline");
  expect(started).toBe(false);
  await expect(
    beforeDeadline(Date.now() + 10, async () => new Promise(() => {})),
  ).rejects.toThrow("deadline");
});
test("filtered/unknown provider termination cannot be a final answer", async () => {
  for (const finish_reason of ["content_filter", "unknown", undefined]) {
    await expect(
      agentLoop({
        model: "test",
        system: "s",
        question: "q",
        resources: [],
        tools,
        record: () => {},
        complete: async () => ({ ...final, finish_reason }),
        call: async () => ({}),
      }),
    ).rejects.toThrow("normal stop");
  }
});
test("failure remains authoritative in the experiment summary", () => {
  expect(
    experimentSummary(
      [
        { scene: "first", overall: "pending human review" },
        { scene: "second", overall: "fail" },
      ],
      true,
    ).overall,
  ).toBe("fail");
  expect(
    experimentSummary(
      [{ scene: "first", overall: "pending human review" }],
      true,
    ).overall,
  ).toBe("pending human review");
});

test("Responses wire bodies repeat instructions/tools and keep continuation IDs isolated", async () => {
  const bodies: JsonObject[] = [];
  for (let scene = 0; scene < 2; scene++) {
    let round = 0;
    await agentLoop({
      model: "test",
      system: "fixed instructions",
      question: "fixed question",
      resources: [],
      tools,
      record: (event) => {
        if (event.kind === "provider_request")
          expect(event.serialized_body).toBe(JSON.stringify(event.body));
      },
      complete: async (body) => {
        bodies.push(structuredClone(body));
        return ++round === 1 ? toolReply() : final;
      },
      call: async () => ({ result: "unchanged" }),
    });
  }
  expect(bodies[0]).not.toHaveProperty("previous_response_id");
  expect(bodies[2]).not.toHaveProperty("previous_response_id");
  for (const body of bodies) {
    expect(body.instructions).toBe("fixed instructions");
    expect(body.max_output_tokens).toBe(2048);
    expect(body.truncation).toBe("disabled");
    expect(body.store).toBe(true);
    expect(body.tools).toEqual([
      {
        type: "function",
        name: tools[0].name,
        description: tools[0].description,
        parameters: tools[0].inputSchema,
        strict: false,
      },
    ]);
    expect(body).not.toHaveProperty("messages");
  }
});
test("Responses call_id rather than item ID binds multiple tool outputs; mixed prose is not final", async () => {
  const bodies: JsonObject[] = [];
  let called = 0;
  const complete = responsesProvider(
    "https://example.test/v1/",
    "dummy-test-key",
    async (url, options) => {
      expect(String(url)).toBe("https://example.test/v1/responses");
      bodies.push(JSON.parse(String(options?.body)));
      const output =
        bodies.length === 1
          ? [
              {
                type: "message",
                role: "assistant",
                content: [{ type: "output_text", text: "I will query." }],
              },
              ...["one", "two"].map((id) => ({
                type: "function_call",
                id: `item-${id}`,
                call_id: `call-${id}`,
                name: "population_query",
                arguments: '{"concept":"X","filters":"[]"}',
                status: "completed",
              })),
            ]
          : [
              {
                type: "message",
                role: "assistant",
                content: [{ type: "output_text", text: "final answer" }],
              },
            ];
      return new Response(
        JSON.stringify({
          id: `response-${bodies.length}`,
          status: "completed",
          output,
        }),
      );
    },
  );
  const result = await agentLoop({
    model: "test",
    system: "s",
    question: "q",
    resources: [],
    tools,
    record: () => {},
    complete,
    call: async () => ({ result: ++called }),
  });
  expect(result.content).toBe("final answer");
  expect(called).toBe(2);
  expect((bodies[1].input as JsonObject[]).map((x) => x.call_id)).toEqual([
    "call-one",
    "call-two",
  ]);
});
test("Responses incomplete/failed outcomes never execute partially generated calls", async () => {
  for (const status of ["incomplete", "failed", "queued"]) {
    let called = false;
    const complete = responsesProvider(
      "https://example.test/",
      "dummy-test-key",
      async () =>
        new Response(
          JSON.stringify({
            id: "r",
            status,
            incomplete_details: { reason: "max_output_tokens" },
            output: [
              {
                type: "function_call",
                call_id: "c",
                name: "population_query",
                arguments: '{"concept":"X","filters":"[]"}',
                status: "in_progress",
              },
            ],
          }),
        ),
    );
    await expect(
      agentLoop({
        model: "test",
        system: "s",
        question: "q",
        resources: [],
        tools,
        record: () => {},
        complete,
        call: async () => {
          called = true;
          return {};
        },
      }),
    ).rejects.toThrow();
    expect(called).toBe(false);
  }
});
test("missing/repeated Responses IDs and invalid call batches stop before further MCP execution", async () => {
  let called = 0;
  const options = {
    model: "test",
    system: "s",
    question: "q",
    resources: [],
    tools,
    record: () => {},
    call: async () => {
      called++;
      return {};
    },
  };
  await expect(
    agentLoop({
      ...options,
      complete: async () => ({ ...toolReply(), id: undefined }),
    }),
  ).rejects.toThrow("Responses ID");
  expect(called).toBe(0);
  const second = toolReply("reload", '{"concept":"X","filters":"[]"}', "c2")
    .message.tool_calls![0];
  await expect(
    agentLoop({
      ...options,
      complete: async () => ({
        ...toolReply(),
        message: {
          ...toolReply().message,
          tool_calls: [toolReply().message.tool_calls![0], second],
        },
      }),
    }),
  ).rejects.toThrow("allowlist");
  expect(called).toBe(0);
  await expect(
    agentLoop({ ...options, complete: async () => toolReply() }),
  ).rejects.toThrow("Responses ID");
  expect(called).toBe(1);
});
test("Responses refusal and declined provider continuation do not trigger protocol fallback", async () => {
  const refusal = responsesProvider(
    "https://example.test/",
    "dummy-test-key",
    async () =>
      new Response(
        JSON.stringify({
          id: "r",
          status: "completed",
          output: [
            {
              type: "message",
              role: "assistant",
              content: [{ type: "refusal", refusal: "cannot answer" }],
            },
          ],
        }),
      ),
  );
  await expect(
    agentLoop({
      model: "test",
      system: "s",
      question: "q",
      resources: [],
      tools,
      record: () => {},
      complete: refusal,
      call: async () => ({}),
    }),
  ).rejects.toThrow("normal stop");
  let requested = 0;
  let called = 0;
  const complete = responsesProvider(
    "https://example.test/",
    "dummy-test-key",
    async () => {
      if (++requested > 1)
        return new Response("unsupported previous_response_id", {
          status: 400,
        });
      return new Response(
        JSON.stringify({
          id: "r",
          status: "completed",
          output: [
            {
              type: "function_call",
              call_id: "c",
              name: "population_query",
              arguments: '{"concept":"X","filters":"[]"}',
            },
          ],
        }),
      );
    },
  );
  await expect(
    agentLoop({
      model: "test",
      system: "s",
      question: "q",
      resources: [],
      tools,
      record: () => {},
      complete,
      call: async () => {
        called++;
        return {};
      },
    }),
  ).rejects.toThrow("HTTP 400");
  expect(requested).toBe(2);
  expect(called).toBe(1);
});

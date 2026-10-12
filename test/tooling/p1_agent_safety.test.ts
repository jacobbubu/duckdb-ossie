import { hashBytes } from "../../scripts/observation_agent_archive.ts";
import { verifiedP1Bytes } from "../../scripts/p1_agent_public.ts";
import { test, expect } from "bun:test";
import {
  cleanupSteps,
  CLEANUP_POLICY,
  p1ResponseBoundary,
} from "../../scripts/p1_agent_safety.ts";
import {
  responsesProvider,
  type JsonObject,
} from "../../scripts/agent_loop.ts";
const signal = () => new AbortController().signal;
const base = {
  id: "response-1",
  model: "synthetic-model",
  status: "completed",
};
const text = {
  type: "message",
  role: "assistant",
  status: "completed",
  content: [{ type: "output_text", text: '{"visible":"界\\u0000"}' }],
};
async function projected(data: JsonObject) {
  return (
    await p1ResponseBoundary(
      new Response(JSON.stringify(data)),
      2097152,
      signal(),
    )
  ).json();
}
test("response boundary drops reasoning and metadata, preserves visible text and numeric usage only", async () => {
  const sentinel = "PRIVATE_REASONING_SENTINEL";
  const result = await projected({
    ...base,
    extra: sentinel,
    metadata: { x: sentinel },
    output: [
      {
        type: "reasoning",
        summary: [{ text: sentinel }],
        encrypted_content: sentinel,
      },
      {
        ...text,
        extra: sentinel,
        content: [
          { ...text.content[0], annotations: [sentinel], logprobs: sentinel },
        ],
      },
    ],
    usage: {
      input_tokens: 12,
      output_tokens: 3,
      total_tokens: 15,
      extra: sentinel,
      input_tokens_details: { cached_tokens: 4, extra: sentinel },
      output_tokens_details: { reasoning_tokens: 2, extra: sentinel },
    },
  });
  expect(JSON.stringify(result)).not.toContain(sentinel);
  expect(result.output[0].content[0].text).toBe(text.content[0].text);
  expect(result.usage).toEqual({
    input_tokens: 12,
    output_tokens: 3,
    total_tokens: 15,
    input_tokens_details: { cached_tokens: 4 },
    output_tokens_details: { reasoning_tokens: 2 },
  });
});
test("unknown usage is not converted to zero, exact function arguments preserved", async () => {
  const argumentsText = '{"request_json":"{\\"x\\":1}"}';
  const r = await projected({
    ...base,
    usage: { input_tokens: -1, output_tokens: 1.5, total_tokens: "PRIVATE" },
    output: [
      {
        type: "function_call",
        call_id: "call-1",
        name: "tool",
        arguments: argumentsText,
        status: "completed",
      },
    ],
  });
  expect(r.usage).toBeUndefined();
  expect(r.output[0].arguments).toBe(argumentsText);
});
test("unsupported output/content, invalid roles, HTTP failures and duplicate fields cannot become successful responses", async () => {
  for (const output of [
    [text, { type: "unknown_action" }],
    [{ ...text, role: "user" }],
    [{ ...text, status: 42 }],
    [{ ...text, content: [{ type: "hidden_action" }] }],
  ])
    await expect(projected({ ...base, output })).rejects.toThrow();
  await expect(
    p1ResponseBoundary(
      new Response("secret error", { status: 401 }),
      2097152,
      signal(),
    ),
  ).rejects.toThrow("provider HTTP failure");
  await expect(
    p1ResponseBoundary(
      new Response('{"id":"a","id":"b","output":[]}'),
      2097152,
      signal(),
    ),
  ).rejects.toThrow();
});
test("refusal and incomplete survive projection and keep adapter terminal decisions", async () => {
  const provider = (data: JsonObject) =>
    responsesProvider("https://simulated.invalid/v1/", "", async () =>
      p1ResponseBoundary(new Response(JSON.stringify(data)), 2097152, signal()),
    );
  expect(
    (
      await provider({
        ...base,
        output: [
          {
            ...text,
            content: [{ type: "refusal", refusal: "visible refusal" }],
          },
        ],
      })({}, signal())
    ).finish_reason,
  ).toBe("refusal");
  expect(
    (
      await provider({
        ...base,
        status: "incomplete",
        incomplete_details: { reason: "max_output_tokens", private: "hidden" },
        output: [],
      })({}, signal())
    ).finish_reason,
  ).toBe("length");
  expect(
    (
      await projected({
        ...base,
        status: "incomplete",
        incomplete_details: { reason: "unknown secret" },
        output: [],
      })
    ).incomplete_details.reason,
  ).toBe("unknown");
});
test("cleanup has sequential windows and stops at the first failure", async () => {
  const seen: string[] = [];
  await expect(
    cleanupSteps(
      [
        async () => {
          seen.push("close");
          throw Error("failure");
        },
        async () => {
          seen.push("work");
        },
      ],
      10,
    ),
  ).rejects.toThrow("failure");
  expect(seen).toEqual(["close"]);
  expect(CLEANUP_POLICY.maximum_normal_requested_wait_ms).toBe(60000);
  await expect(
    cleanupSteps(
      [
        () => new Promise(() => {}),
        async () => {
          seen.push("late");
        },
      ],
      5,
    ),
  ).rejects.toThrow("deadline");
  expect(seen).toEqual(["close"]);
  await expect(cleanupSteps([], 30001)).rejects.toThrow();
});

test("every consumed summary file must use the original hash-bound bytes", () => {
  const raw = Buffer.from('{"count":1}');
  const expected = hashBytes(raw);
  expect(verifiedP1Bytes(raw, expected)).toBe(raw.toString());
  expect(() => verifiedP1Bytes(Buffer.from('{"count":2}'), expected)).toThrow(
    "source snapshot changed",
  );
  expect(() => verifiedP1Bytes(raw, "invalid")).toThrow();
});

test("malformed JSON and stream failures cannot leak raw response text through diagnostics", async () => {
  for (const response of [
    new Response('{"reasoning":"PRIVATE_PARSE_SENTINEL" broken'),
    new Response(
      new ReadableStream({
        start(c) {
          c.error(Error("PRIVATE_PARSE_SENTINEL"));
        },
      }),
    ),
  ]) {
    try {
      await p1ResponseBoundary(response, 2097152, signal());
      throw Error("unexpected success");
    } catch (error) {
      expect(String(error)).toContain("invalid provider response body");
      expect(String(error)).not.toContain("PRIVATE_PARSE_SENTINEL");
    }
  }
});

/** Explicit model-originated calls; no business parameters or oracle in this module. */
import assert from "node:assert/strict";
export type JsonObject = Record<string, unknown>;
export interface Tool {
  name: string;
  description?: string;
  inputSchema: JsonObject;
}
export interface ToolCall {
  id: string;
  type: "function";
  function: { name: string; arguments: string };
}
export interface Assistant {
  role: "assistant";
  content: string | null;
  tool_calls?: ToolCall[];
}
export interface Completion {
  message: Assistant;
  id?: string;
  model?: string;
  usage?: unknown;
  finish_reason?: string;
  response_status?: string;
  public_output?: JsonObject[];
  incomplete_details?: unknown;
  error_code?: string;
}
export interface LoopOptions {
  model: string;
  system: string;
  question: string;
  resources: unknown;
  tools: Tool[];
  complete: (body: JsonObject, signal: AbortSignal) => Promise<Completion>;
  call: (name: string, args: JsonObject) => Promise<unknown>;
  record: (event: JsonObject) => void;
  limits?: {
    rounds: number;
    calls: number;
    milliseconds: number;
    outputTokens: number;
    inputBytes: number;
  };
}
export const LIMITS = Object.freeze({
  rounds: 8,
  calls: 12,
  milliseconds: 120000,
  outputTokens: 2048,
  inputBytes: 131072,
});
export function toolArguments(call: ToolCall, tools: Tool[]): JsonObject {
  assert(
    call.type === "function" &&
      typeof call.id === "string" &&
      call.id.length > 0,
    "invalid tool call identity",
  );
  const tool = tools.find((t) => t.name === call.function.name);
  assert(tool, "tool outside discovered allowlist");
  const args: unknown = JSON.parse(call.function.arguments);
  assert(
    args && typeof args === "object" && !Array.isArray(args),
    "arguments must be object",
  );
  const properties = tool.inputSchema.properties as JsonObject;
  assert(
    properties && typeof properties === "object",
    "missing input properties",
  );
  for (const required of tool.inputSchema.required as string[])
    assert(Object.hasOwn(args, required), "missing required argument");
  for (const [key, value] of Object.entries(args)) {
    assert(Object.hasOwn(properties, key), "unknown argument");
    assert(
      (properties[key] as JsonObject).type === "string" &&
        typeof value === "string",
      "string argument required",
    );
  }
  return args as JsonObject;
}
export async function agentLoop(options: LoopOptions) {
  const limits = options.limits ?? LIMITS;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), limits.milliseconds);
  let input: JsonObject[] = [
    {
      role: "system",
      content: JSON.stringify({ discovered_resources: options.resources }),
    },
    { role: "user", content: options.question },
  ];
  const tools = options.tools.map((t) => ({
    type: "function",
    name: t.name,
    description: t.description,
    parameters: t.inputSchema,
    strict: false,
  }));
  let previousResponseId: string | undefined;
  const responseIds = new Set<string>();
  let calls = 0;
  const ids = new Set<string>();
  async function bounded<T>(promise: Promise<T>): Promise<T> {
    controller.signal.throwIfAborted();
    let abort: () => void = () => {};
    try {
      return await Promise.race([
        promise,
        new Promise<never>((_, reject) => {
          abort = () => reject(new Error("scenario deadline exceeded"));
          controller.signal.addEventListener("abort", abort, { once: true });
        }),
      ]);
    } finally {
      controller.signal.removeEventListener("abort", abort);
    }
  }
  try {
    for (let round = 1; round <= limits.rounds; round++) {
      controller.signal.throwIfAborted();
      const body = {
        model: options.model,
        instructions: options.system,
        input: structuredClone(input),
        store: true,
        truncation: "disabled",
        ...(previousResponseId
          ? { previous_response_id: previousResponseId }
          : {}),
        tools,
        tool_choice: "auto",
        max_output_tokens: limits.outputTokens,
        stream: false,
      };
      assert(
        Buffer.byteLength(JSON.stringify(body)) <= limits.inputBytes,
        "model request byte budget exceeded",
      );
      options.record({
        kind: "provider_request",
        round,
        body,
        serialized_body: JSON.stringify(body),
      });
      const response = await bounded(options.complete(body, controller.signal));
      options.record({
        kind: "provider_response",
        round,
        projection: "public Responses fields; private reasoning omitted",
        response,
      });
      assert(
        response.finish_reason !== "length",
        "model output token budget exhausted",
      );
      const message = response.message;
      assert(message.role === "assistant", "invalid assistant role");
      assert(
        typeof response.id === "string" &&
          response.id.length > 0 &&
          !responseIds.has(response.id),
        "missing or reused Responses ID",
      );
      responseIds.add(response.id);
      previousResponseId = response.id;
      input = [];
      if (!message.tool_calls?.length) {
        assert(
          response.finish_reason === "stop",
          "final answer requires normal stop",
        );
        assert(
          typeof message.content === "string" && message.content.length > 0,
          "missing final answer",
        );
        options.record({
          kind: "final_answer",
          content: message.content,
          calls,
          rounds: round,
        });
        return { content: message.content, calls, rounds: round };
      }
      assert(
        response.finish_reason === "tool_calls",
        "unexpected tool-call finish reason",
      );
      assert(
        calls + message.tool_calls.length <= limits.calls,
        "tool call budget exceeded",
      );
      const batchIds = new Set<string>();
      const batch = message.tool_calls.map((call) => {
        assert(
          !ids.has(call.id) && !batchIds.has(call.id),
          "duplicate tool call ID",
        );
        batchIds.add(call.id);
        return { call, args: toolArguments(call, options.tools) };
      });
      for (const { call, args } of batch) {
        controller.signal.throwIfAborted();
        ids.add(call.id);
        const evidence_id = `tool-${++calls}`;
        options.record({
          kind: "tool_request",
          evidence_id,
          provider_call_id: call.id,
          name: call.function.name,
          arguments: args,
          arguments_json: call.function.arguments,
        });
        const result = await bounded(options.call(call.function.name, args));
        options.record({ kind: "tool_response", evidence_id, result });
        input.push({
          type: "function_call_output",
          call_id: call.id,
          output: JSON.stringify({ evidence_id, mcp_response: result }),
        });
      }
    }
    throw new Error("model round budget exceeded");
  } finally {
    clearTimeout(timer);
    controller.abort();
  }
}
/** Reject credential-bearing endpoints before any evidence writer sees configuration. */
export function providerBaseURL(baseURL: string) {
  const base = new URL(baseURL);
  assert(base.protocol === "https:", "HTTPS provider required");
  assert(
    !base.username && !base.password && !base.search && !base.hash,
    "invalid provider URL",
  );
  return base.href.endsWith("/") ? base.href : base.href + "/";
}
export async function beforeDeadline<T>(
  deadline: number,
  action: () => Promise<T>,
): Promise<T> {
  const remaining = deadline - Date.now();
  assert(remaining > 0, "scenario deadline exceeded");
  let timer: ReturnType<typeof setTimeout> | undefined;
  try {
    return await Promise.race([
      action(),
      new Promise<never>((_, reject) => {
        timer = setTimeout(
          () => reject(new Error("scenario deadline exceeded")),
          remaining,
        );
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
}
/** Record model-visible fields only; never persist authentication or private reasoning. */
export function responsesProvider(
  baseURL: string,
  key: string,
  request: (
    input: string | URL | Request,
    init?: RequestInit,
  ) => Promise<Response> = fetch,
) {
  const endpoint = new URL("responses", providerBaseURL(baseURL));
  return async (body: JsonObject, signal: AbortSignal): Promise<Completion> => {
    const response = await request(endpoint, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${key}`,
      },
      body: JSON.stringify(body),
      signal,
      redirect: "error",
    });
    assert(response.ok, `provider HTTP ${response.status}; no automatic retry`);
    const data = (await response.json()) as JsonObject;
    assert(
      typeof data.id === "string" && data.id.length > 0,
      "Responses ID required",
    );
    assert(Array.isArray(data.output), "Responses output array required");
    const calls: ToolCall[] = [];
    const text: string[] = [];
    const publicOutput: JsonObject[] = [];
    let refusal = false;
    for (const [output_index, item] of (
      data.output as JsonObject[]
    ).entries()) {
      if (item.type === "reasoning") continue; // Continuation remains at provider; private reasoning is not archived.
      if (item.type === "function_call") {
        assert(
          typeof item.call_id === "string" &&
            typeof item.name === "string" &&
            typeof item.arguments === "string",
          "invalid Responses function call",
        );
        assert(
          item.status === "completed" ||
            item.status === undefined ||
            data.status !== "completed",
          "incomplete function call",
        );
        calls.push({
          id: item.call_id,
          type: "function",
          function: { name: item.name, arguments: item.arguments },
        });
        publicOutput.push({
          output_index,
          type: item.type,
          id: item.id,
          call_id: item.call_id,
          name: item.name,
          arguments: item.arguments,
          status: item.status,
        });
      } else if (item.type === "message") {
        assert(
          item.status === "completed" ||
            item.status === undefined ||
            data.status !== "completed",
          "incomplete Responses message",
        );
        assert(
          item.role === "assistant" && Array.isArray(item.content),
          "invalid Responses message",
        );
        const content: JsonObject[] = [];
        for (const block of item.content as JsonObject[]) {
          if (block.type === "output_text") {
            assert(typeof block.text === "string", "invalid output text");
            text.push(block.text);
            content.push({ type: block.type, text: block.text });
          } else if (block.type === "refusal") {
            refusal = true;
            content.push({ type: block.type, refusal: block.refusal });
          } else throw new Error("unsupported Responses content type");
        }
        publicOutput.push({
          output_index,
          type: item.type,
          id: item.id,
          role: item.role,
          phase: item.phase,
          status: item.status,
          content,
        });
      } else throw new Error("unsupported Responses output item");
    }
    const reason = (data.incomplete_details as JsonObject | undefined)?.reason;
    const finish_reason =
      data.status === "completed"
        ? refusal
          ? "refusal"
          : calls.length
            ? "tool_calls"
            : "stop"
        : data.status === "incomplete" && reason === "max_output_tokens"
          ? "length"
          : String(data.status ?? "unknown");
    return {
      message: {
        role: "assistant",
        content: text.length ? text.join("") : null,
        ...(calls.length ? { tool_calls: calls } : {}),
      },
      id: data.id,
      model: typeof data.model === "string" ? data.model : "unknown",
      usage: data.usage ?? "unknown",
      response_status:
        typeof data.status === "string" ? data.status : undefined,
      public_output: publicOutput,
      incomplete_details: reason === undefined ? undefined : { reason },
      error_code:
        typeof (data.error as JsonObject | undefined)?.code === "string"
          ? ((data.error as JsonObject).code as string)
          : undefined,
      finish_reason,
    };
  };
}

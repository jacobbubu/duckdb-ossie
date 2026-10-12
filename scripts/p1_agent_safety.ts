/** Explicit response injection only. No network, credentials or raw-response recorder. */
import assert from "node:assert/strict";
import { beforeDeadline, type JsonObject } from "./agent_loop.ts";
import { boundedResponse } from "./p1_agent_simulation.ts";
import { p1JSON } from "./p1_agent_json.ts";
export const STATE_SCOPE_NOTICE =
  "操作适用范围：当前可调用工具以实际tools/list为准。每日状态查询以state-program、state-surface及population_daily_states专用工具合同为准；通用capabilities/concepts中的稀疏观察查询、请求内User投影、无日历补齐及旧结果schema描述其他操作，不能覆盖每日状态合同。program的mcp_registered字段是原生声明，不代表本次tools/list注册状态。";
export const CLEANUP_POLICY = {
  schema: "ossie-p1-cleanup-policy-v1",
  timing: "separate sequential waits",
  step_wait_ms: 30000,
  normal_steps: ["close", "work"],
  optional_test_step: "cleanupFault between close and work",
  maximum_normal_requested_wait_ms: 60000,
  maximum_test_requested_wait_ms: 90000,
  stop_on_first_failure: true,
  hard_realtime: false,
} as const;
export async function cleanupSteps(
  steps: (() => Promise<unknown>)[],
  waitMs = CLEANUP_POLICY.step_wait_ms as number,
) {
  assert(Number.isInteger(waitMs) && waitMs > 0 && waitMs <= 30000);
  for (const step of steps) await beforeDeadline(Date.now() + waitMs, step);
}
function numericFields(value: unknown, keys: string[]) {
  const output: JsonObject = {};
  if (value && typeof value === "object" && !Array.isArray(value))
    for (const k of keys) {
      const n = (value as JsonObject)[k];
      if (Number.isSafeInteger(n) && Number(n) >= 0) output[k] = n;
    }
  return output;
}
/** The returned body is a derivative, not the original provider wire. Opaque visible text remains private. */
export async function p1ResponseBoundary(
  response: Response,
  limit: number,
  signal: AbortSignal,
) {
  assert(Number.isInteger(limit) && limit > 0 && limit <= 2097152);
  assert(response.ok, "provider HTTP failure; no automatic retry");
  let data: JsonObject;
  try {
    const bounded = await boundedResponse(response, limit, signal);
    data = p1JSON(await bounded.text(), limit) as JsonObject;
  } catch (error) {
    // Parser and stream errors may quote raw provider bytes. Never propagate that text to evidence.
    const safe = [
      "provider inbound byte budget exceeded",
      "duplicate JSON key",
      "JSON byte budget exceeded",
      "JSON structural budget exceeded",
    ];
    const message =
      error instanceof Error && safe.includes(error.message)
        ? error.message
        : signal.aborted
          ? "provider response aborted"
          : "invalid provider response body";
    throw Error(message);
  }
  assert(data && typeof data === "object" && !Array.isArray(data));
  assert(
    typeof data.id === "string" && data.id.length > 0,
    "Responses ID required",
  );
  assert(Array.isArray(data.output), "Responses output array required");
  const output: JsonObject[] = [];
  for (const value of data.output) {
    assert(
      value && typeof value === "object" && !Array.isArray(value),
      "invalid Responses item",
    );
    const item = value as JsonObject;
    if (item.type === "reasoning") continue;
    assert(
      item.status === undefined || typeof item.status === "string",
      "invalid Responses item status",
    );
    if (item.type === "function_call") {
      assert(
        typeof item.call_id === "string" &&
          typeof item.name === "string" &&
          typeof item.arguments === "string",
        "invalid Responses function call",
      );
      output.push({
        type: "function_call",
        call_id: item.call_id,
        name: item.name,
        arguments: item.arguments,
        ...(typeof item.id === "string" ? { id: item.id } : {}),
        ...(typeof item.status === "string" ? { status: item.status } : {}),
      });
    } else if (item.type === "message") {
      assert(
        item.role === "assistant" && Array.isArray(item.content),
        "invalid Responses message",
      );
      const content: JsonObject[] = [];
      for (const value of item.content) {
        assert(
          value && typeof value === "object" && !Array.isArray(value),
          "invalid Responses content",
        );
        const block = value as JsonObject;
        if (block.type === "output_text") {
          assert(typeof block.text === "string", "invalid output text");
          content.push({ type: "output_text", text: block.text });
        } else if (block.type === "refusal") {
          assert(typeof block.refusal === "string", "invalid refusal");
          content.push({ type: "refusal", refusal: block.refusal });
        } else throw Error("unsupported Responses content type");
      }
      output.push({
        type: "message",
        role: "assistant",
        content,
        ...(typeof item.id === "string" ? { id: item.id } : {}),
        ...(typeof item.status === "string" ? { status: item.status } : {}),
        ...(["analysis", "final", "commentary"].includes(String(item.phase))
          ? { phase: item.phase }
          : {}),
      });
    } else throw Error("unsupported Responses output item");
  }
  const usage = numericFields(data.usage, [
    "input_tokens",
    "output_tokens",
    "total_tokens",
  ]);
  if (data.usage && typeof data.usage === "object") {
    for (const [name, keys] of [
      ["input_tokens_details", ["cached_tokens"]],
      ["output_tokens_details", ["reasoning_tokens"]],
    ] as const) {
      const fields = numericFields((data.usage as JsonObject)[name], [...keys]);
      if (Object.keys(fields).length) usage[name] = fields;
    }
  }
  const reason = (data.incomplete_details as JsonObject | undefined)?.reason;
  const projected = {
    id: data.id,
    model: typeof data.model === "string" ? data.model : "unknown",
    status: typeof data.status === "string" ? data.status : "unknown",
    output,
    ...(Object.keys(usage).length ? { usage } : {}),
    ...(reason === undefined
      ? {}
      : {
          incomplete_details: {
            reason: ["max_output_tokens", "content_filter"].includes(
              String(reason),
            )
              ? reason
              : "unknown",
          },
        }),
  };
  signal.throwIfAborted();
  return new Response(JSON.stringify(projected), { status: 200 });
}

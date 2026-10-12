import { p1JSON as factJSON } from "./p1_agent_json.ts";
/** Injected Responses bodies only; no credentials or live network transport. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "./artifact_helpers.ts";

import { factContent } from "./fact_mcp.ts";
import type { JsonObject } from "./agent_loop.ts";
import { RESEARCH, type Scene } from "./p1_agent_contract.ts";
export const DEFINITIONS: Record<Scene, string> = {
  facts:
    "两事实分别按User归约后取共同域；排除成本>50为TRUE集合，NULL成本保留。金额USD，人数区分共同域与筛选结果，不计算比率。",
  states:
    "当前正式资格规则按classification_value判断观察。任一TRUE为TRUE，非空全FALSE为FALSE，其余UNKNOWN。整个冻结User域，产品X与含首尾自然日只筛观察，不继承前日；包括未变化相邻日对。",
  history:
    "完整User/DATE/product/Team关系；有限[start,end)匹配，缺口无link，同目标不同记录见证不重复关系。原始身份保留，输入记录数不冒充本次链接数。",
};
export type Simulation = (
  scene: Scene,
  body: JsonObject,
  events: JsonObject[],
) => string | Promise<string>;
export function scriptedSimulation(
  options: {
    wrongScene?: Scene;
    predicate?: string;
    failScene?: Scene;
    duplicateOuter?: boolean;
    oversized?: boolean;
    incomplete?: boolean;
    endless?: boolean;
  } = {},
): Simulation {
  return (scene, body, events) => {
    if (options.oversized) return " ".repeat(2097153);
    const responses = events.filter(
      (e) => e.kind === "provider_response",
    ).length;
    const base = {
      id: `${scene}-response-${responses + 1}`,
      model: "simulated-p1",
      status: "completed",
      usage: { input_tokens: 0, output_tokens: 0, total_tokens: 0 },
    };
    if (options.incomplete)
      return JSON.stringify({
        ...base,
        status: "incomplete",
        incomplete_details: { reason: "max_output_tokens" },
        output: [],
      });
    if (options.failScene === scene)
      return JSON.stringify({
        ...base,
        output: [
          {
            type: "message",
            role: "assistant",
            content: [
              {
                type: "output_text",
                text: JSON.stringify({
                  status: "failed",
                  scene,
                  reason: "Synthetic failure control",
                  evidence_ids: [],
                  limitations: ["Simulation only"],
                }),
              },
            ],
          },
        ],
      });
    if (!responses || options.endless) {
      // Correct request templates belong to this explicit simulator, never model context/runtime guards.
      const answers = factJSON(
        readFileSync(join(ROOT, RESEARCH + "independent-answers.json"), "utf8"),
      ) as Record<string, JsonObject>;
      const request = structuredClone(
        answers[
          scene === "facts" ? "fact" : scene === "states" ? "state" : "history"
        ].request,
      ) as JsonObject;
      if (
        scene === "facts" &&
        (options.wrongScene === scene || options.predicate)
      )
        ((request.cohort as JsonObject).children as JsonObject[])[1].predicate =
          options.predicate ?? "expense>80";
      if (scene === "states" && options.wrongScene === scene)
        request.end_date = "2026-02-01";
      if (scene === "history" && options.wrongScene === scene)
        (request.dates as JsonObject).end_date = "2024-03-01";
      const name =
        scene === "facts"
          ? "population_fact_users"
          : scene === "states"
            ? "population_daily_states"
            : "population_history_links";
      const resources = JSON.parse(
        String((body.input as JsonObject[])[0]?.content ?? "{}"),
      );
      const program = Object.entries(resources.discovered_resources ?? {}).find(
        ([k]) => k.endsWith("program"),
      )?.[1] as JsonObject[] | undefined;
      const inner =
        typeof program?.[0]?.program_json === "string"
          ? (factJSON(program[0].program_json) as JsonObject)
          : program?.[0];
      const args = {
        ...(scene === "states"
          ? { concept: String(inner?.qualification_concept) }
          : scene === "history"
            ? { relationship: String(inner?.relationship) }
            : {}),
        request_json: JSON.stringify(request),
      };
      const raw = options.duplicateOuter
        ? `{"request_json":${JSON.stringify(args.request_json)},"request_json":${JSON.stringify(args.request_json)}}`
        : JSON.stringify(args);
      return JSON.stringify({
        ...base,
        output: [
          {
            type: "function_call",
            call_id: `${scene}-call-${responses + 1}`,
            name,
            arguments: raw,
            status: "completed",
          },
        ],
      });
    }
    const response = events.filter((e) => e.kind === "tool_response").at(-1)!;
    assert(response);
    const carrier = factJSON(
      factContent(response.result as Parameters<typeof factContent>[0]),
    ) as JsonObject[];
    const result = factJSON(String(carrier[0].result_json));
    const final = {
      status: "answered",
      scene,
      result,
      definition: DEFINITIONS[scene],
      evidence_ids: [response.evidence_id],
      limitations: [
        "Scripted simulation with real MCP; no real model or business completeness claim.",
      ],
    };
    return JSON.stringify({
      ...base,
      output: [
        {
          type: "message",
          role: "assistant",
          content: [{ type: "output_text", text: JSON.stringify(final) }],
        },
      ],
    });
  };
}
export async function boundedResponse(
  response: Response,
  limit: number,
  signal: AbortSignal,
) {
  assert(response.body);
  const reader = response.body.getReader();
  const chunks: Uint8Array[] = [];
  let bytes = 0;
  const cancel = () => {
    void reader.cancel();
  };
  signal.addEventListener("abort", cancel, { once: true });
  try {
    while (true) {
      signal.throwIfAborted();
      const { done, value } = await reader.read();
      signal.throwIfAborted();
      if (done) break;
      bytes += value.byteLength;
      assert(bytes <= limit, "provider inbound byte budget exceeded");
      chunks.push(value);
    }
    const payload = Buffer.concat(chunks);
    factJSON(new TextDecoder("utf-8", { fatal: true }).decode(payload), limit);
    return new Response(payload, { status: response.status });
  } finally {
    signal.removeEventListener("abort", cancel);
    await reader.cancel();
    reader.releaseLock();
  }
}
export async function abortable<T>(
  promise: Promise<T>,
  signal: AbortSignal,
): Promise<T> {
  signal.throwIfAborted();
  let cancel: () => void = () => {};
  try {
    return await Promise.race([
      promise,
      new Promise<never>((_, reject) => {
        cancel = () => reject(signal.reason);
        signal.addEventListener("abort", cancel, { once: true });
      }),
    ]);
  } finally {
    signal.removeEventListener("abort", cancel);
  }
}

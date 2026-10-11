/** Simulation-only actual MCP entry; no provider configuration or live transport branch. */
import assert from "node:assert/strict";
import { join } from "node:path";
import { ROOT, digest } from "./artifact_helpers.ts";
import { PrivateArchive } from "./observation_agent_archive.ts";
import {
  agentLoop,
  responsesProvider,
  LIMITS,
  type JsonObject,
  type Completion,
} from "./agent_loop.ts";
import { withFactSession, factRuntime, factContent } from "./fact_mcp.ts";
import { factJSON } from "./fact_result.ts";
import {
  FACT_SCENES,
  FACT_SYSTEM,
  FACT_SOURCE_NAMES,
  FACT_BUDGET,
  auditFactAnswer,
  decodeFactCall,
  factFixtureBinding,
  FACT_LIMITATIONS,
  FACT_UNEXECUTED_REASON,
  type FactScene,
} from "./fact_agent_contract.ts";
export function scriptedFact() {
  const rounds = new Map<string, number>();
  return async (
    scene: FactScene,
    body: JsonObject,
    signal: AbortSignal,
  ): Promise<Completion> => {
    const round = (rounds.get(scene.id) ?? 0) + 1;
    rounds.set(scene.id, round);
    let output: JsonObject[];
    if (round === 1 || (round === 2 && !scene.compileOnly)) {
      output = [
        {
          type: "function_call",
          id: `item-${scene.id}-${round}`,
          call_id: `call-${scene.id}-${round}`,
          name: `population_fact_${scene.kind}${round === 1 ? "_compile" : ""}`,
          arguments: JSON.stringify({
            request_json: JSON.stringify(scene.request),
          }),
          status: "completed",
        },
      ];
    } else {
      const input = body.input as { output: string }[];
      assert.equal(input.length, 1);
      const actual = JSON.parse(input[0].output);
      const carrier = factJSON(
        factContent(actual.mcp_response),
        8 * 1048576,
      ) as Record<string, string>[];
      const envelope = factJSON(
        carrier[0][scene.compileOnly ? "plan_json" : "result_json"],
      ) as JsonObject;
      const limitations = FACT_LIMITATIONS;
      const answer = scene.compileOnly
        ? {
            status: "not_executed",
            reason: FACT_UNEXECUTED_REASON,
            evidence_ids: [actual.evidence_id],
            limitations,
          }
        : {
            status: "answered",
            kind: scene.kind,
            result: envelope,
            evidence_ids: [actual.evidence_id],
            limitations,
          };
      output = [
        {
          type: "message",
          id: `message-${scene.id}`,
          role: "assistant",
          status: "completed",
          content: [{ type: "output_text", text: JSON.stringify(answer) }],
        },
      ];
    }
    const parser = responsesProvider(
      "https://simulated.invalid/v1/",
      "synthetic-unused",
      async () =>
        new Response(
          JSON.stringify({
            id: `response-${scene.id}-${round}`,
            model: "simulated-fact",
            status: "completed",
            usage: { input_tokens: 0, output_tokens: 0, total_tokens: 0 },
            output,
          }),
          { status: 200 },
        ),
    );
    return parser(body, signal);
  };
}
export async function rehearseFact(
  cli: string,
  artifact: string,
  out: string,
  complete = scriptedFact(),
) {
  const archive = PrivateArchive.create(out);
  let currentScene = "setup",
    requests = 0,
    status: "complete" | "failed" = "failed";
  const scenes: JsonObject[] = [];
  try {
    archive.save("run.json", {
      schema: "ossie-fact-agent-rehearsal-v1",
      mode: "simulated",
      provider_config_read: false,
      real_model_requests: 0,
      model: "simulated-fact",
      runtime: factRuntime(cli, artifact),
      budget: FACT_BUDGET,
      usage_semantics:
        "Simulated zero usage placeholders, not measured token consumption",
      source_hashes: Object.fromEntries(
        FACT_SOURCE_NAMES.map((n) => [n, digest(join(ROOT, n))]),
      ),
      container_schema:
        "ossie-observation-agent-archive-v1 is reused hash storage, not an observation scene contract",
      limitations:
        "Hashes attest local byte integrity, not provider authenticity or model ability",
    });
    for (const scene of FACT_SCENES) {
      currentScene = scene.id;
      const events: JsonObject[] = [];
      let sequence = 0;
      archive.startEvents(scene.id);
      const record = (event: JsonObject) => {
        if (event.kind === "provider_request") requests++;
        const tagged = {
          ...event,
          scene: scene.id,
          mode: "simulated",
          sequence: ++sequence,
        };
        events.push(structuredClone(tagged));
        archive.record(scene.id, tagged);
      };
      record({ kind: "scene_start", limits: LIMITS });
      await withFactSession(
        cli,
        artifact,
        scene.setup,
        async (session) => {
          const discovered = await session.discover();
          factFixtureBinding(discovered.program);
          const context = {
            system: FACT_SYSTEM,
            question: scene.question,
            tools: discovered.tools,
            resources: discovered.rows,
          };
          archive.save(`${scene.id}/context.json`, context);
          archive.save(`${scene.id}/setup.json`, {
            binding: discovered.binding,
            setup: scene.setup,
          });
          const final = await agentLoop({
            model: "simulated-fact",
            ...context,
            limits: LIMITS,
            complete: (body, signal) => complete(scene, body, signal),
            call: async (name, args) => {
              const reply = await session.call(name, args);
              decodeFactCall(name, args, reply, scene, discovered.program);
              return reply;
            },
            record,
          });
          const evaluation = auditFactAnswer(
            final.content,
            events,
            scene,
            discovered.program,
          );
          record({ kind: "evaluation", value: evaluation });
          record({
            kind: "scene_end",
            status: "complete",
            calls: final.calls,
            rounds: final.rounds,
          });
          archive.save(`${scene.id}/review.json`, {
            mode: "simulated",
            semantic_verdict: "not applicable: scripted integration",
            scene: scene.id,
            final,
            evaluation,
          });
          scenes.push({
            scene: scene.id,
            status: "complete",
            calls: final.calls,
            rounds: final.rounds,
            evaluation,
          });
        },
        record,
      );
    }
    status = "complete";
  } catch (error) {
    archive.save("failure.json", {
      scene: currentScene,
      category: error instanceof Error ? error.name : "Error",
      mode: "simulated",
      details:
        "Inspect preserved projected simulation events; no provider configuration or model transport used.",
    });
  } finally {
    archive.save("summary.json", {
      schema: "ossie-fact-agent-summary-v1",
      mode: "simulated",
      status,
      real_model_requests: 0,
      simulated_completion_requests: requests,
      scenes,
      scope: "scripted Responses with actual MCP; not live Agent acceptance",
    });
  }
  return {
    status,
    mode: "simulated",
    seal_sha256: archive.seal(),
    path: archive.path,
    real_model_requests: 0,
  };
}

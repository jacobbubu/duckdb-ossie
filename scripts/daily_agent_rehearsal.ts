/** Simulation-only entry. Injected Responses transport; no provider environment/live branch. */
import assert from "node:assert/strict";
import { digest, ROOT } from "./artifact_helpers.ts";
import { PrivateArchive } from "./observation_agent_archive.ts";
import {
  agentLoop,
  responsesProvider,
  LIMITS,
  type JsonObject,
  type Completion,
} from "./agent_loop.ts";
import { withDailySession, dailyRuntime, dailyContent } from "./daily_mcp.ts";
import {
  DAILY_SCENES,
  DAILY_SYSTEM,
  DAILY_SOURCE_NAMES,
  DAILY_BUDGET,
  dailySceneRequest,
  auditDailyAnswer,
  type DailyScene,
} from "./daily_agent_contract.ts";
import { dailyResult, dailyPlan, DAILY_TRACE } from "./daily_result.ts";
import { join } from "node:path";
export function scriptedDaily() {
  const rounds = new Map<string, number>();
  return async (
    scene: DailyScene,
    body: JsonObject,
    signal: AbortSignal,
  ): Promise<Completion> => {
    const round = (rounds.get(scene.id) ?? 0) + 1;
    rounds.set(scene.id, round);
    const args = {
      concept: "QualifiedObservation",
      request_json: JSON.stringify(dailySceneRequest(scene)),
    };
    let output: JsonObject[];
    if (round === 1 || (round === 2 && scene.id !== "compile-only")) {
      output = [
        {
          type: "function_call",
          id: `item-${scene.id}-${round}`,
          call_id: `call-${scene.id}-${round}`,
          name:
            round === 1 ? "population_daily_compile" : "population_daily_users",
          arguments: JSON.stringify(args),
          status: "completed",
        },
      ];
    } else {
      const input = body.input as { output: string }[];
      assert.equal(input.length, 1);
      const actual = JSON.parse(input[0].output);
      const limitations = [
        "Scripted integration from actual tool output, not model autonomy or business acceptance; no completeness or real-world qualification inference.",
      ];
      let answer: JsonObject;
      if (scene.id === "compile-only") {
        const plan = dailyPlan(dailyContent(actual.mcp_response));
        assert.equal(plan.executed, false);
        answer = {
          status: "not_executed",
          reason:
            "Only the logical plan was inspected; members were not calculated.",
          evidence_ids: [actual.evidence_id],
          limitations,
        };
      } else {
        const result = dailyResult(dailyContent(actual.mcp_response));
        answer = {
          status: "answered",
          ...Object.fromEntries(
            [
              "request",
              "members",
              "member_count",
              "required_day_count",
              "qualification_rule",
            ].map((key) => [key, result[key]]),
          ),
          evidence_ids: [actual.evidence_id],
          limitations,
        };
      }
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
    // Explicit injected transport parses the same public Responses fields without fetch.
    const parser = responsesProvider(
      "https://simulated.invalid/v1/",
      "synthetic-unused",
      async () =>
        new Response(
          JSON.stringify({
            id: `response-${scene.id}-${round}`,
            model: "simulated-daily",
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
export async function rehearseDaily(
  cli: string,
  artifact: string,
  out: string,
  complete = scriptedDaily(),
) {
  const archive = PrivateArchive.create(out);
  let currentScene = "setup",
    requests = 0,
    status: "complete" | "failed" = "failed";
  const scenes: JsonObject[] = [];
  try {
    archive.save("run.json", {
      schema: "ossie-daily-agent-rehearsal-v1",
      mode: "simulated",
      provider_config_read: false,
      real_model_requests: 0,
      model: "simulated-daily",
      runtime: dailyRuntime(cli, artifact),
      budget: DAILY_BUDGET,
      source_hashes: Object.fromEntries(
        DAILY_SOURCE_NAMES.map((n) => [n, digest(join(ROOT, n))]),
      ),
      container_schema:
        "ossie-observation-agent-archive-v1 is a reused hash container, not an observation scene contract",
      limitations:
        "Hashes attest local byte integrity, not provider authenticity or model ability",
    });
    for (const scene of DAILY_SCENES) {
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
      await withDailySession(
        cli,
        artifact,
        scene.threshold,
        "extended",
        async (session) => {
          const discovered = await session.discover();
          const context = {
            system: DAILY_SYSTEM,
            question: scene.question,
            tools: discovered.tools,
            resources: discovered.rows,
          };
          archive.save(`${scene.id}/context.json`, context);
          archive.save(`${scene.id}/setup.json`, {
            binding: discovered.binding,
            threshold: scene.threshold,
          });
          const final = await agentLoop({
            model: "simulated-daily",
            ...context,
            limits: LIMITS,
            complete: (body, signal) => complete(scene, body, signal),
            call: async (name, args) => {
              const reply = await session.call(name, args);
              const decoded =
                name === "population_daily_users"
                  ? dailyResult(dailyContent(reply))
                  : dailyPlan(dailyContent(reply));
              for (const key of DAILY_TRACE)
                assert.equal(decoded[key], discovered.binding[key]);
              return reply;
            },
            record,
          });
          const evaluation = auditDailyAnswer(
            final.content,
            events,
            scene,
            discovered.binding,
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
    // No arbitrary provider/error text; original projected events retain diagnostics.
    archive.save("failure.json", {
      scene: currentScene,
      category: error instanceof Error ? error.name : "Error",
      mode: "simulated",
      details:
        "Inspect preserved simulation events; no model credentials or transport were used.",
    });
  } finally {
    archive.save("summary.json", {
      schema: "ossie-daily-agent-summary-v1",
      mode: "simulated",
      status,
      real_model_requests: 0,
      simulated_completion_requests: requests,
      scenes,
      scope: "scripted Responses with actual MCP; not live Agent acceptance",
    });
  }
  const seal_sha256 = archive.seal();
  return {
    status,
    mode: "simulated",
    seal_sha256,
    path: archive.path,
    real_model_requests: 0,
  };
}

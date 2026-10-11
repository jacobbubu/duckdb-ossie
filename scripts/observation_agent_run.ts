/** Explicit live entry, also injectable simulated orchestration. Never called by prepare/replay. */
import assert from "node:assert/strict";
import { join, resolve, relative } from "node:path";
import { spawnSync } from "node:child_process";
import { ROOT, digest } from "./artifact_helpers.ts";
import { PrivateArchive } from "./observation_agent_archive.ts";
import {
  agentLoop,
  responsesProvider,
  providerBaseURL,
  beforeDeadline,
  LIMITS,
  type Completion,
  type JsonObject,
} from "./agent_loop.ts";
import {
  SCENES,
  OBSERVATION_SYSTEM,
  OBSERVATION_CSV,
  observationInput,
  checkObservationAnswer,
  assertResultBinding,
} from "./observation_evidence.ts";
import {
  SERVER,
  runtimeMetadata,
  withObservationSession,
  type ObservationSession,
} from "./observation_mcp.ts";
import { populationResult } from "./population_result.ts";
import { AGENT_SOURCE_FILES, BUDGET } from "./observation_agent_contract.ts";
export function liveArguments(args: string[]) {
  assert(
    args.length === 8 &&
      args[0] === "--run" &&
      args[1] === "--token-budget-only" &&
      args[2] === "--cli" &&
      args[4] === "--extension" &&
      args[6] === "--out",
    "Usage: bun scripts/observation_agent_run.ts --run --token-budget-only --cli PATH --extension PATH --out NEW_IGNORED_PRIVATE_DIRECTORY",
  );
  return {
    cli: resolve(args[3]),
    artifact: resolve(args[5]),
    out: resolve(args[7]),
  };
}
export function liveConfiguration(env: Record<string, string | undefined>) {
  const baseURL = providerBaseURL(
    env.OPENAI_BASE_URL ?? "https://api.openai.com/v1/",
  );
  const model = env.OPENAI_MODEL?.trim(),
    key = env.OPENAI_API_KEY?.trim();
  assert(model, "OPENAI_MODEL required");
  assert(key, "OPENAI_API_KEY required");
  return { baseURL, model, key };
}
function safeFailure(error: unknown, stage: string) {
  const name =
    error instanceof Error && /^[A-Za-z]+$/.test(error.name)
      ? error.name
      : "Error";
  const status =
    error instanceof Error
      ? /^provider HTTP (\d{3});/.exec(error.message)?.[1]
      : undefined;
  return {
    category: name,
    stage,
    provider_http_status: status ?? null,
    details:
      "inspect preserved projected events; arbitrary exception text is not archived",
  };
}
export interface ExperimentOptions {
  mode: "live" | "simulated";
  cli: string;
  artifact: string;
  out: string;
  complete?: (
    scene: (typeof SCENES)[number],
    body: JsonObject,
    signal: AbortSignal,
  ) => Promise<Completion>;
  env?: Record<string, string | undefined>;
}
export async function executeObservation(options: ExperimentOptions) {
  assert(options.mode === "live" || options.mode === "simulated");
  assert(
    options.mode === "simulated"
      ? Boolean(options.complete)
      : !options.complete,
    "provider injection is simulation-only",
  );
  const config =
    options.mode === "live"
      ? liveConfiguration(options.env ?? process.env)
      : {
          baseURL: "https://simulated.invalid/v1/",
          model: "simulated-responses",
          key: "",
        };
  const archive = PrivateArchive.create(
    options.out,
    config.key ? [config.key] : [],
  );
  const scenes: JsonObject[] = [];
  let stage = "runtime setup";
  let terminal: "complete" | "failed" = "failed";
  let providerRequests = 0;
  try {
    const input = observationInput();
    const runtime = runtimeMetadata(options.cli, options.artifact);
    const git = (args: string[]) => {
      const r = spawnSync("git", args, { cwd: ROOT, encoding: "utf8" });
      assert.equal(r.status, 0);
      return r.stdout.trim();
    };
    archive.save("run.json", {
      schema: "ossie-observation-agent-run-v1",
      mode: options.mode,
      provider_config_read: options.mode === "live",
      head: git(["rev-parse", "HEAD"]),
      dirty: git(["status", "--porcelain"]),
      provider: {
        baseURL: config.baseURL,
        model: config.model,
        api: "responses",
        endpoint: new URL("responses", config.baseURL).href,
        sampling: "provider default; effective unknown",
        backend_revision: "unknown",
        billing:
          options.mode === "live" ? "unknown" : "not applicable: simulated",
      },
      runtime,
      budget: BUDGET,
      source_hashes: Object.fromEntries(
        AGENT_SOURCE_FILES.map((file) => [relative(ROOT, file), digest(file)]),
      ),
      source_integrity: "hashes are local observations, not signatures",
      capture_policy:
        "outbound JSON bodies and inbound public Responses projections; no headers/credentials/private reasoning; credential echoes are redacted if present",
      live_acceptance:
        options.mode === "live"
          ? "pending semantic review"
          : "not applicable: simulated",
    });
    // Factory does not send a request; only live completion invocation can do so.
    const provider =
      options.mode === "live"
        ? responsesProvider(config.baseURL, config.key)
        : undefined;
    for (const scene of SCENES) {
      const started = Date.now(),
        deadline = started + LIMITS.milliseconds;
      let sequence = 0;
      const events: JsonObject[] = [];
      archive.startEvents(scene.id);
      const record = (event: JsonObject) => {
        if (event.kind === "provider_request") providerRequests++;
        const now = Date.now();
        const tagged = {
          ...event,
          scene: scene.id,
          mode: options.mode,
          sequence: ++sequence,
          timestamp: new Date(now).toISOString(),
          elapsed_ms: now - started,
        };
        events.push(structuredClone(tagged));
        archive.record(scene.id, tagged);
      };
      record({
        kind: "scene_start",
        started_at: new Date(started).toISOString(),
        limits: LIMITS,
      });
      try {
        stage = `${scene.id}: discovery`;
        let active: ObservationSession | undefined;
        const work = withObservationSession(
          options.cli,
          options.artifact,
          scene.threshold,
          async (session) => {
            active = session;
            const discovered = await session.discover();
            assert(
              Date.now() <= deadline,
              "scenario deadline exceeded during discovery",
            );
            archive.save(`${scene.id}/setup.json`, {
              scene: scene.id,
              threshold: scene.threshold,
              manifest: discovered.manifest,
              manifest_sha256: discovered.manifest_sha256,
              binding: discovered.binding,
            });
            archive.save(`${scene.id}/model-context.json`, {
              system: OBSERVATION_SYSTEM,
              question: scene.question,
              tools: discovered.tools,
              resources: discovered.resources,
            });
            stage = `${scene.id}: model/tool loop`;
            const final = await agentLoop({
              model: config.model,
              system: OBSERVATION_SYSTEM,
              question: scene.question,
              resources: discovered.resources,
              tools: discovered.tools,
              limits: { ...LIMITS, milliseconds: deadline - Date.now() },
              complete: (body, signal) =>
                options.mode === "simulated"
                  ? options.complete!(scene, body, signal)
                  : provider!(body, signal),
              call: async (name, args) => {
                const reply = await session.call(
                  name,
                  args as Record<string, string>,
                );
                if (!reply.error && !reply.result?.isError) {
                  const blocks = reply.result?.content as { text: string }[];
                  assert.equal(blocks.length, 1);
                  if (name === "population_compile") {
                    const [compiled] = JSON.parse(blocks[0].text);
                    assert.equal(compiled.executed, false);
                    const { binding } = await import(
                      "./observation_evidence.ts"
                    );
                    assert.deepEqual(binding(compiled), discovered.binding);
                  } else
                    assertResultBinding(
                      populationResult(blocks[0].text),
                      discovered.binding,
                    );
                }
                return reply;
              },
              record,
            });
            stage = `${scene.id}: evaluation`;
            assert(
              Date.now() <= deadline,
              "scenario deadline exceeded before evaluation",
            );
            const evaluation = checkObservationAnswer(final.content, events, {
              scene: scene.id,
              threshold: scene.threshold,
              binding: discovered.binding,
              input,
            });
            record({ kind: "evaluation", value: evaluation });
            record({
              kind: "scene_end",
              status: "complete",
              calls: final.calls,
              rounds: final.rounds,
            });
            archive.save(`${scene.id}/review.json`, {
              mode: options.mode,
              scene: scene.id,
              final,
              evaluation,
              semantic_verdict: "pending",
              manual_checks: [
                "object/scope/unit/registered rule and denominators",
                "actual response and column references",
                "interval distinct is not sum of daily counts",
                "registered capability refusal without replacement",
              ],
            });
            scenes.push({
              scene: scene.id,
              status: "complete",
              calls: final.calls,
              rounds: final.rounds,
              elapsed_ms: Date.now() - started,
              evaluation,
            });
          },
          record,
        );
        try {
          await beforeDeadline(deadline, () => work);
        } catch (error) {
          await active?.server.close();
          await work.catch(() => {});
          throw error;
        }
      } catch (error) {
        const failure = safeFailure(error, stage);
        record({ kind: "scene_failure", failure });
        archive.save(`${scene.id}/failure.json`, failure);
        scenes.push({
          scene: scene.id,
          status: "failed",
          elapsed_ms: Date.now() - started,
          failure,
        });
        throw error;
      }
    }
    terminal = "complete";
  } catch (error) {
    // If runtime setup failed, still create mode metadata so the failed archive can be sealed.
    if (!scenes.length && stage === "runtime setup") {
      try {
        archive.save("run.json", {
          schema: "ossie-observation-agent-run-v1",
          mode: options.mode,
          provider: { baseURL: config.baseURL, model: config.model },
          setup_failed: true,
        });
      } catch {
        /* Earlier run.json is immutable. */
      }
    }
    archive.save("failure.json", safeFailure(error, stage));
  } finally {
    archive.save("summary.json", {
      mode: options.mode,
      status: terminal,
      overall: terminal === "complete" ? "pending semantic review" : "failed",
      provider_requests: providerRequests,
      model_requests: options.mode === "live" ? providerRequests : 0,
      scenes,
      scope:
        options.mode === "simulated"
          ? "scripted completions with real MCP; not real model acceptance"
          : "bounded real run; semantic review remains separate",
    });
  }
  const seal_sha256 = archive.seal();
  return {
    mode: options.mode,
    status: terminal,
    path: archive.path,
    seal_sha256,
  };
}
if (import.meta.main) {
  try {
    const result = await executeObservation({
      mode: "live",
      ...liveArguments(process.argv.slice(2)),
    });
    console.log(
      `Private observation experiment sealed: ${result.status}; semantic review remains separate`,
    );
    if (result.status !== "complete") process.exitCode = 1;
  } catch {
    console.error(
      "Observation run stopped; inspect private archive if created. No automatic retry.",
    );
    process.exitCode = 1;
  }
}

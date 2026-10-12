import {
  CLEANUP_POLICY,
  cleanupSteps,
  p1ResponseBoundary,
} from "./p1_agent_safety.ts";
import { digest } from "./artifact_helpers.ts";
import { existsSync } from "node:fs";
/** Prepare + simulated orchestration only. There is deliberately no --run/live branch. */
import assert from "node:assert/strict";
import { resolve } from "node:path";
import {
  agentLoop,
  responsesProvider,
  beforeDeadline,
  type JsonObject,
  type Completion,
} from "./agent_loop.ts";
import { PrivateArchive } from "./observation_agent_archive.ts";
import { withFactSession, factRuntime } from "./fact_mcp.ts";
import { withStateSession, stateRuntime } from "./daily_state_mcp.ts";
import { withHistorySession, historyRuntime } from "./history_mcp.ts";
import {
  SCENES,
  LIMITS,
  INBOUND_BYTES,
  context,
  validateArguments,
  decode,
  evaluate,
  sourceHashes,
  fixtureDefinition,
  type Scene,
  type Discovery,
} from "./p1_agent_contract.ts";
import {
  scriptedSimulation,
  abortable,
  DEFINITIONS,
  type Simulation,
} from "./p1_agent_simulation.ts";
export type Review = "scripted_pass" | "pending" | "fail";
export function validateCompletion(
  response: Completion,
  tools: Discovery["tools"],
) {
  for (const call of response.message.tool_calls ?? [])
    validateArguments(call.function.name, call.function.arguments, tools);
}
export async function executeP1(options: {
  mode: "prepared" | "simulated";
  cli: string;
  artifact: string;
  out: string;
  simulate?: Simulation;
  review?: (scene: Scene, final: JsonObject) => Review;
  limits?: typeof LIMITS;
  inboundBytes?: number;
  cleanupFault?: () => Promise<void>;
}) {
  assert(["prepared", "simulated"].includes(options.mode));
  assert(
    options.mode === "simulated" || (!options.simulate && !options.review),
  );
  const limits = options.limits ?? LIMITS;
  for (const k of Object.keys(LIMITS) as (keyof typeof LIMITS)[])
    assert(
      Number.isInteger(limits[k]) && limits[k] >= 0 && limits[k] <= LIMITS[k],
    );
  assert(
    Number.isInteger(options.inboundBytes ?? INBOUND_BYTES) &&
      (options.inboundBytes ?? INBOUND_BYTES) > 0 &&
      (options.inboundBytes ?? INBOUND_BYTES) <= INBOUND_BYTES,
  );
  const archive = PrivateArchive.create(options.out);
  let runtimes: { facts: JsonObject; states: JsonObject; history: JsonObject };
  try {
    runtimes = {
      facts: factRuntime(options.cli, options.artifact),
      states: stateRuntime(options.cli, options.artifact),
      history: historyRuntime(options.cli, options.artifact),
    };
  } catch (error) {
    const failure = {
      stage: "runtime preflight",
      category: error instanceof Error ? error.name : "Error",
      reason:
        error instanceof Error
          ? error.message.split("\n")[0]
          : "Unknown startup error",
    };
    archive.save("run.json", {
      schema: "ossie-p1-agent-run-v1",
      mode: options.mode,
      real_model_requests: 0,
      provider_config_read: false,
      model: "simulated-p1",
      limits,
      cleanup_policy: CLEANUP_POLICY,
      inbound_bytes: options.inboundBytes ?? INBOUND_BYTES,
      scenes: SCENES,
      source_hashes: sourceHashes(),
      startup_failure: failure,
      declared_artifact_sha256: existsSync(options.artifact)
        ? digest(options.artifact)
        : null,
    });
    archive.save("failure.json", failure);
    const result = {
      schema: "ossie-p1-agent-summary-v1",
      mode: options.mode,
      status: "failed",
      scenes: [] as JsonObject[],
      real_model_requests: 0,
      provider_config_read: false,
    };
    archive.save("summary.json", result);
    return { ...result, seal_sha256: archive.seal() };
  }
  archive.save("run.json", {
    schema: "ossie-p1-agent-run-v1",
    mode: options.mode,
    real_model_requests: 0,
    provider_config_read: false,
    limits,
    cleanup_policy: CLEANUP_POLICY,
    inbound_bytes: options.inboundBytes ?? INBOUND_BYTES,
    source_hashes: sourceHashes(),
    runtimes,
    scenes: SCENES,
    model: "simulated-p1",
    review_mode: "scripted; no real semantic acceptance",
  });
  const summary: JsonObject[] = [];
  let status = "complete";
  for (const scene of SCENES) {
    const started = Date.now();
    const deadline = started + limits.milliseconds;
    let acceptEvents = true;
    let work: Promise<JsonObject> | undefined;
    let sequence = 0;
    const events: JsonObject[] = [];
    archive.startEvents(scene);
    const record = (e: JsonObject) => {
      if (!acceptEvents) return;
      const row = {
        ...e,
        scene,
        sequence: ++sequence,
        elapsed_ms: Date.now() - started,
      };
      events.push(structuredClone(row));
      archive.record(scene, row);
    };
    let active: { close: () => Promise<void> } | undefined;
    let stage = "discovery";
    try {
      const action = async (session: {
        discover: () => Promise<Discovery>;
        call: (name: string, args: JsonObject) => Promise<unknown>;
        server: { close: () => Promise<void> };
      }) => {
        active = session.server;
        const d = await session.discover();
        fixtureDefinition(scene, d);
        assert(Date.now() <= deadline, "scene deadline exceeded");
        const c = context(scene, d);
        archive.save(scene + "/context.json", c);
        archive.save(scene + "/setup.json", {
          scene,
          program: d.program,
          binding: d.binding,
        });
        record({ kind: "context", context: c });
        if (options.mode === "prepared") {
          record({ kind: "prepared" });
          return { status: "prepared" };
        }
        stage = "loop";
        const simulation = options.simulate ?? scriptedSimulation();
        const provider = responsesProvider(
          "https://simulated.invalid/v1/",
          "",
          async (_url, init) => {
            const raw = await abortable(
              Promise.resolve(
                simulation(scene, JSON.parse(String(init?.body)), events),
              ),
              init!.signal as AbortSignal,
            );
            (init!.signal as AbortSignal).throwIfAborted();
            assert(acceptEvents, "scene terminated");
            record({
              kind: "simulation_response",
              recording_scope: "synthetic_only; never raw real Responses",
              encoding: "base64",
              body: Buffer.from(raw)
                .subarray(0, (options.inboundBytes ?? INBOUND_BYTES) + 1)
                .toString("base64"),
              truncated:
                Buffer.byteLength(raw) >
                (options.inboundBytes ?? INBOUND_BYTES) + 1,
            });
            return p1ResponseBoundary(
              new Response(raw),
              options.inboundBytes ?? INBOUND_BYTES,
              init!.signal as AbortSignal,
            );
          },
        );
        const final = await agentLoop({
          model: "simulated-p1",
          system: c.system,
          question: c.question,
          resources: c.resources,
          tools: c.tools,
          limits: { ...limits, milliseconds: deadline - Date.now() },
          complete: provider,
          record: (e) => {
            record(e);
            if (e.kind === "provider_response")
              validateCompletion(e.response as unknown as Completion, d.tools);
          },
          call: async (name, args) => {
            assert(acceptEvents && Date.now() <= deadline, "scene terminated");
            const reply = await session.call(name, args);
            decode(name, args, reply, d);
            return reply;
          },
        });
        stage = "evaluation";
        assert(Date.now() <= deadline, "scene deadline exceeded");
        const evaluation = evaluate(scene, final.content, events, d);
        let review: Review = "pending";
        if (evaluation.status === "mechanical_pass") {
          const a = JSON.parse(final.content) as JsonObject;
          review = options.review
            ? options.review(scene, a)
            : a.definition === DEFINITIONS[scene]
              ? "scripted_pass"
              : "pending";
        }
        const result = {
          ...evaluation,
          review,
          calls: final.calls,
          rounds: final.rounds,
        };
        record({ kind: "evaluation", result });
        archive.save(scene + "/evaluation.json", result);
        return result;
      };
      const callback = (e: JsonObject) => record(e);
      work =
        scene === "facts"
          ? withFactSession(
              options.cli,
              options.artifact,
              "original",
              action,
              callback,
            )
          : scene === "states"
            ? withStateSession(
                options.cli,
                options.artifact,
                10,
                "extended",
                action,
                callback,
              )
            : withHistorySession(
                options.cli,
                options.artifact,
                "extended",
                action,
                callback,
              );
      const result = await beforeDeadline(deadline, () => work!);
      summary.push({ scene, ...result });
      if (
        options.mode === "simulated" &&
        (result.status !== "mechanical_pass" ||
          (result as JsonObject).review !== "scripted_pass")
      ) {
        status =
          result.status === "task_failed" ||
          (result as JsonObject).review === "fail"
            ? "failed"
            : "pending";
        break;
      }
    } catch (error) {
      status = "failed";
      const failure = {
        stage,
        category: error instanceof Error ? error.name : "Error",
        reason:
          error instanceof Error
            ? error.message.split("\n")[0]
            : "Unknown error",
      };
      record({ kind: "failure", failure });
      archive.save(scene + "/failure.json", failure);
      summary.push({
        scene,
        status: stage === "evaluation" ? "task_failed" : "failed",
        failure,
      });
      acceptEvents = false;
      break;
    } finally {
      acceptEvents = false;
      try {
        const steps: (() => Promise<unknown>)[] = [];
        if (active) steps.push(() => active!.close());
        if (options.cleanupFault) steps.push(options.cleanupFault);
        if (work) steps.push(() => work!.catch(() => undefined));
        await cleanupSteps(steps);
      } catch (error) {
        status = "failed";
        const failure = {
          stage: "cleanup",
          category: error instanceof Error ? error.name : "Error",
          reason:
            error instanceof Error
              ? error.message.split("\n")[0]
              : "Unknown cleanup error",
        };
        archive.record(scene, {
          kind: "cleanup_failure",
          scene,
          sequence: ++sequence,
          elapsed_ms: Date.now() - started,
          failure,
        });
        archive.save(scene + "/cleanup-failure.json", failure);
        summary.at(-1)!.cleanup_failure = failure;
      }
    }
    if (status === "failed") break;
  }
  const result = {
    schema: "ossie-p1-agent-summary-v1",
    mode: options.mode,
    status,
    scenes: summary,
    real_model_requests: 0,
    provider_config_read: false,
  };
  archive.save("summary.json", result);
  const seal = archive.seal();
  return { ...result, seal_sha256: seal };
}
export function cliArguments(args: string[]) {
  assert(
    args.length === 7 &&
      ["--prepare", "--rehearse"].includes(args[0]) &&
      args[1] === "--cli" &&
      args[3] === "--extension" &&
      args[5] === "--out",
    "Usage: p1_agent.ts --prepare|--rehearse --cli PATH --extension PATH --out NEW_PRIVATE_DIR; no live mode",
  );
  return {
    mode:
      args[0] === "--prepare" ? ("prepared" as const) : ("simulated" as const),
    cli: resolve(args[2]),
    artifact: resolve(args[4]),
    out: resolve(args[6]),
  };
}
if (import.meta.main) {
  const result = await executeP1(cliArguments(process.argv.slice(2)));
  console.log(JSON.stringify(result, null, 2));
  if (result.status !== "complete") process.exitCode = 1;
}

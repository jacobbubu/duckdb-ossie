import {
  p1Configuration,
  p1Transport,
  p1ProtectedFailure,
  P1ProviderFailure,
  type P1Request,
} from "./p1_agent_provider.ts";
import {
  CLEANUP_POLICY,
  cleanupSteps,
  p1ResponseBoundary,
} from "./p1_agent_safety.ts";
import { digest } from "./artifact_helpers.ts";
import { Deadline, P1_TIMING } from "./agent_deadline.ts";
/** Prepare + simulated orchestration only. There is deliberately no --run/live branch. */
import assert from "node:assert/strict";
import { resolve } from "node:path";
import {
  agentLoop,
  responsesProvider,
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
type P1Base = {
  cli: string;
  artifact: string;
  out: string;
  limits?: typeof LIMITS;
  inboundBytes?: number;
};
export type P1Options = P1Base &
  (
    | {
        mode: "prepared" | "simulated";
        simulate?: Simulation;
        review?: (scene: Scene, final: JsonObject) => Review;
        cleanupFault?: () => Promise<void>;
        scene?: never;
        env?: never;
        transport?: never;
      }
    | {
        mode: "live";
        scene: Scene;
        env?: Record<string, string | undefined>;
        transport?: never;
        simulate?: never;
        review?: never;
        cleanupFault?: never;
      }
    | {
        mode: "transport-rehearsal";
        scene: Scene;
        env: Record<string, string | undefined>;
        transport: P1Request;
        simulate?: never;
        review?: never;
        cleanupFault?: never;
      }
  );
export async function executeP1(options: P1Options) {
  const protectedMode =
    options.mode === "live" || options.mode === "transport-rehearsal";
  assert(
    ["prepared", "simulated", "live", "transport-rehearsal"].includes(
      options.mode,
    ),
  );
  if (protectedMode) {
    assert(SCENES.includes(options.scene!));
    assert(
      !options.simulate && !options.review && !options.cleanupFault,
      "scripted overrides refused for transport modes",
    );
    assert(
      options.mode === "live"
        ? !options.transport
        : typeof options.transport === "function" && !!options.env,
      "transport mode isolation",
    );
  } else {
    assert(!options.env && !options.transport && !options.scene);
    assert(
      options.mode === "simulated" || (!options.simulate && !options.review),
    );
  }
  const selected = protectedMode ? [options.scene!] : SCENES;
  let config: ReturnType<typeof p1Configuration> | undefined,
    configFailure: unknown;
  if (protectedMode)
    try {
      config = p1Configuration(
        options.mode === "live" ? (options.env ?? process.env) : options.env!,
      );
    } catch {
      configFailure = new P1ProviderFailure("configuration_failure");
    }
  const model =
    config?.model ?? (protectedMode ? "unavailable" : "simulated-p1");
  const providerConfigRead = options.mode === "live";
  let transportAttempts = 0,
    constructedRequests = 0,
    receivedResponses = 0,
    projectedResponses = 0;
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
  const archive = PrivateArchive.create(
    options.out,
    config ? [config.key] : [],
  );
  let runtimes: { facts: JsonObject; states: JsonObject; history: JsonObject };
  try {
    if (configFailure) throw configFailure;
    runtimes = {
      facts: factRuntime(options.cli, options.artifact),
      states: stateRuntime(options.cli, options.artifact),
      history: historyRuntime(options.cli, options.artifact),
    };
  } catch (error) {
    const failure = protectedMode
      ? p1ProtectedFailure(
          error,
          configFailure ? "configuration" : "runtime preflight",
        )
      : {
          stage: "runtime preflight",
          category: error instanceof Error ? error.name : "Error",
          reason:
            error instanceof Error
              ? error.message.split("\n")[0]
              : "Unknown startup error",
        };
    archive.save("run.json", {
      schema: "ossie-p1-agent-run-v1",
      timing: P1_TIMING,
      mode: options.mode,
      real_model_requests: 0,
      provider_config_read: providerConfigRead,
      model,
      limits,
      cleanup_policy: CLEANUP_POLICY,
      inbound_bytes: options.inboundBytes ?? INBOUND_BYTES,
      scenes: selected,
      source_hashes: sourceHashes(),
      request_count_scope: "initial zero; final attempt counts in summary",
      startup_failure: failure,
      declared_artifact_sha256: (() => {
        try {
          return digest(options.artifact);
        } catch {
          return null;
        }
      })(),
    });
    archive.save("failure.json", failure);
    const result = {
      schema: "ossie-p1-agent-summary-v1",
      mode: options.mode,
      status: "failed",
      scenes: [] as JsonObject[],
      real_model_requests: 0,
      constructed_requests: 0,
      transport_attempts: 0,
      received_responses: 0,
      projected_responses: 0,
      provider_config_read: providerConfigRead,
    };
    archive.save("summary.json", result);
    return { ...result, seal_sha256: archive.seal() };
  }
  archive.save("run.json", {
    schema: "ossie-p1-agent-run-v1",
    timing: P1_TIMING,
    mode: options.mode,
    real_model_requests: 0,
    provider_config_read: providerConfigRead,
    limits,
    cleanup_policy: CLEANUP_POLICY,
    inbound_bytes: options.inboundBytes ?? INBOUND_BYTES,
    source_hashes: sourceHashes(),
    runtimes,
    scenes: selected,
    model,
    review_mode: protectedMode
      ? "pending semantic review; no scripted approval"
      : "scripted; no real semantic acceptance",
    request_count_scope: "initial zero; final attempt counts in summary",
    ...(config
      ? {
          provider: {
            baseURL: config.baseURL,
            api: "responses",
            retention: "unknown",
            billing: "unknown",
          },
        }
      : {}),
  });
  const summary: JsonObject[] = [];
  let status = "complete";
  for (const scene of selected) {
    const deadline = new Deadline(limits.milliseconds);
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
        elapsed_ms: deadline.elapsed(),
      };
      if (e.kind === "provider_request") constructedRequests++;
      if (e.kind === "transport_attempt") transportAttempts++;
      if (e.kind === "transport_response") receivedResponses++;
      if (e.kind === "provider_projection") projectedResponses++;
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
        deadline.assertActive();
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
        const provider = protectedMode
          ? responsesProvider(
              config!.baseURL,
              config!.key,
              p1Transport(
                config!,
                options.mode === "live" ? fetch : options.transport!,
                record,
                options.inboundBytes ?? INBOUND_BYTES,
              ),
            )
          : responsesProvider(
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
          model,
          system: c.system,
          question: c.question,
          resources: c.resources,
          tools: c.tools,
          limits,
          deadline,
          complete: provider,
          record: (e) => {
            record(e);
            if (e.kind === "provider_response")
              validateCompletion(e.response as unknown as Completion, d.tools);
          },
          call: async (name, args) => {
            assert(acceptEvents, "scene terminated");
            deadline.assertActive();
            const reply = await session.call(name, args);
            decode(name, args, reply, d);
            return reply;
          },
        });
        stage = "evaluation";
        deadline.assertActive();
        const evaluation = evaluate(scene, final.content, events, d);
        let review: Review = "pending";
        if (!protectedMode && evaluation.status === "mechanical_pass") {
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
      const result = await deadline.wait(() => {
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
        return work;
      });
      summary.push({ scene, ...result });
      if (
        options.mode !== "prepared" &&
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
      const failure = protectedMode
        ? p1ProtectedFailure(error, stage)
        : {
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
        const failure = protectedMode
          ? p1ProtectedFailure(error, "cleanup")
          : {
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
          elapsed_ms: deadline.elapsed(),
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
    real_model_requests: options.mode === "live" ? transportAttempts : 0,
    transport_attempts: transportAttempts,
    constructed_requests: constructedRequests,
    received_responses: receivedResponses,
    projected_responses: projectedResponses,
    provider_config_read: providerConfigRead,
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

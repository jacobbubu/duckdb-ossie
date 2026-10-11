/** Scripted Responses + real MCP rehearsal. Never contacts a model or reads provider env. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { executeObservation } from "./observation_agent_run.ts";
import { replayObservation } from "./observation_agent_replay.ts";
import {
  exportObservationArchive,
  verifyArchive,
} from "./observation_agent_archive.ts";
import {
  responsesProvider,
  type Completion,
  type JsonObject,
} from "./agent_loop.ts";
import {
  SCENES,
  METRICS,
  aggregate,
  observationInput,
} from "./observation_evidence.ts";
export function scriptedResponses() {
  const rounds = new Map<string, number>();
  const input = observationInput();
  return async (
    scene: (typeof SCENES)[number],
    body: JsonObject,
    signal: AbortSignal,
  ): Promise<Completion> => {
    const round = (rounds.get(scene.id) ?? 0) + 1;
    rounds.set(scene.id, round);
    const tools = body.tools as JsonObject[];
    assert.equal(tools.length, 3);
    let output: JsonObject[];
    if (round === 1 && scene.id !== "unsupported-every-day") {
      const filters = JSON.stringify([
        "observations.ds >= DATE '2026-01-31' AND observations.ds < DATE '2026-02-02'",
      ]);
      const metrics = JSON.stringify(
        scene.id === "interval-users" ? ["user_count"] : METRICS,
      );
      const calls =
        scene.id === "interval-users" ? ["[]", '["observations.ds"]'] : ["[]"];
      output = calls.map((dimensions, i) => ({
        type: "function_call",
        id: `item-${scene.id}-${i}`,
        call_id: `call-${scene.id}-${i}`,
        name: "population_query",
        arguments: JSON.stringify({
          concept: "QualifiedObservation",
          metrics,
          dimensions,
          filters,
        }),
        status: "completed",
      }));
    } else {
      const unsupported = scene.id === "unsupported-every-day",
        interval = scene.id === "interval-users";
      const selected = input.filter((r) => r.classification >= scene.threshold);
      const values = aggregate(selected);
      const numerical = Object.fromEntries(
        METRICS.map((m) => [
          m,
          unsupported || (interval && m !== "user_count") ? null : values[m],
        ]),
      );
      const answer = {
        status: unsupported ? "unsupported" : "answered",
        scope: "2026-01-31 through 2026-02-01 inclusive; all products",
        definition: unsupported
          ? "every-day User qualification is not registered; no existential replacement"
          : "current observation qualification; User is existential distinct in requested scope; AVG uses nonnull observation amounts",
        unit: unsupported || interval ? null : "USD",
        ...numerical,
        daily_user_counts: interval
          ? Object.fromEntries(
              ["2026-01-31", "2026-02-01"].map((day) => [
                day,
                aggregate(selected.filter((r) => r.date === day)).user_count,
              ]),
            )
          : null,
        evidence_ids: [
          "ossie://population/concepts",
          ...(unsupported ? [] : interval ? ["tool-1", "tool-2"] : ["tool-1"]),
        ],
        limitations: [
          "scripted test response with oracle-derived answer; not model autonomy or business acceptance",
        ],
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
            model: "simulated-responses",
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
export async function verifyOfflineExperiment(
  cli: string,
  artifact: string,
  privateOut: string,
  publicOut: string,
) {
  const execution = await executeObservation({
    mode: "simulated",
    cli,
    artifact,
    out: privateOut,
    complete: scriptedResponses(),
  });
  assert.equal(
    execution.status,
    "complete",
    "inspect sealed simulated failure",
  );
  const replay = await replayObservation(privateOut);
  assert.equal(replay.mode, "simulated");
  assert.equal(replay.overall, "pending semantic review");
  const before = verifyArchive(privateOut);
  const exported = exportObservationArchive(privateOut, publicOut);
  assert.equal(verifyArchive(privateOut).seal_sha256, before.seal_sha256);
  const derivative = JSON.parse(
    readFileSync(join(publicOut, "derivative.json"), "utf8"),
  );
  assert.equal(derivative.mode, "simulated");
  assert.equal(derivative.exact_original_request_bytes, false);
  const failedPath = privateOut + "-incomplete";
  const incomplete = responsesProvider(
    "https://simulated.invalid/v1/",
    "synthetic-unused",
    async () =>
      new Response(
        JSON.stringify({
          id: "incomplete-response",
          model: "simulated-responses",
          status: "incomplete",
          incomplete_details: { reason: "max_output_tokens" },
          output: [],
          usage: { input_tokens: 0, output_tokens: 0, total_tokens: 0 },
        }),
        { status: 200 },
      ),
  );
  const failure = await executeObservation({
    mode: "simulated",
    cli,
    artifact,
    out: failedPath,
    complete: (_scene, body, signal) => incomplete(body, signal),
  });
  assert.equal(failure.status, "failed");
  verifyArchive(failedPath);
  await assert.rejects(
    () => replayObservation(failedPath),
    /failed\/incomplete/,
  );
  const failedSummary = JSON.parse(
    readFileSync(join(failedPath, "summary.json"), "utf8"),
  );
  assert.equal(failedSummary.provider_requests, 1);
  assert.equal(failedSummary.model_requests, 0);
  const failedExport = exportObservationArchive(
    failedPath,
    publicOut + "-incomplete",
  );
  return {
    status: "offline rehearsal complete",
    mode: "simulated",
    real_model_requests: 0,
    execution,
    sealed_incomplete_control: {
      failure,
      failedExport,
      replay: "refused complete replay",
      provider_requests: 1,
      real_model_requests: 0,
    },
    replay,
    exported,
    scope:
      "scripted completions with real MCP, integrity and exact ordered replay; not Agent acceptance",
  };
}
if (import.meta.main) {
  const args = process.argv.slice(2);
  assert(
    args.length === 10 &&
      args[0] === "--cli" &&
      args[2] === "--extension" &&
      args[4] === "--private-out" &&
      args[6] === "--public-out" &&
      args[8] === "--out",
    "Usage: bun scripts/verify_observation_agent_offline.ts --cli PATH --extension PATH --private-out NEW_IGNORED_DIR --public-out NEW_DIR --out NEW_JSON",
  );
  const out = resolve(args[9]);
  assert(!existsSync(out), "fresh report required");
  try {
    const report = await verifyOfflineExperiment(
      resolve(args[1]),
      resolve(args[3]),
      resolve(args[5]),
      resolve(args[7]),
    );
    writeFileSync(out, JSON.stringify(report, null, 2) + "\n", { flag: "wx" });
    console.log(
      "Four simulated scenes, actual MCP, offline replay and derivative export passed; real model requests: 0",
    );
  } catch {
    writeFileSync(
      out,
      JSON.stringify(
        {
          status: "failed",
          mode: "simulated",
          real_model_requests: 0,
          details: "preserved private archive; no retry",
        },
        null,
        2,
      ) + "\n",
      { flag: "wx" },
    );
    console.error(
      "Offline rehearsal failed; inspect preserved private evidence",
    );
    process.exitCode = 1;
  }
}

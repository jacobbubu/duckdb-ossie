/** Ordered offline replay: no provider config, fetch, subprocess or MCP startup. */
import assert from "node:assert/strict";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { ROOT, digest } from "./artifact_helpers.ts";
import {
  verifyArchive,
  exportObservationArchive,
  writeReplayReport,
} from "./observation_agent_archive.ts";
import { SOURCE_NAMES, BUDGET } from "./observation_agent_contract.ts";
import {
  SCENES,
  OBSERVATION_SYSTEM,
  observationInput,
  checkObservationAnswer,
  assertResultBinding,
  binding,
} from "./observation_evidence.ts";
import { ObservationSession } from "./observation_mcp.ts";
import type { RpcProcess } from "./mcp_check.ts";
import {
  agentLoop,
  responsesProvider,
  providerBaseURL,
  LIMITS,
  type Completion,
  type JsonObject,
} from "./agent_loop.ts";
import { populationResult } from "./population_result.ts";
const jsonValue = (value: unknown) =>
  value === undefined ? undefined : JSON.parse(JSON.stringify(value));
/** Derive normalized completion again from the saved public Responses projection. */
export async function completionFromProjection(saved: Completion) {
  assert(
    Array.isArray(saved.public_output),
    "public response projection required for replay",
  );
  let index = -1;
  for (const item of saved.public_output) {
    assert(
      Number.isSafeInteger(item.output_index) &&
        Number(item.output_index) > index,
      "ordered unique public output indices required",
    );
    index = Number(item.output_index);
    assert(
      ["function_call", "message"].includes(String(item.type)),
      "unprojected output item refused",
    );
  }
  const parser = responsesProvider(
    "https://replay.invalid/v1/",
    "offline-unused",
    async () =>
      new Response(
        JSON.stringify({
          id: saved.id,
          model: saved.model,
          usage: saved.usage,
          status: saved.response_status,
          output: saved.public_output,
          incomplete_details: saved.incomplete_details,
          error: saved.error_code ? { code: saved.error_code } : undefined,
        }),
        { status: 200 },
      ),
  );
  const canonical = await parser({}, new AbortController().signal);
  for (const key of [
    "id",
    "model",
    "usage",
    "message",
    "finish_reason",
    "response_status",
    "incomplete_details",
    "error_code",
  ] as const)
    assert.deepEqual(
      jsonValue(canonical[key]),
      jsonValue(saved[key]),
      `public projection disagrees with normalized ${key}`,
    );
  const withoutIndex = (items: JsonObject[]) =>
    items.map(({ output_index: _, ...rest }) => rest);
  assert.deepEqual(
    jsonValue(withoutIndex(canonical.public_output!)),
    jsonValue(withoutIndex(saved.public_output)),
    "public projection shape changed",
  );
  return saved;
}
function bare(event: JsonObject) {
  const {
    scene: _,
    mode: __,
    sequence: ___,
    timestamp: ____,
    elapsed_ms: _____,
    capture_redacted: ______,
    ...value
  } = event;
  return value;
}
export async function replayObservation(path: string) {
  const verified = verifyArchive(path);
  const read = (name: string) =>
    JSON.parse(readFileSync(join(verified.path, name), "utf8"));
  const run = read("run.json"),
    summary = read("summary.json");
  assert.equal(run.schema, "ossie-observation-agent-run-v1");
  assert(["live", "simulated"].includes(run.mode));
  assert.equal(summary.mode, run.mode);
  assert.equal(
    summary.status,
    "complete",
    "failed/incomplete archives cannot be a complete replay",
  );
  assert.equal(summary.overall, "pending semantic review");
  assert(
    typeof run.provider.model === "string" && run.provider.model.length > 0,
  );
  assert.equal(providerBaseURL(run.provider.baseURL), run.provider.baseURL);
  assert.deepEqual(run.budget, BUDGET, "archived budget changed");
  assert.deepEqual(
    Object.keys(run.source_hashes).sort(),
    [...SOURCE_NAMES].sort(),
    "source inventory mismatch",
  );
  for (const name of SOURCE_NAMES)
    assert.equal(
      digest(join(ROOT, name)),
      run.source_hashes[name],
      "source/fixture mismatch; never execute archived source",
    );
  assert.deepEqual(
    summary.scenes.map((s: JsonObject) => s.scene),
    SCENES.map((s) => s.id),
    "four ordered scenes required",
  );
  const expectedFiles = [
    "run.json",
    "summary.json",
    ...SCENES.flatMap((s) =>
      ["events.jsonl", "model-context.json", "setup.json", "review.json"].map(
        (name) => `${s.id}/${name}`,
      ),
    ),
  ].sort();
  assert.deepEqual(
    Object.keys(verified.files).sort(),
    expectedFiles,
    "complete experiment file inventory mismatch",
  );
  const input = observationInput();
  const results: unknown[] = [];
  let totalRequests = 0;
  let redactedEvents = 0;
  for (const scene of SCENES) {
    const events = readFileSync(
      join(verified.path, scene.id, "events.jsonl"),
      "utf8",
    )
      .trim()
      .split("\n")
      .map((line) => JSON.parse(line) as JsonObject);
    let elapsed = -1;
    for (const [i, event] of events.entries()) {
      assert.equal(event.scene, scene.id);
      assert.equal(event.mode, run.mode);
      assert.equal(event.sequence, i + 1);
      if (event.capture_redacted !== undefined) {
        assert.equal(event.capture_redacted, true);
        redactedEvents++;
      }
      assert(
        typeof event.elapsed_ms === "number" &&
          Number.isFinite(event.elapsed_ms) &&
          event.elapsed_ms >= elapsed &&
          event.elapsed_ms <= LIMITS.milliseconds,
        "invalid original event timing",
      );
      elapsed = event.elapsed_ms;
      assert(
        typeof event.timestamp === "string" &&
          Number.isFinite(Date.parse(event.timestamp)),
      );
    }
    let cursor = 0;
    const peek = (kind: string) => {
      const event = events[cursor];
      assert(
        event && event.kind === kind,
        `ordered replay expected ${kind} at ${cursor}`,
      );
      return event;
    };
    const consume = (event: JsonObject) => {
      assert.deepEqual(
        bare(peek(String(event.kind))),
        jsonValue(event),
        "ordered event content mismatch",
      );
      cursor++;
    };
    const start = peek("scene_start");
    assert.deepEqual(start.limits, LIMITS);
    assert(typeof start.started_at === "string");
    const started = Date.parse(start.started_at);
    for (const event of events)
      assert.equal(
        Date.parse(String(event.timestamp)) - started,
        event.elapsed_ms,
        "original timestamp/elapsed mismatch",
      );
    cursor++;
    const fake = {
      request: async (id: number) => {
        const reply = peek("rpc_response").reply as JsonObject;
        assert.equal(reply.id, id, "RPC response id mismatch");
        assert.equal(reply.jsonrpc, "2.0");
        return reply;
      },
      send: () => {},
    } as unknown as RpcProcess;
    const session = new ObservationSession(fake, scene.threshold, consume);
    const d = await session.discover();
    assert.deepEqual(
      read(`${scene.id}/setup.json`),
      {
        scene: scene.id,
        threshold: scene.threshold,
        manifest: d.manifest,
        manifest_sha256: d.manifest_sha256,
        binding: d.binding,
      },
      "setup binding mismatch",
    );
    assert.deepEqual(
      read(`${scene.id}/model-context.json`),
      {
        system: OBSERVATION_SYSTEM,
        question: scene.question,
        tools: d.tools,
        resources: d.resources,
      },
      "model context does not match actual discovery and fixed question",
    );
    const final = await agentLoop({
      model: run.provider.model,
      system: OBSERVATION_SYSTEM,
      question: scene.question,
      resources: d.resources,
      tools: d.tools,
      limits: LIMITS,
      record: consume,
      complete: async () =>
        completionFromProjection(
          peek("provider_response").response as unknown as Completion,
        ),
      call: async (name, args) => {
        const reply = await session.call(name, args as Record<string, string>);
        if (!reply.error && !reply.result?.isError) {
          const blocks = reply.result?.content as { text: string }[];
          assert.equal(blocks.length, 1);
          if (name === "population_compile") {
            const [compiled] = JSON.parse(blocks[0].text);
            assert.equal(compiled.executed, false);
            assert.deepEqual(binding(compiled), d.binding);
          } else
            assertResultBinding(populationResult(blocks[0].text), d.binding);
        }
        return reply;
      },
    });
    const evaluation = checkObservationAnswer(final.content, events, {
      scene: scene.id,
      threshold: scene.threshold,
      binding: d.binding,
      input,
    });
    consume({ kind: "evaluation", value: evaluation });
    consume({
      kind: "scene_end",
      status: "complete",
      calls: final.calls,
      rounds: final.rounds,
    });
    assert.equal(cursor, events.length, "extra/unpaired trailing events");
    const review = read(`${scene.id}/review.json`);
    assert.equal(review.mode, run.mode);
    assert.equal(review.scene, scene.id);
    assert.equal(review.semantic_verdict, "pending");
    assert.deepEqual(review.final, final);
    assert.deepEqual(review.evaluation, evaluation);
    const recorded = summary.scenes.find(
      (s: JsonObject) => s.scene === scene.id,
    );
    assert.equal(recorded.status, "complete");
    assert.equal(recorded.calls, final.calls);
    assert.equal(recorded.rounds, final.rounds);
    assert.deepEqual(recorded.evaluation, evaluation);
    assert(
      typeof recorded.elapsed_ms === "number" &&
        recorded.elapsed_ms >= elapsed &&
        recorded.elapsed_ms <= LIMITS.milliseconds,
      "invalid recorded scenario elapsed time",
    );
    totalRequests += final.rounds;
    results.push({
      scene: scene.id,
      calls: final.calls,
      rounds: final.rounds,
      evaluation,
      recorded_elapsed_ms: recorded.elapsed_ms,
    });
  }
  assert.equal(summary.provider_requests, totalRequests);
  assert.equal(summary.model_requests, run.mode === "live" ? totalRequests : 0);
  verifyArchive(path); // Catch changes during replay without modifying any originals.
  return {
    schema: "ossie-observation-agent-replay-v1",
    mode: run.mode,
    status: "internally consistent replay",
    overall: "pending semantic review",
    source_seal_sha256: verified.seal_sha256,
    provider_requests: totalRequests,
    live_model_requests_during_replay: 0,
    credential_redacted_events: redactedEvents,
    original_provider_request_byte_claim: redactedEvents
      ? "not claimed; credential echoes were redacted"
      : "serialized body consistency checked within projected archive; not proof of network delivery",
    scope:
      "checks archived ordered event/Responses/tool/definition correspondence; not provider authenticity or external execution proof",
    scenes: results,
  };
}
if (import.meta.main) {
  try {
    const args = process.argv.slice(2);
    assert(
      args.length === 4 &&
        ["--replay", "--export"].includes(args[0]) &&
        args[2] === "--out",
      "Usage: bun scripts/observation_agent_replay.ts --replay PRIVATE_DIR --out NEW_PRIVATE_JSON | --export PRIVATE_DIR --out NEW_PUBLIC_DIRECTORY",
    );
    if (args[0] === "--export") {
      const result = exportObservationArchive(args[1], args[3]);
      console.log(
        `Explicit derivative export: ${result.files} hash-linked files; raw unchanged`,
      );
    } else {
      const out = resolve(args[3]);
      assert(!existsSync(out), "fresh replay output required");
      const result = await replayObservation(args[1]);
      writeReplayReport(args[1], out, result);
      console.log(
        `Offline ${result.mode} replay is internally consistent; semantic review pending`,
      );
    }
  } catch {
    console.error(
      "Offline verification stopped; preserved raw archive was not changed.",
    );
    process.exitCode = 1;
  }
}

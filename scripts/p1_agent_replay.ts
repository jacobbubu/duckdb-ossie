import { CLEANUP_POLICY, p1ResponseBoundary } from "./p1_agent_safety.ts";
import { p1JSON as factJSON } from "./p1_agent_json.ts";
/** Offline only: recorded transport and completions, no server/config/provider access. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import {
  agentLoop,
  responsesProvider,
  type JsonObject,
  type Completion,
} from "./agent_loop.ts";
import { PrivateArchive, verifyArchive } from "./observation_agent_archive.ts";

import {
  SCENES,
  LIMITS,
  context,
  decode,
  evaluate,
  sourceHashes,
  fixtureDefinition,
  type Scene,
} from "./p1_agent_contract.ts";
import { DEFINITIONS } from "./p1_agent_simulation.ts";
import { FactSession } from "./fact_mcp.ts";
import { StateSession } from "./daily_state_mcp.ts";
import { HistorySession } from "./history_mcp.ts";
import type { RpcProcess } from "./mcp_check.ts";
import { validateCompletion } from "./p1_agent.ts";
export async function replayP1(path: string, artifactSha: string) {
  assert(/^[a-f0-9]{64}$/.test(artifactSha));
  const verified = verifyArchive(path);
  const read = (name: string) =>
    factJSON(
      readFileSync(join(verified.path, name), "utf8"),
      16 * 1048576,
    ) as JsonObject;
  const run = read("run.json"),
    summary = read("summary.json");
  assert(
    run.schema === "ossie-p1-agent-run-v1" &&
      ["prepared", "simulated"].includes(String(run.mode)) &&
      verified.mode === run.mode,
  );
  assert(
    run.real_model_requests === 0 &&
      run.provider_config_read === false &&
      run.model === "simulated-p1",
  );
  assert.deepEqual(
    run.source_hashes,
    sourceHashes(),
    "current source inventory mismatch",
  );
  assert.deepEqual(run.scenes, SCENES);
  assert.deepEqual(run.cleanup_policy, CLEANUP_POLICY);
  const limits = run.limits as typeof LIMITS;
  for (const key of Object.keys(LIMITS) as (keyof typeof LIMITS)[])
    assert(
      Number.isInteger(limits[key]) &&
        limits[key] >= 0 &&
        limits[key] <= LIMITS[key],
    );
  assert(
    Number.isInteger(run.inbound_bytes) &&
      Number(run.inbound_bytes) > 0 &&
      Number(run.inbound_bytes) <= 2097152,
  );
  if (run.startup_failure) {
    assert(
      summary.status === "failed" && (summary.scenes as unknown[]).length === 0,
    );
    assert.deepEqual(read("failure.json"), run.startup_failure);
    assert(
      run.declared_artifact_sha256 === artifactSha ||
        run.declared_artifact_sha256 === null,
    );
    return {
      schema: "ossie-p1-agent-replay-v1",
      mode: run.mode,
      status: "failed",
      diagnostic_only: true,
      scenes: 0,
      real_model_requests: 0,
      provider_config_read: false,
      source_seal_sha256: verified.seal_sha256,
      limitation:
        "Startup failure inventory only; no runtime or execution correspondence proof.",
    };
  }
  const runtimes = run.runtimes as Record<string, JsonObject>;
  for (const s of SCENES) {
    assert(runtimes[s].extension_sha256 === artifactSha);
    assert(
      runtimes[s].version === "v1.5.6" &&
        runtimes[s].source_id === "069cc9f9b5",
    );
  }
  assert(
    summary.schema === "ossie-p1-agent-summary-v1" &&
      summary.mode === run.mode &&
      summary.real_model_requests === 0 &&
      summary.provider_config_read === false,
  );
  const scenes = summary.scenes as JsonObject[];
  assert(scenes.length > 0 && scenes.length <= 3);
  const results: JsonObject[] = [];
  for (const [index, item] of scenes.entries()) {
    const scene = SCENES[index];
    assert(item.scene === scene);
    const text = readFileSync(
      join(verified.path, scene + "/events.jsonl"),
      "utf8",
    );
    const events = text
      .trim()
      .split("\n")
      .map((v) => factJSON(v, 16 * 1048576) as JsonObject);
    let elapsed = -1;
    events.forEach((e, i) => {
      assert(e.scene === scene && e.sequence === i + 1);
      assert(Number.isFinite(e.elapsed_ms) && Number(e.elapsed_ms) >= elapsed);
      elapsed = Number(e.elapsed_ms);
      if (
        summary.status === "complete" &&
        !["failure", "cleanup_failure"].includes(String(e.kind))
      )
        assert(elapsed <= limits.milliseconds, "event after deadline");
    });
    const requests = events.filter((e) => e.kind === "rpc_request"),
      responses = events.filter((e) => e.kind === "rpc_response");
    assert(new Set(requests.map((e) => e.id)).size === requests.length);
    assert.deepEqual(
      responses.map((e) => (e.reply as JsonObject).id),
      requests.slice(0, responses.length).map((e) => e.id),
      "RPC order/identity mismatch",
    );
    if (!events.some((e) => e.kind === "context")) {
      assert(index === scenes.length - 1 && summary.status === "failed");
      assert(
        !events.some((e) =>
          ["provider_request", "tool_request", "simulation_response"].includes(
            String(e.kind),
          ),
        ),
      );
      const failure = events.find((e) => e.kind === "failure")?.failure;
      assert(failure);
      assert.deepEqual(read(scene + "/failure.json"), failure);
      assert.deepEqual(item, { scene, status: "failed", failure });
      results.push(item);
      continue;
    }
    let discoveryCursor = 0;
    const fake = {
      request: async (id: number, method: string, params?: unknown) => {
        const request = events[discoveryCursor++];
        const {
          scene: _s,
          sequence: _seq,
          elapsed_ms: _time,
          ...payload
        } = request;
        assert.deepEqual(
          payload,
          JSON.parse(
            JSON.stringify({ kind: "rpc_request", id, method, params }),
          ),
          "discovery request mismatch",
        );
        const reply = events[discoveryCursor++];
        assert(
          reply.kind === "rpc_response" &&
            (reply.reply as JsonObject).id === id,
        );
        if (method === "resources/read")
          for (const block of ((reply.reply as JsonObject).result as JsonObject)
            .contents as JsonObject[])
            factJSON(String(block.text), 8388608);
        return reply.reply;
      },
      send: (notification: unknown) => {
        const e = events[discoveryCursor++];
        assert(e.kind === "rpc_notification");
        assert.deepEqual(e.notification, notification);
      },
    } as unknown as RpcProcess;
    const session =
      scene === "facts"
        ? new FactSession(fake)
        : scene === "states"
          ? new StateSession(fake)
          : new HistorySession(fake);
    const d = await session.discover();
    fixtureDefinition(scene, d);
    const c = context(scene, d);
    assert.deepEqual(
      read(scene + "/context.json"),
      c,
      "context differs from actual discovered resources/fixed prompts",
    );
    assert.deepEqual(read(scene + "/setup.json"), {
      scene,
      program: d.program,
      binding: d.binding,
    });
    const contextIndex = events.findIndex((e) => e.kind === "context");
    assert(contextIndex >= 0 && discoveryCursor === contextIndex);
    assert.deepEqual(events[contextIndex].context, c);
    const discovery = events.slice(0, contextIndex);
    const expectedDiscoveryRequests = scene === "states" ? 7 : 5;
    assert(
      discovery.filter((e) => e.kind === "rpc_request").length ===
        expectedDiscoveryRequests,
    );
    assert(discovery.filter((e) => e.kind === "rpc_notification").length === 1);
    assert(
      discovery
        .filter((e) => e.kind === "rpc_request")
        .every((e) =>
          [
            "initialize",
            "tools/list",
            "resources/list",
            "resources/read",
          ].includes(String(e.method)),
        ),
    );
    let cursor = contextIndex + 1;
    const take = (kind: string) => {
      const e = events[cursor++];
      assert(e?.kind === kind, "ordered event expected: " + kind);
      return e;
    };
    if (run.mode === "prepared") {
      take("prepared");
      assert(cursor === events.length);
      assert.deepEqual(item, { scene, status: "prepared" });
      results.push(item);
      continue;
    }
    const provider = responsesProvider(
      "https://simulated.invalid/v1/",
      "",
      async (_url, init) => {
        if (
          events[cursor]?.kind === "failure" &&
          Number(events[cursor].elapsed_ms) >= limits.milliseconds
        ) {
          const failure = events[cursor].failure as JsonObject;
          assert(
            [
              "scenario deadline exceeded",
              "This operation was aborted",
            ].includes(String(failure.reason)),
          );
          const error = new Error(String(failure.reason));
          error.name = String(failure.category);
          throw error;
        }
        const input = take("simulation_response");
        assert(
          input.recording_scope ===
            "synthetic_only; never raw real Responses" &&
            typeof input.body === "string" &&
            typeof input.truncated === "boolean" &&
            input.encoding === "base64",
        );
        const bytes = Buffer.from(String(input.body), "base64");
        assert(
          bytes.toString("base64") === input.body &&
            bytes.length <= Number(run.inbound_bytes) + 1,
        );
        if (input.truncated)
          assert(bytes.length === Number(run.inbound_bytes) + 1);
        return p1ResponseBoundary(
          new Response(bytes),
          Number(run.inbound_bytes),
          init!.signal as AbortSignal,
        );
      },
    );
    let final: Awaited<ReturnType<typeof agentLoop>> | undefined;
    let caught: unknown;
    try {
      final = await agentLoop({
        model: "simulated-p1",
        system: c.system,
        question: c.question,
        resources: c.resources,
        tools: c.tools,
        limits,
        complete: provider,
        record: (e) => {
          const actual = take(String(e.kind));
          const {
            scene: _s,
            sequence: _seq,
            elapsed_ms: _elapsed,
            ...payload
          } = actual;
          assert.deepEqual(
            payload,
            JSON.parse(JSON.stringify(e)),
            "provider/tool/final exact event mismatch",
          );
          if (e.kind === "provider_response")
            validateCompletion(e.response as unknown as Completion, d.tools);
        },
        call: async (name, args) => {
          const req = take("rpc_request");
          assert(req.method === "tools/call");
          assert.deepEqual(req.params, { name, arguments: args });
          const response = take("rpc_response");
          decode(name, args, response.reply, d);
          return response.reply;
        },
      });
      const audit = evaluate(scene, final.content, events, d);
      const recorded = take("evaluation").result as JsonObject;
      assert.deepEqual(read(scene + "/evaluation.json"), recorded);
      const { review, calls, rounds, ...rest } = recorded;
      assert.deepEqual(rest, audit);
      assert(calls === final.calls && rounds === final.rounds);
      assert(["pending", "fail", "scripted_pass"].includes(String(review)));
      if (review === "scripted_pass")
        assert(
          (factJSON(final.content) as JsonObject).definition ===
            DEFINITIONS[scene],
          "scripted review mismatch",
        );
      const { cleanup_failure: _cleanup, ...withoutCleanup } = item;
      assert.deepEqual(withoutCleanup, { scene, ...recorded });
    } catch (error) {
      caught = error;
    }
    if (caught) {
      if (events[cursor]?.kind !== "failure") throw caught;
      // Only a recorded reproduction of the same terminal error is a valid failed trace.
      const failure = take("failure").failure as JsonObject;
      assert.deepEqual(read(scene + "/failure.json"), failure);
      assert(
        caught instanceof Error &&
          failure.category === caught.name &&
          failure.reason === caught.message.split("\n")[0],
        "failure not reproduced",
      );
      const { cleanup_failure: _cleanup, ...withoutCleanup } = item;
      assert.deepEqual(withoutCleanup, {
        scene,
        status: failure.stage === "evaluation" ? "task_failed" : "failed",
        failure,
      });
    }
    if (events[cursor]?.kind === "cleanup_failure") {
      const error = take("cleanup_failure");
      assert.deepEqual(error.failure, item.cleanup_failure);
      assert.deepEqual(
        read(scene + "/cleanup-failure.json"),
        item.cleanup_failure,
      );
      assert(index === scenes.length - 1 && summary.status === "failed");
    }
    assert(cursor === events.length, "unused events after terminal state");
    results.push(item);
    if (
      (item as JsonObject).status !== "mechanical_pass" ||
      (item as JsonObject).review !== "scripted_pass"
    )
      assert(index === scenes.length - 1, "continued after failure/pending");
  }
  const status = results.some((v) => v.status === "failed" || v.cleanup_failure)
    ? "failed"
    : run.mode === "prepared"
      ? "complete"
      : results.at(-1)!.status === "failed" ||
          results.at(-1)!.status === "task_failed" ||
          results.at(-1)!.review === "fail"
        ? "failed"
        : results.at(-1)!.review === "pending"
          ? "pending"
          : "complete";
  assert(summary.status === status);
  if (status === "complete")
    assert(results.length === 3, "complete batch missing scene");
  assert(verifyArchive(path).seal_sha256 === verified.seal_sha256);
  return {
    schema: "ossie-p1-agent-replay-v1",
    mode: run.mode,
    status,
    internally_consistent: true,
    scenes: results.length,
    artifact_sha256: artifactSha,
    source_seal_sha256: verified.seal_sha256,
    real_model_requests: 0,
    provider_config_read: false,
    limitation:
      "Recorded simulation consistency; no provider authenticity, private continuation or model autonomy proof.",
  };
}
if (import.meta.main) {
  const a = process.argv.slice(2);
  assert(
    a.length === 4 && a[0] === "--source" && a[2] === "--artifact-sha",
    "Usage: p1_agent_replay.ts --source PRIVATE_DIR --artifact-sha SHA256",
  );
  console.log(JSON.stringify(await replayP1(resolve(a[1]), a[3]), null, 2));
}

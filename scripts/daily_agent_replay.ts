/** Offline strict sequential replay: source/fixture/current contract, no subprocess or model call. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT, digest } from "./artifact_helpers.ts";
import { verifyArchive } from "./observation_agent_archive.ts";
import { completionFromProjection } from "./observation_agent_replay.ts";
import {
  agentLoop,
  LIMITS,
  type JsonObject,
  type Completion,
} from "./agent_loop.ts";
import { DailySession, dailyContent } from "./daily_mcp.ts";
import type { RpcProcess } from "./mcp_check.ts";
import {
  DAILY_SOURCE_NAMES,
  DAILY_SYSTEM,
  DAILY_SCENES,
  DAILY_BUDGET,
  auditDailyAnswer,
} from "./daily_agent_contract.ts";
import { dailyResult, dailyPlan, DAILY_TRACE } from "./daily_result.ts";
const normalized = (value: unknown) => JSON.parse(JSON.stringify(value));
export async function replayDaily(path: string) {
  const verified = verifyArchive(path);
  const read = (name: string) =>
    JSON.parse(readFileSync(join(verified.path, name), "utf8"));
  const run = read("run.json"),
    summary = read("summary.json");
  assert.equal(run.schema, "ossie-daily-agent-rehearsal-v1");
  assert.equal(run.mode, "simulated");
  assert.equal(run.provider_config_read, false);
  assert.equal(run.real_model_requests, 0);
  assert.equal(run.model, "simulated-daily");
  assert.equal(summary.schema, "ossie-daily-agent-summary-v1");
  assert.equal(summary.mode, "simulated");
  assert.equal(
    summary.status,
    "complete",
    "failed/incomplete daily archives cannot completely replay",
  );
  assert.equal(summary.real_model_requests, 0);
  assert.deepEqual(run.budget, DAILY_BUDGET);
  assert.deepEqual(
    Object.keys(run.source_hashes).sort(),
    [...DAILY_SOURCE_NAMES].sort(),
  );
  for (const name of DAILY_SOURCE_NAMES)
    assert.equal(
      run.source_hashes[name],
      digest(join(ROOT, name)),
      "current source/fixture differs: " + name,
    );
  assert.deepEqual(
    summary.scenes.map((s: JsonObject) => s.scene),
    DAILY_SCENES.map((s) => s.id),
  );
  assert.deepEqual(
    Object.keys(verified.files).sort(),
    [
      "run.json",
      "summary.json",
      ...DAILY_SCENES.flatMap((s) =>
        ["events.jsonl", "setup.json", "context.json", "review.json"].map(
          (n) => s.id + "/" + n,
        ),
      ),
    ].sort(),
  );
  const results: unknown[] = [];
  let total = 0;
  for (const scene of DAILY_SCENES) {
    const events: JsonObject[] = readFileSync(
      join(verified.path, scene.id, "events.jsonl"),
      "utf8",
    )
      .trim()
      .split("\n")
      .map((line) => JSON.parse(line));
    for (const [i, event] of events.entries()) {
      assert.equal(event.scene, scene.id);
      assert.equal(event.mode, "simulated");
      assert.equal(event.sequence, i + 1);
      assert.equal(
        event.capture_redacted,
        undefined,
        "simulation event must not be substituted with redacted bytes",
      );
    }
    let cursor = 0;
    const peek = (kind: string) => {
      const event = events[cursor];
      assert(event && event.kind === kind, `expected ${kind} at${cursor}`);
      return event;
    };
    const consume = (event: JsonObject) => {
      const {
        scene: _,
        mode: __,
        sequence: ___,
        ...bare
      } = peek(String(event.kind));
      assert.deepEqual(bare, normalized(event), "ordered daily event mismatch");
      cursor++;
    };
    consume({ kind: "scene_start", limits: LIMITS });
    const fake = {
      request: async (id: number) => {
        const reply = peek("rpc_response").reply as JsonObject;
        assert.equal(reply.id, id);
        assert.equal(reply.jsonrpc, "2.0");
        return reply;
      },
      send: () => {},
    } as unknown as RpcProcess;
    const session = new DailySession(fake, consume),
      discovered = await session.discover();
    const context = {
      system: DAILY_SYSTEM,
      question: scene.question,
      tools: discovered.tools,
      resources: discovered.rows,
    };
    assert.deepEqual(read(scene.id + "/context.json"), context);
    assert.deepEqual(read(scene.id + "/setup.json"), {
      binding: discovered.binding,
      threshold: scene.threshold,
    });
    const final = await agentLoop({
      model: run.model,
      ...context,
      limits: LIMITS,
      record: consume,
      complete: async () =>
        completionFromProjection(
          peek("provider_response").response as unknown as Completion,
        ),
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
    });
    const evaluation = auditDailyAnswer(
      final.content,
      events,
      scene,
      discovered.binding,
    );
    consume({ kind: "evaluation", value: evaluation });
    consume({
      kind: "scene_end",
      status: "complete",
      calls: final.calls,
      rounds: final.rounds,
    });
    assert.equal(cursor, events.length, "extra/unpaired trailing events");
    const review = {
      mode: "simulated",
      semantic_verdict: "not applicable: scripted integration",
      scene: scene.id,
      final,
      evaluation,
    };
    assert.deepEqual(read(scene.id + "/review.json"), review);
    assert.deepEqual(
      summary.scenes.find((s: JsonObject) => s.scene === scene.id),
      {
        scene: scene.id,
        status: "complete",
        calls: final.calls,
        rounds: final.rounds,
        evaluation,
      },
    );
    total += final.rounds;
    results.push({
      scene: scene.id,
      calls: final.calls,
      rounds: final.rounds,
      evaluation,
    });
  }
  assert.equal(summary.simulated_completion_requests, total);
  assert.equal(verifyArchive(path).seal_sha256, verified.seal_sha256);
  return {
    schema: "ossie-daily-agent-replay-v1",
    mode: "simulated",
    passed: true,
    real_model_requests: 0,
    source_seal_sha256: verified.seal_sha256,
    simulated_completion_requests: total,
    scenes: results,
    limitations:
      "Exact local ordered correspondence and hand-oracle audit; no provider authenticity, real-model autonomy or business-data acceptance",
  };
}

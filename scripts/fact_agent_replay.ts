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
import { FactSession, FACT_SERVER } from "./fact_mcp.ts";
import { factJSON } from "./fact_result.ts";
import type { RpcProcess } from "./mcp_check.ts";
import {
  FACT_SOURCE_NAMES,
  FACT_SYSTEM,
  FACT_SCENES,
  FACT_BUDGET,
  auditFactAnswer,
  decodeFactCall,
  factFixtureBinding,
} from "./fact_agent_contract.ts";
const normalized = (value: unknown) => JSON.parse(JSON.stringify(value));
export async function replayFact(path: string, expectedArtifactSHA?: string) {
  const verified = verifyArchive(path);
  const read = (name: string) =>
    factJSON(
      readFileSync(join(verified.path, name), "utf8"),
      8 * 1048576,
    ) as any;
  const run = read("run.json"),
    summary = read("summary.json");
  assert.equal(run.schema, "ossie-fact-agent-rehearsal-v1");
  assert.equal(run.mode, "simulated");
  assert.equal(run.provider_config_read, false);
  assert.equal(run.real_model_requests, 0);
  assert.equal(run.model, "simulated-fact");
  assert.equal(
    run.usage_semantics,
    "Simulated zero usage placeholders, not measured token consumption",
  );
  assert(
    run.runtime && typeof run.runtime === "object",
    "runtime metadata missing",
  );
  assert.deepEqual(
    Object.keys(run.runtime).sort(),
    [
      "version",
      "source_id",
      "extension_version",
      "install_path",
      "cli_sha256",
      "mcp_sha256",
      "extension_sha256",
      "server_sha256",
    ].sort(),
    "runtime metadata shape",
  );
  assert.equal(run.runtime.version, "v1.5.6");
  assert.equal(run.runtime.source_id, "069cc9f9b5");
  assert.equal(run.runtime.extension_version, "6f9c4ba");
  assert(
    typeof run.runtime.install_path === "string" &&
      run.runtime.install_path.length > 0,
  );
  for (const k of [
    "cli_sha256",
    "mcp_sha256",
    "extension_sha256",
    "server_sha256",
  ])
    assert(
      typeof run.runtime[k] === "string" &&
        /^[a-f0-9]{64}$/.test(run.runtime[k]),
      "runtime hash shape",
    );
  assert.equal(
    run.runtime.server_sha256,
    digest(FACT_SERVER),
    "runtime server binding",
  );
  if (expectedArtifactSHA !== undefined)
    assert.equal(
      run.runtime.extension_sha256,
      expectedArtifactSHA,
      "runtime artifact binding",
    );
  assert.equal(summary.schema, "ossie-fact-agent-summary-v1");
  assert.equal(summary.mode, "simulated");
  assert.equal(
    summary.status,
    "complete",
    "failed/incomplete fact archives cannot completely replay",
  );
  assert.equal(summary.real_model_requests, 0);
  assert.deepEqual(run.budget, FACT_BUDGET);
  assert.deepEqual(
    Object.keys(run.source_hashes).sort(),
    [...FACT_SOURCE_NAMES].sort(),
  );
  for (const name of FACT_SOURCE_NAMES)
    assert.equal(
      run.source_hashes[name],
      digest(join(ROOT, name)),
      "current source/fixture differs: " + name,
    );
  assert.deepEqual(
    summary.scenes.map((s: JsonObject) => s.scene),
    FACT_SCENES.map((s) => s.id),
  );
  assert.deepEqual(
    Object.keys(verified.files).sort(),
    [
      "run.json",
      "summary.json",
      ...FACT_SCENES.flatMap((s) =>
        ["events.jsonl", "setup.json", "context.json", "review.json"].map(
          (n) => s.id + "/" + n,
        ),
      ),
    ].sort(),
  );
  const results: unknown[] = [];
  let total = 0;
  for (const scene of FACT_SCENES) {
    const events: JsonObject[] = readFileSync(
      join(verified.path, scene.id, "events.jsonl"),
      "utf8",
    )
      .trim()
      .split("\n")
      .map((line) => factJSON(line, 8 * 1048576) as JsonObject);
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
      assert.deepEqual(bare, normalized(event), "ordered fact event mismatch");
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
    const session = new FactSession(fake, consume),
      discovered = await session.discover();
    factFixtureBinding(discovered.program);
    const context = {
      system: FACT_SYSTEM,
      question: scene.question,
      tools: discovered.tools,
      resources: discovered.rows,
    };
    assert.deepEqual(read(scene.id + "/context.json"), context);
    assert.deepEqual(read(scene.id + "/setup.json"), {
      binding: discovered.binding,
      setup: scene.setup,
    });
    const final = await agentLoop({
      model: run.model,
      ...context,
      limits: LIMITS,
      record: consume,
      complete: async () => {
        const response = peek("provider_response")
          .response as unknown as Completion;
        assert.equal(
          response.model,
          "simulated-fact",
          "simulated response model mismatch",
        );
        assert.deepEqual(
          response.usage,
          { input_tokens: 0, output_tokens: 0, total_tokens: 0 },
          "simulated usage mismatch",
        );
        return completionFromProjection(response);
      },
      call: async (name, args) => {
        const reply = await session.call(name, args);
        decodeFactCall(name, args, reply, scene, discovered.program);
        return reply;
      },
    });
    const evaluation = auditFactAnswer(
      final.content,
      events,
      scene,
      discovered.program,
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
    schema: "ossie-fact-agent-replay-v1",
    mode: "simulated",
    passed: true,
    real_model_requests: 0,
    source_seal_sha256: verified.seal_sha256,
    recorded_artifact_sha256: run.runtime.extension_sha256,
    artifact_binding:
      expectedArtifactSHA === undefined
        ? "recorded hash shape only; no external artifact assertion"
        : "matches caller-provided artifact hash; no execution authenticity assertion",
    simulated_completion_requests: total,
    scenes: results,
    limitations:
      "Exact local ordered correspondence and independent BigInt/full-identity oracle audit; no provider authenticity, real-model autonomy or business-data acceptance",
  };
}

/** Actual MCP + injected completion, private seal, replay and re-sealed negative controls. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import {
  rehearseState,
  replayState,
  auditStateAnswer,
  STATE_SCENES,
  stateSceneRequest,
  STATE_AGENT_SOURCES,
} from "./daily_state_agent.ts";
import { PrivateArchive, verifyArchive } from "./observation_agent_archive.ts";
import { responsesProvider, type JsonObject } from "./agent_loop.ts";
import { stateResult, type StateResult } from "./daily_state_result.ts";
import { STATE_TOOLS } from "./daily_state_mcp.ts";
import { ROOT, digest } from "./artifact_helpers.ts";
export async function verifyStateOffline(
  cli: string,
  artifact: string,
  out: string,
) {
  const execution = await rehearseState(cli, artifact, out);
  assert.equal(execution.status, "complete", "inspect sealed failed evidence");
  const verified = verifyArchive(out),
    replay = await replayState(out);
  assert(replay.passed);
  const controls: [string, (files: Map<string, any>) => void][] = [
    [
      "wrong schema",
      (f) => (f.get("run.json").schema = "ossie-daily-agent-rehearsal-v1"),
    ],
    ["live mode", (f) => (f.get("run.json").mode = "live")],
    [
      "source hash",
      (f) =>
        (f.get("run.json").source_hashes[STATE_AGENT_SOURCES[0]] = "0".repeat(
          64,
        )),
    ],
    ["failed terminal", (f) => (f.get("summary.json").status = "failed")],
    ["missing scene", (f) => f.get("summary.json").scenes.pop()],
    ["missing context", (f) => f.delete("all/context.json")],
    [
      "changed question",
      (f) => (f.get("all/context.json").question = "different scope"),
    ],
    ["changed setup", (f) => (f.get("all/setup.json").threshold = 20)],
    [
      "wrong review",
      (f) => (f.get("all/review.json").evaluation.status = "not_executed"),
    ],
    [
      "missing RPC response",
      (f) => {
        const e = f.get("all/events.jsonl");
        e.splice(
          e.findIndex((v: JsonObject) => v.kind === "rpc_response"),
          1,
        );
      },
    ],
    [
      "RPC ID mismatch",
      (f) => {
        const e = f
          .get("all/events.jsonl")
          .find((v: JsonObject) => v.kind === "rpc_response");
        e.reply.id = 999;
      },
    ],
    [
      "wrong tool evidence ID",
      (f) => {
        const e = f
          .get("all/events.jsonl")
          .find((v: JsonObject) => v.kind === "tool_response");
        e.evidence_id = "missing";
      },
    ],
    [
      "out of allowlist",
      (f) => {
        const e = f
          .get("all/events.jsonl")
          .find((v: JsonObject) => v.kind === "tool_request");
        e.name = "execute";
      },
    ],
    [
      "wrong argument",
      (f) => {
        const e = f
          .get("all/events.jsonl")
          .find((v: JsonObject) => v.kind === "tool_request");
        e.arguments.concept = "User";
      },
    ],
    [
      "extra trailing event",
      (f) =>
        f
          .get("all/events.jsonl")
          .push({ kind: "extra", scene: "all", mode: "simulated" }),
    ],
    [
      "forged completion projection",
      (f) => {
        const e = f
          .get("all/events.jsonl")
          .find((v: JsonObject) => v.kind === "provider_response");
        e.response.public_output[0].name = "execute";
      },
    ],
  ];
  const refusals: unknown[] = [];
  for (const [i, [label, edit]] of controls.entries()) {
    const files = new Map<string, any>();
    for (const n of Object.keys(verified.files)) {
      const raw = readFileSync(join(out, n), "utf8");
      files.set(
        n,
        n.endsWith(".jsonl")
          ? raw
              .trim()
              .split("\n")
              .map((v) => JSON.parse(v))
          : JSON.parse(raw),
      );
    }
    files.get("run.json").test_fixture =
      "Synthetic mutated and re-sealed control, not original evidence";
    edit(files);
    const mutated = PrivateArchive.create(out + "-forgery-" + i);
    for (const [n, v] of files) {
      if (n.endsWith(".jsonl")) {
        const s = n.split("/")[0];
        mutated.startEvents(s);
        v.forEach((e: JsonObject, index: number) =>
          mutated.record(s, { ...e, sequence: index + 1 }),
        );
      } else mutated.save(n, v);
    }
    const seal = mutated.seal();
    await assert.rejects(() => replayState(mutated.path), label);
    refusals.push({
      label,
      source_seal_sha256: seal,
      result: "refused re-sealed synthetic control",
    });
  }
  // Both tool and answer agree on a structurally complete but wrong semantic result.
  const review = JSON.parse(readFileSync(join(out, "all/review.json"), "utf8"));
  const original = JSON.parse(review.final.content).result as StateResult;
  const bad = structuredClone(original);
  bad.states = bad.states.map(([u, d]) => [u, d, "FALSE"]);
  bad.comparisons = bad.comparisons.map(([u, a, b]) => [
    u,
    a,
    b,
    "FALSE",
    "FALSE",
    false,
  ]);
  bad.state_totals = {
    unit: "User/day",
    TRUE: "0",
    FALSE: String(bad.states.length),
    UNKNOWN: "0",
  };
  bad.changed_comparison_count = "0";
  const carrier = JSON.stringify([{ result_json: JSON.stringify(bad) }]);
  stateResult(carrier);
  const reply = {
    jsonrpc: "2.0",
    id: 1,
    result: { content: [{ type: "text", text: carrier }] },
  };
  const events: JsonObject[] = [
    {
      kind: "tool_request",
      evidence_id: "tool-1",
      name: STATE_TOOLS[0],
      arguments: {
        concept: "QualifiedObservation",
        request_json: JSON.stringify(stateSceneRequest(STATE_SCENES[0])),
      },
    },
    { kind: "tool_response", evidence_id: "tool-1", result: reply },
  ];
  assert.throws(
    () =>
      auditStateAnswer(
        JSON.stringify({
          status: "answered",
          result: bad,
          evidence_ids: ["tool-1"],
          limitations: ["synthetic"],
        }),
        events,
        STATE_SCENES[0],
        original,
      ),
    "independent oracle must reject joint wrong answer/tool",
  );
  refusals.push({
    label: "jointly wrong structurally valid tool/answer",
    result: "independent complete row oracle refused",
  });
  const incomplete = responsesProvider(
    "https://simulated.invalid/v1/",
    "unused",
    async () =>
      new Response(
        JSON.stringify({
          id: "incomplete",
          model: "simulated-state",
          status: "incomplete",
          incomplete_details: { reason: "max_output_tokens" },
          output: [],
          usage: { input_tokens: 0, output_tokens: 0, total_tokens: 0 },
        }),
        { status: 200 },
      ),
  );
  const failed = await rehearseState(
    cli,
    artifact,
    out + "-incomplete",
    async (_s, b, signal) => incomplete(b, signal),
  );
  assert.equal(failed.status, "failed");
  await assert.rejects(() => replayState(out + "-incomplete"));
  refusals.push({
    label: "incomplete completion",
    result: "failed sealed; complete replay refused",
    source_seal_sha256: failed.source_seal_sha256,
  });
  return {
    passed: true,
    scope:
      "six scripted state integration scenes with actual MCP; not live Agent acceptance",
    real_model_requests: 0,
    provider_config_read: false,
    execution,
    replay,
    refusals,
    artifact_sha256: digest(artifact),
    source_hashes: Object.fromEntries(
      STATE_AGENT_SOURCES.map((n) => [n, digest(join(ROOT, n))]),
    ),
  };
}
if (import.meta.main) {
  const [cli, artifact, privateOut, publicOut] = process.argv.slice(2);
  assert(
    cli && artifact && privateOut && publicOut && process.argv.length === 6,
  );
  const r = await verifyStateOffline(
    resolve(cli),
    resolve(artifact),
    resolve(privateOut),
  );
  writeFileSync(publicOut, JSON.stringify(r, null, 2) + "\n");
  console.log(
    `PASS six state scenes and ${r.refusals.length} replay/semantic/incomplete controls; real model0`,
  );
}

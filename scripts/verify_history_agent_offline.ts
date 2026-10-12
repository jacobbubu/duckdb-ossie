/** Actual MCP + injected completion, private seal, replay and re-sealed negative controls. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import childProcess from "node:child_process";
import { spyOn } from "bun:test";
import {
  rehearseHistory,
  replayHistory,
  auditHistoryAnswer,
  HISTORY_SCENES,
  historySceneRequest,
  HISTORY_AGENT_SOURCES,
  scriptedHistory,
} from "./history_agent.ts";
import { PrivateArchive, verifyArchive } from "./observation_agent_archive.ts";
import { responsesProvider, type JsonObject } from "./agent_loop.ts";
import { historyResult } from "./history_result.ts";
import { HISTORY_TOOLS } from "./history_mcp.ts";
import { ROOT, digest } from "./artifact_helpers.ts";
export async function verifyHistoryOffline(
  cli: string,
  artifact: string,
  out: string,
) {
  const execution = await rehearseHistory(cli, artifact, out);
  assert.equal(execution.status, "complete", "inspect sealed failed evidence");
  const verified = verifyArchive(out);
  const blockedFetch = Object.assign(
    () => {
      throw new Error("offline network forbidden");
    },
    {
      preconnect: () => {
        throw new Error("offline network forbidden");
      },
    },
  );
  const fetchSpy = spyOn(globalThis, "fetch").mockImplementation(
    blockedFetch as typeof fetch,
  );
  const spawnSpy = spyOn(childProcess, "spawn").mockImplementation(() => {
    throw new Error("offline subprocess forbidden");
  });
  const spawnSyncSpy = spyOn(childProcess, "spawnSync").mockImplementation(
    () => {
      throw new Error("offline subprocess forbidden");
    },
  );
  let replay;
  try {
    replay = await replayHistory(out, digest(artifact));
    assert.equal(fetchSpy.mock.calls.length, 0);
    assert.equal(spawnSpy.mock.calls.length, 0);
    assert.equal(spawnSyncSpy.mock.calls.length, 0);
  } finally {
    fetchSpy.mockRestore();
    spawnSpy.mockRestore();
    spawnSyncSpy.mockRestore();
  }
  assert(replay.passed);
  const controls: [string, (files: Map<string, any>) => void][] = [
    [
      "wrong schema",
      (f) => (f.get("run.json").schema = "ossie-unrelated-agent-rehearsal-v1"),
    ],
    ["live mode", (f) => (f.get("run.json").mode = "live")],
    [
      "wrong artifact identity",
      (f) => (f.get("run.json").runtime.extension_sha256 = "0".repeat(64)),
    ],
    [
      "wrong runtime server hash",
      (f) => (f.get("run.json").runtime.server_sha256 = "0".repeat(64)),
    ],
    ["missing runtime", (f) => delete f.get("run.json").runtime],
    [
      "source hash",
      (f) =>
        (f.get("run.json").source_hashes[HISTORY_AGENT_SOURCES[0]] = "0".repeat(
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
    ["changed setup", (f) => (f.get("all/setup.json").dataset = "empty")],
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
        e.arguments.relationship = "other";
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
    await assert.rejects(
      () => replayHistory(mutated.path, digest(artifact)),
      label,
    );
    refusals.push({
      label,
      source_seal_sha256: seal,
      result: "refused re-sealed synthetic control",
    });
  }
  // Semantic controls first pass the strict structural decoder. Tool and answer agree,
  // but an independent raw-record oracle must still refuse them.
  const review = JSON.parse(readFileSync(join(out, "all/review.json"), "utf8"));
  const original = historyResult(
    JSON.stringify([
      { result_json: JSON.stringify(JSON.parse(review.final.content).result) },
    ]),
  );
  const context = JSON.parse(
    readFileSync(join(out, "all/context.json"), "utf8"),
  );
  const program = JSON.parse(
    context.resources["ossie://population/history-program"][0].program_json,
  );
  for (const label of ["joint wrong target", "joint missing member"]) {
    const bad = structuredClone(original);
    if (label === "joint wrong target") bad.links[0][3] += "-wrong";
    else {
      bad.links.pop();
      bad.link_count = String(bad.links.length);
    }
    const carrier = JSON.stringify([{ result_json: JSON.stringify(bad) }]);
    historyResult(carrier);
    const args = {
      relationship: "team_at_observation",
      request_json: JSON.stringify(historySceneRequest(HISTORY_SCENES[0])),
    };
    const events: JsonObject[] = [
      {
        kind: "tool_request",
        evidence_id: "tool-1",
        name: HISTORY_TOOLS[0],
        arguments: args,
        arguments_json: JSON.stringify(args),
      },
      {
        kind: "tool_response",
        evidence_id: "tool-1",
        result: {
          jsonrpc: "2.0",
          id: 1,
          result: { content: [{ type: "text", text: carrier }] },
        },
      },
    ];
    assert.throws(
      () =>
        auditHistoryAnswer(
          JSON.stringify({
            status: "answered",
            result: bad,
            evidence_ids: ["tool-1"],
            limitations: ["synthetic"],
          }),
          events,
          HISTORY_SCENES[0],
          program,
        ),
      label,
    );
    refusals.push({
      label,
      result:
        "independent complete member oracle refused; structural decoder passed",
    });
  }
  const incomplete = responsesProvider(
    "https://simulated.invalid/v1/",
    "unused",
    async () =>
      new Response(
        JSON.stringify({
          id: "incomplete",
          model: "simulated-history",
          status: "incomplete",
          incomplete_details: { reason: "max_output_tokens" },
          output: [],
          usage: { input_tokens: 0, output_tokens: 0, total_tokens: 0 },
        }),
        { status: 200 },
      ),
  );
  const failed = await rehearseHistory(
    cli,
    artifact,
    out + "-incomplete",
    async (_s, b, signal) => incomplete(b, signal),
  );
  assert.equal(failed.status, "failed");
  await assert.rejects(() => replayHistory(out + "-incomplete"));
  refusals.push({
    label: "incomplete completion",
    result: "failed sealed; complete replay refused",
    source_seal_sha256: failed.source_seal_sha256,
  });
  const guards: [string, string, (call: Record<string, any>) => void][] = [
    [
      "unknown outer argument",
      "all",
      (c) => {
        const a = JSON.parse(c.function.arguments);
        a.extra = "X";
        c.function.arguments = JSON.stringify(a);
      },
    ],
    [
      "object request instead of String",
      "all",
      (c) => {
        const a = JSON.parse(c.function.arguments);
        a.request_json = JSON.parse(a.request_json);
        c.function.arguments = JSON.stringify(a);
      },
    ],
    [
      "duplicate decoded outer key",
      "all",
      (c) => {
        const a = JSON.parse(c.function.arguments);
        c.function.arguments = `{"relationship":"wrong","\\u0072elationship":"team_at_observation","request_json":${JSON.stringify(a.request_json)}}`;
      },
    ],
    [
      "duplicate decoded inner key",
      "all",
      (c) => {
        const a = JSON.parse(c.function.arguments);
        a.request_json =
          '{"dates":{"mode":"all"},"\\u0064ates":{"mode":"all"},"products":{"mode":"all"}}';
        c.function.arguments = JSON.stringify(a);
      },
    ],
    [
      "different scene scope",
      "all",
      (c) => {
        const a = JSON.parse(c.function.arguments);
        a.request_json = JSON.stringify({
          dates: { mode: "all" },
          products: { mode: "subset", values: ["Y"] },
        });
        c.function.arguments = JSON.stringify(a);
      },
    ],
    [
      "wrong declared relationship",
      "all",
      (c) => {
        const a = JSON.parse(c.function.arguments);
        a.relationship = "other";
        c.function.arguments = JSON.stringify(a);
      },
    ],
    [
      "compile-only execution",
      "compile-only",
      (c) => (c.function.name = HISTORY_TOOLS[0]),
    ],
  ];
  for (const [i, [label, target, edit]] of guards.entries()) {
    const scripted = scriptedHistory(),
      path = out + "-guard-" + i;
    const failed = await rehearseHistory(
      cli,
      artifact,
      path,
      async (scene, body, signal) => {
        const completion = await scripted(scene, body, signal);
        if (scene.id === target && completion.message.tool_calls?.length) {
          const call = completion.message.tool_calls[0];
          edit(call);
          const projection = completion.public_output?.[0];
          assert(projection);
          projection.name = call.function.name;
          projection.arguments = call.function.arguments;
        }
        return completion;
      },
    );
    assert.equal(failed.status, "failed", label);
    const events = readFileSync(join(path, target, "events.jsonl"), "utf8")
      .trim()
      .split("\n")
      .map((v) => JSON.parse(v));
    assert(
      !events.some(
        (e) => e.kind === "rpc_request" && e.method === "tools/call",
      ),
      "guard must reject before actual tool RPC",
    );
    await assert.rejects(() => replayHistory(path));
    refusals.push({
      label,
      result:
        "caller refused before actual tool RPC; failed seal cannot pass complete replay",
      source_seal_sha256: failed.source_seal_sha256,
    });
  }
  return {
    passed: true,
    scope:
      "seven scripted history integration scenes with actual MCP; not live Agent acceptance",
    real_model_requests: 0,
    provider_config_read: false,
    offline_network_requests: 0,
    offline_subprocess_starts: 0,
    execution,
    replay,
    refusals,
    artifact_sha256: digest(artifact),
    source_hashes: Object.fromEntries(
      HISTORY_AGENT_SOURCES.map((n) => [n, digest(join(ROOT, n))]),
    ),
  };
}
if (import.meta.main) {
  const [cli, artifact, privateOut, publicOut] = process.argv.slice(2);
  assert(
    cli && artifact && privateOut && publicOut && process.argv.length === 6,
  );
  const r = await verifyHistoryOffline(
    resolve(cli),
    resolve(artifact),
    resolve(privateOut),
  );
  writeFileSync(publicOut, JSON.stringify(r, null, 2) + "\n");
  console.log(
    `PASS seven history scenes and ${r.refusals.length} replay/semantic/incomplete controls; real model0`,
  );
}

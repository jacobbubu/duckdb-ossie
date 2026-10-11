/** Actual daily MCP + injected completions + offline replay and re-sealed synthetic forgery controls. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { rehearseDaily } from "./daily_agent_rehearsal.ts";
import { replayDaily } from "./daily_agent_replay.ts";
import { verifyArchive, PrivateArchive } from "./observation_agent_archive.ts";
import { responsesProvider, type JsonObject } from "./agent_loop.ts";
import { DAILY_SOURCE_NAMES } from "./daily_agent_contract.ts";
import { ROOT, digest } from "./artifact_helpers.ts";
export async function verifyDailyOffline(
  cli: string,
  artifact: string,
  privateOut: string,
) {
  const execution = await rehearseDaily(cli, artifact, privateOut);
  assert.equal(execution.status, "complete");
  const verified = verifyArchive(privateOut),
    replay = await replayDaily(privateOut);
  assert.equal(replay.passed, true);
  const refusals: unknown[] = [];
  type Mutator = (files: Map<string, unknown>) => void;
  function events(
    files: Map<string, unknown>,
    edit: (rows: JsonObject[]) => void,
    scene = "all",
  ) {
    const rows = files.get(scene + "/events.jsonl") as JsonObject[];
    edit(rows);
    rows.forEach((e, i) => (e.sequence = i + 1));
  }
  const controls: [string, Mutator][] = [
    [
      "wrong run schema",
      (f) =>
        ((f.get("run.json") as JsonObject).schema =
          "ossie-observation-agent-run-v1"),
    ],
    [
      "wrong run mode",
      (f) => ((f.get("run.json") as JsonObject).mode = "live"),
    ],
    [
      "wrong source hash",
      (f) =>
        (((f.get("run.json") as JsonObject).source_hashes as JsonObject)[
          DAILY_SOURCE_NAMES[0]
        ] = "0".repeat(64)),
    ],
    [
      "failed terminal",
      (f) => ((f.get("summary.json") as JsonObject).status = "failed"),
    ],
    [
      "wrong scene inventory",
      (f) => ((f.get("summary.json") as JsonObject).scenes as unknown[]).pop(),
    ],
    [
      "missing RPC response",
      (f) =>
        events(f, (e) =>
          e.splice(
            e.findIndex((v) => v.kind === "rpc_response"),
            1,
          ),
        ),
    ],
    [
      "duplicate RPC response",
      (f) =>
        events(f, (e) => {
          const i = e.findIndex((v) => v.kind === "rpc_response");
          e.splice(i, 0, structuredClone(e[i]));
        }),
    ],
    [
      "wrong RPC id",
      (f) =>
        events(
          f,
          (e) =>
            ((
              e.find((v) => v.kind === "rpc_response")!.reply as JsonObject
            ).id = 999),
        ),
    ],
    [
      "RPC order mismatch",
      (f) =>
        events(f, (e) => {
          const i = e.findIndex((v) => v.kind === "rpc_request");
          [e[i], e[i + 1]] = [e[i + 1], e[i]];
        }),
    ],
    [
      "tool call id mismatch",
      (f) =>
        events(
          f,
          (e) =>
            (e.find((v) => v.kind === "tool_request")!.provider_call_id =
              "cross-scene"),
        ),
    ],
    [
      "tool arguments mismatch",
      (f) =>
        events(
          f,
          (e) =>
            ((
              e.find((v) => v.kind === "tool_request")!.arguments as JsonObject
            ).concept = "User"),
        ),
    ],
    [
      "wrong function_call_output",
      (f) =>
        events(f, (e) => {
          const requests = e.filter((v) => v.kind === "provider_request");
          const input = (requests[1].body as JsonObject).input as JsonObject[];
          input[0].call_id = "alien";
        }),
    ],
    [
      "public projection differs from normalized completion",
      (f) =>
        events(f, (e) => {
          const response = e.find((v) => v.kind === "provider_response")!
            .response as JsonObject;
          (response.public_output as JsonObject[])[0].arguments = "{}";
        }),
    ],
    [
      "member replacement preserves count",
      (f) =>
        events(f, (e) => {
          const response = e.filter((v) => v.kind === "tool_response").at(-1)!;
          const reply = response.result as any;
          const carrier = JSON.parse(reply.result.content[0].text);
          const result = JSON.parse(carrier[0].result_json);
          result.members[0] = "A";
          carrier[0].result_json = JSON.stringify(result);
          reply.result.content[0].text = JSON.stringify(carrier);
        }),
    ],
    [
      "old snapshot trace",
      (f) =>
        events(f, (e) => {
          const response = e.filter((v) => v.kind === "tool_response").at(-1)!;
          const reply = response.result as any;
          const carrier = JSON.parse(reply.result.content[0].text);
          const result = JSON.parse(carrier[0].result_json);
          result.snapshot_id = "old";
          carrier[0].result_json = JSON.stringify(result);
          reply.result.content[0].text = JSON.stringify(carrier);
        }),
    ],
    [
      "cross-scene final citation",
      (f) =>
        events(f, (e) => {
          const answer = e.find((v) => v.kind === "final_answer")!;
          const json = JSON.parse(String(answer.content));
          json.evidence_ids = ["tool-999"];
          answer.content = JSON.stringify(json);
        }),
    ],
    ["missing scene terminal", (f) => events(f, (e) => e.pop())],
    [
      "extra event after terminal",
      (f) =>
        events(f, (e) =>
          e.push({ kind: "unpaired", scene: "all", mode: "simulated" }),
        ),
    ],
  ];
  for (const [i, [label, edit]] of controls.entries()) {
    const files = new Map<string, unknown>();
    for (const name of Object.keys(verified.files)) {
      const text = readFileSync(join(privateOut, name), "utf8");
      files.set(
        name,
        name.endsWith("jsonl")
          ? text
              .trim()
              .split("\n")
              .map((l) => JSON.parse(l))
          : JSON.parse(text),
      );
    }
    (files.get("run.json") as JsonObject).test_fixture =
      "synthetic mutated/re-sealed control copied from simulation; not original evidence";
    edit(files);
    const path = privateOut + `-forgery-${i}`;
    const archive = PrivateArchive.create(path);
    for (const [name, value] of files) {
      if (name.endsWith("jsonl")) {
        const scene = name.split("/")[0];
        archive.startEvents(scene);
        for (const row of value as unknown[]) archive.record(scene, row);
      } else archive.save(name, value);
    }
    const seal = archive.seal();
    let error: unknown;
    try {
      await replayDaily(path);
    } catch (e) {
      error = e;
    }
    assert(error, label + ": incorrectly replayed");
    refusals.push({
      label,
      source_seal_sha256: seal,
      result: "refused re-sealed synthetic record",
      reason: error instanceof Error ? error.message.slice(0, 250) : "unknown",
    });
  }
  const incomplete = responsesProvider(
    "https://simulated.invalid/v1/",
    "unused",
    async () =>
      new Response(
        JSON.stringify({
          id: "incomplete",
          model: "simulated-daily",
          status: "incomplete",
          incomplete_details: { reason: "max_output_tokens" },
          output: [],
          usage: { input_tokens: 0, output_tokens: 0, total_tokens: 0 },
        }),
        { status: 200 },
      ),
  );
  const failed = await rehearseDaily(
    cli,
    artifact,
    privateOut + "-incomplete",
    (_scene, body, signal) => incomplete(body, signal),
  );
  assert.equal(failed.status, "failed");
  verifyArchive(failed.path);
  await assert.rejects(() => replayDaily(failed.path), /failed\/incomplete/);
  assert.equal(verifyArchive(privateOut).seal_sha256, verified.seal_sha256);
  return {
    schema: "ossie-daily-agent-public-receipt-v1",
    mode: "simulated",
    passed: true,
    real_model_requests: 0,
    provider_config_read: false,
    source_seal_sha256: verified.seal_sha256,
    runtime_artifact_sha256: digest(artifact),
    source_hashes: Object.fromEntries(
      DAILY_SOURCE_NAMES.map((n) => [n, digest(join(ROOT, n))]),
    ),
    replay,
    forgery_refusals: refusals,
    failed_control: {
      status: failed.status,
      source_seal_sha256: failed.seal_sha256,
      replay: "refused complete replay",
    },
    publication_scope:
      "selected receipt only; raw events/context/review remain in ignored private archives; not an exact raw transcript export",
    limitations: [
      "Scripted parameters and answers exercise integration, not model tool selection or real Agent acceptance",
      "Independent hand oracle gates members; source hashes/seals do not prove provider authenticity",
      "Compile-only deliberately not executed, not unsupported capability",
      "No new real model calls, push or remote CI",
    ],
  };
}
if (import.meta.main) {
  assert(
    process.argv.length === 6,
    "usage: verify_daily_agent_offline.ts CLI ARTIFACT NEW_PRIVATE_OUT RECEIPT",
  );
  const report = await verifyDailyOffline(
    resolve(process.argv[2]),
    resolve(process.argv[3]),
    resolve(process.argv[4]),
  );
  writeFileSync(
    resolve(process.argv[5]),
    JSON.stringify(report, null, 2) + "\n",
  );
  console.log(
    `PASS five daily scenes; ${report.forgery_refusals.length} re-sealed forgeries refused; real model requests0`,
  );
}

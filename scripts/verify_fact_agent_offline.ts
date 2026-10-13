/** Actual fact MCP + injected completions + offline replay and re-sealed synthetic forgery controls. */
import assert from "node:assert/strict";
import { isDeepStrictEqual } from "node:util";
import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { rehearseFact } from "./fact_agent_rehearsal.ts";
import { replayFact } from "./fact_agent_replay.ts";
import { verifyArchive, PrivateArchive } from "./observation_agent_archive.ts";
import { responsesProvider, type JsonObject } from "./agent_loop.ts";
import { FACT_SOURCE_NAMES } from "./fact_agent_contract.ts";
import { ROOT, digest } from "./artifact_helpers.ts";
export async function verifyFactOffline(
  cli: string,
  artifact: string,
  privateOut: string,
) {
  const execution = await rehearseFact(cli, artifact, privateOut);
  assert.equal(execution.status, "complete");
  const verified = verifyArchive(privateOut),
    replay = await replayFact(privateOut, digest(artifact));
  assert.equal(replay.passed, true);
  const refusals: unknown[] = [];
  type Mutator = (files: Map<string, unknown>) => void;
  function events(
    files: Map<string, unknown>,
    edit: (rows: JsonObject[]) => void,
    scene = "separate-grains",
  ) {
    const rows = files.get(scene + "/events.jsonl") as JsonObject[];
    edit(rows);
    rows.forEach((e, i) => (e.sequence = i + 1));
  }
  // Coordinate every repeated JSON carrier so semantic controls reach the target audit,
  // rather than failing on an earlier RPC/tool/provider correspondence mismatch.
  function coordinated(
    files: Map<string, unknown>,
    mode:
      | "money"
      | "members"
      | "denominator"
      | "window"
      | "citation"
      | "definition"
      | "explanation"
      | "limitations",
  ) {
    const transform = (value: any): any => {
      if (typeof value === "string") {
        if (!/^\s*[\[{]/.test(value)) return value;
        try {
          const parsed = JSON.parse(value);
          const transformed = transform(parsed);
          return isDeepStrictEqual(parsed, transformed)
            ? value
            : JSON.stringify(transformed);
        } catch {
          return value;
        }
      }
      if (Array.isArray(value)) return value.map(transform);
      if (value === null || typeof value !== "object") return value;
      const next = Object.fromEntries(
        Object.entries(value).map(([k, v]) => [k, transform(v)]),
      );
      if (mode === "definition" && Object.hasOwn(next, "core_sha256"))
        next.core_sha256 = "f".repeat(64);
      if (mode === "explanation" && next.status === "not_executed")
        next.reason = "I calculated 42 users.";
      if (mode === "limitations" && next.status === "answered")
        next.limitations = ["Production data and model autonomy proved."];
      if (next.schema_version === "ossie-fact-users-result-v1") {
        if (mode === "money") next.rows[0][1] = "291.00";
        if (mode === "members") next.rows[0][0] = "0";
        if (mode === "denominator") next.common_user_count = "2";
      }
      if (
        mode === "window" &&
        next.schema_version === "ossie-fact-windows-result-v1"
      ) {
        next.rows[0][2] = null;
        next.rows[0][3] = "50.00";
      }
      if (mode === "citation" && next.status === "answered")
        next.evidence_ids = ["tool-1"];
      return next;
    };
    for (const [name, value] of files)
      if (
        name.startsWith(
          (mode === "window"
            ? "displayed-window"
            : mode === "denominator"
              ? "empty-cohort"
              : mode === "explanation"
                ? "compile-only"
                : "separate-grains") + "/",
        )
      )
        files.set(name, transform(value));
  }
  const controls: [string, Mutator][] = [
    [
      "coordinated definition hash differs from verified fixture",
      (f) => coordinated(f, "definition"),
    ],
    [
      "simulated response model mismatch",
      (f) =>
        events(f, (e) => {
          for (const row of e.filter((e) => e.kind === "provider_response"))
            (row.response as JsonObject).model = "other-model";
        }),
    ],
    [
      "simulated usage mismatch",
      (f) =>
        events(f, (e) => {
          for (const row of e.filter((e) => e.kind === "provider_response"))
            (row.response as JsonObject).usage = {
              input_tokens: 1,
              output_tokens: 1,
              total_tokens: 2,
            };
        }),
    ],
    [
      "runtime metadata missing",
      (f) => {
        delete (f.get("run.json") as JsonObject).runtime;
      },
    ],
    [
      "runtime artifact mismatches caller hash",
      (f) => {
        (
          (f.get("run.json") as JsonObject).runtime as JsonObject
        ).extension_sha256 = "f".repeat(64);
      },
    ],
    [
      "coordinated compile-only calculation prose",
      (f) => coordinated(f, "explanation"),
    ],
    [
      "coordinated false acceptance limitation",
      (f) => coordinated(f, "limitations"),
    ],
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
          FACT_SOURCE_NAMES[0]
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
            ).request_json = "{}"),
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
          result.rows[0][0] = "0";
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
    ["coordinated money replacement", (f) => coordinated(f, "money")],
    [
      "coordinated same-count member replacement",
      (f) => coordinated(f, "members"),
    ],
    [
      "coordinated wrong common denominator",
      (f) => coordinated(f, "denominator"),
    ],
    ["coordinated prefiltered window values", (f) => coordinated(f, "window")],
    [
      "coordinated compile citation instead of execution",
      (f) => coordinated(f, "citation"),
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
      await replayFact(path, digest(artifact));
    } catch (e) {
      error = e;
    }
    assert(error, label + ": incorrectly replayed");
    const targeted: Record<string, RegExp> = {
      "coordinated definition hash differs from verified fixture":
        /fixture definition binding mismatch/,
      "simulated response model mismatch": /simulated response model mismatch/,
      "simulated usage mismatch": /simulated usage mismatch/,
      "runtime artifact mismatches caller hash": /runtime artifact binding/,
      "coordinated compile-only calculation prose":
        /scripted compile-only explanation mismatch/,
      "coordinated false acceptance limitation":
        /scripted limitation declaration mismatch/,
    };
    if (targeted[label]) assert.match(String(error), targeted[label]);
    if (
      label === "coordinated money replacement" ||
      label === "coordinated same-count member replacement"
    )
      assert.match(
        String(error),
        /independent complete fact User\/money oracle/,
      );
    if (label === "coordinated wrong common denominator")
      assert.match(String(error), /independent common denominator oracle/);
    if (label === "coordinated prefiltered window values")
      assert.match(
        String(error),
        /independent whole-capture window then display oracle/,
      );
    if (label === "coordinated compile citation instead of execution")
      assert.match(
        String(error),
        /citation must name actual executed complete fact result/,
      );
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
          model: "simulated-fact",
          status: "incomplete",
          incomplete_details: { reason: "max_output_tokens" },
          output: [],
          usage: { input_tokens: 0, output_tokens: 0, total_tokens: 0 },
        }),
        { status: 200 },
      ),
  );
  const failed = await rehearseFact(
    cli,
    artifact,
    privateOut + "-incomplete",
    (_scene, body, signal) => incomplete(body, signal),
  );
  assert.equal(failed.status, "failed");
  verifyArchive(failed.path);
  await assert.rejects(() => replayFact(failed.path), /failed\/incomplete/);
  assert.equal(verifyArchive(privateOut).seal_sha256, verified.seal_sha256);
  return {
    schema: "ossie-fact-agent-public-receipt-v1",
    mode: "simulated",
    passed: true,
    real_model_requests: 0,
    provider_config_read: false,
    source_seal_sha256: verified.seal_sha256,
    runtime_artifact_sha256: digest(artifact),
    source_hashes: Object.fromEntries(
      FACT_SOURCE_NAMES.map((n) => [n, digest(join(ROOT, n))]),
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
      "Independent BigInt/full-identity oracle gates complete values and members; source hashes/seals do not prove provider authenticity",
      "Compile-only deliberately not executed, not unsupported capability",
      "User common/selected counts and window row counts have explicitly separate grains",
      "No new real model calls, push or remote CI",
    ],
  };
}
if (import.meta.main) {
  assert(
    process.argv.length === 6,
    "usage: verify_fact_agent_offline.ts CLI ARTIFACT NEW_PRIVATE_OUT RECEIPT",
  );
  assert(
    !existsSync(resolve(process.argv[5])),
    "fresh receipt output required",
  );
  const report = await verifyFactOffline(
    resolve(process.argv[2]),
    resolve(process.argv[3]),
    resolve(process.argv[4]),
  );
  writeFileSync(
    resolve(process.argv[5]),
    JSON.stringify(report, null, 2) + "\n",
    { flag: "wx" },
  );
  console.log(
    `PASS eight fact scenes; ${report.forgery_refusals.length} re-sealed forgeries refused; real model requests0`,
  );
}

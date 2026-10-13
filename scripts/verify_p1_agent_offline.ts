import fs from "node:fs";
import { exportP1Summary } from "./p1_agent_public.ts";
import childProcess from "node:child_process";
import { spyOn } from "bun:test";
/** Actual stock MCP integration and re-sealed adversarial replay controls; model requests zero. */
import assert from "node:assert/strict";
import { isDeepStrictEqual } from "node:util";
import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { executeP1 } from "./p1_agent.ts";
import { scriptedSimulation, type Simulation } from "./p1_agent_simulation.ts";
import { replayP1 } from "./p1_agent_replay.ts";
import { PrivateArchive, verifyArchive } from "./observation_agent_archive.ts";
import { ROOT, digest } from "./artifact_helpers.ts";
import { LIMITS, type Scene } from "./p1_agent_contract.ts";
import type { JsonObject } from "./agent_loop.ts";
export async function verifyP1Offline(
  cli: string,
  artifact: string,
  out: string,
) {
  assert(!existsSync(out));
  const sha = digest(artifact);
  const controls: JsonObject[] = [];
  const run = (
    name: string,
    options: Partial<
      Extract<
        Parameters<typeof executeP1>[0],
        { mode: "prepared" | "simulated" }
      >
    > = {},
  ) =>
    executeP1({
      mode: "simulated",
      cli,
      artifact,
      out: join(out, name),
      ...options,
    });
  const prepared = await run("prepare", { mode: "prepared" });
  assert(prepared.status === "complete");
  const execution = await run("positive");
  assert(execution.status === "complete");
  const fetchOriginal = globalThis.fetch;
  let fetches = 0;
  const spawnSpy = spyOn(childProcess, "spawn").mockImplementation(() => {
    throw Error("offline subprocess refused");
  });
  const spawnSyncSpy = spyOn(childProcess, "spawnSync").mockImplementation(
    () => {
      throw Error("offline subprocess refused");
    },
  );
  globalThis.fetch = Object.assign(
    async () => {
      fetches++;
      throw Error("offline fetch refused");
    },
    { preconnect: fetchOriginal.preconnect },
  );
  let replay: Awaited<ReturnType<typeof replayP1>>;
  try {
    replay = await replayP1(join(out, "positive"), sha);
    assert((await replayP1(join(out, "prepare"), sha)).status === "complete");
  } finally {
    globalThis.fetch = fetchOriginal;
    assert(
      spawnSpy.mock.calls.length === 0 && spawnSyncSpy.mock.calls.length === 0,
    );
    spawnSpy.mockRestore();
    spawnSyncSpy.mockRestore();
  }
  assert(fetches === 0);
  controls.push({
    name: "positive+prepare sealed replay has zero fetch and subprocess",
    passed: true,
  });
  for (const scene of ["facts", "states", "history"] as Scene[]) {
    const bad = await run("wrong-" + scene, {
      simulate: scriptedSimulation({ wrongScene: scene }),
    });
    assert(bad.status === "failed");
    const file = join(out, "wrong-" + scene, scene, "events.jsonl");
    const events = readFileSync(file, "utf8")
      .trim()
      .split("\n")
      .map((v) => JSON.parse(v));
    const tool = events.find((e) => e.kind === "tool_request"),
      rpc = events.find(
        (e) => e.kind === "rpc_request" && e.method === "tools/call",
      );
    assert(tool && rpc);
    assert.deepEqual(rpc.params.arguments, tool.arguments);
    assert(events.some((e) => e.kind === "tool_response"));
    assert(
      (await replayP1(join(out, "wrong-" + scene), sha)).status === "failed",
    );
    controls.push({
      name:
        scene +
        " valid wrong scope reaches real MCP unchanged then fails task audit",
      passed: true,
    });
  }
  const faultCases: [
    string,
    Partial<
      Extract<
        Parameters<typeof executeP1>[0],
        { mode: "prepared" | "simulated" }
      >
    >,
  ][] = [
    [
      "duplicate-outer",
      { simulate: scriptedSimulation({ duplicateOuter: true }) },
    ],
    ["inbound-limit", { simulate: scriptedSimulation({ oversized: true }) }],
    ["incomplete", { simulate: scriptedSimulation({ incomplete: true }) }],
    ["round-limit", { limits: { ...LIMITS, rounds: 1 } }],
    ["call-limit", { limits: { ...LIMITS, calls: 0 } }],
    ["outbound-limit", { limits: { ...LIMITS, inputBytes: 1 } }],
    [
      "model-declared-failure",
      { simulate: scriptedSimulation({ failScene: "facts" }) },
    ],
    ["review-pending", { review: () => "pending" }],
    ["review-fail", { review: () => "fail" }],
  ];
  for (const [name, options] of faultCases) {
    const result = await run(name, options);
    assert(
      result.status === (name === "review-pending" ? "pending" : "failed"),
    );
    assert(result.scenes.length === 1);
    assert((await replayP1(join(out, name), sha)).status === result.status);
    if (name === "duplicate-outer")
      assert(
        !readFileSync(join(out, name, "facts/events.jsonl"), "utf8").includes(
          '"kind":"tool_request"',
        ),
      );
    controls.push({
      name:
        name + " sealed failure/pending stops subsequent scenes and replays",
      passed: true,
    });
  }
  const cleanup = await run("cleanup-failure", {
    cleanupFault: async () => {
      throw Error("Synthetic close failure");
    },
  });
  assert(cleanup.status === "failed" && cleanup.scenes.length === 1);
  assert(
    (await replayP1(join(out, "cleanup-failure"), sha)).status === "failed",
  );
  controls.push({
    name: "cleanup failure is sealed, cannot continue or pass",
    passed: true,
  });
  let lateReturned = false;
  const slow: Simulation = async () => {
    await new Promise((r) => setTimeout(r, 600));
    lateReturned = true;
    return "{}";
  };
  const timed = await run("deadline", {
    simulate: slow,
    limits: { ...LIMITS, milliseconds: 250 },
  });
  assert(timed.status === "failed" && timed.scenes.length === 1);
  const timedSeal = verifyArchive(join(out, "deadline")).seal_sha256;
  await new Promise((r) => setTimeout(r, 700));
  assert(lateReturned);
  assert(verifyArchive(join(out, "deadline")).seal_sha256 === timedSeal);
  assert((await replayP1(join(out, "deadline"), sha)).status === "failed");
  controls.push({
    name: "deadline closes scene, late simulator cannot write to sealed archive, diagnostic replay",
    passed: true,
  });
  const early = await run("discovery-deadline", {
    limits: { ...LIMITS, milliseconds: 1 },
  });
  assert(early.status === "failed");
  assert(
    (await replayP1(join(out, "discovery-deadline"), sha)).status === "failed",
  );
  controls.push({
    name: "discovery failure preserved before context and no model/tool calls",
    passed: true,
  });
  const equivalent = await run("equivalent-order", {
    simulate: scriptedSimulation({ predicate: " EXPENSE > 50.00 " }),
  });
  assert(equivalent.status === "complete");
  assert(
    (await replayP1(join(out, "equivalent-order"), sha)).status === "complete",
  );
  controls.push({
    name: "limited semantically equal predicate spelling allowed without parameter rewriting",
    passed: true,
  });
  const startup = await run("startup-failure", {
    cli: "/does-not-exist/p1-stock-cli",
  });
  assert(startup.status === "failed" && startup.scenes.length === 0);
  assert(
    (await replayP1(join(out, "startup-failure"), sha)).status === "failed",
  );
  controls.push({
    name: "runtime preflight failure sealed before any scene, diagnostic only",
    passed: true,
  });
  const original = verifyArchive(join(out, "positive"));
  type Mutator = (files: Map<string, unknown>) => void;
  const eventsOf = (f: Map<string, unknown>) =>
    f.get("facts/events.jsonl") as JsonObject[];
  const coordinated = (
    value: unknown,
    edit: (o: JsonObject) => void,
  ): unknown => {
    if (typeof value === "string") {
      if (!/^[\[{]/.test(value.trim())) return value;
      try {
        const parsed = JSON.parse(value);
        const transformed = coordinated(parsed, edit);
        return isDeepStrictEqual(parsed, transformed)
          ? value
          : JSON.stringify(transformed);
      } catch {
        return value;
      }
    }
    if (Array.isArray(value)) return value.map((v) => coordinated(v, edit));
    if (value !== null && typeof value === "object") {
      const raw = value as JsonObject;
      if (raw.kind === "simulation_response" && raw.encoding === "base64") {
        return {
          ...raw,
          body: Buffer.from(
            JSON.stringify(
              coordinated(
                JSON.parse(Buffer.from(String(raw.body), "base64").toString()),
                edit,
              ),
            ),
          ).toString("base64"),
        };
      }
      const o = Object.fromEntries(
        Object.entries(value).map(([k, v]) => [k, coordinated(v, edit)]),
      );
      edit(o);
      return o;
    }
    return value;
  };
  function nameTamper(
    f: Map<string, unknown>,
    scene: string,
    key: string,
    value: string,
  ) {
    const ev = f.get(scene + "/events.jsonl") as JsonObject[];
    const edit = (o: JsonObject) => {
      if (typeof o[key] === "string") o[key] = value;
    };
    for (const e of ev) {
      if (e.kind === "simulation_response") {
        const response = JSON.parse(
          Buffer.from(String(e.body), "base64").toString(),
        );
        for (const item of response.output)
          if (item.type === "function_call")
            item.arguments = JSON.stringify(
              coordinated(JSON.parse(item.arguments), edit),
            );
        e.body = Buffer.from(JSON.stringify(response)).toString("base64");
      } else if (
        e.kind === "provider_response" ||
        e.kind === "tool_request" ||
        (e.kind === "rpc_request" && e.method === "tools/call")
      )
        Object.assign(e, coordinated(e, edit));
    }
  }
  const tampering: [string, Mutator][] = [
    [
      "context-oracle-injection",
      (f) => {
        (f.get("facts/context.json") as JsonObject).oracle = "hidden";
      },
    ],
    [
      "serialized-outbound-change",
      (f) => {
        const e = eventsOf(f).find((e) => e.kind === "provider_request")!;
        e.serialized_body = "{}";
      },
    ],
    [
      "coordinated-wrong-amount",
      (f) => {
        for (const [name, v] of f)
          if (name.startsWith("facts/"))
            f.set(
              name,
              coordinated(v, (o) => {
                if (o.schema_version === "ossie-fact-users-result-v1")
                  (o.rows as unknown[][])[0][1] = "81.00";
              }),
            );
      },
    ],
    [
      "coordinated-concept-name",
      (f) => nameTamper(f, "states", "concept", "WrongConcept"),
    ],
    [
      "coordinated-relationship-name",
      (f) => nameTamper(f, "history", "relationship", "WrongRelationship"),
    ],
    [
      "discovery-resource-list-replaced",
      (f) => {
        const ev = eventsOf(f);
        const req = ev.find(
          (e) => e.kind === "rpc_request" && e.method === "resources/list",
        )!;
        req.method = "tools/list";
        const resp = ev.find(
          (e) =>
            e.kind === "rpc_response" && (e.reply as JsonObject).id === req.id,
        )!;
        (resp.reply as JsonObject).result = (
          ev.find(
            (e) =>
              e.kind === "rpc_response" && (e.reply as JsonObject).id === 2,
          )!.reply as JsonObject
        ).result;
      },
    ],
    [
      "inbound-budget-increase",
      (f) => {
        (f.get("run.json") as JsonObject).inbound_bytes = 2097153;
      },
    ],
    [
      "event-after-deadline",
      (f) => {
        const ev = eventsOf(f);
        ev.at(-1)!.elapsed_ms = 180001;
      },
    ],
    [
      "missing-last-scene",
      (f) => {
        const s = f.get("summary.json") as JsonObject;
        (s.scenes as JsonObject[]).pop();
      },
    ],
    [
      "source-hash-change",
      (f) => {
        ((f.get("run.json") as JsonObject).source_hashes as JsonObject)[
          "scripts/p1_agent.ts"
        ] = "0".repeat(64);
      },
    ],
  ];
  for (const [name, edit] of tampering) {
    const files = new Map<string, unknown>();
    for (const file of Object.keys(original.files)) {
      const text = readFileSync(join(out, "positive", file), "utf8");
      files.set(
        file,
        file.endsWith(".jsonl")
          ? text
              .trim()
              .split("\n")
              .map((v) => JSON.parse(v))
          : JSON.parse(text),
      );
    }
    edit(files);
    const target = join(out, "tamper-" + name);
    const archive = PrivateArchive.create(target);
    for (const [file, value] of files)
      if (file.endsWith(".jsonl")) {
        const scene = file.split("/")[0];
        archive.startEvents(scene);
        for (const e of value as JsonObject[]) archive.record(scene, e);
      } else archive.save(file, value);
    archive.seal();
    verifyArchive(target);
    const expectedError =
      name === "coordinated-concept-name" ||
      name === "coordinated-relationship-name"
        ? /correspondence mismatch/
        : name === "coordinated-wrong-amount"
          ? /independent fact oracle mismatch/
          : undefined;
    if (expectedError)
      await assert.rejects(() => replayP1(target, sha), expectedError);
    else await assert.rejects(() => replayP1(target, sha));
    controls.push({ name: name + " re-sealed forgery rejected", passed: true });
  }
  const sentinel = "PRIVATE_PROJECTION_SENTINEL";
  const script = scriptedSimulation();
  const projected = await run("response-projection", {
    simulate: async (scene, body, events) => {
      const raw = JSON.parse(await script(scene, body, events));
      raw.metadata = { secret: sentinel };
      raw.usage.extra = sentinel;
      raw.output.unshift({
        type: "reasoning",
        summary: [{ text: sentinel }],
        encrypted_content: sentinel,
      });
      return JSON.stringify(raw);
    },
  });
  assert(projected.status === "complete");
  assert(
    (await replayP1(join(out, "response-projection"), sha)).status ===
      "complete",
  );
  for (const scene of ["facts", "states", "history"]) {
    const ev = readFileSync(
      join(out, "response-projection", scene, "events.jsonl"),
      "utf8",
    )
      .trim()
      .split("\n")
      .map((v) => JSON.parse(v));
    assert(
      ev
        .filter((e) => e.kind === "provider_response")
        .every((e) => !JSON.stringify(e).includes(sentinel)),
    );
    assert(
      ev
        .filter((e) => e.kind === "simulation_response")
        .every((e) => e.recording_scope.startsWith("synthetic_only")),
    );
  }
  controls.push({
    name: "reasoning and metadata stripped before recorded completions; synthetic-only raw replays",
    passed: true,
  });
  const publicFile = join(out, "public-summary.json");
  const exported = await exportP1Summary(
    join(out, "positive"),
    publicFile,
    sha,
  );
  const publicText = readFileSync(publicFile, "utf8");
  assert(digest(publicFile) === exported.summary_sha256);
  const publicValue = JSON.parse(publicText);
  assert(
    publicValue.scenes.length === 3 &&
      publicValue.scenes.every(
        (v: JsonObject) =>
          v.tool_calls === 1 && v.semantic_acceptance === "not_proven",
      ),
  );
  for (const marker of [
    "result_json",
    "definition",
    "serialized_body",
    "simulated-p1",
    "provider_response",
    "install_path",
  ])
    assert(!publicText.includes(marker));
  await assert.rejects(() =>
    exportP1Summary(join(out, "positive"), publicFile, sha),
  );
  await assert.rejects(() =>
    exportP1Summary(
      join(out, "positive"),
      join(out, "positive", "export.json"),
      sha,
    ),
  );
  fs.symlinkSync(join(out, "positive"), join(out, "summary-symlink"));
  await assert.rejects(() =>
    exportP1Summary(
      join(out, "positive"),
      join(out, "summary-symlink", "export.json"),
      sha,
    ),
  );
  controls.push({
    name: "public summary exact fixed fields, no business/raw fields, no overwrite/source/symlink writes",
    passed: true,
  });
  const metadataTarget = join(out, "metadata-extra");
  const metadataArchive = PrivateArchive.create(metadataTarget);
  for (const file of Object.keys(original.files)) {
    const raw = readFileSync(join(out, "positive", file), "utf8");
    if (file.endsWith(".jsonl")) {
      const scene = file.split("/")[0];
      metadataArchive.startEvents(scene);
      for (const line of raw.trim().split("\n"))
        metadataArchive.record(scene, JSON.parse(line));
    } else {
      const value = JSON.parse(raw);
      if (["run.json", "summary.json"].includes(file)) {
        value.extra = {
          reason: sentinel,
          provider: sentinel,
          results: sentinel,
        };
      }
      metadataArchive.save(file, value);
    }
  }
  metadataArchive.seal();
  const metadataFile = join(out, "metadata-summary.json");
  await exportP1Summary(metadataTarget, metadataFile, sha);
  assert(!readFileSync(metadataFile, "utf8").includes(sentinel));
  await assert.rejects(
    () =>
      exportP1Summary(
        join(out, "tamper-coordinated-wrong-amount"),
        join(out, "forged-summary.json"),
        sha,
      ),
    /independent fact oracle mismatch/,
  );
  assert(!existsSync(join(out, "forged-summary.json")));
  controls.push({
    name: "public export drops re-sealed free metadata and rejects coordinated semantic forgery",
    passed: true,
  });
  const startupFile = join(out, "startup-summary.json");
  await exportP1Summary(join(out, "startup-failure"), startupFile, sha);
  assert(
    JSON.parse(readFileSync(startupFile, "utf8")).correspondence ===
      "startup_diagnostic_only",
  );
  controls.push({
    name: "startup export is diagnostic only, no runtime/execution proof",
    passed: true,
  });
  const changingTarget = join(out, "changing-source");
  fs.cpSync(join(out, "positive"), changingTarget, { recursive: true });
  const pendingExport = exportP1Summary(
    changingTarget,
    join(out, "changed-source-summary.json"),
    sha,
  );
  const changedRun = join(changingTarget, "run.json");
  const oldRun = readFileSync(changedRun, "utf8");
  writeFileSync(
    changedRun,
    JSON.stringify({ ...JSON.parse(oldRun), extra: sentinel }),
  );
  assert(readFileSync(changedRun, "utf8") !== oldRun);
  await assert.rejects(
    () => pendingExport,
    /archive byte hash mismatch|source snapshot changed/,
  );
  assert(!existsSync(join(out, "changed-source-summary.json")));
  controls.push({
    name: "source copy changed during asynchronous export is rejected; no output written (not a filesystem race guarantee)",
    passed: true,
  });
  assert(
    verifyArchive(join(out, "positive")).seal_sha256 === original.seal_sha256,
  );
  return {
    scope:
      "three-scene offline entry + actual stock MCP + strict recorded replay; no real Agent acceptance",
    passed: true,
    prepare: prepared,
    execution,
    replay,
    artifact_sha256: sha,
    controls,
    real_model_requests: 0,
    provider_config_read: false,
  };
}
if (import.meta.main) {
  const a = process.argv.slice(2);
  assert(
    a.length === 4,
    "Usage: verify_p1_agent_offline.ts CLI EXTENSION NEW_PRIVATE_ROOT REPORT",
  );
  const result = await verifyP1Offline(
    resolve(a[0]),
    resolve(a[1]),
    resolve(a[2]),
  );
  writeFileSync(resolve(a[3]), JSON.stringify(result, null, 2) + "\n", {
    flag: "wx",
  });
  console.log(`PASS P1 offline: ${result.controls.length} controls; model0`);
}

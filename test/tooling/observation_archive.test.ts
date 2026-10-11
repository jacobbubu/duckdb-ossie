import { beforeAll, afterAll, test, expect, spyOn } from "bun:test";
import {
  readFileSync,
  writeFileSync,
  mkdirSync,
  rmSync,
  statSync,
  symlinkSync,
  unlinkSync,
} from "node:fs";
import { join } from "node:path";
import { randomUUID } from "node:crypto";
import * as childProcess from "node:child_process";
import { ROOT, digest } from "../../scripts/artifact_helpers.ts";
import {
  PrivateArchive,
  verifyArchive,
  exportObservationArchive,
  hashBytes,
  writeReplayReport,
} from "../../scripts/observation_agent_archive.ts";
import { replayObservation } from "../../scripts/observation_agent_replay.ts";
import { SOURCE_NAMES } from "../../scripts/observation_agent_contract.ts";
import {
  liveArguments,
  liveConfiguration,
  executeObservation,
} from "../../scripts/observation_agent_run.ts";
const root = join(
  ROOT,
  ".local-agent-evidence",
  `unit-agent-archive-${randomUUID()}`,
);
const fixture = join(
  ROOT,
  "docs/research/ossie-req-001/observation-prompt-controls/simulated",
);
const descriptor = JSON.parse(
  readFileSync(join(fixture, "derivative.json"), "utf8"),
);
let serial = 0;
const read = (path: string, name: string) =>
  JSON.parse(readFileSync(join(path, name), "utf8"));
function clone() {
  const path = join(root, String(++serial));
  const archive = PrivateArchive.create(path);
  for (const name of Object.keys(descriptor.files)) {
    let value: unknown;
    if (name.endsWith("jsonl")) {
      archive.startEvents(name.split("/")[0]);
      for (const line of readFileSync(join(fixture, name), "utf8")
        .trim()
        .split("\n"))
        archive.record(name.split("/")[0], JSON.parse(line));
      continue;
    }
    value = read(fixture, name);
    if (name === "run.json") {
      const run = value as any;
      run.source_hashes = Object.fromEntries(
        SOURCE_NAMES.map((name) => [name, digest(join(ROOT, name))]),
      );
      run.test_fixture =
        "synthetic regression transcript derived from public simulated rehearsal; rebased local source hashes; not an original raw archive";
    }
    archive.save(name, value);
  }
  archive.seal();
  return path;
}
function reseal(path: string) {
  const seal = read(path, "seal.json");
  for (const name of Object.keys(seal.files))
    seal.files[name] = hashBytes(readFileSync(join(path, name)));
  writeFileSync(join(path, "seal.json"), JSON.stringify(seal, null, 2) + "\n");
}
function editEvents(path: string, fn: (events: any[]) => void) {
  const name = "baseline/events.jsonl";
  const rows = readFileSync(join(path, name), "utf8")
    .trim()
    .split("\n")
    .map((line) => JSON.parse(line));
  fn(rows);
  writeFileSync(
    join(path, name),
    rows.map((row) => JSON.stringify(row)).join("\n") + "\n",
  );
  reseal(path);
}
beforeAll(() => {
  PrivateArchive.create(root);
});
afterAll(() => rmSync(root, { recursive: true, force: true }));
test("raw archive is private, exclusive, sealed and unchanged by exact offline replay", async () => {
  const path = clone();
  const before = verifyArchive(path);
  expect(statSync(path).mode & 0o777).toBe(0o700);
  for (const name of Object.keys(before.files))
    expect(statSync(join(path, name)).mode & 0o777).toBe(0o600);
  const result = await replayObservation(path);
  expect(result.mode).toBe("simulated");
  expect(result.provider_requests).toBe(7);
  expect(result.live_model_requests_during_replay).toBe(0);
  expect(result.overall).toBe("pending semantic review");
  expect(verifyArchive(path).seal_sha256).toBe(before.seal_sha256);
  expect(() => PrivateArchive.create(path)).toThrow("fresh private archive");
});
test("offline replay performs no fetch or subprocess startup", async () => {
  const path = clone();
  const blockedFetch = Object.assign(
    () => {
      throw new Error("network forbidden");
    },
    {
      preconnect: () => {
        throw new Error("network forbidden");
      },
    },
  );
  const fetch = spyOn(globalThis, "fetch").mockImplementation(
    blockedFetch as typeof globalThis.fetch,
  );
  const spawn = spyOn(childProcess, "spawn").mockImplementation(() => {
    throw new Error("subprocess forbidden");
  });
  const spawnSync = spyOn(childProcess, "spawnSync").mockImplementation(() => {
    throw new Error("subprocess forbidden");
  });
  try {
    expect((await replayObservation(path)).status).toBe(
      "internally consistent replay",
    );
    expect(fetch).not.toHaveBeenCalled();
    expect(spawn).not.toHaveBeenCalled();
    expect(spawnSync).not.toHaveBeenCalled();
  } finally {
    fetch.mockRestore();
    spawn.mockRestore();
    spawnSync.mockRestore();
  }
});
test("changed bytes, missing files and extra files refuse integrity verification", () => {
  const changed = clone();
  writeFileSync(join(changed, "baseline/review.json"), "{}\n");
  expect(() => verifyArchive(changed)).toThrow("hash mismatch");
  const missing = clone();
  unlinkSync(join(missing, "baseline/review.json"));
  expect(() => verifyArchive(missing)).toThrow("missing/extra");
  const extra = clone();
  writeFileSync(join(extra, "extra.json"), "{}\n", { mode: 0o600 });
  expect(() => verifyArchive(extra)).toThrow("missing/extra");
});
test("recomputed seal cannot hide wrong continuation, call_id or RPC id", async () => {
  for (const kind of ["continuation", "call", "rpc"]) {
    const path = clone();
    editEvents(path, (events) => {
      if (kind === "rpc") {
        events.find((e) => e.kind === "rpc_response").reply.id = 99;
        return;
      }
      const request = events.filter((e) => e.kind === "provider_request")[1];
      if (kind === "continuation")
        request.body.previous_response_id = "foreign-response";
      else request.body.input[0].call_id = "foreign-call";
      request.serialized_body = JSON.stringify(request.body);
    });
    await expect(replayObservation(path)).rejects.toThrow();
  }
});
test("ordered replay rejects reordered, duplicate and trailing events even with new seal", async () => {
  for (const change of ["reorder", "duplicate", "trailing"]) {
    const path = clone();
    editEvents(path, (events) => {
      if (change === "reorder") [events[1], events[2]] = [events[2], events[1]];
      else if (change === "duplicate")
        events.splice(2, 0, structuredClone(events[1]));
      else events.push({ ...events.at(-1), kind: "provider_request" });
      events.forEach((event, i) => {
        event.sequence = i + 1;
        event.elapsed_ms = 0;
        event.timestamp = events[0].started_at;
      });
    });
    await expect(replayObservation(path)).rejects.toThrow();
  }
});
test("saved normalized answer cannot disagree with public Responses projection", async () => {
  const path = clone();
  editEvents(path, (events) => {
    const response = events.find(
      (e) => e.kind === "provider_response",
    ).response;
    response.message.tool_calls[0].function.name = "population_objects";
  });
  await expect(replayObservation(path)).rejects.toThrow("projection disagrees");
});
test("setup, discovered model context and source inventory are independently checked", async () => {
  for (const name of [
    "baseline/setup.json",
    "baseline/model-context.json",
    "run.json",
  ]) {
    const path = clone();
    const data = read(path, name);
    if (name.endsWith("setup.json")) data.binding.ontology_sha256 = "wrong";
    else if (name.endsWith("model-context.json"))
      data.question = "Oracle-injected question";
    else data.source_hashes[SOURCE_NAMES[0]] = "0".repeat(64);
    writeFileSync(join(path, name), JSON.stringify(data, null, 2) + "\n");
    reseal(path);
    await expect(replayObservation(path)).rejects.toThrow();
  }
});
test("failure and partial archives may retain integrity but cannot pass complete replay", async () => {
  const path = clone();
  const summary = read(path, "summary.json");
  summary.status = "failed";
  writeFileSync(
    join(path, "summary.json"),
    JSON.stringify(summary, null, 2) + "\n",
  );
  reseal(path);
  expect(verifyArchive(path).mode).toBe("simulated");
  await expect(replayObservation(path)).rejects.toThrow("failed/incomplete");
  unlinkSync(join(path, "seal.json"));
  expect(() => verifyArchive(path)).toThrow();
});
test("original timing checks do not rely on fast replay elapsed time", async () => {
  const path = clone();
  editEvents(path, (events) => {
    events.at(-1).elapsed_ms = 120001;
  });
  await expect(replayObservation(path)).rejects.toThrow(
    "original event timing",
  );
});
test("public paths, traversal, symlink and tracked targets cannot receive raw archives", () => {
  expect(() =>
    PrivateArchive.create(join(ROOT, "docs", "raw-agent-test")),
  ).toThrow("ignored");
  expect(() =>
    PrivateArchive.create(join(root, "..", "..", "docs", "raw-agent-test")),
  ).toThrow("ignored");
  const link = join(root, "linked");
  symlinkSync("/tmp", link);
  expect(() => PrivateArchive.create(join(link, "raw-agent-test"))).toThrow(
    "symlink",
  );
  const dangling = join(root, "dangling");
  symlinkSync(join(root, "absent-parent"), dangling);
  expect(() => PrivateArchive.create(join(dangling, "raw-agent-test"))).toThrow(
    "symlink",
  );
  const path = clone();
  const artifact = join(path, "baseline", "link.json");
  symlinkSync(join(path, "run.json"), artifact);
  expect(() => verifyArchive(path)).toThrow("symlink");
});
test("seal path and duplicate keys are rejected without executing any archived code", () => {
  const path = clone();
  const seal = read(path, "seal.json");
  seal.files["../outside.json"] = "0".repeat(64);
  writeFileSync(join(path, "seal.json"), JSON.stringify(seal, null, 2) + "\n");
  expect(() => verifyArchive(path)).toThrow();
  const dup = clone();
  const text = readFileSync(join(dup, "seal.json"), "utf8").replace(
    '"schema":',
    '"schema": "ossie-observation-agent-archive-v1",\n  "schema":',
  );
  writeFileSync(join(dup, "seal.json"), text);
  expect(() => verifyArchive(dup)).toThrow("duplicate seal");
});
test("derivative export redacts nested escaped provider configuration before public writes", () => {
  const path = clone();
  const run = read(path, "run.json");
  run.provider.baseURL = "https://private-gateway.example/v1/";
  run.provider.model = "private-model-id";
  writeFileSync(join(path, "run.json"), JSON.stringify(run, null, 2) + "\n");
  const eventsPath = join(path, "baseline/events.jsonl");
  const rows = readFileSync(eventsPath, "utf8")
    .trim()
    .split("\n")
    .map((line) => JSON.parse(line));
  const e = rows.find((e) => e.kind === "provider_request");
  e.body.model = run.provider.model;
  e.body.note = JSON.stringify({
    url: run.provider.baseURL,
    nested: JSON.stringify({
      model: run.provider.model,
      escaped: "private-gateway.example",
    }),
  }).replaceAll("private-gateway", "private\\u002dgateway");
  e.serialized_body = JSON.stringify(e.body);
  writeFileSync(
    eventsPath,
    rows.map((e) => JSON.stringify(e)).join("\n") + "\n",
  );
  reseal(path);
  const before = verifyArchive(path).seal_sha256;
  const out = join(root, "export");
  exportObservationArchive(path, out);
  const text = readFileSync(join(out, "baseline/events.jsonl"), "utf8");
  expect(text).not.toContain("private-gateway");
  expect(text).not.toContain("private-model-id");
  expect(text).not.toContain("private\\u002dgateway");
  expect(read(out, "derivative.json").exact_original_request_bytes).toBe(false);
  expect(read(out, "derivative.json").source_seal_sha256).toBe(before);
  expect(verifyArchive(path).seal_sha256).toBe(before);
  expect(() => exportObservationArchive(path, out)).toThrow("fresh public");
  expect(() => verifyArchive(out)).toThrow();
});
test("credential echoes are removed from private projected records; keys are never archived", () => {
  const path = join(root, "secret-echo");
  const archive = PrivateArchive.create(path, [
    "synthetic-secret-only-for-test",
  ]);
  archive.save("run.json", {
    schema: "ossie-observation-agent-run-v1",
    mode: "simulated",
    provider: { baseURL: "https://fixture.invalid/v1/", model: "fixture" },
  });
  archive.save("summary.json", {
    echo: "synthetic-secret-only-for-test",
    encoded: JSON.stringify({ echo: "synthetic-secret-only-for-test" }).replace(
      "synthetic",
      "syn\\u0074hetic",
    ),
  });
  archive.startEvents("baseline");
  archive.record("baseline", {
    kind: "test",
    echo: "synthetic-secret-only-for-test",
  });
  archive.seal();
  expect(readFileSync(join(path, "summary.json"), "utf8")).not.toContain(
    "syn\\u0074hetic",
  );
  expect(readFileSync(join(path, "baseline/events.jsonl"), "utf8")).toContain(
    '"capture_redacted":true',
  );
  expect(readFileSync(join(path, "summary.json"), "utf8")).not.toContain(
    "synthetic-secret-only-for-test",
  );
});
test("live entry needs explicit mode and token-budget choice; standard config validation is local", () => {
  expect(() =>
    liveArguments(["--run", "--cli", "x", "--extension", "y", "--out", "z"]),
  ).toThrow();
  expect(
    liveArguments([
      "--run",
      "--token-budget-only",
      "--cli",
      "x",
      "--extension",
      "y",
      "--out",
      "z",
    ]).out.endsWith("/z"),
  ).toBe(true);
  expect(() => liveConfiguration({ OPENAI_MODEL: "fixture" })).toThrow(
    "OPENAI_API_KEY",
  );
  expect(() =>
    liveConfiguration({
      OPENAI_MODEL: "fixture",
      OPENAI_API_KEY: "synthetic",
      OPENAI_BASE_URL: "https://user:secret@example.com/v1/",
    }),
  ).toThrow("invalid provider URL");
});

test("runtime failure is sealed, has safe diagnostics and makes no simulated/model completion call", async () => {
  let called = false;
  const path = join(root, "failed-runtime");
  const result = await executeObservation({
    mode: "simulated",
    cli: join(root, "absent-cli"),
    artifact: join(root, "absent-artifact"),
    out: path,
    complete: async () => {
      called = true;
      throw new Error("completion must not run");
    },
  });
  expect(called).toBe(false);
  expect(result.status).toBe("failed");
  expect(verifyArchive(path).mode).toBe("simulated");
  const summary = read(path, "summary.json");
  expect(summary.model_requests).toBe(0);
  expect(summary.provider_requests).toBe(0);
  const failure = read(path, "failure.json");
  expect(failure.stage).toBe("runtime setup");
  expect(failure.message).toBeUndefined();
  await expect(replayObservation(path)).rejects.toThrow("failed/incomplete");
});

test("replay report stays outside the sealed tree and refuses overwrites", async () => {
  const path = clone();
  const result = await replayObservation(path);
  const before = verifyArchive(path).seal_sha256;
  expect(() =>
    writeReplayReport(path, join(path, "replay.json"), result),
  ).toThrow("sealed archive");
  const out = join(root, "replay-output.json");
  writeReplayReport(path, out, result);
  expect(statSync(out).mode & 0o777).toBe(0o600);
  expect(verifyArchive(path).seal_sha256).toBe(before);
  expect(() => writeReplayReport(path, out, result)).toThrow(
    "fresh replay report",
  );
});

import { beforeAll, afterAll, test, expect, spyOn } from "bun:test";
import { readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { randomUUID } from "node:crypto";
import * as childProcess from "node:child_process";
import { ROOT, digest } from "../../scripts/artifact_helpers.ts";
import {
  agentLoop,
  LIMITS,
  type JsonObject,
} from "../../scripts/agent_loop.ts";
import {
  PrivateArchive,
  verifyArchive,
  hashBytes,
} from "../../scripts/observation_agent_archive.ts";
import { FactSession } from "../../scripts/fact_mcp.ts";
import type { RpcProcess } from "../../scripts/mcp_check.ts";
import { scriptedFact } from "../../scripts/fact_agent_rehearsal.ts";
import { replayFact } from "../../scripts/fact_agent_replay.ts";
import {
  FACT_SOURCE_NAMES,
  FACT_SCENES,
  FACT_SYSTEM,
  FACT_BUDGET,
  auditFactAnswer,
  decodeFactCall,
} from "../../scripts/fact_agent_contract.ts";
const fixture = JSON.parse(
  readFileSync(
    join(
      ROOT,
      "docs/research/ossie-req-001/p1-fact-agent-controls/rpc-fixture.json",
    ),
    "utf8",
  ),
);
const root = join(ROOT, ".local-agent-evidence", `unit-fact-${randomUUID()}`),
  path = join(root, "run");
const cases: Record<
  string,
  { content: string; events: JsonObject[]; program: any }
> = {};
beforeAll(async () => {
  // Synthetic regression replay, rebased source hashes; never presented as original execution evidence.
  const archive = PrivateArchive.create(path),
    complete = scriptedFact();
  archive.save("run.json", {
    schema: "ossie-fact-agent-rehearsal-v1",
    mode: "simulated",
    provider_config_read: false,
    real_model_requests: 0,
    model: "simulated-fact",
    budget: FACT_BUDGET,
    runtime: fixture.runtime,
    usage_semantics:
      "Simulated zero usage placeholders, not measured token consumption",
    source_hashes: Object.fromEntries(
      FACT_SOURCE_NAMES.map((n) => [n, digest(join(ROOT, n))]),
    ),
    test_fixture:
      "synthetic reconstructed loop from hash-bound public actual RPC replies, rebased sources; not original evidence",
  });
  const summaries: JsonObject[] = [];
  let requests = 0;
  for (const scene of FACT_SCENES) {
    const source = fixture.scenes.find((s: any) => s.scene === scene.id);
    let rpc = 0,
      sequence = 0;
    const events: JsonObject[] = [];
    archive.startEvents(scene.id);
    const record = (e: JsonObject) => {
      if (e.kind === "provider_request") requests++;
      const event = {
        ...e,
        scene: scene.id,
        mode: "simulated",
        sequence: ++sequence,
      };
      events.push(structuredClone(event));
      archive.record(scene.id, event);
    };
    record({ kind: "scene_start", limits: LIMITS });
    const fake = {
      request: async (id: number) => {
        const reply = structuredClone(source.rpc_replies[rpc++]);
        expect(reply.id).toBe(id);
        return reply;
      },
      send: () => {},
    } as unknown as RpcProcess;
    const session = new FactSession(fake, record),
      discovery = await session.discover();
    const context = {
      system: FACT_SYSTEM,
      question: scene.question,
      tools: discovery.tools,
      resources: discovery.rows,
    };
    archive.save(scene.id + "/context.json", context);
    archive.save(scene.id + "/setup.json", {
      binding: discovery.binding,
      setup: scene.setup,
    });
    const final = await agentLoop({
      model: "simulated-fact",
      ...context,
      limits: LIMITS,
      complete: (body, signal) => complete(scene, body, signal),
      call: async (name, args) => {
        const reply = await session.call(name, args);
        decodeFactCall(name, args, reply, scene, discovery.program);
        return reply;
      },
      record,
    });
    const evaluation = auditFactAnswer(
      final.content,
      events,
      scene,
      discovery.program,
    );
    record({ kind: "evaluation", value: evaluation });
    record({
      kind: "scene_end",
      status: "complete",
      calls: final.calls,
      rounds: final.rounds,
    });
    archive.save(scene.id + "/review.json", {
      mode: "simulated",
      semantic_verdict: "not applicable: scripted integration",
      scene: scene.id,
      final,
      evaluation,
    });
    summaries.push({
      scene: scene.id,
      status: "complete",
      calls: final.calls,
      rounds: final.rounds,
      evaluation,
    });
    expect(rpc).toBe(source.rpc_replies.length);
    cases[scene.id] = {
      content: final.content,
      events,
      program: discovery.program,
    };
  }
  archive.save("summary.json", {
    schema: "ossie-fact-agent-summary-v1",
    mode: "simulated",
    status: "complete",
    real_model_requests: 0,
    simulated_completion_requests: requests,
    scenes: summaries,
  });
  archive.seal();
});
afterAll(() => rmSync(root, { recursive: true, force: true }));
test("eight fact scenes audit actual native carriers without substituting counts for complete values", () => {
  for (const scene of FACT_SCENES) {
    const c = cases[scene.id];
    expect(auditFactAnswer(c.content, c.events, scene, c.program).passed).toBe(
      true,
    );
  }
});
test("fact offline replay performs no network or subprocess and preserves seal", async () => {
  const seal = verifyArchive(path).seal_sha256;
  const fetch = spyOn(globalThis, "fetch").mockImplementation(
    Object.assign(
      () => {
        throw Error("network forbidden");
      },
      {
        preconnect: () => {
          throw Error("network forbidden");
        },
      },
    ) as typeof globalThis.fetch,
  );
  const spawn = spyOn(childProcess, "spawn").mockImplementation(() => {
    throw Error("subprocess forbidden");
  });
  const spawnSync = spyOn(childProcess, "spawnSync").mockImplementation(() => {
    throw Error("subprocess forbidden");
  });
  const bunSpawn = spyOn(Bun, "spawn").mockImplementation(() => {
    throw Error("subprocess forbidden");
  });
  try {
    const result = await replayFact(path, fixture.runtime.extension_sha256);
    expect(result.passed).toBe(true);
    expect(result.scenes.length).toBe(8);
    expect(result.simulated_completion_requests).toBe(23);
    for (const fn of [fetch, spawn, spawnSync, bunSpawn])
      expect(fn).not.toHaveBeenCalled();
    expect(verifyArchive(path).seal_sha256).toBe(seal);
  } finally {
    fetch.mockRestore();
    spawn.mockRestore();
    spawnSync.mockRestore();
    bunSpawn.mockRestore();
  }
});
test("fact answer and copied tool can agree on wrong money or members and still fail independent oracle", () => {
  const scene = FACT_SCENES[0];
  for (const mode of ["money", "members"]) {
    const c = structuredClone(cases[scene.id]);
    const answer = JSON.parse(c.content);
    const response = c.events.filter((e) => e.kind === "tool_response").at(-1)!;
    const reply = response.result as any;
    const carrier = JSON.parse(reply.result.content[0].text),
      v = JSON.parse(carrier[0].result_json);
    if (mode === "money") v.rows[0][1] = "291.00";
    else v.rows[0][0] = "0";
    carrier[0].result_json = JSON.stringify(v);
    reply.result.content[0].text = JSON.stringify(carrier);
    answer.result = v;
    expect(() =>
      auditFactAnswer(JSON.stringify(answer), c.events, scene, c.program),
    ).toThrow(/independent complete fact User\/money oracle/);
  }
});
test("fact denominators and whole-capture windows are independent of matching answer evidence", () => {
  for (const id of ["empty-cohort", "displayed-window"]) {
    const scene = FACT_SCENES.find((s) => s.id === id)!;
    const c = structuredClone(cases[id]),
      answer = JSON.parse(c.content),
      response = c.events.filter((e) => e.kind === "tool_response").at(-1)!;
    const reply = response.result as any,
      carrier = JSON.parse(reply.result.content[0].text),
      v = JSON.parse(carrier[0].result_json);
    if (id === "empty-cohort") v.common_user_count = "2";
    else {
      v.rows[0][2] = null;
      v.rows[0][3] = "50.00";
    }
    carrier[0].result_json = JSON.stringify(v);
    reply.result.content[0].text = JSON.stringify(carrier);
    answer.result = v;
    expect(() =>
      auditFactAnswer(JSON.stringify(answer), c.events, scene, c.program),
    ).toThrow(
      id === "empty-cohort"
        ? /independent common denominator oracle/
        : /independent whole-capture window then display oracle/,
    );
  }
});
test("fact compile-only rejects execution citations results and calculation prose", () => {
  const scene = FACT_SCENES.at(-1)!,
    c = cases[scene.id];
  for (const mutate of [
    (v: any) => (v.result = {}),
    (v: any) => (v.status = "answered"),
    (v: any) => (v.evidence_ids = ["tool-999"]),
    (v: any) => (v.reason = "Calculated42 users"),
    (v: any) => (v.limitations = ["Real model proved"]),
  ]) {
    const answer = JSON.parse(c.content);
    mutate(answer);
    expect(() =>
      auditFactAnswer(JSON.stringify(answer), c.events, scene, c.program),
    ).toThrow();
  }
  const query = cases[FACT_SCENES[0].id],
    answer = JSON.parse(query.content);
  answer.evidence_ids = ["tool-1"];
  expect(() =>
    auditFactAnswer(
      JSON.stringify(answer),
      query.events,
      FACT_SCENES[0],
      query.program,
    ),
  ).toThrow(/citation must name actual executed/);
});
test("fact definition hashes are checked against fixture bytes independently of result agreement", () => {
  for (const role of ["core", "ontology", "mapping", "manifest"]) {
    const c = structuredClone(cases[FACT_SCENES[0].id]);
    c.program[role + "_sha256"] = "f".repeat(64);
    expect(() =>
      auditFactAnswer(c.content, c.events, FACT_SCENES[0], c.program),
    ).toThrow(/fixture definition binding mismatch/);
  }
});
test("fact scene scope and successful evidence cannot be forged by a valid final envelope", () => {
  const scene = FACT_SCENES[0];
  for (const mode of ["scope", "error", "duplicate", "missing"]) {
    const c = structuredClone(cases[scene.id]);
    const request = c.events.find((e) => e.kind === "tool_request")!;
    if (mode === "scope") (request.arguments as JsonObject).request_json = "{}";
    else if (mode === "error")
      (
        c.events.filter((e) => e.kind === "tool_response").at(-1)!.result as any
      ).result.isError = true;
    else if (mode === "duplicate") c.events.push(structuredClone(request));
    else
      c.events.splice(
        c.events.findIndex((e) => e.kind === "tool_response"),
        1,
      );
    expect(() =>
      auditFactAnswer(c.content, c.events, scene, c.program),
    ).toThrow();
  }
});

test("fact replay refuses duplicate metadata keys even after a synthetic record is resealed", async () => {
  const verified = verifyArchive(path),
    copy = join(root, "duplicate-control"),
    archive = PrivateArchive.create(copy);
  for (const name of Object.keys(verified.files)) {
    const text = readFileSync(join(path, name), "utf8");
    if (name.endsWith("jsonl")) {
      const scene = name.split("/")[0];
      archive.startEvents(scene);
      for (const line of text.trim().split("\n"))
        archive.record(scene, JSON.parse(line));
    } else archive.save(name, JSON.parse(text));
  }
  archive.seal();
  const runFile = join(copy, "run.json");
  const run = readFileSync(runFile, "utf8");
  writeFileSync(runFile, run.replace("{", '{"mode":"live",'));
  const seal = JSON.parse(readFileSync(join(copy, "seal.json"), "utf8"));
  seal.files["run.json"] = hashBytes(readFileSync(runFile));
  writeFileSync(join(copy, "seal.json"), JSON.stringify(seal, null, 2) + "\n");
  expect(() => verifyArchive(copy)).not.toThrow();
  await expect(replayFact(copy)).rejects.toThrow(/duplicate JSON key/);
});

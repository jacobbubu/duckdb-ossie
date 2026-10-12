/** Stock MCP + mock HTTPS transport. No .env_test, actual model configuration or network access. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { join, resolve } from "node:path";
import childProcess from "node:child_process";
import { spyOn } from "bun:test";
import { executeP1, type P1Options } from "./p1_agent.ts";
import { scriptedSimulation } from "./p1_agent_simulation.ts";
import { p1Configuration, type P1Request } from "./p1_agent_provider.ts";
import { replayP1 } from "./p1_agent_replay.ts";
import { exportP1Summary } from "./p1_agent_public.ts";
import { verifyArchive, PrivateArchive } from "./observation_agent_archive.ts";
import { LIMITS, type Scene } from "./p1_agent_contract.ts";
import { digest } from "./artifact_helpers.ts";
import type { JsonObject } from "./agent_loop.ts";
export async function verifyP1Transport(
  cli: string,
  artifact: string,
  out: string,
) {
  assert(!existsSync(out));
  const sha = digest(artifact),
    controls: JsonObject[] = [],
    summaries: JsonObject[] = [];
  const virtual = {
    OPENAI_BASE_URL: "https://transport-test.invalid/proxy/v1/",
    OPENAI_MODEL: "test-only-model",
    OPENAI_API_KEY: "TEST_ONLY_P1_CREDENTIAL_SENTINEL_LONG",
  };
  const config = p1Configuration(virtual);
  const extra = "PRIVATE_REASONING_SENTINEL";
  const events = (path: string, scene: Scene) =>
    readFileSync(join(path, scene, "events.jsonl"), "utf8")
      .trim()
      .split("\n")
      .map((v) => JSON.parse(v) as JsonObject);
  function transport(
    scene: Scene,
    kind = "ok",
    seen: JsonObject[] = [],
  ): P1Request {
    const simulator = scriptedSimulation({
      wrongScene: kind === "wrong-scope" ? scene : undefined,
      incomplete: kind === "incomplete",
    });
    return async (url, init) => {
      assert(
        String(url) === "https://transport-test.invalid/proxy/v1/responses",
      );
      assert(
        init?.method === "POST" && init.redirect === "error" && init.signal,
      );
      const headers = new Headers(init.headers);
      assert(
        headers.get("authorization") === "Bearer " + virtual.OPENAI_API_KEY,
      );
      assert(headers.get("content-type") === "application/json");
      const body = JSON.parse(String(init.body));
      assert(
        body.model === virtual.OPENAI_MODEL &&
          body.max_output_tokens === 4096 &&
          body.store === true &&
          body.truncation === "disabled",
      );
      if (!seen.length) assert(!body.previous_response_id);
      else assert(body.previous_response_id === scene + "-response-1");
      seen.push(body);
      if (kind === "http")
        return new Response(virtual.OPENAI_API_KEY, { status: 401 });
      if (kind === "network") throw Error(virtual.OPENAI_API_KEY);
      if (kind === "inbound") return new Response(" ".repeat(2097153));
      const input = (body.input as JsonObject[])[0];
      const mock: JsonObject[] = [];
      if (input?.type === "function_call_output") {
        const result = JSON.parse(String(input.output));
        mock.push(
          { kind: "provider_response" },
          {
            kind: "tool_response",
            evidence_id: result.evidence_id,
            result: result.mcp_response,
          },
        );
      }
      const raw = JSON.parse(await simulator(scene, body, mock));
      raw.metadata = { extra };
      raw.output.unshift({
        type: "reasoning",
        encrypted_content: extra,
        summary: [{ text: extra }],
      });
      raw.usage.extra = extra;
      if (kind === "echo-id") raw.id = virtual.OPENAI_API_KEY;
      if (kind === "echo-arguments")
        raw.output.push({
          type: "function_call",
          name: "population_fact_users",
          call_id: "echo",
          arguments: virtual.OPENAI_API_KEY,
        });
      if (kind === "echo-text")
        raw.output.push({
          type: "message",
          role: "assistant",
          content: [{ type: "output_text", text: virtual.OPENAI_API_KEY }],
        });
      return new Response(JSON.stringify(raw));
    };
  }
  async function audit(path: string) {
    const spawn = spyOn(childProcess, "spawn").mockImplementation(() => {
      throw Error("replay subprocess forbidden");
    });
    const spawnSync = spyOn(childProcess, "spawnSync").mockImplementation(
      () => {
        throw Error("replay subprocess forbidden");
      },
    );
    try {
      const replay = await replayP1(path, sha);
      await exportP1Summary(path, path + "-summary.json", sha);
      assert(
        spawn.mock.calls.length === 0 && spawnSync.mock.calls.length === 0,
      );
      return replay;
    } finally {
      spawn.mockRestore();
      spawnSync.mockRestore();
    }
  }
  const savedFetch = globalThis.fetch;
  let unmockedFetches = 0,
    mockedLiveFetches = 0;
  const blocked = Object.assign(
    async () => {
      unmockedFetches++;
      throw Error("default network forbidden");
    },
    { preconnect: savedFetch.preconnect },
  );
  globalThis.fetch = blocked;
  try {
    for (const scene of ["facts", "states", "history"] as Scene[]) {
      const path = join(out, scene),
        seen: JsonObject[] = [];
      const result = await executeP1({
        mode: "transport-rehearsal",
        scene,
        env: virtual,
        transport: transport(scene, "ok", seen),
        cli,
        artifact,
        out: path,
      });
      assert(
        result.status === "pending" &&
          result.scenes.length === 1 &&
          result.real_model_requests === 0 &&
          result.transport_attempts === 2 &&
          result.received_responses === 2 &&
          result.constructed_requests === 2,
      );
      assert((result.scenes[0] as JsonObject).review === "pending");
      assert((await audit(path)).status === "pending");
      const text = readFileSync(join(path, scene, "events.jsonl"), "utf8");
      assert(
        !text.includes(virtual.OPENAI_API_KEY) &&
          !text.includes(extra) &&
          !text.includes("Authorization") &&
          !text.includes("simulation_response"),
      );
      assert(
        events(path, scene).filter((e) => e.kind === "tool_request").length ===
          1,
      );
      summaries.push({
        scene,
        status: result.status,
        mode: result.mode,
        transport_attempts: result.transport_attempts,
        seal_sha256: result.seal_sha256,
      });
      controls.push({
        name:
          scene +
          " single-scene HTTP headers/body/continuation, actual MCP, pending projected replay/export",
        passed: true,
      });
    }
    for (const kind of [
      "http",
      "network",
      "inbound",
      "incomplete",
      "wrong-scope",
      "echo-id",
      "echo-arguments",
      "echo-text",
    ]) {
      const path = join(out, kind);
      const result = await executeP1({
        mode: "transport-rehearsal",
        scene: "facts",
        env: virtual,
        transport: transport("facts", kind),
        cli,
        artifact,
        out: path,
      });
      assert(result.status === "failed" && result.scenes.length === 1);
      assert((await audit(path)).status === "failed");
      const text = readFileSync(join(path, "facts/events.jsonl"), "utf8");
      assert(!text.includes(virtual.OPENAI_API_KEY) && !text.includes(extra));
      if (kind !== "wrong-scope")
        assert(!events(path, "facts").some((e) => e.kind === "tool_request"));
      if (kind === "http") {
        const exported = JSON.parse(
          readFileSync(path + "-summary.json", "utf8"),
        );
        assert(
          exported.scenes[0].received_responses === 1 &&
            exported.scenes[0].projected_responses === 0,
        );
        assert(
          result.transport_attempts === 1 &&
            result.received_responses === 1 &&
            result.projected_responses === 0,
        );
      }
      controls.push({
        name:
          kind +
          " safe sealed failure/diagnostic replay, no credential/raw reasoning or correction",
        passed: true,
      });
    }
    for (const [kind, limits] of [
      ["outbound", { ...LIMITS, inputBytes: 1 }],
      ["rounds", { ...LIMITS, rounds: 1 }],
      ["calls", { ...LIMITS, calls: 0 }],
    ] as const) {
      const path = join(out, kind);
      const result = await executeP1({
        mode: "transport-rehearsal",
        scene: "facts",
        env: virtual,
        transport: transport("facts"),
        cli,
        artifact,
        out: path,
        limits,
      });
      assert(result.status === "failed");
      assert((await audit(path)).status === "failed");
      if (kind === "outbound")
        assert(
          result.transport_attempts === 0 && result.constructed_requests === 0,
        );
      controls.push({
        name: kind + " original budget and attempt accounting, no retries",
        passed: true,
      });
    }
    let lateReturned = false;
    const latePath = join(out, "deadline");
    const timed = await executeP1({
      mode: "transport-rehearsal",
      scene: "facts",
      env: virtual,
      transport: async () => {
        await new Promise((r) => setTimeout(r, 600));
        lateReturned = true;
        return new Response("{}");
      },
      cli,
      artifact,
      out: latePath,
      limits: { ...LIMITS, milliseconds: 250 },
    });
    assert(timed.status === "failed");
    const sealed = verifyArchive(latePath).seal_sha256;
    await new Promise((r) => setTimeout(r, 700));
    assert(lateReturned && verifyArchive(latePath).seal_sha256 === sealed);
    assert((await audit(latePath)).status === "failed");
    controls.push({
      name: "deadline seals fixed failure, late transport cannot record or dispatch after termination",
      passed: true,
    });
    const livePath = join(out, "live-mock");
    const mock = transport("history");
    globalThis.fetch = Object.assign(
      async (input: string | URL | Request, init?: RequestInit) => {
        mockedLiveFetches++;
        return mock(input, init);
      },
      { preconnect: savedFetch.preconnect },
    );
    const live = await executeP1({
      mode: "live",
      scene: "history",
      env: virtual,
      cli,
      artifact,
      out: livePath,
    });
    globalThis.fetch = blocked;
    assert(
      live.status === "pending" &&
        live.real_model_requests === 2 &&
        live.transport_attempts === 2 &&
        mockedLiveFetches === 2,
    );
    assert((await audit(livePath)).status === "pending");
    controls.push({
      name: "live branch uses fetch only when explicitly invoked; mocked fetch2, actual network0; pending cannot be scripted",
      passed: true,
    });
    const missing = await executeP1({
      mode: "live",
      scene: "facts",
      env: {},
      cli,
      artifact,
      out: join(out, "missing-config"),
    });
    assert(
      missing.status === "failed" &&
        missing.scenes.length === 0 &&
        missing.real_model_requests === 0,
    );
    assert((await audit(join(out, "missing-config"))).status === "failed");
    controls.push({
      name: "missing standard config sealed before preflight, zero transport attempts",
      passed: true,
    });
    const badArtifactPath = join(out, "unreadable-artifact");
    const badArtifact = await executeP1({
      mode: "transport-rehearsal",
      scene: "history",
      env: virtual,
      transport: transport("history"),
      cli,
      artifact: out,
      out: badArtifactPath,
    });
    assert(
      badArtifact.status === "failed" &&
        badArtifact.scenes.length === 0 &&
        badArtifact.transport_attempts === 0,
    );
    assert((await audit(badArtifactPath)).status === "failed");
    assert(
      JSON.parse(readFileSync(join(badArtifactPath, "run.json"), "utf8"))
        .declared_artifact_sha256 === null,
    );
    controls.push({
      name: "existing unreadable artifact path still seals preflight diagnostic without secondary hash failure",
      passed: true,
    });
    for (const [name, edit] of [
      [
        "selected-scene",
        (v: JsonObject) => {
          v.scenes = ["facts"];
        },
      ],
      [
        "attempt-count",
        (v: JsonObject) => {
          v.real_model_requests = 0;
          v.transport_attempts = 0;
        },
      ],
    ] as const) {
      const original = verifyArchive(livePath),
        target = join(out, "tamper-" + name),
        a = PrivateArchive.create(target);
      for (const file of Object.keys(original.files)) {
        const raw = readFileSync(join(livePath, file), "utf8");
        if (file.endsWith(".jsonl")) {
          const scene = file.split("/")[0];
          a.startEvents(scene);
          for (const l of raw.trim().split("\n"))
            a.record(scene, JSON.parse(l));
        } else {
          const v = JSON.parse(raw);
          if (
            file === (name === "selected-scene" ? "run.json" : "summary.json")
          )
            edit(v);
          a.save(file, v);
        }
      }
      a.seal();
      await assert.rejects(() => replayP1(target, sha));
      controls.push({
        name: name + " re-sealed cross-scene/count forgery rejected",
        passed: true,
      });
    }
    assert(unmockedFetches === 0);
  } finally {
    globalThis.fetch = savedFetch;
  }
  return {
    schema: "ossie-p1-transport-controls-v1",
    passed: true,
    actual_network_requests: 0,
    actual_model_requests: 0,
    actual_provider_config_read: false,
    env_test_read: false,
    mocked_live_fetches: mockedLiveFetches,
    unmocked_fetches: unmockedFetches,
    artifact_sha256: sha,
    controls,
    summaries,
  };
}
if (import.meta.main) {
  const a = process.argv.slice(2);
  assert(
    a.length === 4,
    "Usage: verify_p1_agent_transport.ts CLI EXTENSION NEW_PRIVATE_ROOT REPORT",
  );
  const result = await verifyP1Transport(
    resolve(a[0]),
    resolve(a[1]),
    resolve(a[2]),
  );
  writeFileSync(resolve(a[3]), JSON.stringify(result, null, 2) + "\n", {
    flag: "wx",
  });
  console.log(
    `PASS P1 transport: ${result.controls.length} controls; actual network/model0`,
  );
}

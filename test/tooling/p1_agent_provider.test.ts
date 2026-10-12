import { test, expect } from "bun:test";
import {
  p1Configuration,
  p1Transport,
  P1ProviderFailure,
  p1ProtectedFailure,
} from "../../scripts/p1_agent_provider.ts";
import { p1LiveArguments } from "../../scripts/p1_agent_run.ts";
import { executeP1 } from "../../scripts/p1_agent.ts";
const env = {
  OPENAI_MODEL: "test-model",
  OPENAI_API_KEY: "TEST_ONLY_VIRTUAL_SECRET_LONG",
};
test("P1 config only accepts standard variables and safe HTTPS bases", () => {
  expect(p1Configuration(env).baseURL).toBe("https://api.openai.com/v1/");
  expect(
    p1Configuration({
      ...env,
      OPENAI_BASE_URL: "https://provider.invalid/proxy/v1",
    }).baseURL,
  ).toBe("https://provider.invalid/proxy/v1/");
  for (const url of [
    "http://provider.invalid/v1/",
    "https://u:p@provider.invalid/v1/",
    "https://provider.invalid/v1/?q=x",
    "https://provider.invalid/v1/#a",
  ])
    expect(() => p1Configuration({ ...env, OPENAI_BASE_URL: url })).toThrow();
  expect(() =>
    p1Configuration({ OPENAI_MODEL: "m", LEGACY_API_KEY: "test" }),
  ).toThrow("OPENAI_API_KEY");
  expect(() => p1Configuration({ OPENAI_API_KEY: "test" })).toThrow(
    "OPENAI_MODEL",
  );
});
test("single-scene live CLI requires explicit run and token-budget-only, with no config flags", () => {
  const args = [
    "--run",
    "--token-budget-only",
    "--scene",
    "history",
    "--cli",
    "/path/duckdb",
    "--extension",
    "/path/ext",
    "--out",
    "/path/new",
  ];
  expect(p1LiveArguments(args).scene).toBe("history");
  for (const bad of [
    args.slice(1),
    [...args, "--env", ".env_test"],
    args.map((x) => (x === "history" ? "all" : x)),
  ])
    expect(() => p1LiveArguments(bad)).toThrow();
});
test("live mode rejects transport/scripted overrides before accessing environment or files", async () => {
  const forbidden = new Proxy(
    {},
    {
      get() {
        throw Error("environment touched");
      },
    },
  );
  for (const override of [
    { transport: async () => new Response("{}") },
    { simulate: () => "{}" },
    { review: () => "scripted_pass" },
    { cleanupFault: async () => {} },
  ]) {
    await expect(
      executeP1({
        mode: "live",
        scene: "facts",
        env: forbidden,
        cli: "x",
        artifact: "x",
        out: "x",
        ...override,
      } as never),
    ).rejects.toThrow(/refused|isolation/);
  }
});
test("transport records fixed diagnostics without exception text or headers", async () => {
  const events: unknown[] = [];
  const config = p1Configuration(env);
  const signal = new AbortController().signal;
  const call = p1Transport(
    config,
    async () => {
      throw Error(env.OPENAI_API_KEY);
    },
    (e) => events.push(e),
    2097152,
  );
  await expect(
    call("https://provider.invalid/v1/responses", { signal }),
  ).rejects.toThrow("transport_failure");
  expect(JSON.stringify(events)).not.toContain(env.OPENAI_API_KEY);
  expect(JSON.stringify(events)).not.toContain("Authorization");
  expect(p1ProtectedFailure(Error(env.OPENAI_API_KEY), "loop").code).toBe(
    "validation_failure",
  );
});
test("credential echoes in public text, arguments and response identity stop before recording body", async () => {
  for (const body of [
    { id: env.OPENAI_API_KEY, output: [] },
    {
      id: "r",
      output: [
        {
          type: "function_call",
          name: "tool",
          call_id: "c",
          arguments: env.OPENAI_API_KEY,
        },
      ],
    },
    {
      id: "r",
      output: [
        {
          type: "message",
          role: "assistant",
          content: [{ type: "output_text", text: env.OPENAI_API_KEY }],
        },
      ],
    },
  ]) {
    const events: unknown[] = [];
    const config = p1Configuration(env);
    const request = p1Transport(
      config,
      async () =>
        new Response(
          JSON.stringify({ ...body, status: "completed", model: "m" }),
        ),
      (e) => events.push(e),
      2097152,
    );
    await expect(
      request("https://provider.invalid/v1/responses", {
        signal: new AbortController().signal,
      }),
    ).rejects.toThrow("credential_echo");
    expect(JSON.stringify(events)).not.toContain(env.OPENAI_API_KEY);
    expect(JSON.stringify(events)).not.toContain("provider_projection");
  }
});
test("HTTP failure retains only status and counts attempt; raw body is not read", async () => {
  const events: unknown[] = [];
  const request = p1Transport(
    p1Configuration(env),
    async () => new Response(env.OPENAI_API_KEY, { status: 401 }),
    (e) => events.push(e),
    2097152,
  );
  await expect(
    request("https://provider.invalid/v1/responses", {
      signal: new AbortController().signal,
    }),
  ).rejects.toThrow("http_failure");
  expect(events).toEqual([
    { kind: "transport_attempt" },
    { kind: "transport_response", http_status: 401 },
    {
      kind: "provider_failure",
      code: "http_failure",
      diagnostic_only: true,
      http_status: 401,
    },
  ]);
  expect(
    p1ProtectedFailure(new P1ProviderFailure("http_failure", 401), "loop")
      .http_status,
  ).toBe(401);
});

test("fresh import of live entry and offline APIs never reads provider environment or fetches", async () => {
  const { spawnSync } = await import("node:child_process");
  const { ROOT } = await import("../../scripts/artifact_helpers.ts");
  const files = [
    "p1_agent_run",
    "p1_agent",
    "p1_agent_replay",
    "p1_agent_public",
  ];
  const code = `let reads=0,calls=0;process.env=new Proxy({}, {get(_t,k){if(typeof k==='string'&&(k.startsWith('OPENAI_')||k==='LEGACY_API_KEY')){reads++;throw Error('provider env read on import');}return undefined;}});globalThis.fetch=()=>{calls++;throw Error('fetch on import');};for(const name of ${JSON.stringify(files)}) await import(${JSON.stringify(ROOT + "/scripts/")}+name+'.ts');console.log(JSON.stringify({reads,calls}));`;
  const result = spawnSync(process.execPath, ["--no-env-file", "-e", code], {
    cwd: ROOT,
    env: { PATH: process.env.PATH ?? "" },
    encoding: "utf8",
  });
  expect(result.status).toBe(0);
  expect(JSON.parse(result.stdout.trim())).toEqual({ reads: 0, calls: 0 });
});

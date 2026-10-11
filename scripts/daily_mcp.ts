/** Stock daily-only stdio surface; shared by transport checks and later no-model rehearsal. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { ROOT, temporary, sqlString, digest } from "./artifact_helpers.ts";
import { RpcProcess } from "./mcp_check.ts";
import { INITIALIZE, runtimeMetadata } from "./observation_mcp.ts";
import { binding, type Binding } from "./observation_evidence.ts";
import type { JsonObject, Tool } from "./agent_loop.ts";
export const DAILY_SERVER = join(ROOT, "examples/daily_user_server.sql");
export const DAILY_TOOLS = [
  "population_daily_compile",
  "population_daily_users",
];
export const DAILY_URIS = [
  "capabilities",
  "concepts",
  "daily-surface",
  "fields",
  "metrics",
].map((k) => "ossie://population/" + k);
export function dailyRuntime(cli: string, artifact: string) {
  return {
    ...runtimeMetadata(cli, artifact),
    server_sha256: digest(DAILY_SERVER),
  };
}
export function dailySetup(
  artifact: string,
  threshold: number,
  dataset = "extended",
) {
  assert([10, 20].includes(threshold));
  assert(["extended", "original", "empty"].includes(dataset));
  let text = readFileSync(DAILY_SERVER, "utf8");
  const registry = "INSTALL ossie FROM community;\nLOAD ossie;\n";
  assert(text.includes(registry));
  text = text
    .replace(registry, `LOAD ${sqlString(resolve(artifact))};\n`)
    .replace("bundle-10.json", `bundle-${threshold}.json`);
  const copy =
    "COPY observations FROM 'docs/research/ossie-req-001/multi-period-research/every-day/observations.csv' (HEADER);";
  assert(text.includes(copy));
  if (dataset === "empty") text = text.replace(copy, "");
  if (dataset === "original")
    text = text.replace(
      "multi-period-research/every-day/observations.csv",
      "multi-period-research/observations.csv",
    );
  return text;
}
export async function withDailySession<T>(
  cli: string,
  artifact: string,
  threshold: number,
  dataset: string,
  action: (session: DailySession) => Promise<T>,
) {
  return temporary("ossie-daily-mcp-", async (directory) => {
    const path = join(directory, "server.sql");
    writeFileSync(path, dailySetup(artifact, threshold, dataset));
    const server = new RpcProcess([cli, "-unsigned", "-bail", "-init", path]);
    try {
      return await action(new DailySession(server));
    } finally {
      await server.close();
    }
  });
}
export class DailySession {
  private id = 0;
  readonly events: JsonObject[] = [];
  constructor(readonly server: RpcProcess) {}
  async rpc(method: string, params?: unknown) {
    const id = ++this.id;
    this.events.push({ kind: "rpc_request", id, method, params });
    const reply = await this.server.request(id, method, params);
    this.events.push({ kind: "rpc_response", reply });
    return reply;
  }
  async call(name: string, args: Record<string, unknown>) {
    return this.rpc("tools/call", { name, arguments: args });
  }
  async discover() {
    const hello = await this.rpc("initialize", INITIALIZE);
    assert(
      !hello.error &&
        hello.result?.protocolVersion === INITIALIZE.protocolVersion,
    );
    this.server.send({ jsonrpc: "2.0", method: "notifications/initialized" });
    const listed = await this.rpc("tools/list");
    assert(!listed.error);
    const tools = listed.result?.tools as Tool[];
    assert.deepEqual(tools.map((t) => t.name).sort(), DAILY_TOOLS);
    for (const tool of tools) {
      const properties = tool.inputSchema.properties as Record<
        string,
        { type: string }
      >;
      assert.deepEqual(Object.keys(properties).sort(), [
        "concept",
        "request_json",
      ]);
      assert(Object.values(properties).every((p) => p.type === "string"));
      assert.deepEqual([...(tool.inputSchema.required as string[])].sort(), [
        "concept",
        "request_json",
      ]);
      assert(tool.description?.includes("Unknown outer arguments are ignored"));
    }
    const resources = await this.rpc("resources/list");
    assert(!resources.error);
    assert.deepEqual(
      resources.result?.resources?.map((r) => r.uri).sort(),
      DAILY_URIS,
    );
    const rows: Record<string, JsonObject[]> = {};
    let current: Binding | undefined;
    for (const uri of DAILY_URIS) {
      const reply = await this.rpc("resources/read", { uri });
      assert(!reply.error && !reply.result?.isError);
      const contents = reply.result?.contents as { text: string }[];
      assert.equal(contents.length, 1);
      rows[uri] = JSON.parse(contents[0].text);
      assert(rows[uri].length > 0);
      if (uri.endsWith("daily-surface")) {
        const surface = rows[uri][0];
        assert.equal(surface.schema_version, "ossie-daily-mcp-surface-v1");
        assert.deepEqual(
          JSON.parse(String(surface.tools_json)).sort(),
          DAILY_TOOLS,
        );
        assert(
          surface.generic_objects_tool === false &&
            surface.generic_metrics_tool === false &&
            surface.reload_tool === false,
        );
      } else
        for (const row of rows[uri]) {
          assert.equal(row.profile, "daily-observation-population-v1");
          const actual = binding(row);
          if (current) assert.deepEqual(actual, current);
          else current = actual;
        }
    }
    const cap = rows["ossie://population/capabilities"][0];
    assert.equal(cap.api_version, "2.2.0");
    const native = JSON.parse(String(cap.capabilities_json));
    assert(
      native.daily_user_set.enabled && native.daily_user_set.compile_supported,
    );
    // Native declaration is not a query of the live registry; tools/list above proves exposure.
    assert.equal(native.daily_user_set.mcp_example_registered, false);
    assert(current);
    return { tools, rows, binding: current };
  }
}
export function dailyContent(reply: Awaited<ReturnType<DailySession["call"]>>) {
  assert(!reply.error && !reply.result?.isError, JSON.stringify(reply));
  const blocks = reply.result?.content as { type: string; text: string }[];
  assert(blocks.length === 1 && blocks[0].type === "text");
  return blocks[0].text;
}

/** Independent state-only registry, native program disclosure and stock stdio transport. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { ROOT, temporary, sqlString, digest } from "./artifact_helpers.ts";
import { RpcProcess } from "./mcp_check.ts";
import { INITIALIZE, runtimeMetadata } from "./observation_mcp.ts";
import { binding } from "./observation_evidence.ts";
import { stateProgram, sameStateBinding } from "./daily_state_result.ts";
import {
  checkStateDescriptions,
  renderStateDescriptions,
} from "./state_request_notice.ts";
import type { JsonObject, Tool } from "./agent_loop.ts";
export { dailyContent as stateContent } from "./daily_mcp.ts";
export const STATE_SERVER = join(ROOT, "examples/daily_state_server.sql");
export const STATE_TOOLS = [
  "population_daily_states",
  "population_daily_states_compile",
];
export const STATE_URIS = [
  "capabilities",
  "concepts",
  "state-program",
  "state-surface",
].map((k) => "ossie://population/" + k);
export const stateRuntime = (cli: string, artifact: string) => ({
  ...runtimeMetadata(cli, artifact),
  server_sha256: digest(STATE_SERVER),
});
export function stateSetup(
  artifact: string,
  threshold: number,
  dataset = "extended",
) {
  assert([10, 20].includes(threshold));
  assert(["extended", "empty", "budget"].includes(dataset));
  const registry = "INSTALL ossie FROM community;\nLOAD ossie;\n",
    copy =
      "COPY observations FROM 'docs/research/ossie-req-001/multi-period-research/every-day/observations.csv' (HEADER);";
  let text = readFileSync(STATE_SERVER, "utf8");
  assert.equal(
    text,
    renderStateDescriptions(text),
    "state SQL descriptions are stale",
  );
  assert(text.includes(registry) && text.includes(copy));
  text = text
    .replace(registry, `LOAD ${sqlString(resolve(artifact))};\n`)
    .replace("bundle-10.json", `bundle-${threshold}.json`);
  if (dataset === "empty") text = text.replace(copy, "");
  if (dataset === "budget")
    text = text.replace(
      copy,
      "INSERT INTO observations SELECT 'o'||i,'u'||i,DATE '2026-01-31','X',1.00,10 FROM range(3334) t(i);",
    );
  return text;
}
export async function withStateSession<T>(
  cli: string,
  artifact: string,
  threshold: number,
  dataset: string,
  action: (session: StateSession) => Promise<T>,
  onEvent?: (event: JsonObject) => void,
) {
  return temporary("ossie-state-mcp-", async (dir) => {
    const path = join(dir, "server.sql");
    writeFileSync(path, stateSetup(artifact, threshold, dataset));
    const server = new RpcProcess([cli, "-unsigned", "-bail", "-init", path]);
    try {
      return await action(new StateSession(server, onEvent));
    } finally {
      await server.close();
    }
  });
}
export class StateSession {
  private id = 0;
  readonly events: JsonObject[] = [];
  constructor(
    readonly server: RpcProcess,
    readonly onEvent?: (event: JsonObject) => void,
  ) {}
  private record(event: JsonObject) {
    this.events.push(event);
    this.onEvent?.(event);
  }
  async rpc(method: string, params?: unknown) {
    const id = ++this.id;
    this.record({ kind: "rpc_request", id, method, params });
    const reply = await this.server.request(id, method, params);
    this.record({ kind: "rpc_response", reply });
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
    const notification = {
      jsonrpc: "2.0",
      method: "notifications/initialized",
    };
    this.record({ kind: "rpc_notification", notification });
    this.server.send(notification);
    const listed = await this.rpc("tools/list");
    assert(!listed.error);
    const tools = listed.result?.tools as Tool[];
    assert.deepEqual(tools.map((t) => t.name).sort(), STATE_TOOLS);
    checkStateDescriptions(tools);
    for (const t of tools) {
      const p = t.inputSchema.properties as Record<string, { type: string }>;
      assert.deepEqual(Object.keys(p).sort(), ["concept", "request_json"]);
      assert(Object.values(p).every((v) => v.type === "string"));
      assert.deepEqual([...(t.inputSchema.required as string[])].sort(), [
        "concept",
        "request_json",
      ]);
      assert(t.description?.includes("Unknown outer arguments are ignored"));
    }
    const resources = await this.rpc("resources/list");
    assert(!resources.error);
    assert.deepEqual(
      resources.result?.resources?.map((r) => r.uri).sort(),
      STATE_URIS,
    );
    const rows: Record<string, JsonObject[]> = {};
    for (const uri of STATE_URIS) {
      const reply = await this.rpc("resources/read", { uri });
      assert(!reply.error && !reply.result?.isError);
      const content = reply.result?.contents as { text: string }[];
      assert(content.length === 1);
      rows[uri] = JSON.parse(content[0].text);
      assert(rows[uri].length > 0);
    }
    const program = stateProgram(
        JSON.stringify(rows["ossie://population/state-program"]),
      ),
      current = binding(program);
    for (const uri of ["concepts", "capabilities"])
      for (const row of rows["ossie://population/" + uri]) {
        assert(row.profile === "daily-observation-population-v1");
        assert.deepEqual(binding(row), current);
      }
    assert(rows["ossie://population/capabilities"][0].api_version === "2.2.0");
    const surface = rows["ossie://population/state-surface"][0];
    assert(surface.schema_version === "ossie-daily-state-mcp-surface-v1");
    assert.deepEqual(
      JSON.parse(String(surface.tools_json)).sort(),
      STATE_TOOLS,
    );
    assert(
      surface.reload_tool === false && surface.arbitrary_sql_tool === false,
    );
    // Native program never probes a running MCP registry; actual tools/list establishes exposure.
    sameStateBinding(program, program);
    return { tools, rows, binding: current, program };
  }
}

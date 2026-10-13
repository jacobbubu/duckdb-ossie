/** Stock stdio transport and a history-only registry; native program does not probe registry. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { ROOT, temporary, sqlString, digest } from "./artifact_helpers.ts";
import { RpcProcess } from "./mcp_check.ts";
import { INITIALIZE, runtimeMetadata } from "./observation_mcp.ts";
import { binding } from "./observation_evidence.ts";
import { historyProgram } from "./history_result.ts";
import { HISTORY_FIXTURE } from "./history_oracle.ts";
import {
  checkHistoryDescriptions,
  renderHistoryDescriptions,
} from "./history_request_notice.ts";
import type { JsonObject, Tool } from "./agent_loop.ts";
export { dailyContent as historyContent } from "./daily_mcp.ts";
export const HISTORY_SERVER = join(ROOT, "examples/history_server.sql");
export const HISTORY_TOOLS = [
  "population_history_links",
  "population_history_links_compile",
];
export const HISTORY_URIS = [
  "ossie://population/history-program",
  "ossie://population/history-surface",
];
export const historyRuntime = (cli: string, artifact: string) => ({
  ...runtimeMetadata(cli, artifact),
  server_sha256: digest(HISTORY_SERVER),
});
export function historySetup(artifact: string, dataset = "extended") {
  assert(["extended", "empty", "budget", "large"].includes(dataset));
  const registry = "INSTALL ossie FROM community;\nLOAD ossie;\n",
    marker = ".read " + HISTORY_FIXTURE;
  let text = readFileSync(HISTORY_SERVER, "utf8");
  assert.equal(
    text,
    renderHistoryDescriptions(text),
    "history SQL descriptions are stale",
  );
  assert(text.includes(registry) && text.includes(marker));
  const rows =
    dataset === "empty"
      ? ""
      : dataset === "large"
        ? "INSERT INTO observations SELECT 'u',DATE '2024-02-29','p'||i FROM range(10000) t(i);\nINSERT INTO history VALUES ('a','u','T',DATE '2024-02-28',DATE '2024-03-01');"
        : dataset === "budget"
          ? "INSERT INTO observations SELECT 'u',DATE '2024-02-29','p'||i FROM range(1000) t(i);\nINSERT INTO history VALUES ('a','u',repeat('T',1000000),DATE '2024-02-28',DATE '2024-03-01');"
          : readFileSync(join(ROOT, HISTORY_FIXTURE), "utf8");
  return text
    .replace(registry, `LOAD ${sqlString(resolve(artifact))};\n`)
    .replace(marker, rows);
}
export async function withHistorySession<T>(
  cli: string,
  artifact: string,
  dataset: string,
  action: (session: HistorySession) => Promise<T>,
  onEvent?: (event: JsonObject) => void,
) {
  return temporary("ossie-history-mcp-", async (dir) => {
    const path = join(dir, "server.sql");
    writeFileSync(path, historySetup(artifact, dataset));
    const server = new RpcProcess([cli, "-unsigned", "-bail", "-init", path]);
    try {
      return await action(new HistorySession(server, onEvent));
    } finally {
      await server.close();
    }
  });
}
export class HistorySession {
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
    assert.deepEqual(tools.map((t) => t.name).sort(), HISTORY_TOOLS);
    checkHistoryDescriptions(tools);
    for (const t of tools) {
      const p = t.inputSchema.properties as Record<string, { type: string }>;
      assert.deepEqual(Object.keys(p).sort(), ["relationship", "request_json"]);
      assert(Object.values(p).every((v) => v.type === "string"));
      assert.deepEqual([...(t.inputSchema.required as string[])].sort(), [
        "relationship",
        "request_json",
      ]);
      assert(t.description?.includes("Unknown outer arguments are ignored"));
    }
    const resources = await this.rpc("resources/list");
    assert(!resources.error);
    assert.deepEqual(
      resources.result?.resources?.map((r) => r.uri).sort(),
      HISTORY_URIS,
    );
    const rows: Record<string, JsonObject[]> = {};
    let program: Record<string, unknown> | undefined;
    for (const uri of HISTORY_URIS) {
      const reply = await this.rpc("resources/read", { uri });
      assert(!reply.error && !reply.result?.isError);
      const content = reply.result?.contents as { text: string }[];
      assert(content.length === 1);
      if (uri === HISTORY_URIS[0]) program = historyProgram(content[0].text);
      rows[uri] = JSON.parse(content[0].text);
      assert(rows[uri].length === 1);
    }
    assert(program);
    const surface = rows[HISTORY_URIS[1]][0];
    assert(surface.schema_version === "ossie-history-mcp-surface-v1");
    assert.deepEqual(
      JSON.parse(String(surface.tools_json)).sort(),
      HISTORY_TOOLS,
    );
    assert(
      surface.reload_tool === false && surface.arbitrary_sql_tool === false,
    );
    return { tools, rows, program, binding: binding(program) };
  }
}

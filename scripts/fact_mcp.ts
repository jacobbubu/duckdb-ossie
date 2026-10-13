/** Independent fact registry; no model or generic SQL exposure. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { ROOT, temporary, sqlString, digest } from "./artifact_helpers.ts";
import { RpcProcess } from "./mcp_check.ts";
import { DailySession, dailyContent } from "./daily_mcp.ts";
import { INITIALIZE, runtimeMetadata } from "./observation_mcp.ts";
import type { JsonObject, Tool } from "./agent_loop.ts";
import { factProgram, factJSON } from "./fact_result.ts";
import { relationDate } from "./relation_result.ts";
import { binding } from "./observation_evidence.ts";
import {
  FACT_CONTRACT_URI,
  factRequestContract,
  factContractToolDescriptions,
} from "./fact_request_contract.ts";
export const FACT_SERVER = join(ROOT, "examples/fact_server.sql");
export const FACT_TOOLS = [
  "population_fact_users",
  "population_fact_users_compile",
  "population_fact_windows",
  "population_fact_windows_compile",
];
export const FACT_URIS = [
  "ossie://facts/program",
  "ossie://facts/surface",
  FACT_CONTRACT_URI,
];
export const factContent = dailyContent;
export const factRuntime = (cli: string, artifact: string) => ({
  ...runtimeMetadata(cli, artifact),
  server_sha256: digest(FACT_SERVER),
});
export class FactSession extends DailySession {
  override async discover() {
    const hello = await this.rpc("initialize", INITIALIZE);
    assert(
      !hello.error &&
        hello.result?.protocolVersion === INITIALIZE.protocolVersion,
    );
    const notification = {
      jsonrpc: "2.0",
      method: "notifications/initialized",
    };
    this.events.push({ kind: "rpc_notification", notification });
    this.onEvent?.({ kind: "rpc_notification", notification });
    this.server.send(notification);
    const listed = await this.rpc("tools/list");
    assert(!listed.error);
    const tools = listed.result?.tools as Tool[];
    assert.deepEqual(tools.map((t) => t.name).sort(), FACT_TOOLS);
    for (const t of tools) {
      assert.deepEqual(Object.keys(t.inputSchema.properties as object), [
        "request_json",
      ]);
      assert.deepEqual(t.inputSchema.required, ["request_json"]);
      assert.equal(
        (t.inputSchema.properties as Record<string, { type: string }>)
          .request_json.type,
        "string",
      );
      assert(t.description?.includes("Unknown outer arguments are ignored"));
    }
    factContractToolDescriptions(tools);
    const resources = await this.rpc("resources/list");
    assert(!resources.error);
    assert.deepEqual(
      resources.result?.resources?.map((r) => r.uri).sort(),
      [...FACT_URIS].sort(),
    );
    const rows: Record<string, JsonObject[]> = {};
    const raw: Record<string, string> = {};
    for (const uri of FACT_URIS) {
      const reply = await this.rpc("resources/read", { uri });
      assert(!reply.error && !reply.result?.isError);
      const contents = reply.result?.contents as { text: string }[];
      assert.equal(contents.length, 1);
      raw[uri] = contents[0].text;
      rows[uri] = factJSON(contents[0].text, 8 * 1048576) as JsonObject[];
      assert.equal(rows[uri].length, 1);
    }
    const program = factProgram(raw[FACT_URIS[0]]);
    factRequestContract(raw[FACT_CONTRACT_URI]);
    const surface = rows[FACT_URIS[1]][0];
    assert.deepEqual(
      Object.keys(surface).sort(),
      [
        "schema_version",
        "tools_json",
        "generic_objects_tool",
        "reload_tool",
        "arbitrary_sql_tool",
        "argument_boundary",
      ].sort(),
    );
    assert.equal(surface.schema_version, "ossie-fact-mcp-surface-v1");
    assert.deepEqual(JSON.parse(String(surface.tools_json)), FACT_TOOLS);
    for (const k of [
      "generic_objects_tool",
      "reload_tool",
      "arbitrary_sql_tool",
    ])
      assert.equal(surface[k], false);
    assert.equal(
      surface.argument_boundary,
      "Unknown outer arguments are ignored; clients must send request_json Strings",
    );
    return { tools, rows, binding: binding(program), program };
  }
}
export type FactSetup =
  | "original"
  | "empty"
  | "raw"
  | "wide"
  | "finite-minimum"
  | "full"
  | "oversize";
export async function withFactSession<T>(
  cli: string,
  artifact: string,
  setup: FactSetup,
  action: (session: FactSession) => Promise<T>,
  onEvent?: (event: JsonObject) => void,
) {
  return temporary("ossie-fact-mcp-", async (directory) => {
    let serverSQL = readFileSync(FACT_SERVER, "utf8");
    const registry = "INSTALL ossie FROM community;\nLOAD ossie;";
    assert(serverSQL.includes(registry));
    serverSQL = serverSQL.replace(
      registry,
      `LOAD ${sqlString(resolve(artifact))};`,
    );
    const read = ".read test/fixtures/population-fact-program/rows.sql";
    assert(serverSQL.includes(read));
    let source = readFileSync(
      join(ROOT, "test/fixtures/population-fact-program/rows.sql"),
      "utf8",
    );
    if (setup !== "original") {
      source =
        "CREATE TABLE revenue(who VARCHAR,day DATE,value DECIMAL(18,2));\nCREATE TABLE cost(who VARCHAR,day DATE,item VARCHAR,value DECIMAL(18,2));\n";
      if (setup === "oversize")
        source +=
          "INSERT INTO revenue SELECT repeat(chr(10),128)||'U'||lpad(i::VARCHAR,5,'0'), DATE '2026-01-01', 1.01 FROM range(10000) t(i); INSERT INTO cost SELECT who,day,'X',0.02 FROM revenue;";
      else if (setup === "full")
        source +=
          "INSERT INTO revenue SELECT 'U'||lpad(i::VARCHAR,5,'0'), DATE '2026-01-01', 1.01 FROM range(10000) t(i); INSERT INTO cost SELECT who,day,'X',0.02 FROM revenue;";
      else if (setup !== "empty") {
        const ids = setup === "raw" ? ["é", "a\0", "A", "a"] : ["D"];
        const dates =
          setup === "wide" || setup === "finite-minimum"
            ? [
                ...(setup === "finite-minimum" ? ["5877642-06-23 (BC)"] : []),
                "5877642-06-25 (BC)",
                "0001-01-01 (BC)",
                "2026-01-01",
                "10000-01-01",
                "5881580-07-10",
              ]
            : ["2026-01-01"];
        for (const id of ids)
          for (const date of dates) {
            const sqlDate =
              "DATE '1970-01-01'+(" + relationDate(date) + ")::INTEGER";
            const sqlID = id.includes("\0") ? "'a'||chr(0)" : sqlString(id);
            source += `INSERT INTO revenue VALUES (${sqlID},${sqlDate},1.01); INSERT INTO cost VALUES (${sqlID},${sqlDate},'X',0.02);\n`;
          }
      }
    }
    serverSQL = serverSQL
      .replace(read, setup === "original" ? read : source)
      .replace(
        "'test/fixtures/population-fact-program/bundle.json'",
        sqlString(
          join(ROOT, "test/fixtures/population-fact-program/bundle.json"),
        ),
      );
    const path = join(directory, "server.sql");
    writeFileSync(path, serverSQL);
    const server = new RpcProcess([cli, "-unsigned", "-bail", "-init", path]);
    try {
      return await action(new FactSession(server, onEvent));
    } finally {
      await server.close();
    }
  });
}

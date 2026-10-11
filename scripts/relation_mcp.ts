/** Actual stock stdio relation surface; operator setup is outside the tool registry. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { ROOT, temporary, sqlString, digest } from "./artifact_helpers.ts";
import { RpcProcess } from "./mcp_check.ts";
import { DailySession, dailyContent } from "./daily_mcp.ts";
import { INITIALIZE, runtimeMetadata } from "./observation_mcp.ts";
import { binding } from "./observation_evidence.ts";
import type { Tool, JsonObject } from "./agent_loop.ts";
export const RELATION_SERVER = join(ROOT, "examples/relation_server.sql");
export const RELATION_TOOLS = [
  "population_relation_compile",
  "population_relation_objects",
];
export const RELATION_URIS = [
  "ossie://relations/program",
  "ossie://relations/surface",
];
export const relationContent = dailyContent;
export function relationRuntime(cli: string, artifact: string) {
  return {
    ...runtimeMetadata(cli, artifact),
    server_sha256: digest(RELATION_SERVER),
  };
}
export class RelationSession extends DailySession {
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
    const event = { kind: "rpc_notification", notification };
    this.events.push(event);
    this.onEvent?.(event);
    this.server.send(notification);
    const listed = await this.rpc("tools/list");
    assert(!listed.error);
    const tools = listed.result?.tools as Tool[];
    assert.deepEqual(tools.map((t) => t.name).sort(), RELATION_TOOLS);
    for (const t of tools) {
      const p = t.inputSchema.properties as Record<string, { type: string }>;
      assert.deepEqual(Object.keys(p), ["relationship"]);
      assert(p.relationship.type === "string");
      assert.deepEqual(t.inputSchema.required, ["relationship"]);
      assert(t.description?.includes("Unknown outer arguments are ignored"));
    }
    const resources = await this.rpc("resources/list");
    assert(!resources.error);
    assert.deepEqual(
      resources.result?.resources?.map((r) => r.uri).sort(),
      RELATION_URIS,
    );
    const rows: Record<string, JsonObject[]> = {};
    for (const uri of RELATION_URIS) {
      const r = await this.rpc("resources/read", { uri });
      assert(!r.error && !r.result?.isError);
      const c = r.result?.contents as { text: string }[];
      assert(c.length === 1);
      rows[uri] = JSON.parse(c[0].text);
      assert(rows[uri].length === 1);
    }
    const surface = rows[RELATION_URIS[1]][0];
    assert(surface.schema_version === "ossie-relation-mcp-surface-v1");
    assert.deepEqual(
      JSON.parse(String(surface.tools_json)).sort(),
      RELATION_TOOLS,
    );
    assert(
      surface.generic_objects_tool === false &&
        surface.reload_tool === false &&
        surface.arbitrary_sql_tool === false,
    );
    const program = JSON.parse(String(rows[RELATION_URIS[0]][0].program_json));
    assert(
      program.schema_version === "ossie-relation-program-v1" &&
        program.profile === "observation-relation-program-v1" &&
        program.objects_queryable === true &&
        program.mcp_registered === false &&
        program.load_constraints_validated === true &&
        program.query_executed === false,
    );
    return { tools, rows, binding: binding(program), program };
  }
}
export async function withRelationSession<T>(
  cli: string,
  artifact: string,
  threshold: 10 | 20,
  setup: "extended" | "empty" | "wide" | "raw" | "full",
  action: (s: RelationSession) => Promise<T>,
) {
  return temporary("ossie-relation-mcp-", async (directory) => {
    const fixture = join(ROOT, "test/fixtures/population-relation-program");
    const ontology = JSON.parse(
      readFileSync(join(fixture, "ontology.json"), "utf8"),
    );
    const root = ontology.ontology.find(
      (c: { concept: string }) => c.concept === "DailyObservation",
    );
    root.relationships.find(
      (r: { name: string }) => r.name === "qualified_user",
    ).derived_by = [
      `DailyObservation.classification_value >= ${threshold} AND DailyObservation.user(User)`,
    ];
    const ontologyPath = join(directory, "ontology.json");
    writeFileSync(ontologyPath, JSON.stringify(ontology) + "\n");
    const manifest = JSON.parse(
      readFileSync(join(fixture, "bundle.json"), "utf8"),
    );
    for (const key of ["core", "mapping"])
      manifest[key].path = resolve(fixture, manifest[key].path);
    manifest.ontology.path = ontologyPath;
    manifest.ontology.sha256 = digest(ontologyPath);
    const bundle = join(directory, "bundle.json");
    writeFileSync(bundle, JSON.stringify(manifest) + "\n");
    let sql = readFileSync(RELATION_SERVER, "utf8");
    sql = sql.replace(
      "INSTALL ossie FROM community;\nLOAD ossie;",
      `LOAD ${sqlString(resolve(artifact))};`,
    );
    sql = sql.replace(
      "'test/fixtures/population-relation-program/bundle.json'",
      sqlString(bundle),
    );
    const copy =
      "COPY observations FROM 'docs/research/ossie-req-001/multi-period-research/every-day/observations.csv' (HEADER);";
    assert(sql.includes(copy));
    if (setup === "empty") sql = sql.replace(copy, "");
    if (setup === "wide")
      sql = sql.replace(
        copy,
        "INSERT INTO observations VALUES ('min','D',DATE '5877642-06-25 (BC)','X',NULL,20),('max','D',DATE '5881580-07-10','X',NULL,20),('bc','D',DATE '0001-01-01 (BC)','X',NULL,20),('future','D',DATE '10000-01-01','X',NULL,20),('current','D',DATE '2026-01-01','X',NULL,20);",
      );
    if (setup === "full")
      sql = sql.replace(
        copy,
        "INSERT INTO observations SELECT 'o'||i::VARCHAR, 'U'||lpad(i::VARCHAR,5,'0'),DATE '2026-01-01','X',NULL,20 FROM range(10000) r(i);",
      );
    if (setup === "raw")
      sql = sql.replace(
        copy,
        `INSERT INTO observations VALUES ('a','é',DATE '2026-01-01','p'||chr(10),NULL,20),('b','a'||chr(0),DATE '2026-01-01','p',NULL,20),('c','A',DATE '2026-01-01','"'||chr(92),NULL,20),('d','a',DATE '2026-01-01','x',NULL,20);`,
      );
    const path = join(directory, "server.sql");
    writeFileSync(path, sql);
    const server = new RpcProcess([cli, "-unsigned", "-bail", "-init", path]);
    try {
      return await action(new RelationSession(server));
    } finally {
      await server.close();
    }
  });
}

/** Actual stock stdio session shared by the observation verifier and prepare-only driver. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { ROOT, digest, temporary, sqlString } from "./artifact_helpers.ts";
import { RpcProcess } from "./mcp_check.ts";
import {
  binding,
  BINDING_KEYS,
  METRIC_EXPRESSIONS,
  OBSERVATION_PROFILE,
  type Binding,
} from "./observation_evidence.ts";
import type { Tool, JsonObject } from "./agent_loop.ts";
export const SERVER = join(ROOT, "examples/observation_server.sql");
export const INITIALIZE = {
  protocolVersion: "2024-11-05",
  capabilities: {},
  clientInfo: { name: "ossie-observation-preflight", version: "1" },
};
export const URIS = ["capabilities", "concepts", "fields", "metrics"].map(
  (k) => "ossie://population/" + k,
);
export const TOOL_NAMES = [
  "population_compile",
  "population_objects",
  "population_query",
];
export function runtimeMetadata(cli: string, artifact: string) {
  const result = spawnSync(
    cli,
    [
      "-json",
      "-c",
      "LOAD duckdb_mcp; SELECT version() AS version, (SELECT source_id FROM pragma_version()) AS source_id, extension_version, install_path FROM duckdb_extensions() WHERE extension_name='duckdb_mcp'",
    ],
    { encoding: "utf8", timeout: 10000 },
  );
  assert.equal(result.status, 0, result.stderr);
  const runtime = JSON.parse(result.stdout)[0];
  assert.equal(runtime.version, "v1.5.6");
  assert.equal(runtime.source_id, "069cc9f9b5");
  return {
    ...runtime,
    cli_sha256: digest(cli),
    mcp_sha256: digest(runtime.install_path),
    extension_sha256: digest(artifact),
    server_sha256: digest(SERVER),
  };
}
export function setupText(artifact: string, threshold: number) {
  assert([10, 20].includes(threshold));
  const source = readFileSync(SERVER, "utf8");
  const registry = "INSTALL ossie FROM community;\nLOAD ossie;\n";
  assert(source.includes(registry), "exact artifact replacement required");
  return source
    .replace(registry, `LOAD ${sqlString(resolve(artifact))};\n`)
    .replace("bundle-10.json", `bundle-${threshold}.json`);
}
export async function withObservationSession<T>(
  cli: string,
  artifact: string,
  threshold: number,
  action: (session: ObservationSession) => Promise<T>,
  onEvent?: (event: JsonObject) => void,
) {
  return temporary("ossie-observation-mcp-", async (directory) => {
    const path = join(directory, "server.sql");
    writeFileSync(path, setupText(artifact, threshold));
    const server = new RpcProcess([cli, "-unsigned", "-bail", "-init", path]);
    const session = new ObservationSession(server, threshold, onEvent);
    try {
      return await action(session);
    } finally {
      await server.close();
    }
  });
}
export class ObservationSession {
  private id = 0;
  readonly events: JsonObject[] = [];
  constructor(
    readonly server: RpcProcess,
    readonly threshold: number,
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
  async call(name: string, args: Record<string, string>) {
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
    const toolReply = await this.rpc("tools/list");
    assert(!toolReply.error);
    const tools = toolReply.result?.tools as Tool[];
    assert.deepEqual(tools.map((t) => t.name).sort(), TOOL_NAMES);
    for (const tool of tools) {
      const properties = tool.inputSchema.properties as Record<
        string,
        { type: string }
      >;
      const expected =
        tool.name === "population_objects"
          ? ["concept", "filters"]
          : ["concept", "dimensions", "filters", "metrics"];
      assert.deepEqual(Object.keys(properties).sort(), expected);
      assert.deepEqual(
        [...(tool.inputSchema.required as string[])].sort(),
        expected,
      );
      assert(Object.values(properties).every((p) => p.type === "string"));
      assert(tool.description?.includes("referent-only"));
    }
    const list = await this.rpc("resources/list");
    assert(!list.error);
    assert.deepEqual(list.result?.resources?.map((r) => r.uri).sort(), URIS);
    const resources: JsonObject[] = [];
    const rowsByKind: Record<string, JsonObject[]> = {};
    let expectedBinding: Binding | undefined;
    const manifestPath = join(
      ROOT,
      `test/fixtures/population-observation/bundle-${this.threshold}.json`,
    );
    const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
    for (const kind of ["core", "ontology", "mapping"])
      assert.equal(
        digest(resolve(manifestPath, "..", manifest[kind].path)),
        manifest[kind].sha256,
      );
    for (const uri of URIS) {
      const reply = await this.rpc("resources/read", { uri });
      assert(!reply.error && !reply.result?.isError);
      const contents = reply.result?.contents as { text: string }[];
      assert.equal(contents.length, 1);
      const rows = JSON.parse(contents[0].text) as JsonObject[];
      assert(rows.length > 0);
      for (const row of rows) {
        assert.equal(row.profile, OBSERVATION_PROFILE);
        const actual = binding(row);
        if (expectedBinding)
          assert.deepEqual(
            actual,
            expectedBinding,
            "cross-resource binding mismatch",
          );
        else expectedBinding = actual;
        assert.equal(actual.manifest_sha256, digest(manifestPath));
        for (const kind of ["core", "ontology", "mapping"])
          assert.equal(
            actual[`${kind}_sha256` as keyof Binding],
            manifest[kind].sha256,
          );
      }
      rowsByKind[uri.split("/").at(-1)!] = rows;
      resources.push({ evidence_id: uri, response: reply.result });
    }
    const [cap] = rowsByKind.capabilities;
    // Existing observation operations are compatible across the additive 2.1/2.2 APIs.
    assert.match(String(cap.api_version), /^2\.(1|2)\.\d+$/);
    const capabilities = JSON.parse(String(cap.capabilities_json));
    assert.deepEqual(capabilities.concept_discovery, {
      function: "ossie_concepts_v2",
      schema: "ossie-population-concepts-v2",
    });
    assert.equal(cap.plan_schema, "ossie-population-plan-v2");
    assert.deepEqual(capabilities.observation_contract.groupable_fields, [
      "observations.ds",
      "observations.product",
    ]);
    assert.equal(capabilities.observation_contract.user_queryable, false);
    assert.equal(
      capabilities.observation_contract.internal_string_comparison,
      "binary",
    );
    assert.deepEqual(rowsByKind.concepts.map((c) => c.concept).sort(), [
      "DailyObservation",
      "QualifiedObservation",
      "User",
    ]);
    for (const concept of rowsByKind.concepts) {
      const user = concept.concept === "User";
      assert.equal(concept.objects_queryable, !user);
      assert.equal(concept.metrics_queryable, !user);
      assert.equal(String(concept.captured_observation_rows), "8");
      assert.equal(String(concept.captured_user_count), "4");
      const components = JSON.parse(String(concept.identity_components_json));
      assert.deepEqual(
        components.map((c: JsonObject) => c.field),
        user ? ["user_id"] : ["user_id", "ds", "product"],
      );
      assert(components.every((c: JsonObject) => c.dataset === "observations"));
      assert.deepEqual(
        components.map((c: JsonObject) => c.relationship),
        user ? ["id"] : ["user", "date", "product"],
      );
      assert.deepEqual(
        components.map((c: JsonObject) => c.bound_type),
        user ? ["VARCHAR"] : ["VARCHAR", "DATE", "VARCHAR"],
      );
      if (!user)
        assert.deepEqual(components[0].referent, {
          concept: "User",
          relationship: "id",
        });
      if (concept.concept === "QualifiedObservation")
        assert.equal(
          concept.derived_by,
          `DailyObservation.classification_value >= ${this.threshold}`,
        );
    }
    assert.equal(rowsByKind.fields.length, 5);
    const fields = {
      user_id: "VARCHAR",
      ds: "DATE",
      product: "VARCHAR",
      amount_usd: "DECIMAL(18,2)",
      classification_value: "INTEGER",
    };
    for (const [name, type] of Object.entries(fields)) {
      const field = rowsByKind.fields.find(
        (f) => f.qualified_name === `observations.${name}`,
      );
      assert(field, `missing field ${name}`);
      assert.equal(field.bound_type, type);
      assert.equal(field.object_column, `observations.${name}`);
    }
    assert.equal(rowsByKind.metrics.length, 5);
    for (const [name, expression] of Object.entries(METRIC_EXPRESSIONS)) {
      const metric = rowsByKind.metrics.find((m) => m.name === name);
      assert(metric, `metric missing ${name}`);
      assert.equal(
        metric.expression,
        expression,
        `metric role mismatch ${name}`,
      );
    }
    return {
      tools,
      resources,
      binding: expectedBinding!,
      rows: rowsByKind,
      manifest,
      manifest_sha256: digest(manifestPath),
      binding_keys: BINDING_KEYS,
    };
  }
}

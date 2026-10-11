/** Actual stock CLI JSON-RPC controls; no in-process or schema-only substitute. */
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { RpcProcess } from "./mcp_check.ts";
import { populationError } from "./population_error.ts";
import { populationResult, resultRecords } from "./population_result.ts";
import {
  assert,
  digest,
  json,
  ROOT,
  sqlString,
  temporary,
  emitReport,
} from "./artifact_helpers.ts";

type Row = Record<string, unknown>;
const EVIDENCE = join(ROOT, "docs/research/ossie-req-001");
const SOURCE = join(ROOT, "examples/population_server.sql");
const FILTERS = [
  "User.product = 'X'",
  "User.date >= DATE '2026-03-01' AND User.date < DATE '2026-04-01'",
];
const METRICS = ["record_count", "gaap_value_count", "gaap_sum", "mean_gaap"];
const INITIALIZE = {
  protocolVersion: "2024-11-05",
  capabilities: {},
  clientInfo: { name: "ossie-population-probe", version: "1" },
};
export async function verify(artifact: string) {
  const cli = Bun.which("duckdb");
  assert(cli, "stock duckdb CLI required on PATH");
  const metadata = spawnSync(
    cli,
    [
      "-json",
      "-c",
      "LOAD duckdb_mcp; SELECT version() AS version, (SELECT source_id FROM pragma_version()) AS source_id, extension_version, install_path FROM duckdb_extensions() WHERE extension_name='duckdb_mcp'",
    ],
    { encoding: "utf8", timeout: 10000 },
  );
  assert.equal(metadata.status, 0, metadata.stderr);
  const runtime = JSON.parse(metadata.stdout)[0] as {
    version: string;
    source_id: string;
    extension_version: string;
    install_path: string;
  };
  assert.deepEqual(
    [runtime.version, runtime.source_id],
    ["v1.5.6", "069cc9f9b5"],
  );
  const mcpRuntime = {
    engine_version: runtime.version,
    source_id: runtime.source_id,
    extension_version: runtime.extension_version,
    artifact_sha256: digest(runtime.install_path),
  };
  const source = readFileSync(SOURCE, "utf8");
  assert(source.includes("INSTALL ossie FROM community;\nLOAD ossie;\n"));
  const start = source.indexOf("-- BEGIN OPERATOR SETUP"),
    end = source.indexOf("-- END OPERATOR SETUP");
  assert(start >= 0 && end > start, "operator setup markers required");
  const baseline = json<{
    fixture_sha256: string;
    cases: Record<
      string,
      {
        record_count: number;
        gaap_sum: string;
        region_user_type_groups: {
          ds: string;
          region: string;
          user_type: string;
          record_count: number;
          gaap_sum: string;
        }[];
        daily: {
          ds: string;
          record_count: number;
          gaap_sum: string;
          gaap_average: string;
        }[];
      }
    >;
  }>(join(EVIDENCE, "baseline-results.json"));
  assert.equal(
    digest(join(EVIDENCE, "gaap-original-475.csv")),
    baseline.fixture_sha256,
  );
  const checks: string[] = [],
    cases: Row[] = [],
    refusals: Row[] = [],
    observations: Row[] = [];
  function check(name: string, action: () => void) {
    action();
    checks.push(name);
  }
  await temporary("ossie-population-mcp-", async (directory) => {
    async function session(
      label: string,
      setup: string | undefined,
      action: (
        server: RpcProcess,
        call: (
          tool: string,
          args: Row,
        ) => Promise<Awaited<ReturnType<RpcProcess["request"]>>>,
        rows: (tool: string, args: Row) => Promise<Row[]>,
      ) => Promise<void>,
    ) {
      let text = source.replace(
        "INSTALL ossie FROM community;\nLOAD ossie;\n",
        `LOAD ${sqlString(resolve(artifact))};\n`,
      );
      if (setup !== undefined) {
        const original = source.replace(
          "INSTALL ossie FROM community;\nLOAD ossie;\n",
          `LOAD ${sqlString(resolve(artifact))};\n`,
        );
        text =
          original.slice(0, original.indexOf("-- BEGIN OPERATOR SETUP")) +
          "-- BEGIN OPERATOR SETUP\n" +
          setup +
          "\n" +
          original.slice(original.indexOf("-- END OPERATOR SETUP"));
      }
      const path = join(directory, label + ".sql");
      writeFileSync(path, text);
      const server = new RpcProcess([
        cli!,
        "-unsigned",
        "-bail",
        "-init",
        path,
      ]);
      let id = 0;
      const call = (tool: string, args: Row) =>
        server.request(++id, "tools/call", { name: tool, arguments: args });
      const rows = async (tool: string, args: Row) => {
        const reply = await call(tool, args);
        assert(!reply.error && !reply.result?.isError, JSON.stringify(reply));
        const content = reply.result?.content as { text?: string }[];
        assert.equal(content.length, 1);
        if (tool !== "population_compile") {
          const result = populationResult(content[0].text!);
          checks.push(label + ": actual typed result " + tool);
          return resultRecords(result);
        }
        const data: unknown = JSON.parse(content[0].text!);
        assert(
          Array.isArray(data) &&
            data.every(
              (x) => x !== null && typeof x === "object" && !Array.isArray(x),
            ),
        );
        return data as Row[];
      };
      try {
        const hello = await server.request(++id, "initialize", INITIALIZE);
        assert(!hello.error && hello.result?.serverInfo);
        server.send({ jsonrpc: "2.0", method: "notifications/initialized" });
        const tools = (await server.request(++id, "tools/list")).result?.tools;
        check(label + ": exclusive tool list", () =>
          assert.deepEqual(tools?.map((t) => t.name).sort(), [
            "population_compile",
            "population_objects",
            "population_query",
          ]),
        );
        for (const tool of tools || []) {
          const schema = (
            tool as unknown as {
              inputSchema: {
                required: string[];
                properties: Record<string, { type: string }>;
              };
            }
          ).inputSchema;
          check(label + ": " + tool.name + " schema", () => {
            assert.deepEqual(
              schema.required.slice().sort(),
              (tool.name === "population_objects"
                ? ["concept", "filters"]
                : ["concept", "dimensions", "filters", "metrics"]
              ).sort(),
            );
            assert(
              Object.values(schema.properties).every(
                (p) => p.type === "string",
              ),
            );
          });
        }
        const resources = (await server.request(++id, "resources/list")).result
          ?.resources;
        check(label + ": discovery URIs", () =>
          assert.deepEqual(resources?.map((r) => r.uri).sort(), [
            "ossie://population/capabilities",
            "ossie://population/concepts",
            "ossie://population/fields",
            "ossie://population/metrics",
          ]),
        );
        const read = await server.request(++id, "resources/read", {
          uri: "ossie://population/concepts",
        });
        assert(!read.error && !read.result?.isError, JSON.stringify(read));
        const resourceContents = read.result?.contents as { text: string }[];
        const concepts: Row[] = JSON.parse(resourceContents[0].text);
        check(label + ": actual discovery content", () =>
          assert.deepEqual(concepts.map((c) => c.concept).sort(), [
            "User",
            "ValidUser",
          ]),
        );
        const vocabulary: Record<string, Row[]> = {};
        for (const kind of ["metrics", "fields", "capabilities"]) {
          const response = await server.request(++id, "resources/read", {
            uri: "ossie://population/" + kind,
          });
          assert(
            !response.error && !response.result?.isError,
            JSON.stringify(response),
          );
          const content = response.result?.contents as { text: string }[];
          vocabulary[kind] = JSON.parse(content[0].text);
        }
        check(label + ": actual versioned vocabulary content", () => {
          const capability = vocabulary.capabilities![0]!;
          assert.equal(capability.api_version, "2.2.0");
          assert.equal(
            capability.discovery_schema,
            "ossie-population-discovery-v1",
          );
          assert.equal(capability.plan_schema, "ossie-population-plan-v1");
          assert.equal(
            JSON.parse(String(capability.capabilities_json)).error_contract
              .schema,
            "ossie-population-error-v1",
          );
          assert(
            vocabulary.metrics!.length > 0 && vocabulary.fields!.length > 0,
          );
          for (const row of [...vocabulary.metrics!, ...vocabulary.fields!]) {
            assert.equal(row.snapshot_id, concepts[0]!.snapshot_id);
            assert.equal(row.core_sha256, concepts[0]!.core_sha256);
            assert.equal(row.load_preflight_validated, true);
            assert.equal(row.execution_verified, false);
            assert.equal(typeof row.bound_type, "string");
          }
        });
        const metric = vocabulary.metrics![0]!,
          field = vocabulary.fields![0]!;
        const generic = await rows("population_query", {
          concept: concepts[0]!.concept,
          metrics: JSON.stringify([metric.name]),
          dimensions: JSON.stringify([field.qualified_name]),
          filters: "[]",
        });
        check(label + ": resource-driven generic query", () => {
          assert(generic.length > 0);
          assert(
            generic.every(
              (row) =>
                Object.hasOwn(row, String(metric.name)) &&
                Object.hasOwn(row, String(field.qualified_name)),
            ),
          );
          assert(
            generic.every(
              (row) => row._ossie_snapshot_id === concepts[0]!.snapshot_id,
            ),
          );
        });
        for (const tool of [
          "query",
          "export",
          "describe",
          "list_tables",
          "database_info",
          "execute",
          "ossie_bundle_load",
          "population_reload",
          "unknown",
        ]) {
          const reply = await call(tool, { sql: "SELECT 1" });
          check(label + ": refuse " + tool, () =>
            assert(JSON.stringify(reply.error).includes("Tool not found")),
          );
        }
        await action(server, call, rows);
        cases.push({
          label,
          server_info: hello.result!.serverInfo,
          concepts,
          vocabulary,
        });
      } finally {
        await server.close();
      }
    }
    for (const threshold of [10, 20]) {
      // Separate trusted startup assets change only ontology rule; agent tool/metric stay identical.
      const setup = source
        .slice(start + "-- BEGIN OPERATOR SETUP".length, end)
        .replace("bundle-10.json", `bundle-${threshold}.json`);
      await session("gaap-" + threshold, setup, async (_server, call, rows) => {
        const args = {
          concept: "ValidUser",
          metrics: JSON.stringify(METRICS),
          dimensions: "[]",
          filters: JSON.stringify(FILTERS),
        };
        const compiled = (await rows("population_compile", args))[0];
        check(`gaap-${threshold}: bound compile`, () => {
          assert.equal(compiled.executed, false);
          assert(String(compiled.sql).includes(String(threshold)));
          assert.equal(
            compiled.core_sha256,
            digest(join(EVIDENCE, "proposed-definitions/gaap.core.json")),
          );
          assert.equal(
            compiled.ontology_sha256,
            digest(
              join(
                EVIDENCE,
                `proposed-definitions/gaap.ontology.threshold-${threshold}.json`,
              ),
            ),
          );
        });
        const plan = JSON.parse(String(compiled.plan_json));
        check(`gaap-${threshold}: actual versioned compile explanation`, () => {
          assert.equal(plan.schema_version, "ossie-population-plan-v1");
          assert.equal(plan.physical_plan, false);
          assert.equal(plan.executed, false);
          assert.equal(plan.snapshot.id, compiled.snapshot_id);
          assert.equal(
            plan.definitions.ontology.sha256,
            compiled.ontology_sha256,
          );
          assert.equal(plan.concept.formal_rule.applied, true);
          assert.equal(
            plan.concept.formal_rule.declared_sql,
            `User.ytd_gaap >= ${threshold}`,
          );
          assert(
            String(compiled.sql).includes(plan.filters.effective_where_sql),
          );
          assert.deepEqual(
            plan.generated_ast.projections
              .slice(-3)
              .map((projection: { alias: string }) => projection.alias),
            [
              "_ossie_snapshot_id",
              "_ossie_content_sha256",
              "_ossie_ontology_sha256",
            ],
          );
        });
        const total = (await rows("population_query", args))[0];
        const expected = baseline.cases[String(threshold)];
        check(`gaap-${threshold}: independent totals and trace`, () => {
          assert.equal(total.record_count, String(expected.record_count));
          assert.equal(total.gaap_value_count, String(expected.record_count));
          assert.equal(Number(total.gaap_sum).toFixed(2), expected.gaap_sum);
          assert.equal(total._ossie_snapshot_id, compiled.snapshot_id);
          assert.equal(total._ossie_content_sha256, compiled.content_sha256);
          assert.equal(total._ossie_ontology_sha256, compiled.ontology_sha256);
        });
        const objects = await rows("population_objects", {
          concept: args.concept,
          filters: args.filters,
        });
        check(`gaap-${threshold}: independent object IDs`, () =>
          assert.deepEqual(
            objects.map((o) => o.entity_id).sort(),
            json<string[]>(
              join(EVIDENCE, `baseline-objects-threshold-${threshold}.json`),
            ),
          ),
        );
        check(`gaap-${threshold}: every object trace`, () =>
          assert(
            objects.every(
              (o) =>
                o.record_id === o.entity_id &&
                o._ossie_snapshot_id === compiled.snapshot_id &&
                o._ossie_content_sha256 === compiled.content_sha256 &&
                o._ossie_ontology_sha256 === compiled.ontology_sha256,
            ),
          ),
        );
        const daily = await rows("population_query", {
          ...args,
          dimensions: '["dws_gaap_di.ds"]',
        });
        check(`gaap-${threshold}: all 15 independent days`, () => {
          assert.equal(daily.length, expected.daily.length);
          for (const target of expected.daily) {
            const actual = daily.find(
              (r) => r["dws_gaap_di.ds"] === target.ds,
            )!;
            assert(actual);
            assert.equal(actual.record_count, String(target.record_count));
            assert.equal(Number(actual.gaap_sum).toFixed(2), target.gaap_sum);
            assert(
              Math.abs(Number(actual.mean_gaap) - Number(target.gaap_average)) <
                1e-10,
            );
          }
        });
        const groups = await rows("population_query", {
          ...args,
          dimensions:
            '["dws_gaap_di.ds","dws_gaap_di.region","dws_gaap_di.user_type"]',
        });
        check(`gaap-${threshold}: every day/region/type group`, () => {
          assert.equal(groups.length, expected.region_user_type_groups.length);
          for (const target of expected.region_user_type_groups) {
            const actual = groups.find(
              (r) =>
                r["dws_gaap_di.ds"] === target.ds &&
                r["dws_gaap_di.region"] === target.region &&
                r["dws_gaap_di.user_type"] === target.user_type,
            );
            assert(actual);
            assert.equal(actual.record_count, String(target.record_count));
            assert.equal(Number(actual.gaap_sum).toFixed(2), target.gaap_sum);
          }
        });
        const group = expected.region_user_type_groups[0];
        const drilled = await rows("population_objects", {
          concept: args.concept,
          filters: JSON.stringify([
            ...FILTERS,
            `User.date = DATE '${group.ds}'`,
            `User.region = '${group.region}'`,
            `User.user_type = '${group.user_type}'`,
          ]),
        });
        check(
          `gaap-${threshold}: drill inherits concept/product/time filters`,
          () =>
            assert.deepEqual(
              drilled.map((r) => r.entity_id).sort(),
              objects
                .filter(
                  (r) =>
                    r["dws_gaap_di.ds"] === group.ds &&
                    r["dws_gaap_di.region"] === group.region &&
                    r["dws_gaap_di.user_type"] === group.user_type,
                )
                .map((r) => r.entity_id)
                .sort(),
            ),
        );
        const emptyArgs = {
          ...args,
          filters: "[\"User.product = 'missing,;value'\"]",
        };
        assert.deepEqual(
          await rows("population_objects", {
            concept: args.concept,
            filters: emptyArgs.filters,
          }),
          [],
        );
        assert.deepEqual(
          await rows("population_query", {
            ...emptyArgs,
            dimensions: '["dws_gaap_di.ds"]',
          }),
          [],
        );
        checks.push(`gaap-${threshold}: sparse empty objects and groups`);
        const empty = (await rows("population_query", emptyArgs))[0];
        check(`gaap-${threshold}: ungrouped empty metrics`, () => {
          assert.equal(empty.record_count, String(0));
          assert.equal(empty.gaap_sum, null);
          assert.equal(empty.mean_gaap, null);
        });
        const objectCompile = (
          await rows("population_compile", { ...args, metrics: "[]" })
        )[0];
        check(`gaap-${threshold}: object compile`, () => {
          assert.equal(objectCompile.executed, false);
          assert(String(objectCompile.sql).includes("entity_id"));
          assert.equal(objectCompile.snapshot_id, compiled.snapshot_id);
        });
        // Both comma inside COALESCE and semicolon/quote inside literals must survive one predicate.
        const complexFilters = [
          ...FILTERS,
          "COALESCE(User.product, 'fallback,;value') = 'X' AND 'it''s,;intact' = 'it''s,;intact'",
        ];
        const intact = (
          await rows("population_query", {
            ...args,
            filters: JSON.stringify(complexFilters),
          })
        )[0];
        check(`gaap-${threshold}: delimiters and escaped quote preserved`, () =>
          assert.deepEqual(intact, total),
        );
        const invalid: [string, Row][] = [];
        for (const field of ["metrics", "dimensions", "filters"]) {
          for (const value of [
            "{",
            "null",
            "{}",
            '"x"',
            "1",
            "[null]",
            "[1]",
            "[true]",
            "[{}]",
            "[[]]",
            null,
            1,
            {},
          ])
            invalid.push([
              field + "=" + JSON.stringify(value),
              { ...args, [field]: value },
            ]);
          const omitted: Row = { ...args };
          delete omitted[field];
          invalid.push(["missing " + field, omitted]);
        }
        invalid.push(
          ["unknown concept", { ...args, concept: "Ghost" }],
          ["null concept", { ...args, concept: null }],
          [
            "missing concept",
            {
              metrics: args.metrics,
              dimensions: args.dimensions,
              filters: args.filters,
            },
          ],
          [
            "injected concept",
            { ...args, concept: "ValidUser'; SELECT 1; --" },
          ],
          ["unknown metric", { ...args, metrics: '["ghost"]' }],
          [
            "unknown dimension",
            { ...args, dimensions: '["dws_gaap_di.secret"]' },
          ],
          ["unknown field", { ...args, filters: '["User.secret = 1"]' }],
          ["unapproved function", { ...args, filters: '["random() > 0"]' }],
          [
            "subquery",
            { ...args, filters: "[\"User.product IN (SELECT 'X')\"]" },
          ],
          ["multiple statements", { ...args, filters: '["TRUE; SELECT 1"]' }],
          [
            "object dimensions",
            { ...args, metrics: "[]", dimensions: '["dws_gaap_di.ds"]' },
          ],
        );
        for (const [label, bad] of invalid) {
          const reply = await call(
            label === "object dimensions"
              ? "population_compile"
              : "population_query",
            bad,
          );
          check(`gaap-${threshold}: reject ${label}`, () =>
            assert(reply.error || reply.result?.isError, JSON.stringify(reply)),
          );
          const carrier = reply.error as { message?: string } | undefined;
          const content = reply.result?.content as
            | { text?: string }[]
            | undefined;
          const envelope = populationError(
            carrier?.message ??
              content?.map((c) => c.text ?? "").join("\n") ??
              "",
          );
          if (label.startsWith("missing ")) {
            check(
              `gaap-${threshold}: external missing parameter ${label}`,
              () => assert.equal(envelope, undefined),
            );
          } else {
            check(`gaap-${threshold}: structured ${label}`, () => {
              assert(envelope, JSON.stringify(reply));
              assert.equal(
                envelope.schema_version,
                "ossie-population-error-v1",
              );
              assert(
                !envelope.reason.startsWith("ossie_population_error_v1: "),
              );
            });
          }
          refusals.push({
            case: threshold,
            label,
            reply,
            envelope,
            external_boundary: !envelope,
          });
        }
        for (const tool of ["population_objects", "population_compile"]) {
          const bad = await call(tool, { ...args, filters: "[null]" });
          check(`gaap-${threshold}: ${tool} rejects NULL list member`, () =>
            assert(bad.error || bad.result?.isError),
          );
        }
        const extra = (
          await rows("population_query", {
            ...args,
            threshold: 0,
            sql: "SELECT 1",
          })
        )[0];
        check(
          `gaap-${threshold}: unused extra parameters cannot change rules`,
          () => assert.deepEqual(extra, total),
        );
        const recovered = (await rows("population_query", args))[0];
        check(`gaap-${threshold}: recovery after refusals`, () =>
          assert.deepEqual(recovered, total),
        );
        // MCP does not enforce schema types: native arrays serialize to the same validated bytes.
        assert.deepEqual(
          (
            await rows("population_query", {
              ...args,
              metrics: METRICS,
              dimensions: [],
              filters: FILTERS,
            })
          )[0],
          total,
        );
        checks.push(`gaap-${threshold}: native array serialization observed`);
        observations.push({
          threshold,
          compiled,
          object_compile: objectCompile,
          total,
          daily,
          groups,
          object_ids: objects.map((r) => r.entity_id).sort(),
          drill_ids: drilled.map((r) => r.entity_id).sort(),
          empty,
        });
      });
    }
    const joinedSetup =
      readFileSync(
        join(ROOT, "test/fixtures/safe-join-research/rows.sql"),
        "utf8",
      ) +
      "\nCALL ossie_bundle_load('test/fixtures/joined-population/bundle.json');";
    await session("joined", joinedSetup, async (_server, call, rows) => {
      const args = {
        concept: "ValidUser",
        metrics: '["record_count","amount_sum"]',
        dimensions: "[]",
        filters: "[]",
      };
      const total = (await rows("population_query", args))[0];
      const compiled = (await rows("population_compile", args))[0];
      check("joined: independent totals/trace", () => {
        assert.equal(total.record_count, String(4));
        assert.equal(Number(total.amount_sum).toFixed(2), "24.98");
        assert.equal(total._ossie_snapshot_id, compiled.snapshot_id);
        assert.equal(compiled.executed, false);
        assert.equal(
          JSON.parse(String(compiled.execution_target)).metric_grain,
          "fact",
        );
      });
      assert.deepEqual(
        (
          await rows("population_objects", {
            concept: args.concept,
            filters: "[]",
          })
        )
          .map((r) => r.entity_id)
          .sort(),
        ["f1", "f2", "f5", "f6"],
      );
      checks.push("joined: member IDs");
      observations.push({
        label: "joined",
        compiled,
        total,
        object_ids: ["f1", "f2", "f5", "f6"],
      });
      const refusal = await call("population_query", {
        ...args,
        metrics: '["dimension_count"]',
      });
      check("joined: metric outside model refused", () =>
        assert(refusal.error || refusal.result?.isError),
      );
    });
    // Fail-fast startup: a bad operator load must never expose a query surface.
    const bad = source
      .replace(
        "INSTALL ossie FROM community;\nLOAD ossie;\n",
        `LOAD ${sqlString(resolve(artifact))};\n`,
      )
      .replace("bundle-10.json", "bundle-does-not-exist.json");
    const path = join(directory, "bad-setup.sql");
    writeFileSync(path, bad);
    const failed = new RpcProcess([cli, "-unsigned", "-bail", "-init", path]);
    try {
      await assert.rejects(
        () => failed.request(1, "initialize", INITIALIZE),
        /MCP server (exited|ended)/,
      );
      checks.push("failed operator startup exposes no server");
    } finally {
      await failed.close();
    }
  });
  return {
    scope:
      "Actual stock CLI population MCP calling contract; not full R7/P0 or autonomous Agent evidence",
    artifact_sha256: digest(artifact),
    cli_sha256: digest(cli),
    mcp_runtime: mcpRuntime,
    source_sha256: Object.fromEntries(
      [
        "examples/population_server.sql",
        "scripts/verify_population_mcp.ts",
        "scripts/population_error.ts",
        "scripts/population_result.ts",
        "scripts/mcp_check.ts",
        "examples/server.sql",
        ".github/workflows/MainDistributionPipeline.yml",
        "scripts/full_functionality_check.sh",
        "docs/research/ossie-req-001/gaap-original-475.csv",
        "test/fixtures/population/bundle-10.json",
        "test/fixtures/population/bundle-20.json",
        "test/fixtures/safe-join-research/rows.sql",
        "test/fixtures/joined-population/bundle.json",
        "test/fixtures/joined-population/core.json",
        "test/fixtures/joined-population/ontology.json",
        "test/fixtures/joined-population/mapping.json",
        "docs/research/ossie-req-001/baseline-results.json",
        "docs/research/ossie-req-001/baseline-objects-threshold-10.json",
        "docs/research/ossie-req-001/baseline-objects-threshold-20.json",
      ].map((p) => [p, digest(join(ROOT, p))]),
    ),
    checks,
    cases,
    refusals,
    observations,
    limitations: [
      "Integer/Decimal typed strings preserve native values; later conversion to Number can lose precision, FLOAT/DOUBLE remain approximate",
      "Schema types are not enforced by MCP; valid native arrays serialize to accepted JSON bytes; extra unused parameters are ignored",
      "Empty row sets carry bound schema and hashes in the typed envelope; no cross-call reload atomicity",
      "Vocabulary comes from the bound operator-approved bundle; compare hashes across discovery and execution",
      "Registry MCP dependency is observed rather than pinned by this repository",
      "No remote CI, cross-platform or actual autonomous Agent validation",
    ],
  };
}
if (import.meta.main) {
  try {
    assert(
      process.argv[2],
      "Usage: bun scripts/verify_population_mcp.ts ARTIFACT [--output RESULT.json]",
    );
    emitReport(await verify(resolve(process.argv[2])));
  } catch (error) {
    console.error(String(error));
    process.exitCode = 1;
  }
}

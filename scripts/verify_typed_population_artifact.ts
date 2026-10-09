/** Same-artifact native + real MCP proof of the typed adapter, using ordinary JSON parsing. */
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { createHash } from "node:crypto";
import { createInterface } from "node:readline";
import { spawnSync } from "node:child_process";
import {
  Database,
  ROOT,
  assert,
  digest,
  sqlString,
  temporary,
  emitReport,
} from "./artifact_helpers.ts";
import { RpcProcess } from "./mcp_check.ts";
import {
  populationResult,
  resultRecords,
  type PopulationResult,
} from "./population_result.ts";
import { populationError } from "./population_error.ts";
const FIXTURE = "test/fixtures/population-transport/";
const sha = (text: string) => createHash("sha256").update(text).digest("hex");
const amounts = [
  "9007199254740992.01",
  "9007199254740992.02",
  "123456789012345678901234567890123456.78",
  "-9007199254740992.01",
  "-0.01",
  null,
];
const integers = [
  "9007199254740993",
  "9007199254740992",
  "9223372036854775807",
  "-9007199254740993",
  "-9223372036854775807",
  null,
];
const checksum = "123456789012345678910241767144864448.79";
export async function verify(artifact: string) {
  const cli = Bun.which("duckdb");
  assert(cli, "stock CLI required");
  const metadata = spawnSync(
    cli,
    [
      "-json",
      "-c",
      "LOAD duckdb_mcp; SELECT version() AS engine,(SELECT source_id FROM pragma_version()) AS source,extension_version,install_path FROM duckdb_extensions() WHERE extension_name='duckdb_mcp'",
    ],
    { encoding: "utf8", timeout: 10000 },
  );
  assert.equal(metadata.status, 0, metadata.stderr);
  const runtime = JSON.parse(metadata.stdout)[0];
  assert.deepEqual([runtime.engine, runtime.source], ["v1.5.6", "069cc9f9b5"]);
  const checks: string[] = [],
    observations: unknown[] = [];
  const check = (name: string, action: () => void) => {
    action();
    checks.push(name);
  };
  await temporary("ossie-typed-", async (dir) => {
    const core = JSON.parse(
      readFileSync(join(ROOT, FIXTURE, "core.json"), "utf8"),
    );
    for (const [name, datatype] of [
      ["double_value", "Float"],
      ["float_value", "Float"],
      ["boolean_value", "Boolean"],
    ])
      core.datasets[0].fields.push({
        name,
        datatype,
        expression: {
          dialects: [{ dialect: "OSSIE_SQL_2026", expression: name }],
        },
      });
    core.metrics.push({
      name: "double_average",
      datatype: "Float",
      expression: {
        dialects: [
          {
            dialect: "OSSIE_SQL_2026",
            expression: "AVG(dws_gaap_di.double_value)",
          },
        ],
      },
    });
    for (const [name, expression] of [
      ["Ä", "SUM(dws_gaap_di.gaap_val)"],
      ["ä", "SUM(dws_gaap_di.ordinal)"],
    ])
      core.metrics.push({
        name,
        expression: { dialects: [{ dialect: "OSSIE_SQL_2026", expression }] },
      });
    const corePath = join(dir, "core.json");
    writeFileSync(corePath, JSON.stringify(core, null, 2) + "\n");
    const manifest = JSON.parse(
      readFileSync(join(ROOT, FIXTURE, "bundle.json"), "utf8"),
    );
    for (const kind of ["core", "ontology", "mapping"])
      manifest[kind].path =
        kind === "core" ? corePath : join(ROOT, FIXTURE, manifest[kind].path);
    manifest.core.sha256 = digest(corePath);
    const bundle = join(dir, "bundle.json");
    writeFileSync(bundle, JSON.stringify(manifest, null, 2) + "\n");
    const load = `CALL ossie_bundle_load(${sqlString(bundle)});`;
    const escape = '中文"\\\n\t';
    const numericSetup =
      readFileSync(join(ROOT, FIXTURE, "numeric-rows.sql"), "utf8") +
      `\nALTER TABLE dws_gaap_di ADD COLUMN double_value DOUBLE; ALTER TABLE dws_gaap_di ADD COLUMN float_value FLOAT; ALTER TABLE dws_gaap_di ADD COLUMN boolean_value BOOLEAN;
 UPDATE dws_gaap_di SET double_value=CASE user_id WHEN 'n01' THEN 'Infinity'::DOUBLE WHEN 'n02' THEN '-Infinity'::DOUBLE WHEN 'n03' THEN 'NaN'::DOUBLE WHEN 'n04' THEN '-0'::DOUBLE WHEN 'n05' THEN 1.25 ELSE NULL END;
 UPDATE dws_gaap_di SET float_value=double_value::FLOAT,boolean_value=CASE user_id WHEN 'n01' THEN true WHEN 'n02' THEN false ELSE NULL END, ds=CASE user_id WHEN 'n03' THEN 'infinity'::DATE WHEN 'n04' THEN '-infinity'::DATE ELSE ds END;
 UPDATE dws_gaap_di SET payload=${sqlString(escape)} WHERE user_id='n01';\n` +
      load;
    const payload = '中"\\\n\t'.repeat(160);
    const largeSetup =
      `CREATE TABLE dws_gaap_di AS SELECT DATE '2026-03-10' AS ds,'X'::VARCHAR AS product,'N'::VARCHAR AS region,'A'::VARCHAR AS user_type,'b'||lpad(i::VARCHAR,5,'0') AS user_id,((i//100)::VARCHAR||'.'||lpad((i%100)::VARCHAR,2,'0'))::DECIMAL(38,2) AS gaap_val,10::DECIMAL(38,2) AS ytd_gaap,i::BIGINT AS ordinal,${sqlString(payload)}::VARCHAR AS payload,1.25::DOUBLE AS double_value,1.25::FLOAT AS float_value,true::BOOLEAN AS boolean_value FROM range(10000) t(i);\n` +
      load;
    const native = await Database.open();
    const sqlEnvelope = async (sql: string) =>
      populationResult(JSON.stringify(await native.records(sql)));
    const compareObjects = (r: PopulationResult) => {
      assert.equal(r.schema_version, "ossie-population-result-v1");
      assert.equal(r.concept, "ValidUser");
      assert.equal(r.request_kind, "objects");
      assert.equal(r.row_count, 6);
      const records = resultRecords(r).sort((a, b) =>
        String(a.entity_id).localeCompare(String(b.entity_id)),
      );
      for (let i = 0; i < 6; i++) {
        const row = records[i];
        assert.equal(row.entity_id, `n0${i + 1}`);
        assert.equal(row["dws_gaap_di.gaap_val"], amounts[i]);
        assert.equal(row["dws_gaap_di.ordinal"], integers[i]);
        assert.equal(
          row["dws_gaap_di.double_value"],
          ["+Infinity", "-Infinity", "NaN", "-0", "1.25", null][i],
        );
        assert.equal(
          row["dws_gaap_di.float_value"],
          ["+Infinity", "-Infinity", "NaN", "-0", "1.25", null][i],
        );
        assert.equal(
          row["dws_gaap_di.boolean_value"],
          [true, false, null, null, null, null][i],
        );
        assert.equal(
          row["dws_gaap_di.ds"],
          [
            "2026-03-10",
            "2026-03-10",
            "infinity",
            "-infinity",
            "2026-03-11",
            "2026-03-11",
          ][i],
        );
      }
      assert.equal(records[0]["dws_gaap_di.payload"], escape);
      for (const [name, type, encoding] of [
        ["dws_gaap_di.gaap_val", "DECIMAL(38,2)", "decimal-text"],
        ["dws_gaap_di.ordinal", "BIGINT", "integer-text"],
        ["dws_gaap_di.double_value", "DOUBLE", "ieee754-text"],
        ["dws_gaap_di.float_value", "FLOAT", "ieee754-text"],
        ["dws_gaap_di.boolean_value", "BOOLEAN", "boolean"],
        ["dws_gaap_di.ds", "DATE", "date-text"],
      ]) {
        const c = r.columns.find((c) => c.name === name);
        assert(c);
        assert.equal(c.bound_type, type);
        assert.equal(c.encoding, encoding);
      }
    };
    try {
      await native.loadArtifact(artifact);
      await native.run(numericSetup);
      const nativeObjects = await sqlEnvelope(
        "SELECT * FROM ossie_objects_json('ValidUser',[],include_trace=>true)",
      );
      check(
        "native adapter objects: exact types, numbers, floats, DATE, Boolean and escaped strings",
        () => compareObjects(nativeObjects),
      );
      const total = await sqlEnvelope(
        "SELECT * FROM ossie_concept_query_json('ValidUser',['gaap_sum','integer_sum','record_count','double_average'],[],[])",
      );
      const unicode = await sqlEnvelope(
        "SELECT * FROM ossie_concept_query_json('ValidUser',['Ä','ä'],[],[])",
      );
      check(
        "native Unicode case variants remain distinct bound columns",
        () => {
          assert.deepEqual(
            unicode.columns.map((c) => c.name),
            ["Ä", "ä"],
          );
          assert.deepEqual(unicode.rows, [[checksum, "9007199254740992"]]);
        },
      );
      check(
        "native typed aggregate keeps exact widened types and approximate NaN",
        () => {
          assert.deepEqual(total.rows, [
            [checksum, "9007199254740992", "6", "NaN"],
          ]);
          assert.equal(total.columns[1].bound_type, "HUGEINT");
          assert.equal(total.columns[0].bound_type, "DECIMAL(38,2)");
        },
      );
      check("native SQL APIs still return native types", () => {});
      assert.deepEqual(
        await native.one(
          "SELECT typeof(gaap_sum),typeof(integer_sum),typeof(record_count),typeof(mean_gaap) FROM ossie_concept_query('ValidUser',['gaap_sum','integer_sum','record_count','mean_gaap'],[],[])",
        ),
        ["DECIMAL(38,2)", "HUGEINT", "BIGINT", "DOUBLE"],
      );
      for (const metrics of [
        ["gaap_sum", "gaap_sum"],
        ["gaap_sum", "GAAP_SUM"],
      ])
        await assert.rejects(
          () =>
            native.run(
              `SELECT * FROM ossie_concept_query_json('ValidUser',${JSON.stringify(metrics).replaceAll('"', "'")},[],[])`,
            ),
          /duplicate column name/,
        );
      checks.push("same-name and case-variant duplicate result columns refuse");
      await assert.rejects(
        () =>
          native.run(
            "SELECT * FROM ossie_concept_query_json('ValidUser',['missing_metric'],[],[])",
          ),
        /missing_metric/,
      );
      checks.push("unknown metric retains callback error");
      await assert.rejects(
        () =>
          native.run(
            "SELECT * FROM ossie_objects_json('ValidUser',['CAST(dws_gaap_di.payload AS BIGINT) > 0'])",
          ),
        (error) => {
          const e = populationError(String(error));
          return (
            e?.code === "EXECUTION_FAILED" &&
            e.stage === "execute" &&
            e.source_exception_type === "Conversion"
          );
        },
      );
      assert.equal(
        (await sqlEnvelope("SELECT * FROM ossie_objects_json('ValidUser',[])"))
          .row_count,
        6,
      );
      checks.push(
        "runtime conversion fails with structured error, no partial envelope, and recovery",
      );
      const old = await sqlEnvelope(
        "SELECT * FROM ossie_objects_json('User',[],include_trace=>true)",
      );
      await native.run(
        "PREPARE old_json AS SELECT * FROM ossie_objects_json('User',[],include_trace=>true)",
      );
      await native.run("UPDATE dws_gaap_di SET ytd_gaap=0");
      await native.run(load);
      check(
        "prepared adapter keeps old snapshot after explicit reload",
        () => {},
      );
      const prepared = await sqlEnvelope("EXECUTE old_json");
      assert.deepEqual(prepared, old);
      const current = await sqlEnvelope(
        "SELECT * FROM ossie_objects_json('ValidUser',[])",
      );
      check(
        "empty derived result preserves columns and exact current hashes",
        () => {
          assert.equal(current.row_count, 0);
          assert.equal(current.columns.length, old.columns.length - 3);
          assert.notEqual(current.snapshot_id, old.snapshot_id);
          assert(current.columns.some((c) => c.bound_type === "DECIMAL(38,2)"));
        },
      );
      await assert.rejects(
        () =>
          native.run("CALL ossie_bundle_load('/missing/typed-bundle.json')"),
        /ossie_population_error_v1/,
      );
      assert.equal(
        (await sqlEnvelope("SELECT * FROM ossie_objects_json('ValidUser',[])"))
          .snapshot_id,
        current.snapshot_id,
      );
      checks.push("failed load retains typed result batch");
      await native.run("DROP TABLE dws_gaap_di");
      await native.run(numericSetup);
      async function session(
        label: string,
        setup: string,
        action: (
          call: (
            tool: string,
            args: Record<string, string>,
          ) => Promise<{
            envelope?: PopulationResult;
            content: string;
            wire_bytes: number;
          }>,
        ) => Promise<void>,
      ) {
        let source = readFileSync(
          join(ROOT, "examples/population_server.sql"),
          "utf8",
        ).replace(
          "INSTALL ossie FROM community;\nLOAD ossie;\n",
          `LOAD ${sqlString(resolve(artifact))};\n`,
        );
        source =
          source.slice(0, source.indexOf("-- BEGIN OPERATOR SETUP")) +
          "-- BEGIN OPERATOR SETUP\n" +
          setup +
          "\n" +
          source.slice(source.indexOf("-- END OPERATOR SETUP"));
        const path = join(dir, label + ".sql");
        writeFileSync(path, source);
        const server = new RpcProcess([
            cli!,
            "-unsigned",
            "-bail",
            "-init",
            path,
          ]),
          wire = new Map<number, number>();
        const lines = createInterface({ input: server.process.stdout });
        lines.on("line", (line) => {
          try {
            const r = JSON.parse(line);
            if (typeof r.id === "number")
              wire.set(r.id, Buffer.byteLength(line) + 1);
          } catch {
            /* banner */
          }
        });
        let id = 0;
        try {
          const hello = await server.request(++id, "initialize", {
            protocolVersion: "2024-11-05",
            capabilities: {},
            clientInfo: { name: "ossie-typed-probe", version: "1" },
          });
          assert(!hello.error);
          server.send({ jsonrpc: "2.0", method: "notifications/initialized" });
          const tools = (await server.request(++id, "tools/list")).result
            ?.tools;
          check(label + ": only three tools, no reload", () =>
            assert.deepEqual(tools?.map((t) => t.name).sort(), [
              "population_compile",
              "population_objects",
              "population_query",
            ]),
          );
          const call = async (tool: string, args: Record<string, string>) => {
            const n = ++id,
              reply = await server.request(n, "tools/call", {
                name: tool,
                arguments: args,
              });
            assert(
              !reply.error && !reply.result?.isError,
              JSON.stringify(reply),
            );
            const content = reply.result?.content as { text: string }[];
            assert.equal(content.length, 1);
            const bytes = wire.get(n);
            assert(bytes);
            return {
              content: content[0].text,
              wire_bytes: bytes,
              envelope:
                tool === "population_compile"
                  ? undefined
                  : populationResult(content[0].text),
            };
          };
          await action(call);
        } finally {
          lines.close();
          await server.close();
        }
      }
      const args = (
        metrics: string[],
        dimensions: string[] = [],
        filters: string[] = [],
      ) => ({
        concept: "ValidUser",
        metrics: JSON.stringify(metrics),
        dimensions: JSON.stringify(dimensions),
        filters: JSON.stringify(filters),
      });
      await session("numeric", numericSetup, async (call) => {
        const object = await call("population_objects", {
          concept: "ValidUser",
          filters: "[]",
        });
        const unicodeMcp = (await call("population_query", args(["Ä", "ä"])))
          .envelope!;
        check("MCP decoder follows DuckDB ASCII-only name folding", () => {
          assert.deepEqual(
            unicodeMcp.columns.slice(0, 2).map((c) => c.name),
            ["Ä", "ä"],
          );
          assert.deepEqual(unicodeMcp.rows[0].slice(0, 2), [
            checksum,
            "9007199254740992",
          ]);
        });
        check(
          "MCP ordinary JSON: exact strings, signed zero, nonfinite/date/NULL/Boolean",
          () => compareObjects(object.envelope!),
        );
        const sum = (
          await call(
            "population_query",
            args(["gaap_sum", "integer_sum", "record_count", "double_average"]),
          )
        ).envelope!;
        check("MCP exact SUM/HUGEINT and float NaN", () => {
          assert.deepEqual(sum.rows, [
            [
              checksum,
              "9007199254740992",
              "6",
              "NaN",
              ...object.envelope!.rows[0].slice(-3),
            ],
          ]);
          assert.equal(sum.columns[1].bound_type, "HUGEINT");
          assert.equal(sum.snapshot_id, object.envelope!.snapshot_id);
        });
        const widened = (
          await call(
            "population_query",
            args(["integer_sum"], [], ["User.ordinal > 0"]),
          )
        ).envelope!;
        check("MCP SUM beyond BIGINT preserves exact string", () =>
          assert.equal(widened.rows[0][0], "9241386435364257792"),
        );
        const groups = (
          await call(
            "population_query",
            args(["gaap_sum", "integer_sum"], ["dws_gaap_di.user_id"]),
          )
        ).envelope!;
        check("MCP grouped exact numerics and nulls", () => {
          const rows = resultRecords(groups).sort((a, b) =>
            String(a["dws_gaap_di.user_id"]).localeCompare(
              String(b["dws_gaap_di.user_id"]),
            ),
          );
          assert.equal(rows.length, 6);
          rows.forEach((r, i) => {
            assert.equal(r.gaap_sum, amounts[i]);
            assert.equal(r.integer_sum, integers[i]);
          });
        });
        observations.push({
          case: "numeric",
          objects: object.envelope,
          totals: sum,
          widened,
          groups,
          content_bytes: Buffer.byteLength(object.content),
          wire_bytes: object.wire_bytes,
        });
      });
      await session("large", largeSetup, async (call) => {
        const response = await call("population_objects", {
            concept: "ValidUser",
            filters: "[]",
          }),
          r = response.envelope!,
          rows = resultRecords(r),
          ids = new Set<string>();
        check("MCP 10000 typed objects: every field and ID complete", () => {
          assert.equal(r.row_count, 10000);
          for (const row of rows) {
            const id = String(row.entity_id),
              i = Number(id.slice(1));
            assert(i >= 0 && i < 10000);
            assert.equal(id, "b" + String(i).padStart(5, "0"));
            assert(!ids.has(id));
            ids.add(id);
            assert.equal(Object.keys(row).length, 17);
            assert.equal(row.record_id, id);
            assert.equal(row["dws_gaap_di.user_id"], id);
            assert.equal(row["dws_gaap_di.ordinal"], String(i));
            assert.equal(
              row["dws_gaap_di.gaap_val"],
              Math.floor(i / 100) + "." + String(i % 100).padStart(2, "0"),
            );
            assert.equal(row["dws_gaap_di.ytd_gaap"], "10.00");
            assert.equal(row["dws_gaap_di.payload"], payload);
            assert.equal(row["dws_gaap_di.double_value"], "1.25");
            assert.equal(row["dws_gaap_di.float_value"], "1.25");
            assert.equal(row["dws_gaap_di.boolean_value"], true);
            assert.equal(row["dws_gaap_di.ds"], "2026-03-10");
            for (const [name, value] of [
              ["product", "X"],
              ["region", "N"],
              ["user_type", "A"],
            ])
              assert.equal(row["dws_gaap_di." + name], value);
            assert.equal(row._ossie_snapshot_id, r.snapshot_id);
            assert.equal(row._ossie_content_sha256, r.content_sha256);
            assert.equal(row._ossie_ontology_sha256, r.ontology_sha256);
          }
          assert.equal(ids.size, 10000);
        });
        const groupResponse = await call(
          "population_query",
          args(
            ["record_count", "gaap_sum", "integer_sum"],
            ["dws_gaap_di.user_id"],
          ),
        );
        const groups = resultRecords(groupResponse.envelope!);
        check(
          "MCP 10000 typed groups match independent cents/integer oracle",
          () => {
            assert.equal(groups.length, 10000);
            const seen = new Set<string>();
            for (const row of groups) {
              const id = String(row["dws_gaap_di.user_id"]),
                i = Number(id.slice(1));
              assert.equal(id, "b" + String(i).padStart(5, "0"));
              assert(i >= 0 && i < 10000 && !seen.has(id));
              seen.add(id);
              assert.equal(row.record_count, "1");
              assert.equal(row.integer_sum, String(i));
              assert.equal(
                row.gaap_sum,
                Math.floor(i / 100) + "." + String(i % 100).padStart(2, "0"),
              );
              assert.equal(row._ossie_snapshot_id, r.snapshot_id);
            }
          },
        );
        for (const [label, metrics, dimensions] of [
          ["objects", [], []],
          ["grouped", ["gaap_sum"], ["dws_gaap_di.user_id"]],
          ["total", ["record_count", "gaap_sum"], []],
        ] as const) {
          const empty = (
            await call(
              label === "objects" ? "population_objects" : "population_query",
              label === "objects"
                ? { concept: "ValidUser", filters: '["User.ordinal < 0"]' }
                : args([...metrics], [...dimensions], ["User.ordinal < 0"]),
            )
          ).envelope!;
          check(
            "MCP empty " +
              label +
              ": metadata/hash associated with actual batch",
            () => {
              assert.equal(empty.snapshot_id, r.snapshot_id);
              assert.equal(empty.content_sha256, r.content_sha256);
              assert.equal(empty.ontology_sha256, r.ontology_sha256);
              assert.equal(empty.row_count, label === "total" ? 1 : 0);
              assert(empty.columns.length > 0);
              if (label === "total") {
                assert.equal(empty.rows[0][0], "0");
                assert.equal(empty.rows[0][1], null);
              }
            },
          );
          observations.push({ case: "empty_" + label, envelope: empty });
        }
        const compile = JSON.parse(
          (await call("population_compile", args(["gaap_sum"]))).content,
        )[0];
        check("compile stays diagnostic/native projection semantics", () => {
          assert.equal(compile.executed, false);
          assert.equal(compile.snapshot_id, r.snapshot_id);
          assert(!compile.sql.includes("result_json"));
        });
        observations.push({
          case: "large",
          objects: r.row_count,
          object_columns: r.columns,
          object_content_bytes: Buffer.byteLength(response.content),
          object_wire_bytes: response.wire_bytes,
          object_content_sha256: sha(response.content),
          groups: groups.length,
          group_content_bytes: Buffer.byteLength(groupResponse.content),
          group_wire_bytes: groupResponse.wire_bytes,
          snapshot_id: r.snapshot_id,
        });
      });
      await native.run("DROP TABLE dws_gaap_di");
      await native.run(largeSetup);
      const beforeInterrupt = (
        await native.one(
          "SELECT snapshot_id FROM ossie_population_capabilities()",
        )
      )[0];
      const pending = sqlEnvelope(
        "SELECT * FROM ossie_objects_json('ValidUser',[],include_trace=>true)",
      );
      let interrupts = 0;
      const timer = setInterval(() => {
        interrupts++;
        native.connection.interrupt();
      }, 1);
      try {
        await assert.rejects(pending, /interrupt/i);
      } finally {
        clearInterval(timer);
      }
      assert(interrupts > 0, "timer did not run during typed query");
      assert.equal(
        (
          await native.one(
            "SELECT snapshot_id FROM ossie_population_capabilities()",
          )
        )[0],
        beforeInterrupt,
      );
      assert.deepEqual(
        await native.one(
          "SELECT record_count FROM ossie_concept_query('ValidUser',['record_count'],[],[])",
        ),
        [10000],
      );
      checks.push(
        "interrupted typed query remains cancellation, retains batch and recovers",
      );
      observations.push({
        case: "cancellation",
        interrupt_calls: interrupts,
        phase: "timed actual query; exact encoding checkpoint not controlled",
        snapshot_id: beforeInterrupt,
      });
    } finally {
      native.close();
    }
  });
  return {
    scope:
      "Typed adapter ordinary-JSON exact integer/Decimal repair; no paging or universal output-size guarantee",
    artifact_sha256: digest(artifact),
    mcp_runtime: {
      engine: runtime.engine,
      source: runtime.source,
      extension_version: runtime.extension_version,
      artifact_sha256: digest(runtime.install_path),
    },
    source_sha256: Object.fromEntries(
      [
        "src/population/population.cpp",
        "scripts/verify_typed_population_artifact.ts",
        "scripts/population_result.ts",
        "scripts/artifact_helpers.ts",
        "scripts/mcp_check.ts",
        "examples/population_server.sql",
        ...[
          "core.json",
          "ontology.json",
          "mapping.json",
          "bundle.json",
          "numeric-rows.sql",
        ].map((p) => FIXTURE + p),
      ].map((p) => [p, digest(join(ROOT, p))]),
    ),
    checks,
    observations,
    limitations: [
      "Floats remain approximate; text does not preserve NaN payload bits",
      "Complete response materialization; canonical input cap is not output/peak-memory cap",
      "Same successful numeric schema change affects MCP consumers; native SQL remains unchanged",
      "No remote CI, universal size bound or autonomous Agent verification",
    ],
  };
}
if (import.meta.main) {
  try {
    assert(process.argv[2]);
    emitReport(await verify(resolve(process.argv[2])));
  } catch (error) {
    console.error(error);
    process.exitCode = 1;
  }
}

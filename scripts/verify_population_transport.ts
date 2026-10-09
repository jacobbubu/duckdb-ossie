/** Observe real MCP transport separately from exact native arithmetic. No Number oracle. */
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { createInterface } from "node:readline";
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { RpcProcess } from "./mcp_check.ts";
import {
  Database,
  ROOT,
  assert,
  digest,
  sqlString,
  temporary,
  emitReport,
} from "./artifact_helpers.ts";
import { populationError } from "./population_error.ts";
const FIXTURE = "test/fixtures/population-transport/";
const sha = (s: string) => createHash("sha256").update(s).digest("hex");
const amount = [
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
function rational(token: string): { n: bigint; d: bigint } | null {
  const v = token.startsWith('"') ? JSON.parse(token) : token;
  if (v === "null" || v === null) return null;
  assert(typeof v === "string");
  const m = /^(-?)(\d+)(?:\.(\d+))?(?:[eE]([+-]?\d+))?$/.exec(v);
  assert(m, `not a finite numeric token: ${token}`);
  const scale = (m[3]?.length ?? 0) - Number(m[4] ?? 0);
  assert(
    Number.isSafeInteger(scale) && Math.abs(scale) < 400,
    "bounded exponent",
  );
  let n = BigInt((m[1] || "") + m[2] + (m[3] || "")),
    d = 1n;
  if (scale >= 0) d = 10n ** BigInt(scale);
  else n *= 10n ** BigInt(-scale);
  return { n, d };
}
function equalValue(a: string, b: string | null) {
  const x = rational(a),
    y = rational(b ?? "null");
  return x === null || y === null ? x === y : x.n * y.d === y.n * x.d;
}
function decodedComparison(value: unknown, expected: string | null) {
  const roundtrip = JSON.stringify(value);
  assert(roundtrip !== undefined);
  return {
    ordinary_json_value: value,
    ordinary_json_roundtrip_token: roundtrip,
    ordinary_json_exact: equalValue(roundtrip, expected),
  };
}
function tokens(text: string, key: string) {
  const k = JSON.stringify(key).replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return [
    ...text.matchAll(
      new RegExp(
        k +
          '\\s*:\\s*(null|"(?:[^"\\\\]|\\\\.)*"|-?(?:0|[1-9]\\d*)(?:\\.\\d+)?(?:[eE][+-]?\\d+)?)',
        "g",
      ),
    ),
  ].map((m) => m[1]);
}
const cents = (x: string | null) =>
  x === null ? 0n : BigInt(x.replace(".", ""));
function money(x: bigint) {
  return (
    (x < 0n ? "-" : "") +
    (x < 0n ? -x : x) / 100n +
    "." +
    String((x < 0n ? -x : x) % 100n).padStart(2, "0")
  );
}
export async function verify(artifact: string) {
  const cli = Bun.which("duckdb");
  assert(cli, "stock CLI required");
  const meta = spawnSync(
    cli,
    [
      "-json",
      "-c",
      "LOAD duckdb_mcp; SELECT version() AS engine, (SELECT source_id FROM pragma_version()) AS source, extension_version, install_path FROM duckdb_extensions() WHERE extension_name='duckdb_mcp'",
    ],
    { encoding: "utf8", timeout: 10000 },
  );
  assert.equal(meta.status, 0, meta.stderr);
  const runtime = JSON.parse(meta.stdout)[0];
  assert.deepEqual([runtime.engine, runtime.source], ["v1.5.6", "069cc9f9b5"]);
  const checks: string[] = [],
    observations: unknown[] = [],
    failures: unknown[] = [],
    decodedFailures: unknown[] = [];
  const check = (name: string, action: () => void) => {
    action();
    checks.push(name);
  };
  check("independent exact token oracle", () => {
    assert(equalValue("9.007199254740993e15", "9007199254740993"));
    assert(!equalValue("9007199254740992", "9007199254740993"));
    assert(equalValue('"-0.0100"', "-0.01"));
    assert(equalValue("null", null));
  });
  const bundle = join(ROOT, FIXTURE, "bundle.json");
  const numericSetup =
    readFileSync(join(ROOT, FIXTURE, "numeric-rows.sql"), "utf8") +
    `\nCALL ossie_bundle_load(${sqlString(bundle)});`;
  const payload = '中"\\\n\t'.repeat(160);
  const largeSetup = `CREATE TABLE dws_gaap_di AS SELECT DATE '2026-03-10' AS ds, 'X'::VARCHAR AS product, 'N'::VARCHAR AS region, 'A'::VARCHAR AS user_type, 'b' || lpad(i::VARCHAR,5,'0') AS user_id, ((i // 100)::VARCHAR || '.' || lpad((i % 100)::VARCHAR,2,'0'))::DECIMAL(38,2) AS gaap_val, 10::DECIMAL(38,2) AS ytd_gaap, i::BIGINT AS ordinal, ${sqlString(payload)}::VARCHAR AS payload FROM range(10000) t(i);\nCALL ossie_bundle_load(${sqlString(bundle)});`;
  // Exact import builds Decimal text from integer quotient/remainder, without DOUBLE division.
  const native = await Database.open();
  try {
    await native.loadArtifact(artifact);
    await native.run(numericSetup);
    const exact = await native.rows(
      "SELECT user_id,gaap_val::VARCHAR,ordinal::VARCHAR FROM dws_gaap_di ORDER BY user_id",
    );
    check("native source literals match independent text", () =>
      assert.deepEqual(
        exact.map((r) => r.slice(1)),
        amount.map((a, i) => [a, integers[i]]),
      ),
    );
    const nativeObjects = await native.rows(
      'SELECT entity_id,"dws_gaap_di.gaap_val"::VARCHAR,"dws_gaap_di.ordinal"::VARCHAR FROM ossie_objects(\'ValidUser\',[]) ORDER BY entity_id',
    );
    check("native frozen objects preserve every numeric literal", () =>
      assert.deepEqual(nativeObjects, exact),
    );
    const totals = await native.one(
      "SELECT gaap_sum::VARCHAR,integer_sum::VARCHAR,typeof(gaap_sum),typeof(integer_sum) FROM ossie_concept_query('ValidUser',['gaap_sum','integer_sum'],[],[])",
    );
    const expectedSum = money(amount.reduce((a, b) => a + cents(b), 0n)),
      expectedInteger = integers
        .reduce((a, b) => a + BigInt(b ?? 0), 0n)
        .toString();
    check("native exact SUM and widened integer type", () =>
      assert.deepEqual(totals, [
        expectedSum,
        expectedInteger,
        "DECIMAL(38,2)",
        "HUGEINT",
      ]),
    );
    observations.push({
      native_totals: totals,
      expected_sum: expectedSum,
      expected_integer_sum: expectedInteger,
    });
    const positiveIntegerSum = integers
      .filter((x) => x !== null && BigInt(x) > 0n)
      .reduce((a, b) => a + BigInt(b!), 0n)
      .toString();
    const nativePositive = await native.one(
      "SELECT integer_sum::VARCHAR FROM ossie_concept_query('ValidUser',['integer_sum'],[],['User.ordinal > 0'])",
    );
    check("native SUM exceeds BIGINT without overflow", () =>
      assert.deepEqual(nativePositive, [positiveIntegerSum]),
    );
    await temporary("ossie-transport-", async (directory) => {
      async function session(
        label: string,
        setup: string,
        action: (
          call: (
            tool: string,
            args: Record<string, string>,
          ) => Promise<{
            text: string;
            wire_bytes: number;
            wire_sha256: string;
          }>,
          server: RpcProcess,
        ) => Promise<void>,
      ) {
        const source = readFileSync(
          join(ROOT, "examples/population_server.sql"),
          "utf8",
        ).replace(
          "INSTALL ossie FROM community;\nLOAD ossie;\n",
          `LOAD ${sqlString(resolve(artifact))};\n`,
        );
        const text =
          source.slice(0, source.indexOf("-- BEGIN OPERATOR SETUP")) +
          "-- BEGIN OPERATOR SETUP\n" +
          setup +
          "\n" +
          source.slice(source.indexOf("-- END OPERATOR SETUP"));
        const path = join(directory, label + ".sql");
        writeFileSync(path, text);
        const server = new RpcProcess([
          cli!,
          "-unsigned",
          "-bail",
          "-init",
          path,
        ]);
        const wire = new Map<number, { bytes: number; sha256: string }>();
        const lines = createInterface({ input: server.process.stdout });
        lines.on("line", (line) => {
          try {
            const r = JSON.parse(line);
            if (typeof r.id === "number")
              wire.set(r.id, {
                bytes: Buffer.byteLength(line) + 1,
                sha256: sha(line + "\n"),
              });
          } catch {
            /* startup banners */
          }
        });
        let id = 0;
        try {
          const hello = await server.request(++id, "initialize", {
            protocolVersion: "2024-11-05",
            capabilities: {},
            clientInfo: { name: "ossie-transport-probe", version: "1" },
          });
          assert(!hello.error);
          server.send({ jsonrpc: "2.0", method: "notifications/initialized" });
          const tools = (await server.request(++id, "tools/list")).result
            ?.tools;
          check(label + ": fixed tools, no reload", () =>
            assert.deepEqual(tools?.map((t) => t.name).sort(), [
              "population_compile",
              "population_objects",
              "population_query",
            ]),
          );
          const call = async (tool: string, args: Record<string, string>) => {
            const current = ++id,
              reply = await server.request(current, "tools/call", {
                name: tool,
                arguments: args,
              });
            assert(
              !reply.error && !reply.result?.isError,
              JSON.stringify(reply),
            );
            const content = reply.result?.content as { text: string }[];
            assert.equal(content.length, 1);
            const raw = wire.get(current);
            assert(raw);
            return {
              text: content[0].text,
              wire_bytes: raw.bytes,
              wire_sha256: raw.sha256,
            };
          };
          await action(call, server);
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
        const response = await call("population_objects", {
          concept: "ValidUser",
          filters: "[]",
        });
        const rows = JSON.parse(response.text) as Record<string, unknown>[];
        check("numeric objects complete and null retained", () => {
          assert.equal(rows.length, 6);
          assert.deepEqual(
            rows.map((r) => r.entity_id).sort(),
            Array.from({ length: 6 }, (_, i) => `n0${i + 1}`),
          );
        });
        for (const [key, expected] of [
          ["dws_gaap_di.gaap_val", amount],
          ["dws_gaap_di.ordinal", integers],
        ] as const) {
          const raw = tokens(response.text, key);
          assert.equal(raw.length, 6);
          const details = rows.map((r, i) => {
            const index = Number(String(r.entity_id).slice(1)) - 1;
            const exact = equalValue(raw[i], expected[index]);
            return {
              entity_id: r.entity_id,
              expected: expected[index],
              raw_token: raw[i],
              raw_exact: exact,
              ...decodedComparison(r[key], expected[index]),
            };
          });
          observations.push({
            case: "numeric_objects",
            key,
            details,
            content_bytes: Buffer.byteLength(response.text),
            wire_bytes: response.wire_bytes,
            wire_sha256: response.wire_sha256,
          });
          for (const d of details)
            if (!d.raw_exact)
              failures.push({ case: "numeric_objects", key, ...d });
          for (const d of details)
            if (!d.ordinary_json_exact)
              decodedFailures.push({ case: "numeric_objects", key, ...d });
        }
        const responseSum = await call(
          "population_query",
          args(["gaap_sum", "integer_sum", "record_count"]),
        );
        const sum = JSON.parse(responseSum.text)[0];
        for (const [key, expected] of [
          ["gaap_sum", expectedSum],
          ["integer_sum", expectedInteger],
        ] as const) {
          const raw = tokens(responseSum.text, key);
          assert.equal(raw.length, 1);
          const exact = equalValue(raw[0], expected);
          const decoded = decodedComparison(sum[key], expected);
          observations.push({
            case: "numeric_aggregate",
            key,
            expected,
            raw_token: raw[0],
            raw_exact: exact,
            ...decoded,
          });
          if (!decoded.ordinary_json_exact)
            decodedFailures.push({
              case: "numeric_aggregate",
              key,
              expected,
              ...decoded,
            });
          if (!exact)
            failures.push({
              case: "numeric_aggregate",
              key,
              expected,
              raw_token: raw[0],
            });
        }
        check("numeric query count and trace", () => {
          assert.equal(sum.record_count, 6);
          assert(typeof sum._ossie_snapshot_id === "string");
        });
        const positive = await call(
          "population_query",
          args(["integer_sum"], [], ["User.ordinal > 0"]),
        );
        const positiveToken = tokens(positive.text, "integer_sum");
        assert.equal(positiveToken.length, 1);
        const positiveDecoded = decodedComparison(
          JSON.parse(positive.text)[0].integer_sum,
          positiveIntegerSum,
        );
        const positiveExact = equalValue(positiveToken[0], positiveIntegerSum);
        observations.push({
          case: "widened_integer_sum",
          expected: positiveIntegerSum,
          raw_token: positiveToken[0],
          raw_exact: positiveExact,
          ...positiveDecoded,
        });
        if (!positiveExact)
          failures.push({
            case: "widened_integer_sum",
            expected: positiveIntegerSum,
            raw_token: positiveToken[0],
          });
        if (!positiveDecoded.ordinary_json_exact)
          decodedFailures.push({
            case: "widened_integer_sum",
            expected: positiveIntegerSum,
            ...positiveDecoded,
          });
        const grouped = await call(
          "population_query",
          args(["gaap_sum", "integer_sum"], ["dws_gaap_di.user_id"]),
        );
        const groups = JSON.parse(grouped.text) as Record<string, unknown>[];
        check("numeric grouped rows complete", () =>
          assert.deepEqual(
            groups.map((r) => r["dws_gaap_di.user_id"]).sort(),
            rows.map((r) => r.entity_id).sort(),
          ),
        );
        for (const [key, expected] of [
          ["gaap_sum", amount],
          ["integer_sum", integers],
        ] as const) {
          const raw = tokens(grouped.text, key);
          assert.equal(raw.length, 6);
          for (let i = 0; i < groups.length; i++) {
            const index =
              Number(String(groups[i]["dws_gaap_di.user_id"]).slice(1)) - 1;
            const exact = equalValue(raw[i], expected[index]);
            const decoded = decodedComparison(groups[i][key], expected[index]);
            observations.push({
              case: "numeric_group",
              id: groups[i]["dws_gaap_di.user_id"],
              key,
              expected: expected[index],
              raw_token: raw[i],
              raw_exact: exact,
              ...decoded,
            });
            if (!decoded.ordinary_json_exact)
              decodedFailures.push({
                case: "numeric_group",
                key,
                expected: expected[index],
                ...decoded,
              });
            if (!exact)
              failures.push({
                case: "numeric_group",
                id: groups[i]["dws_gaap_di.user_id"],
                key,
                expected: expected[index],
                raw_token: raw[i],
              });
          }
        }
      });
      await native.run("DROP TABLE dws_gaap_di");
      await native.run(largeSetup);
      const largeNative = await native.one(
        "SELECT record_count,gaap_sum::VARCHAR,integer_sum::VARCHAR FROM ossie_concept_query('ValidUser',['record_count','gaap_sum','integer_sum'],[],[])",
      );
      check("native bounded large capture and sum", () =>
        assert.deepEqual(largeNative, [10000, "499950.00", "49995000"]),
      );
      await session("large", largeSetup, async (call) => {
        const response = await call("population_objects", {
          concept: "ValidUser",
          filters: "[]",
        });
        const rows = JSON.parse(response.text) as Record<string, unknown>[];
        check("10000 objects: complete IDs and every field", () => {
          assert.equal(rows.length, 10000);
          const ids = new Set<string>();
          for (const r of rows) {
            const id = String(r.entity_id),
              i = Number(id.slice(1));
            assert(i >= 0 && i < 10000);
            assert.equal(id, "b" + String(i).padStart(5, "0"));
            assert(!ids.has(id));
            ids.add(id);
            assert.equal(r.record_id, id);
            assert.equal(r["dws_gaap_di.user_id"], id);
            assert.equal(r["dws_gaap_di.ordinal"], i);
            assert.equal(r["dws_gaap_di.gaap_val"], i / 100);
            assert.equal(r["dws_gaap_di.ytd_gaap"], 10);
            assert.equal(r["dws_gaap_di.ds"], "2026-03-10");
            assert.equal(r["dws_gaap_di.product"], "X");
            assert.equal(r["dws_gaap_di.region"], "N");
            assert.equal(r["dws_gaap_di.user_type"], "A");
            assert.equal(r["dws_gaap_di.payload"], payload);
            assert.equal(Object.keys(r).length, 14);
            for (const k of [
              "_ossie_snapshot_id",
              "_ossie_content_sha256",
              "_ossie_ontology_sha256",
            ])
              assert.equal(r[k], rows[0][k]);
          }
          assert.equal(ids.size, 10000);
        });
        observations.push({
          case: "large_objects",
          rows: rows.length,
          payload_utf8_bytes: Buffer.byteLength(payload),
          payload_sha256: sha(payload),
          content_bytes: Buffer.byteLength(response.text),
          wire_bytes: response.wire_bytes,
          content_sha256: sha(response.text),
          wire_sha256: response.wire_sha256,
          snapshot_id: rows[0]._ossie_snapshot_id,
        });
        const query = JSON.parse(
          (
            await call(
              "population_query",
              args(["record_count", "gaap_sum", "integer_sum"]),
            )
          ).text,
        )[0];
        check("large query oracle and same batch", () => {
          assert.equal(query.record_count, 10000);
          assert.equal(query.gaap_sum, 499950);
          assert.equal(query.integer_sum, 49995000);
          assert.equal(query._ossie_snapshot_id, rows[0]._ossie_snapshot_id);
        });
        const groupedResponse = await call(
          "population_query",
          args(
            ["record_count", "gaap_sum", "integer_sum"],
            ["dws_gaap_di.user_id"],
          ),
        );
        const groups = JSON.parse(groupedResponse.text) as Record<
          string,
          unknown
        >[];
        check("10000 aggregate groups complete, with per-group oracle", () => {
          assert.equal(groups.length, 10000);
          const ids = new Set<string>();
          for (const r of groups) {
            const id = String(r["dws_gaap_di.user_id"]),
              i = Number(id.slice(1));
            assert.equal(id, "b" + String(i).padStart(5, "0"));
            assert(i >= 0 && i < 10000 && !ids.has(id));
            ids.add(id);
            assert.equal(r.record_count, 1);
            assert.equal(r.gaap_sum, i / 100);
            assert.equal(r.integer_sum, i);
            assert.equal(r._ossie_snapshot_id, rows[0]._ossie_snapshot_id);
          }
          assert.equal(ids.size, 10000);
        });
        observations.push({
          case: "large_grouped",
          rows: groups.length,
          content_bytes: Buffer.byteLength(groupedResponse.text),
          wire_bytes: groupedResponse.wire_bytes,
          wire_sha256: groupedResponse.wire_sha256,
        });
        const filter = ["User.ordinal < 0"];
        const compiled = JSON.parse(
          (await call("population_compile", args([], [], filter))).text,
        )[0];
        const empty = await call("population_objects", {
          concept: "ValidUser",
          filters: JSON.stringify(filter),
        });
        const emptyGrouped = await call(
          "population_query",
          args(["gaap_sum"], ["dws_gaap_di.user_id"], filter),
        );
        check(
          "empty object/group associated with fixed startup capture",
          () => {
            assert.equal(empty.text.trim(), "[]");
            assert.equal(emptyGrouped.text.trim(), "[]");
            assert.equal(compiled.snapshot_id, rows[0]._ossie_snapshot_id);
            assert.equal(
              compiled.content_sha256,
              rows[0]._ossie_content_sha256,
            );
            assert.equal(
              compiled.ontology_sha256,
              rows[0]._ossie_ontology_sha256,
            );
            assert.equal(compiled.executed, false);
            const plan = JSON.parse(compiled.plan_json);
            assert(compiled.sql.includes("< 0"));
            assert.equal(plan.snapshot.id, compiled.snapshot_id);
            assert.equal(plan.snapshot.captured_population_rows, 10000);
            assert.equal(plan.snapshot.filtered_rows_computed, false);
            assert.equal(plan.concept.formal_rule.applied, true);
            assert.deepEqual(plan.filters.requested_sql, filter);
            assert(plan.filters.effective_where_sql.includes("< 0"));
          },
        );
        observations.push({
          case: "empty_fixed_operator",
          object_content: empty.text,
          grouped_content: emptyGrouped.text,
          compile_snapshot_id: compiled.snapshot_id,
          compile_content_sha256: compiled.content_sha256,
          compile_ontology_sha256: compiled.ontology_sha256,
          execution_record_owner:
            "probe retained compile metadata; fixed startup/no reload tool",
        });
        const emptyTotal = JSON.parse(
          (
            await call(
              "population_query",
              args(["record_count", "gaap_sum", "integer_sum"], [], filter),
            )
          ).text,
        );
        check("empty ungrouped COUNT zero, SUM null, trace retained", () => {
          assert.equal(emptyTotal.length, 1);
          assert.equal(emptyTotal[0].record_count, 0);
          assert.equal(emptyTotal[0].gaap_sum, null);
          assert.equal(emptyTotal[0].integer_sum, null);
          assert.equal(emptyTotal[0]._ossie_snapshot_id, compiled.snapshot_id);
        });
      });
      // Native load boundaries, not a fabricated MCP pagination or output limit.
      const old = (
        await native.one(
          "SELECT snapshot_id FROM ossie_population_capabilities()",
        )
      )[0];
      await native.run(
        "INSERT INTO dws_gaap_di SELECT ds,product,region,user_type,'overflow',gaap_val,ytd_gaap,ordinal,payload FROM dws_gaap_di LIMIT 1",
      );
      await assert.rejects(
        () => native.run(`CALL ossie_bundle_load(${sqlString(bundle)})`),
        (error) => {
          const e = populationError(String(error));
          return e?.stage === "snapshot" && e.reason.includes("10000");
        },
      );
      check(
        "10001-row load refuses without replacing or truncating old batch",
        () => {},
      );
      assert.equal(
        (
          await native.one(
            "SELECT snapshot_id FROM ossie_population_capabilities()",
          )
        )[0],
        old,
      );
      await native.run(
        "DELETE FROM dws_gaap_di WHERE user_id='overflow'; UPDATE dws_gaap_di SET payload=repeat('x',2000)",
      );
      await assert.rejects(
        () => native.run(`CALL ossie_bundle_load(${sqlString(bundle)})`),
        (error) => {
          const e = populationError(String(error));
          return e?.stage === "snapshot" && e.reason.includes("16 MiB");
        },
      );
      check(
        "over-16MiB canonical load refuses and retains old batch",
        () => {},
      );
      assert.equal(
        (
          await native.one(
            "SELECT snapshot_id FROM ossie_population_capabilities()",
          )
        )[0],
        old,
      );
    });
  } finally {
    native.close();
  }
  return {
    scope:
      "Real MCP precise numeric and bounded large-output research; observed loss is a finding, not a passing lossless contract",
    artifact_sha256: digest(artifact),
    cli_sha256: digest(cli),
    mcp_runtime: {
      ...runtime,
      install_path: undefined,
      artifact_sha256: digest(runtime.install_path),
    },
    source_sha256: Object.fromEntries(
      [
        "scripts/verify_population_transport.ts",
        "scripts/mcp_check.ts",
        "scripts/artifact_helpers.ts",
        "scripts/population_error.ts",
        "examples/population_server.sql",
        "src/population/population.cpp",
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
    raw_numeric_token_discrepancies: failures,
    ordinary_json_decoding_discrepancies: decodedFailures,
    raw_numeric_tokens_exact_for_fixture: failures.length === 0,
    ordinary_json_roundtrip_exact_for_fixture: decodedFailures.length === 0,
    limitations: [
      "Raw numeric spelling and ordinary JSON Number decoding are separate; token fidelity does not establish a lossless client contract",
      "No production encoding change; decoded discrepancies block a lossless ordinary-JSON client claim",
      "Large completeness only demonstrated for this fixture, not all payloads/configurations",
      "Canonical input cap is not a JSON-RPC output or peak-memory bound",
      "No remote CI, cross-platform or autonomous Agent evidence; observed MCP dependency not repo-pinned",
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

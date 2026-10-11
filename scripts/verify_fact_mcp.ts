/** Actual stock stdio registry plus independent complete synthetic data oracle. No model. */
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { ROOT, assert, digest } from "./artifact_helpers.ts";
import {
  withFactSession,
  factContent,
  factRuntime,
  FACT_TOOLS,
  type FactSession,
} from "./fact_mcp.ts";
import { factValue } from "./fact_result.ts";
import { factInputs, factUserOracle, factWindowOracle } from "./fact_oracle.ts";
import { populationError } from "./population_error.ts";
const [cliArg, artifactArg, outArg] = process.argv.slice(2);
assert(
  cliArg && artifactArg && outArg && process.argv.length === 5,
  "CLI ARTIFACT NEW_OUT_DIR",
);
const cli = resolve(cliArg),
  artifact = resolve(artifactArg),
  out = resolve(outArg);
assert.equal(
  digest(join(ROOT, "test/fixtures/population-fact-program/rows.sql")),
  "78df68efcb6e2d5226d76d888aa91bea36f20e6e244df8038398538e3a17ef4d",
  "reviewed oracle fixture changed",
);
assert(!existsSync(out), "fresh evidence directory required");
mkdirSync(out, { recursive: true });
const checks: string[] = [],
  refusals: unknown[] = [],
  sessions: unknown[] = [];
let failure: unknown;
const all = { operation: "all" };
const predicate = (predicate: string) => ({
  operation: "predicate",
  predicate,
});
function refusalText(reply: Awaited<ReturnType<FactSession["call"]>>) {
  return (
    (reply.result?.content as { text: string }[])
      ?.map((c) => c.text)
      .join("\n") ??
    (reply.error as { message?: string } | undefined)?.message ??
    ""
  );
}
try {
  for (const setup of ["original", "empty", "raw", "wide", "full"] as const)
    await withFactSession(cli, artifact, setup, async (s) => {
      const discovery = await s.discover(),
        inputs = factInputs(setup);
      const cases: unknown[] = [];
      sessions.push({ setup, discovery, cases, events: s.events });
      assert.deepEqual(
        discovery.program.sources.map((s) => s.captured_rows),
        [String(inputs.revenue.length), String(inputs.cost.length)],
      );
      const userCases = [
        {
          name: "all",
          metrics: [
            "BALANCE",
            "income",
            "expense",
            "revenue_rows",
            "revenue_values",
            "cost_rows",
            "cost_values",
          ],
          cohort: all,
          choose: () => true,
        },
        {
          name: "empty-cohort",
          metrics: ["balance"],
          cohort: predicate("balance>999999"),
          choose: () => false,
        },
        ...(setup === "original"
          ? [
              {
                name: "predicate-unprojected",
                metrics: ["balance"],
                cohort: predicate("expense>50"),
                choose: (v: Record<string, bigint | null>) =>
                  v.expense !== null && v.expense > 5000n,
              },
              {
                name: "intersection",
                metrics: ["balance"],
                cohort: {
                  operation: "intersection",
                  children: [predicate("income>100"), predicate("expense>50")],
                },
                choose: (v: Record<string, bigint | null>) =>
                  v.income !== null &&
                  v.income > 10000n &&
                  v.expense !== null &&
                  v.expense > 5000n,
              },
              {
                name: "union",
                metrics: ["balance"],
                cohort: {
                  operation: "union",
                  children: [predicate("income>100"), predicate("expense<50")],
                },
                choose: (v: Record<string, bigint | null>) =>
                  (v.income !== null && v.income > 10000n) ||
                  (v.expense !== null && v.expense < 5000n),
              },
              {
                name: "difference-RHS-UNKNOWN",
                metrics: ["balance"],
                cohort: {
                  operation: "difference",
                  children: [all, predicate("expense>50")],
                },
                choose: (v: Record<string, bigint | null>) =>
                  !(v.expense !== null && v.expense > 5000n),
              },
            ]
          : []),
      ];
      for (const c of userCases) {
        const request = {
          domain: "common",
          input_scope: "entire_capture",
          metrics: c.metrics,
          cohort: c.cohort,
        };
        const plan = factValue(
          factContent(
            await s.call("population_fact_users_compile", {
              request_json: JSON.stringify(request),
            }),
          ),
          request,
          discovery.program,
          false,
          true,
        );
        const result = factValue(
          factContent(
            await s.call("population_fact_users", {
              request_json: JSON.stringify(request),
            }),
          ),
          request,
          discovery.program,
          false,
          false,
        );
        const expected = factUserOracle(inputs, c.metrics, c.choose);
        assert.deepEqual(result.rows, expected.rows);
        assert.equal(result.common_user_count, expected.common_user_count);
        assert.equal(result.selected_user_count, expected.selected_user_count);
        cases.push({
          name: c.name,
          request,
          plan,
          result,
          independent: expected,
        });
        checks.push(
          `${setup}/${c.name}: complete separate-grain exact-money, counts and TRUE-set oracle`,
        );
      }
      const displays = [
        { mode: "all" },
        ...(setup === "original"
          ? [
              {
                mode: "date_range",
                start_date: "2026-02-01",
                end_date: "2026-02-01",
              },
              {
                mode: "date_range",
                start_date: "1900-01-01",
                end_date: "1900-01-01",
              },
            ]
          : setup === "wide"
            ? [
                {
                  mode: "date_range",
                  start_date: "0001-01-01 (BC)",
                  end_date: "10000-01-01",
                },
              ]
            : []),
      ];
      for (const display of displays) {
        const request = {
          input_scope: "entire_capture",
          windows: ["PREVIOUS", "rolling"],
          display,
        };
        const plan = factValue(
          factContent(
            await s.call("population_fact_windows_compile", {
              request_json: JSON.stringify(request),
            }),
          ),
          request,
          discovery.program,
          true,
          true,
        );
        const result = factValue(
          factContent(
            await s.call("population_fact_windows", {
              request_json: JSON.stringify(request),
            }),
          ),
          request,
          discovery.program,
          true,
          false,
        );
        const expected = factWindowOracle(inputs, request.windows, display);
        assert.deepEqual(result.rows, expected);
        cases.push({
          name: `windows/${JSON.stringify(display)}`,
          request,
          plan,
          result,
          independent: { rows: expected },
        });
        checks.push(
          `${setup}/windows/${JSON.stringify(display)}: whole capture then display complete oracle`,
        );
      }
      if (setup !== "original") return;
      for (const tool of FACT_TOOLS) {
        const window = tool.includes("windows"),
          plan = tool.endsWith("compile");
        const request = window
          ? {
              input_scope: "entire_capture",
              windows: ["previous"],
              display: { mode: "all" },
            }
          : {
              domain: "common",
              input_scope: "entire_capture",
              metrics: ["balance"],
              cohort: all,
            };
        for (const bad of [
          "{",
          "{}",
          JSON.stringify({ ...request, limit: 1 }),
          JSON.stringify({ ...request, input_scope: "filtered" }),
          JSON.stringify({
            ...request,
            [window ? "windows" : "metrics"]: [window ? "balance" : "previous"],
          }),
          `{"input_scope":"entire_capture","input_scope":"entire_capture"}`,
        ]) {
          const reply = await s.call(tool, { request_json: bad });
          assert(reply.error || reply.result?.isError);
          refusals.push({ tool, args: { request_json: bad }, reply });
        }
        for (const args of [{}, { request_json: 42 }, { request_json: {} }]) {
          const reply = await s.call(tool, args);
          assert(reply.error || reply.result?.isError);
          refusals.push({ tool, args, reply });
        }
        const raw = JSON.stringify(request);
        const boundary = raw + " ".repeat(16384 - Buffer.byteLength(raw));
        factValue(
          factContent(await s.call(tool, { request_json: boundary })),
          request,
          discovery.program,
          window,
          plan,
        );
        checks.push(`${tool}: exact16384-byte request accepts`);
        const over = await s.call(tool, { request_json: boundary + " " });
        assert(over.error || over.result?.isError);
        assert.equal(
          populationError(refusalText(over))?.code,
          "RESOURCE_LIMIT_EXCEEDED",
        );
        refusals.push({ tool, boundary_bytes: 16385, reply: over });
        const result = factValue(
          factContent(
            await s.call(tool, {
              request_json: JSON.stringify(request),
              limit: 1,
              input_scope: "filtered",
              start_date: "1900-01-01",
            }),
          ),
          request,
          discovery.program,
          window,
          plan,
        );
        if (!plan) assert.equal(result.row_count, window ? "8" : "3");
        checks.push(
          `${tool}: actual unknown outer args ignored, no implicit scope or limit`,
        );
      }
      for (const name of [
        "execute_query",
        "query",
        "population_query",
        "population_daily_users",
        "population_relation_objects",
        "ossie_bundle_load",
      ]) {
        const reply = await s.call(name, {});
        assert(reply.error || reply.result?.isError);
        refusals.push({ name, reply });
      }
    });
  await withFactSession(cli, artifact, "oversize", async (s) => {
    const discovery = await s.discover();
    sessions.push({ setup: "oversize", discovery, events: s.events });
    for (const window of [false, true]) {
      const request = window
        ? {
            input_scope: "entire_capture",
            windows: ["previous"],
            display: { mode: "all" },
          }
        : {
            domain: "common",
            input_scope: "entire_capture",
            metrics: ["balance"],
            cohort: all,
          };
      const tool = `population_fact_${window ? "windows" : "users"}`;
      factValue(
        factContent(
          await s.call(tool + "_compile", {
            request_json: JSON.stringify(request),
          }),
        ),
        request,
        discovery.program,
        window,
        true,
      );
      const reply = await s.call(tool, {
        request_json: JSON.stringify(request),
      });
      assert(reply.error || reply.result?.isError);
      assert.equal(
        populationError(refusalText(reply))?.code,
        "RESOURCE_LIMIT_EXCEEDED",
      );
      refusals.push({ tool, setup: "oversize", reply });
      checks.push(
        `${tool}: compile succeeds but complete escaped JSON refuses without partial result`,
      );
    }
  });
} catch (error) {
  failure = error;
}
writeFileSync(
  join(out, "verification.json"),
  JSON.stringify(
    {
      passed: failure === undefined,
      scope:
        "fact MCP actual registry and complete independent fixture oracles; not full P1 or real Agent",
      runtime: factRuntime(cli, artifact),
      source_sha256: digest(join(ROOT, "scripts/verify_fact_mcp.ts")),
      decoder_sha256: digest(join(ROOT, "scripts/fact_result.ts")),
      oracle_sha256: digest(join(ROOT, "scripts/fact_oracle.ts")),
      fixture_sha256: digest(
        join(ROOT, "test/fixtures/population-fact-program/rows.sql"),
      ),
      checks,
      refusals,
      sessions,
      real_model_requests: 0,
      ...(failure ? { failure: String(failure) } : {}),
    },
    null,
    2,
  ) + "\n",
);
if (failure) throw failure;
console.log(
  `PASS fact MCP ${checks.length} complete oracle/registry checks, ${refusals.length} refusals; model requests0`,
);

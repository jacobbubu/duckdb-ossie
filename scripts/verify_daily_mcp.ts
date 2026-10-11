/** Real daily-only MCP transport and native request boundary; no model calls. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { ROOT, digest } from "./artifact_helpers.ts";
import { dailyRuntime, withDailySession, dailyContent } from "./daily_mcp.ts";
import {
  dailyPlan,
  dailyResult,
  sameDailyBinding,
  DAILY_TRACE,
} from "./daily_result.ts";
import { populationError } from "./population_error.ts";
interface Case {
  id: string;
  start: string;
  end: string;
  products: string[] | null;
  threshold: number;
  dataset: string;
  expected_ids: string[];
}
export async function verifyDailyMcp(cli: string, artifact: string) {
  const runtime = dailyRuntime(cli, artifact),
    checks: string[] = [],
    refusals: unknown[] = [],
    sessions: unknown[] = [];
  const cases: Case[] = JSON.parse(
    readFileSync(
      join(
        ROOT,
        "docs/research/ossie-req-001/multi-period-research/every-day/cases.json",
      ),
      "utf8",
    ),
  );
  for (const c of cases)
    await withDailySession(
      cli,
      artifact,
      c.threshold,
      c.dataset,
      async (session) => {
        const entry: Record<string, unknown> = {
          case: c.id,
          events: session.events,
        };
        sessions.push(entry);
        const discovery = await session.discover();
        entry.discovery = discovery;
        const request = {
          start_date: c.start,
          end_date: c.end,
          products:
            c.products === null
              ? { mode: "all" }
              : { mode: "subset", values: c.products },
        };
        const args = {
          concept: "QualifiedObservation",
          request_json: JSON.stringify(request),
        };
        const plan = dailyPlan(
          dailyContent(await session.call("population_daily_compile", args)),
        );
        const result = dailyResult(
          dailyContent(await session.call("population_daily_users", args)),
        );
        sameDailyBinding(plan, result);
        assert.deepEqual(result.members, c.expected_ids, c.id);
        assert.deepEqual(result.request, {
          ...request,
          products:
            c.products === null
              ? { mode: "all" }
              : {
                  mode: "subset",
                  values: [...c.products].sort((a, b) =>
                    Buffer.compare(Buffer.from(a), Buffer.from(b)),
                  ),
                },
        });
        assert.equal(
          result.qualification_rule.declared_sql,
          `DailyObservation.classification_value >= ${c.threshold}`,
        );
        for (const key of DAILY_TRACE)
          assert.equal(result[key], discovery.binding[key]);
        entry.plan = plan;
        entry.result = result;
        checks.push(
          c.id +
            ": discovery, same scope/rule/binding, full hand-oracle members and count",
        );
        if (c !== cases[0]) return;
        async function refusal(
          label: string,
          name: string,
          arguments_: Record<string, unknown>,
          code?: string,
        ) {
          const reply = await session.call(name, arguments_);
          assert(
            reply.error || reply.result?.isError,
            `${label}: unexpectedly accepted`,
          );
          const blocks = reply.result?.content as
            | { text?: string }[]
            | undefined;
          const text =
            blocks?.map((b) => b.text ?? "").join("\n") ??
            String((reply.error as { message?: string })?.message ?? "");
          if (code) assert.equal(populationError(text)?.code, code, label);
          refusals.push({ label, reply });
          assert.deepEqual(
            dailyResult(
              dailyContent(await session.call("population_daily_users", args)),
            ).members,
            c.expected_ids,
            "recovery: " + label,
          );
        }
        for (const name of [
          "population_daily_users",
          "population_daily_compile",
        ]) {
          for (const [label, text, code] of [
            [
              "invalid calendar day",
              JSON.stringify({ ...request, start_date: "2026-02-30" }),
              "INVALID_REQUEST",
            ],
            [
              "extra scope field",
              JSON.stringify({ ...request, filters: [] }),
              "INVALID_REQUEST",
            ],
            [
              "duplicate escaped key",
              '{"start_date":"2026-01-31","\\u0073tart_date":"2026-01-31","end_date":"2026-02-01","products":{"mode":"all"}}',
              "INVALID_REQUEST",
            ],
            [
              "367 days",
              JSON.stringify({
                ...request,
                start_date: "2024-01-01",
                end_date: "2025-01-01",
              }),
              "RESOURCE_LIMIT_EXCEEDED",
            ],
            [
              "request bytes",
              args.request_json +
                " ".repeat(16385 - Buffer.byteLength(args.request_json)),
              "RESOURCE_LIMIT_EXCEEDED",
            ],
            [
              "numeric product",
              JSON.stringify({
                ...request,
                products: { mode: "subset", values: [1] },
              }),
              "INVALID_REQUEST",
            ],
          ])
            await refusal(
              name + ": " + label,
              name,
              { ...args, request_json: text },
              code,
            );
          await refusal(
            name + ": unsupported User",
            name,
            { ...args, concept: "User" },
            "UNSUPPORTED_CAPABILITY",
          );
          await refusal(
            name + ": SQL injection concept",
            name,
            {
              ...args,
              concept: "QualifiedObservation'); DROP TABLE observations; --",
            },
            "UNSUPPORTED_CAPABILITY",
          );
          await refusal(name + ": absent request", name, {
            concept: args.concept,
          });
          await refusal(name + ": null request", name, {
            ...args,
            request_json: null,
          });
          const objectReply = await session.call(name, {
            ...args,
            request_json: request,
          });
          entry[name + "_object_argument_probe"] = objectReply;
          if (name === "population_daily_users")
            sameDailyBinding(plan, dailyResult(dailyContent(objectReply)));
          else sameDailyBinding(dailyPlan(dailyContent(objectReply)), result);
          checks.push(
            name +
              ": fixed upstream coerces object request into JSON String with same scope",
          );
          await refusal(name + ": numeric request", name, {
            ...args,
            request_json: 3,
          });
        }
        for (const name of [
          "query",
          "execute",
          "population_objects",
          "population_query",
          "population_compile",
          "ossie_bundle_load",
        ])
          await refusal("unexposed tool " + name, name, { sql: "SELECT 1" });
        const extra = dailyResult(
          dailyContent(
            await session.call("population_daily_users", {
              ...args,
              filters: ["false"],
              products: { mode: "subset", values: ["Z"] },
            }),
          ),
        );
        assert.deepEqual(extra.members, result.members);
        sameDailyBinding(plan, extra);
        checks.push(
          "upstream unknown outer arguments ignored without narrowing, documented boundary",
        );
        const escaped = dailyResult(
          dailyContent(
            await session.call("population_daily_users", {
              ...args,
              request_json: JSON.stringify({
                ...request,
                products: {
                  mode: "subset",
                  values: ['quote"\\', "\u0000p", "汉"],
                },
              }),
            }),
          ),
        );
        assert.deepEqual(escaped.members, []);
        assert.deepEqual(escaped.request.products, {
          mode: "subset",
          values: ["\u0000p", 'quote"\\', "汉"],
        });
        checks.push(
          "outer RPC and carrier round trip preserve control/quote/backslash/Unicode scope",
        );
      },
    );
  return {
    passed: true,
    scope: "daily MCP transport only; not whole P1 or real Agent acceptance",
    real_model_requests: 0,
    runtime,
    checks,
    refusals,
    sessions,
    sources_sha256: Object.fromEntries(
      [
        "examples/daily_user_server.sql",
        "scripts/daily_mcp.ts",
        "scripts/daily_result.ts",
        "scripts/verify_daily_mcp.ts",
      ].map((p) => [p, digest(join(ROOT, p))]),
    ),
    limitations: [
      "Fixed upstream outer unknown args ignored, no universal JSON Schema enforcement claim",
      "Native mcp_example_registered is unobserved; actual tools/list and daily-surface define server exposure",
      "No full result-byte wire/memory/time guarantee or new daily no-model Agent rehearsal here",
    ],
  };
}
if (import.meta.main) {
  assert(
    process.argv.length === 5,
    "usage: verify_daily_mcp.ts CLI ARTIFACT OUT",
  );
  const report = await verifyDailyMcp(
    resolve(process.argv[2]),
    resolve(process.argv[3]),
  );
  writeFileSync(
    resolve(process.argv[4]),
    JSON.stringify(report, null, 2) + "\n",
  );
  console.log(
    `PASS daily MCP: ${report.checks.length} checks, ${report.refusals.length} refusals; real model requests0`,
  );
}

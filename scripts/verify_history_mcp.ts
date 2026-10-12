/** Actual stdio MCP and full per-record oracle, damaged carrier/refusal recovery; no model. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { resolve, join } from "node:path";
import { ROOT, digest, Database } from "./artifact_helpers.ts";
import {
  withHistorySession,
  historyRuntime,
  historyContent,
  HISTORY_TOOLS,
} from "./history_mcp.ts";
import {
  historyResult,
  historyPlan,
  historyProgram,
  sameHistoryBinding,
  type HistoryRequest,
} from "./history_result.ts";
import {
  HISTORY_FIXTURE,
  HISTORY_OBSERVATIONS,
  HISTORY_RECORDS,
  historyOracle,
} from "./history_oracle.ts";
import { populationError } from "./population_error.ts";
export async function verifyHistoryMcp(cli: string, artifact: string) {
  const checks: string[] = [],
    refusals: unknown[] = [],
    damage: string[] = [],
    sessions: unknown[] = [];
  // Check source correspondence separately from matching calculations and carrier validity.
  const db = await Database.open();
  try {
    await db.run(
      "CREATE TABLE observations(user_id VARCHAR,ds DATE,product VARCHAR)",
    );
    await db.run(
      "CREATE TABLE history(record_id VARCHAR,user_id VARCHAR,team_id VARCHAR,starts DATE,ends DATE)",
    );
    await db.run(readFileSync(join(ROOT, HISTORY_FIXTURE), "utf8"));
    assert.deepEqual(
      await db.rows("SELECT user_id,ds::VARCHAR,product FROM observations"),
      HISTORY_OBSERVATIONS,
    );
    assert.deepEqual(
      await db.rows(
        "SELECT record_id,user_id,team_id,starts::VARCHAR,ends::VARCHAR FROM history",
      ),
      HISTORY_RECORDS,
    );
    checks.push(
      "actual fixture rows correspond to independent source arrays, including raw NUL/DATE minimum",
    );
  } finally {
    await db.close();
  }
  const all: HistoryRequest = {
    dates: { mode: "all" },
    products: { mode: "all" },
  };
  const requests: HistoryRequest[] = [
    all,
    {
      dates: {
        mode: "range",
        start_date: "2024-02-29",
        end_date: "2024-03-02",
      },
      products: { mode: "all" },
    },
    { dates: { mode: "all" }, products: { mode: "subset", values: ["Y"] } },
    {
      dates: { mode: "all" },
      products: { mode: "subset", values: ["absent"] },
    },
    {
      dates: {
        mode: "range",
        start_date: "5877642-06-23 (BC)",
        end_date: "5877642-06-23 (BC)",
      },
      products: { mode: "all" },
    },
  ];
  for (const dataset of ["extended", "empty"])
    await withHistorySession(cli, artifact, dataset, async (session) => {
      const discovery = await session.discover();
      sessions.push({ dataset, events: session.events });
      for (const request of requests) {
        const args = {
            relationship: String(discovery.program.relationship),
            request_json: JSON.stringify(request),
          },
          result = historyResult(
            historyContent(await session.call(HISTORY_TOOLS[0], args)),
          ),
          plan = historyPlan(
            historyContent(await session.call(HISTORY_TOOLS[1], args)),
          );
        sameHistoryBinding(plan, result);
        sameHistoryBinding(discovery.program, result);
        assert.deepEqual(result.request, request);
        assert.deepEqual(
          result.links,
          historyOracle(request, dataset === "empty"),
        );
        checks.push(
          `${dataset}/${request.dates.mode}/${request.products.mode}: complete independent members and binding`,
        );
        if (dataset !== "extended" || request !== all) continue;
        const pack = (v: unknown, field = "result_json") =>
          JSON.stringify([{ [field]: JSON.stringify(v) }]);
        const mutations: [string, (v: Record<string, any>) => void][] = [
          [
            "wrong schema",
            (v) => (v.schema_version = "ossie-relation-result-v1"),
          ],
          ["wrong version", (v) => (v.api_version = "2.0.0")],
          ["wrong type", (v) => (v.target_identity_bound_type = "INTEGER")],
          ["extra", (v) => (v.extra = true)],
          ["bad hash", (v) => (v.content_sha256 = "x")],
          ["incomplete", (v) => (v.complete = false)],
          ["truncated", (v) => (v.truncated = true)],
          ["numeric count", (v) => (v.link_count = 12)],
          ["missing link", (v) => v.links.pop()],
          ["duplicate link", (v) => v.links.splice(1, 0, v.links[0])],
          [
            "functional conflict",
            (v) => {
              const r = [...v.links[0]];
              r[3] += "x";
              v.links.splice(1, 0, r);
              v.link_count = String(v.links.length);
            },
          ],
          ["wrong order", (v) => v.links.reverse()],
          ["infinity", (v) => (v.links[0][1] = "infinity")],
          ["surrogate", (v) => (v.links[0][0] = "\uD800")],
          [
            "scope violation",
            (v) =>
              (v.request.products = { mode: "subset", values: ["absent"] }),
          ],
          ["bad role concepts", (v) => v.link_role_concepts.reverse()],
          ["index exceeds source", (v) => (v.index_segment_count = "10000")],
        ];
        for (const [label, edit] of mutations) {
          const bad = structuredClone(result);
          edit(bad);
          assert.throws(() => historyResult(pack(bad)), label);
          damage.push(label);
        }
        const raw = JSON.stringify(result).replace(
          '"api_version":"1.0.0"',
          '"api_version":"2.0.0","api_version":"1.0.0"',
        );
        assert.throws(() =>
          historyResult(JSON.stringify([{ result_json: raw }])),
        );
        damage.push("duplicate decoded key");
        for (const [label, edit] of [
          ["executed", (v: Record<string, any>) => (v.executed = true)],
          ["computed", (v: Record<string, any>) => (v.links_computed = true)],
          [
            "wrong operators",
            (v: Record<string, any>) => v.operators.reverse(),
          ],
          [
            "missing DATE",
            (v: Record<string, any>) => (v.input_types[1] = "VARCHAR"),
          ],
          ["bad limits", (v: Record<string, any>) => (v.limits.links = 1)],
        ] as [string, (v: Record<string, any>) => void][]) {
          const bad = structuredClone(plan);
          edit(bad);
          assert.throws(() => historyPlan(pack(bad, "plan_json")));
          damage.push("plan " + label);
        }
        for (const [label, edit] of [
          [
            "wrong source count",
            (v: Record<string, any>) => (v.total_source_rows = "0"),
          ],
          [
            "false support",
            (v: Record<string, any>) => (v.query_supported = false),
          ],
          [
            "wrong role",
            (v: Record<string, any>) => v.sources[1].roles.reverse(),
          ],
          [
            "wrong actual type",
            (v: Record<string, any>) =>
              (v.sources[0].roles[1].bound_type = "VARCHAR"),
          ],
          [
            "fake MCP probe",
            (v: Record<string, any>) => (v.mcp_registered = true),
          ],
        ] as [string, (v: Record<string, any>) => void][]) {
          const bad = structuredClone(discovery.program);
          edit(bad);
          assert.throws(() => historyProgram(pack(bad, "program_json")));
          damage.push("program " + label);
        }
        for (const name of HISTORY_TOOLS)
          for (const [label, bad, code] of [
            ["missing dates", { products: { mode: "all" } }, "INVALID_REQUEST"],
            ["extra", { ...all, carry: true }, "INVALID_REQUEST"],
            [
              "date mode",
              { ...all, dates: { mode: "as_of" } },
              "UNSUPPORTED_CAPABILITY",
            ],
            [
              "numeric product",
              { ...all, products: { mode: "subset", values: [1] } },
              "INVALID_REQUEST",
            ],
            [
              "duplicate product",
              { ...all, products: { mode: "subset", values: ["X", "X"] } },
              "INVALID_REQUEST",
            ],
            [
              "infinity",
              {
                ...all,
                dates: {
                  mode: "range",
                  start_date: "-infinity",
                  end_date: "2024-03-01",
                },
              },
              "INVALID_REQUEST",
            ],
            ["oversize request", " ".repeat(16385), "RESOURCE_LIMIT_EXCEEDED"],
            [
              "decoded duplicate",
              '{"dates":{"mode":"all"},"\\u0064ates":{"mode":"all"},"products":{"mode":"all"}}',
              "INVALID_REQUEST",
            ],
          ] as [string, unknown, string][]) {
            const reply = await session.call(name, {
              ...args,
              request_json: typeof bad === "string" ? bad : JSON.stringify(bad),
            });
            assert(reply.error || reply.result?.isError);
            const txt =
              (reply.result?.content as { text?: string }[] | undefined)
                ?.map((x) => x.text ?? "")
                .join("\n") ??
              String((reply.error as { message?: string })?.message ?? "");
            assert.equal(populationError(txt)?.code, code, label);
            refusals.push({ name, label, code });
            assert.deepEqual(
              historyResult(
                historyContent(await session.call(HISTORY_TOOLS[0], args)),
              ),
              result,
              "recovery " + label,
            );
          }
        for (const name of [
          "population_daily_states",
          "population_relation_links",
          "query",
          "execute",
          "ossie_bundle_load",
        ]) {
          const reply = await session.call(name, args);
          assert(reply.error || reply.result?.isError);
          refusals.push({ name, label: "outside registry" });
        }
        for (const name of HISTORY_TOOLS) {
          const reply = await session.call(name, {
            ...args,
            relationship: "other",
          });
          assert(reply.error || reply.result?.isError);
          refusals.push({ name, label: "wrong relationship" });
        }
        assert.deepEqual(
          historyResult(
            historyContent(
              await session.call(HISTORY_TOOLS[0], {
                ...args,
                products: "absent",
              }),
            ),
          ),
          result,
        );
        checks.push(
          "outer unknown scope ignored; all scope must be in request_json",
        );
        assert.deepEqual(
          historyResult(
            historyContent(
              await session.call(HISTORY_TOOLS[0], {
                ...args,
                request_json: all,
              }),
            ),
          ),
          result,
        );
        checks.push(
          "actual upstream object-to-string coercion; supported callers send Strings",
        );
      }
    });
  await withHistorySession(cli, artifact, "budget", async (session) => {
    await session.discover();
    sessions.push({ dataset: "budget", events: session.events });
    const args = {
      relationship: "team_at_observation",
      request_json: JSON.stringify(all),
    };
    const plan = historyPlan(
      historyContent(await session.call(HISTORY_TOOLS[1], args)),
    );
    assert.equal(plan.observation_rows, "1000");
    const reply = await session.call(HISTORY_TOOLS[0], args);
    assert(reply.error || reply.result?.isError);
    const txt =
      (reply.result?.content as { text?: string }[] | undefined)
        ?.map((x) => x.text ?? "")
        .join("\n") ??
      String((reply.error as { message?: string })?.message ?? "");
    assert.equal(populationError(txt)?.code, "RESOURCE_LIMIT_EXCEEDED");
    refusals.push({
      label: "1000 links/large target complete JSON exceeds1MiB",
    });
    const empty = {
      dates: { mode: "all" },
      products: { mode: "subset", values: ["absent"] },
    };
    const result = historyResult(
      historyContent(
        await session.call(HISTORY_TOOLS[0], {
          ...args,
          request_json: JSON.stringify(empty),
        }),
      ),
    );
    assert.deepEqual(result.links, []);
    sameHistoryBinding(
      result,
      historyPlan(
        historyContent(
          await session.call(HISTORY_TOOLS[1], {
            ...args,
            request_json: JSON.stringify(empty),
          }),
        ),
      ),
    );
    checks.push(
      "plan binds despite execution byte limit; smaller empty scope recovers",
    );
  });
  await withHistorySession(cli, artifact, "large", async (session) => {
    const discovery = await session.discover();
    sessions.push({ dataset: "large", events: session.events });
    const args = {
      relationship: "team_at_observation",
      request_json: JSON.stringify(all),
    };
    const result = historyResult(
      historyContent(await session.call(HISTORY_TOOLS[0], args)),
    );
    const plan = historyPlan(
      historyContent(await session.call(HISTORY_TOOLS[1], args)),
    );
    sameHistoryBinding(discovery.program, result);
    sameHistoryBinding(plan, result);
    assert.equal(result.observation_rows, "10000");
    assert.deepEqual(
      result.links,
      Array.from({ length: 10000 }, (_, i) => [
        "u",
        "2024-02-29",
        "p" + i,
        "T",
      ]).sort((a, b) => Buffer.compare(Buffer.from(a[2]), Buffer.from(b[2]))),
    );
    checks.push(
      "actual MCP complete10000 four-role identities, not count-only; same program/plan binding",
    );
  });
  return {
    passed: true,
    scope:
      "history MCP complete independent synthetic links and strict decoder; no Agent/model autonomy claim",
    runtime: historyRuntime(cli, artifact),
    fixture_sha256: digest(join(ROOT, HISTORY_FIXTURE)),
    decoder_sha256: digest(join(ROOT, "scripts/history_result.ts")),
    checks,
    refusals,
    damage,
    sessions,
    real_model_requests: 0,
  };
}
if (import.meta.main) {
  const [cli, artifact, out] = process.argv.slice(2);
  assert(cli && artifact && out && process.argv.length === 5);
  const result = await verifyHistoryMcp(resolve(cli), resolve(artifact));
  writeFileSync(out, JSON.stringify(result, null, 2) + "\n");
  console.log(
    `PASS history MCP ${result.checks.length} checks/${result.refusals.length} refusals/${result.damage.length} damage; model0`,
  );
}

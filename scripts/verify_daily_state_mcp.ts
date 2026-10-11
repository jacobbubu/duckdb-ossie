/** Real stock MCP, complete independent state rows, strict decoder damage and refusal recovery. */
import assert from "node:assert/strict";
import { writeFileSync } from "node:fs";
import { resolve, join } from "node:path";
import { ROOT, digest } from "./artifact_helpers.ts";
import {
  withStateSession,
  stateRuntime,
  stateContent,
  STATE_TOOLS,
} from "./daily_state_mcp.ts";
import {
  stateResult,
  statePlan,
  sameStateBinding,
  type StateRequest,
} from "./daily_state_result.ts";
import { stateOracle, STATE_FIXTURE } from "./daily_state_oracle.ts";
import { populationError } from "./population_error.ts";
export async function verifyStateMcp(cli: string, artifact: string) {
  const checks: string[] = [],
    refusals: unknown[] = [],
    damage: string[] = [],
    sessions: unknown[] = [];
  for (const threshold of [10, 20])
    for (const dataset of ["extended", "empty"])
      await withStateSession(
        cli,
        artifact,
        threshold,
        dataset,
        async (session) => {
          const discovery = await session.discover();
          const record = { threshold, dataset, events: session.events };
          sessions.push(record);
          const request: StateRequest = {
            start_date: "2026-01-31",
            end_date: "2026-02-02",
            products: { mode: "all" },
            user_domain: "captured_users",
            transitions: "within_request_days",
          };
          for (const scope of [
            request,
            {
              ...request,
              products: { mode: "subset" as const, values: ["X"] },
            },
            {
              ...request,
              products: { mode: "subset" as const, values: ["absent"] },
            },
            { ...request, end_date: request.start_date },
          ]) {
            const args = {
              concept: "QualifiedObservation",
              request_json: JSON.stringify(scope),
            };
            const plan = statePlan(
                stateContent(await session.call(STATE_TOOLS[1], args)),
              ),
              result = stateResult(
                stateContent(await session.call(STATE_TOOLS[0], args)),
              );
            sameStateBinding(plan, result);
            sameStateBinding(discovery.program, result);
            assert.deepEqual(result.request, scope);
            assert.equal(plan.captured_user_count, result.user_count);
            assert.equal(
              result.declared_rule,
              `DailyObservation.classification_value >= ${threshold}`,
            );
            const oracle = stateOracle(scope, threshold, dataset === "empty");
            assert.deepEqual(result.states, oracle.states);
            assert.deepEqual(result.comparisons, oracle.comparisons);
            checks.push(
              `${threshold}/${dataset}/${scope.products.mode}/${scope.end_date}: complete independent rows, program/plan binding`,
            );
            if (threshold !== 10 || dataset !== "extended" || scope !== request)
              continue;
            const mutations: [string, (v: Record<string, any>) => void][] = [
              ["wrong version", (v) => (v.state_api_version = "2.0.0")],
              [
                "wrong schema",
                (v) => (v.schema_version = "ossie-daily-user-set-result-v1"),
              ],
              ["extra field", (v) => (v.ignored = true)],
              ["carry", (v) => (v.carry_forward = true)],
              ["domain", (v) => (v.user_domain = "observed_users")],
              ["incomplete", (v) => (v.complete = false)],
              ["bad hash", (v) => (v.content_sha256 = "x")],
              ["numeric count", (v) => (v.user_count = 10)],
              ["unit", (v) => (v.state_totals.unit = "User")],
              ["wrong totals", (v) => (v.state_totals.UNKNOWN = "0")],
              ["missing state", (v) => v.states.pop()],
              ["wrong calendar", (v) => (v.states[0][1] = "2026-02-01")],
              ["wrong User", (v) => (v.states[1][0] = "other")],
              ["invalid state", (v) => (v.states[0][2] = null)],
              ["surrogate identity", (v) => (v.states[0][0] = "\uD800")],
              [
                "wrong change flag",
                (v) => (v.comparisons[0][5] = !v.comparisons[0][5]),
              ],
              ["missing unchanged pair", (v) => v.comparisons.pop()],
              [
                "external predecessor",
                (v) => (v.comparisons[0][1] = "2026-01-30"),
              ],
              [
                "wrong changed count",
                (v) => (v.changed_comparison_count = "0"),
              ],
              [
                "request mismatch",
                (v) => (v.request.user_domain = "registered_users"),
              ],
            ];
            for (const [label, edit] of mutations) {
              const bad = structuredClone(result);
              edit(bad);
              assert.throws(
                () =>
                  stateResult(
                    JSON.stringify([{ result_json: JSON.stringify(bad) }]),
                  ),
                label,
              );
              damage.push(label);
            }
            const duplicate = JSON.stringify(result).replace(
              '"state_api_version":"1.0.0"',
              '"state_api_version":"2.0.0","state_api_version":"1.0.0"',
            );
            assert.throws(() =>
              stateResult(JSON.stringify([{ result_json: duplicate }])),
            );
            damage.push("duplicate decoded key");
            for (const [label, edit] of [
              ["executed", (v: Record<string, any>) => (v.executed = true)],
              [
                "wrong operators",
                (v: Record<string, any>) => v.operators.reverse(),
              ],
              [
                "missing BOOLEAN",
                (v: Record<string, any>) => v.input_types.pop(),
              ],
              [
                "fake domain count",
                (v: Record<string, any>) => (v.captured_user_count = "10000"),
              ],
            ] as [string, (v: Record<string, any>) => void][]) {
              const bad = structuredClone(plan);
              edit(bad);
              assert.throws(
                () =>
                  statePlan(
                    JSON.stringify([{ plan_json: JSON.stringify(bad) }]),
                  ),
                label,
              );
              damage.push("plan " + label);
            }
            for (const name of STATE_TOOLS)
              for (const [label, bad, code] of [
                [
                  "missing policy",
                  { ...scope, user_domain: undefined },
                  "INVALID_REQUEST",
                ],
                [
                  "wrong domain",
                  { ...scope, user_domain: "registry" },
                  "UNSUPPORTED_CAPABILITY",
                ],
                [
                  "outside predecessor",
                  { ...scope, transitions: "previous_day" },
                  "UNSUPPORTED_CAPABILITY",
                ],
                ["extra", { ...scope, carry_forward: true }, "INVALID_REQUEST"],
                [
                  "bad date",
                  { ...scope, start_date: "2026-02-30" },
                  "INVALID_REQUEST",
                ],
                [
                  "numeric product",
                  { ...scope, products: { mode: "subset", values: [1] } },
                  "INVALID_REQUEST",
                ],
                [
                  "duplicate products",
                  {
                    ...scope,
                    products: { mode: "subset", values: ["X", "X"] },
                  },
                  "INVALID_REQUEST",
                ],
                [
                  "too many days",
                  {
                    ...scope,
                    start_date: "2024-01-01",
                    end_date: "2025-01-01",
                  },
                  "RESOURCE_LIMIT_EXCEEDED",
                ],
              ] as [string, unknown, string][]) {
                const reply = await session.call(name, {
                  concept: "QualifiedObservation",
                  request_json: JSON.stringify(bad),
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
                  stateResult(
                    stateContent(await session.call(STATE_TOOLS[0], args)),
                  ),
                  result,
                  "recovery " + label,
                );
              }
            for (const name of [
              "population_daily_users",
              "query",
              "execute",
              "population_objects",
              "ossie_bundle_load",
            ]) {
              const reply = await session.call(name, args);
              assert(reply.error || reply.result?.isError);
              refusals.push({ name, label: "outside registry" });
            }
            const ignored = stateResult(
              stateContent(
                await session.call(STATE_TOOLS[0], {
                  ...args,
                  products: "absent",
                }),
              ),
            );
            assert.deepEqual(ignored, result);
            checks.push(
              "unknown outer parameter ignored; must place scope in request_json",
            );
            const objectRequest = stateResult(
              stateContent(
                await session.call(STATE_TOOLS[0], {
                  ...args,
                  request_json: scope,
                }),
              ),
            );
            assert.deepEqual(objectRequest, result);
            checks.push(
              "upstream object-to-string coercion observed; caller guard remains Strings",
            );
          }
        },
      );
  await withStateSession(cli, artifact, 10, "budget", async (session) => {
    const discovery = await session.discover();
    sessions.push({ threshold: 10, dataset: "budget", events: session.events });
    assert.equal(discovery.program.captured_user_count, "3334");
    const request: StateRequest = {
      start_date: "2026-01-31",
      end_date: "2026-02-01",
      products: { mode: "all" },
      user_domain: "captured_users",
      transitions: "within_request_days",
    };
    for (const name of STATE_TOOLS) {
      const reply = await session.call(name, {
        concept: "QualifiedObservation",
        request_json: JSON.stringify(request),
      });
      assert(reply.error || reply.result?.isError);
      const text =
        (reply.result?.content as { text: string }[] | undefined)
          ?.map((v) => v.text)
          .join("\n") ??
        String((reply.error as { message?: string })?.message ?? "");
      assert.equal(populationError(text)?.code, "RESOURCE_LIMIT_EXCEEDED");
      refusals.push({
        name,
        label: "3334 Users/two days=10002 complete objects",
      });
    }
    const result = stateResult(
      stateContent(
        await session.call(STATE_TOOLS[0], {
          concept: "QualifiedObservation",
          request_json: JSON.stringify({
            ...request,
            end_date: request.start_date,
          }),
        }),
      ),
    );
    assert.deepEqual(
      result.states,
      Array.from({ length: 3334 }, (_, i) => [
        "u" + i,
        request.start_date,
        "TRUE",
      ]).sort((a, b) => Buffer.compare(Buffer.from(a[0]), Buffer.from(b[0]))),
    );
    checks.push(
      "MCP budget rejects both query/compile before expansion; complete single-day recovery3334 Users",
    );
  });
  return {
    passed: true,
    scope:
      "state MCP complete transport and strict decoder; no Agent/model autonomy claim",
    runtime: stateRuntime(cli, artifact),
    fixture_sha256: digest(join(ROOT, STATE_FIXTURE)),
    decoder_sha256: digest(join(ROOT, "scripts/daily_state_result.ts")),
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
  const result = await verifyStateMcp(resolve(cli), resolve(artifact));
  writeFileSync(out, JSON.stringify(result, null, 2) + "\n");
  console.log(
    `PASS state MCP ${result.checks.length} checks/${result.refusals.length} refusals/${result.damage.length} decoder damage controls; model0`,
  );
}

/** Real observation MCP transport checks, no model access. */
import assert from "node:assert/strict";
import { existsSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { RpcProcess } from "./mcp_check.ts";
import { temporary, ROOT, digest } from "./artifact_helpers.ts";
import { join } from "node:path";
import {
  INITIALIZE,
  runtimeMetadata,
  setupText,
  withObservationSession,
} from "./observation_mcp.ts";
import {
  aggregate,
  observationInput,
  tuple,
  METRICS,
  OBSERVATION_PROFILE,
  metricEqual,
  assertResultBinding,
  binding,
  auditClaim,
  type ExecutionEvidence,
} from "./observation_evidence.ts";
import { populationResult, resultRecords } from "./population_result.ts";
import { populationError } from "./population_error.ts";
export async function verifyObservationMcp(
  cli: string,
  artifact: string,
  progress: Record<string, unknown> = {},
) {
  const runtime = runtimeMetadata(cli, artifact);
  const input = observationInput();
  const checks: string[] = [];
  const refusals: unknown[] = [];
  const sessions: unknown[] = [];
  Object.assign(progress, { runtime, checks, refusals, sessions });
  const check = (label: string, fn: () => void) => {
    fn();
    checks.push(label);
  };
  for (const threshold of [10, 20])
    await withObservationSession(cli, artifact, threshold, async (session) => {
      const entry: Record<string, unknown> = {
        threshold,
        events: session.events,
      };
      sessions.push(entry);
      const discovered = await session.discover();
      entry.discovery = discovered;
      checks.push(
        `${threshold}: exclusive tools/resources; v2 identity, rules, metric roles, capabilities and hashes`,
      );
      const scene = threshold === 10 ? "baseline" : "changed-rule";
      const successes: ExecutionEvidence[] = [];
      const base = {
        concept: "QualifiedObservation",
        metrics: JSON.stringify(METRICS),
        dimensions: "[]",
        filters: "[]",
      };
      async function execution(name: string, args: Record<string, string>) {
        const reply = await session.call(name, args);
        assert(!reply.error && !reply.result?.isError, JSON.stringify(reply));
        const blocks = reply.result?.content as { text: string }[];
        assert.equal(blocks.length, 1);
        const result = populationResult(blocks[0].text);
        assertResultBinding(result, discovered.binding);
        assert.equal(result.profile, OBSERVATION_PROFILE);
        assert.equal(result.concept, args.concept);
        successes.push({
          scene,
          id: String(reply.id),
          name,
          args,
          content: blocks[0].text,
        });
        return result;
      }
      async function query(
        label: string,
        filters: string[],
        predicate: (r: (typeof input)[number]) => boolean,
        dimensions: string[] = [],
      ) {
        const result = await execution("population_query", {
          ...base,
          filters: JSON.stringify(filters),
          dimensions: JSON.stringify(dimensions),
        });
        const rows = resultRecords(result);
        const selected = input.filter(
          (r) => r.classification >= threshold && predicate(r),
        );
        const groups = new Map<string, typeof input>();
        if (!dimensions.length) groups.set("[]", selected);
        else
          for (const r of selected) {
            const k = JSON.stringify(
              dimensions.map((d) =>
                d === "observations.ds" ? r.date : r.product,
              ),
            );
            groups.set(k, [...(groups.get(k) ?? []), r]);
          }
        check(`${threshold}: ${label}`, () => {
          assert.equal(rows.length, groups.size);
          const seen = new Set<string>();
          for (const row of rows) {
            const k = JSON.stringify(dimensions.map((d) => row[d]));
            assert(!seen.has(k));
            seen.add(k);
            const group = groups.get(k);
            assert(group);
            const expected = aggregate(group);
            for (const metric of METRICS)
              metricEqual(metric, row[metric], expected[metric]);
          }
        });
        return result;
      }
      await query("five scalar metrics", [], () => true);
      const scope = [
        "observations.ds >= DATE '2026-01-31' AND observations.ds < DATE '2026-02-02'",
      ];
      await query("explicit interval", scope, () => true);
      const scalar = successes.at(-1)!;
      const claimContext = {
        scene,
        threshold,
        binding: discovered.binding,
        input,
      };
      const expected = aggregate(
        input.filter((r) => r.classification >= threshold),
      );
      for (const metric of METRICS)
        check(
          `${threshold}: actual carrier supports ${metric} with exact scope`,
          () =>
            assert.equal(
              auditClaim(
                { evidence_id: scalar.id, metric, value: expected[metric] },
                successes,
                claimContext,
              ).status,
              "pass",
            ),
        );
      await execution("population_objects", {
        concept: "QualifiedObservation",
        filters: JSON.stringify(scope),
      });
      const fullObjects = successes.at(-1)!;
      for (const day of ["2026-01-31", "2026-02-01"])
        check(
          `${threshold}: actual complete objects derive User count for ${day}`,
          () =>
            assert.equal(
              auditClaim(
                {
                  evidence_id: fullObjects.id,
                  metric: "user_count",
                  day,
                  value: aggregate(
                    input.filter(
                      (r) => r.classification >= threshold && r.date === day,
                    ),
                  ).user_count,
                },
                successes,
                claimContext,
              ).status,
              "pass",
            ),
        );
      await query("date groups", [], () => true, ["observations.ds"]);
      await query("date/product groups", [], () => true, [
        "observations.ds",
        "observations.product",
      ]);
      await query(
        "X scope",
        ["DailyObservation.product = 'X'"],
        (r) => r.product === "X",
      );
      await query(
        "NULL-only scope",
        ["observations.user_id = 'C'"],
        (r) => r.user === "C",
      );
      await query(
        "empty scalar",
        ["DailyObservation.product = 'missing'"],
        () => false,
      );
      await query(
        "empty groups",
        ["DailyObservation.product = 'missing'"],
        () => false,
        ["observations.ds"],
      );
      const objects = await execution("population_objects", {
        concept: "QualifiedObservation",
        filters: "[]",
      });
      const expectedObjects = input.filter(
        (r) => r.classification >= threshold,
      );
      const records = resultRecords(objects);
      check(`${threshold}: complete ordered typed tuple objects`, () => {
        assert.equal(objects.request_kind, "objects");
        assert.deepEqual(
          objects.columns.slice(0, 5).map((c) => [c.name, c.bound_type]),
          [
            ["observations.user_id", "VARCHAR"],
            ["observations.ds", "DATE"],
            ["observations.product", "VARCHAR"],
            ["observations.amount_usd", "DECIMAL(18,2)"],
            ["observations.classification_value", "INTEGER"],
          ],
        );
        assert.deepEqual(
          records
            .map((r) =>
              JSON.stringify([
                r["observations.user_id"],
                r["observations.ds"],
                r["observations.product"],
              ]),
            )
            .sort(),
          expectedObjects.map(tuple).sort(),
        );
        for (const record of records) {
          const row = expectedObjects.find(
            (r) =>
              tuple(r) ===
              JSON.stringify([
                record["observations.user_id"],
                record["observations.ds"],
                record["observations.product"],
              ]),
          );
          assert(row);
          assert.equal(
            record["observations.amount_usd"],
            row.cents === null
              ? null
              : `${row.cents / 100n}.${String(row.cents % 100n).padStart(2, "0")}`,
          );
          assert.equal(
            record["observations.classification_value"],
            String(row.classification),
          );
        }
      });
      const emptyObjects = await execution("population_objects", {
        concept: "QualifiedObservation",
        filters: "[\"DailyObservation.product = 'missing'\"]",
      });
      check(`${threshold}: empty objects retain provenance`, () =>
        assert.equal(emptyObjects.row_count, 0),
      );
      for (const metrics of ["[]", JSON.stringify(METRICS)]) {
        const reply = await session.call("population_compile", {
          ...base,
          metrics,
        });
        assert(!reply.error && !reply.result?.isError);
        const blocks = reply.result?.content as { text: string }[];
        const [compiled] = JSON.parse(blocks[0].text);
        check(
          `${threshold}: compile ${metrics === "[]" ? "objects" : "metrics"} plan-v2 is diagnostic`,
          () => {
            assert.equal(compiled.executed, false);
            assert.deepEqual(binding(compiled), discovered.binding);
            const plan = JSON.parse(compiled.plan_json);
            assert.equal(plan.schema_version, "ossie-population-plan-v2");
            assert.equal(plan.executed, false);
          },
        );
      }
      async function refuse(
        label: string,
        name: string,
        args: Record<string, string>,
        expected?: string,
      ) {
        const reply = await session.call(name, args);
        assert(reply.error || reply.result?.isError, `${label}: must refuse`);
        const normalized = populationError(JSON.stringify(reply));
        // Error JSON nested in MCP is escaped; extract known content before framing.
        const blocks = reply.result?.content as { text?: string }[] | undefined;
        const error =
          normalized ??
          populationError(blocks?.map((b) => b.text ?? "").join("\n") ?? "") ??
          populationError(
            String((reply.error as { message?: string })?.message ?? ""),
          );
        if (expected) assert.equal(error?.code, expected, label);
        refusals.push({ threshold, label, reply, normalized: error ?? null });
      }
      for (const name of [
        "population_query",
        "population_compile",
        "population_objects",
      ])
        await refuse(
          "User referent-only",
          name,
          name === "population_objects"
            ? { concept: "User", filters: "[]" }
            : { ...base, concept: "User" },
          "UNSUPPORTED_CAPABILITY",
        );
      await refuse(
        "illegal User grouping",
        "population_query",
        { ...base, dimensions: '["observations.user_id"]' },
        "UNSUPPORTED_CAPABILITY",
      );
      await refuse(
        "unregistered metric",
        "population_query",
        { ...base, metrics: '["mean_user_total"]' },
        "INVALID_REQUEST",
      );
      for (const filters of ["not-json", "[1]", "[true]", "[null]", "null"])
        await refuse(
          "invalid filter list " + filters,
          "population_query",
          { ...base, filters },
          "INVALID_REQUEST",
        );
      for (const name of [
        "query",
        "execute",
        "export",
        "describe",
        "list_tables",
        "database_info",
        "ossie_bundle_load",
        "population_reload",
        "unknown",
      ])
        await refuse("unpublished tool " + name, name, { sql: "SELECT 1" });
      await query("recovery after refusals", [], () => true);
    });
  await temporary("ossie-observation-failfast-", async (directory) => {
    const path = join(directory, "failure.sql");
    writeFileSync(
      path,
      setupText(artifact, 10).replace("bundle-10.json", "missing-bundle.json"),
    );
    const failed = new RpcProcess([cli, "-unsigned", "-bail", "-init", path]);
    try {
      await assert.rejects(
        () => failed.request(1, "initialize", INITIALIZE),
        /missing-bundle\.json/,
      );
      checks.push("failed operator startup exposes no server");
    } finally {
      await failed.close();
    }
  });
  return {
    status: "pass",
    scope: "observation MCP transport; not Agent acceptance",
    model_requests: 0,
    source_hashes: Object.fromEntries(
      [
        "observation_mcp",
        "observation_evidence",
        "verify_observation_mcp",
        "population_result",
        "population_error",
        "mcp_check",
      ].map((name) => {
        const path = join(ROOT, `scripts/${name}.ts`);
        return [path, digest(path)];
      }),
    ),
    runtime,
    checks,
    refusals,
    sessions,
  };
}
if (import.meta.main) {
  const args = process.argv.slice(2);
  assert(
    args.length === 6 &&
      args[0] === "--cli" &&
      args[2] === "--extension" &&
      args[4] === "--out",
    "Usage: bun scripts/verify_observation_mcp.ts --cli PATH --extension PATH --out NEW_JSON",
  );
  const out = resolve(args[5]);
  assert(!existsSync(out), "fresh output required");
  const progress: Record<string, unknown> = {};
  try {
    const report = await verifyObservationMcp(
      resolve(args[1]),
      resolve(args[3]),
      progress,
    );
    writeFileSync(out, JSON.stringify(report, null, 2) + "\n", { flag: "wx" });
    console.log(
      `Observation MCP: ${report.checks.length} checks and ${report.refusals.length} refusals passed; no model called`,
    );
  } catch (error) {
    writeFileSync(
      out,
      JSON.stringify(
        {
          status: "fail",
          error: String(error),
          model_requests: 0,
          ...progress,
        },
        null,
        2,
      ) + "\n",
      { flag: "wx" },
    );
    console.error(String(error));
    process.exitCode = 1;
  }
}

/** Actual MCP full-link oracle, no model requests. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { ROOT, digest } from "./artifact_helpers.ts";
import {
  withRelationSession,
  relationContent,
  relationRuntime,
} from "./relation_mcp.ts";
import {
  relationResult,
  relationPlan,
  sameRelationBinding,
  compareRelationLinks,
  relationDiscovery,
} from "./relation_result.ts";
import { populationError } from "./population_error.ts";
const [cliArg, artifactArg, outArg] = process.argv.slice(2);
assert(
  cliArg && artifactArg && outArg && process.argv.length === 5,
  "CLI ARTIFACT OUT_JSON",
);
const cli = resolve(cliArg),
  artifact = resolve(artifactArg),
  out = resolve(outArg);
const csv = join(
  ROOT,
  "docs/research/ossie-req-001/multi-period-research/every-day/observations.csv",
);
const rows = readFileSync(csv, "utf8")
  .trim()
  .split("\n")
  .slice(1)
  .map((r) => r.split(","));
const checks: string[] = [],
  refusals: unknown[] = [],
  sessions: unknown[] = [];
for (const [threshold, setup] of [
  [10, "extended"],
  [20, "extended"],
  [10, "empty"],
  [10, "wide"],
  [10, "raw"],
  [10, "full"],
] as const)
  await withRelationSession(cli, artifact, threshold, setup, async (s) => {
    const discovery = await s.discover();
    const entry: Record<string, unknown> = {
      threshold,
      setup,
      discovery,
      events: s.events,
      cases: [],
    };
    sessions.push(entry);
    for (const name of ["qualified_user", "AUDITED_USER"]) {
      const args = { relationship: name };
      const plan = relationPlan(
        relationContent(await s.call("population_relation_compile", args)),
      );
      const result = relationResult(
        relationContent(await s.call("population_relation_objects", args)),
      );
      sameRelationBinding(plan, result);
      const expected =
        setup === "empty"
          ? []
          : setup === "full"
            ? Array.from({ length: 10000 }, (_, i) => {
                const id = `U${String(i).padStart(5, "0")}`;
                return [id, "2026-01-01", "X", id];
              })
            : setup === "raw"
              ? [
                  ["é", "2026-01-01", "p\n", "é"],
                  ["a\u0000", "2026-01-01", "p", "a\u0000"],
                  ["A", "2026-01-01", '"\\', "A"],
                  ["a", "2026-01-01", "x", "a"],
                ].sort(compareRelationLinks)
              : setup === "wide"
                ? [
                    ["D", "5877642-06-25 (BC)", "X", "D"],
                    ["D", "0001-01-01 (BC)", "X", "D"],
                    ["D", "2026-01-01", "X", "D"],
                    ["D", "10000-01-01", "X", "D"],
                    ["D", "5881580-07-10", "X", "D"],
                  ]
                : rows
                    .filter((r) => r[5] !== "" && Number(r[5]) >= threshold)
                    .map((r) => [r[1], r[2], r[3], r[1]])
                    .sort(compareRelationLinks);
      assert.deepEqual(result.links, expected);
      assert.equal(result.link_count, String(expected.length));
      relationDiscovery(name, discovery.program, result);
      relationDiscovery(name, discovery.program, plan);
      assert.equal(result.target_identity_relationship, "id");
      assert.equal(result.target_identity_field, "user_id");
      (entry.cases as unknown[]).push({
        name,
        plan,
        result,
        independent_links: expected,
      });
      checks.push(
        `${setup}/${threshold}/${name}: full links, plan/discovery identity and six hashes`,
      );
    }
    if (setup !== "extended" || threshold !== 10) return;
    for (const tool of [
      "population_relation_compile",
      "population_relation_objects",
    ]) {
      for (const args of [{ relationship: "missing" }, {}]) {
        const reply = await s.call(tool, args);
        assert(reply.error || reply.result?.isError);
        const content = reply.result?.content as
          | { text?: string }[]
          | undefined;
        const text =
          content?.map((c) => c.text ?? "").join("\n") ??
          String((reply.error as { message?: string })?.message ?? "");
        if ("relationship" in args)
          assert.equal(populationError(text)?.code, "INVALID_REQUEST");
        refusals.push({ tool, args, reply });
      }
      for (const value of [42, { name: "audited_user" }]) {
        const reply = await s.call(tool, { relationship: value });
        assert(reply.error || reply.result?.isError);
        refusals.push({ tool, args: { relationship: value }, reply });
      }
      const supplied = {
        relationship: "audited_user",
        date: "1900-01-01",
        products: [],
        limit: 1,
      };
      const reply = await s.call(tool, supplied);
      const decoded = tool.endsWith("objects")
        ? relationResult(relationContent(reply))
        : relationPlan(relationContent(reply));
      assert.equal(
        decoded.scope,
        "entire captured root population; selected relationship predicate only",
      );
      if (tool.endsWith("objects")) assert.equal(decoded.link_count, "18");
      checks.push(
        `${tool}: observed upstream extra arguments ignored, no hidden filter/limit`,
      );
    }
    for (const name of [
      "execute_query",
      "query",
      "population_query",
      "population_daily_users",
      "ossie_bundle_load",
    ]) {
      const reply = await s.call(name, {});
      assert(reply.error || reply.result?.isError);
      refusals.push({ name, reply });
    }
  });
writeFileSync(
  out,
  JSON.stringify(
    {
      passed: true,
      runtime: relationRuntime(cli, artifact),
      artifact_sha256: digest(artifact),
      server_scope: "new relation MCP only, not complete P1 or live Agent",
      input_sha256: digest(csv),
      checks,
      refusals,
      sessions,
      real_model_calls: 0,
    },
    null,
    2,
  ) + "\n",
);
console.log(
  `PASS relation MCP ${checks.length} checks ${refusals.length} refusals; model requests0`,
);

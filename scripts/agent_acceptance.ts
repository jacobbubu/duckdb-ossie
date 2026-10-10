/** Three isolated experiments. --prepare performs MCP discovery without model access. */
import assert from "node:assert/strict";
import {
  appendFileSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { spawnSync } from "node:child_process";
import {
  agentLoop,
  beforeDeadline,
  providerBaseURL,
  responsesProvider,
  LIMITS,
  type JsonObject,
  type Tool,
} from "./agent_loop.ts";
import { RpcProcess } from "./mcp_check.ts";
import { digest, ROOT, sqlString } from "./artifact_helpers.ts";
import { populationResult, resultRecords } from "./population_result.ts";
const EVIDENCE = join(ROOT, "docs/research/ossie-req-001");
const PROMPTS = join(EVIDENCE, "agent-discovery-preflight/prompts.json");
const parse = (p: string) => JSON.parse(readFileSync(p, "utf8"));
const shaFiles = (paths: string[]) =>
  Object.fromEntries(paths.map((p) => [p, digest(p)]));
export function experimentConfiguration(
  env: Record<string, string | undefined>,
) {
  const baseURL = providerBaseURL(
    env.OPENAI_BASE_URL ?? "https://api.openai.com/v1/",
  );
  const model = env.OPENAI_MODEL?.trim();
  assert(
    model,
    "OPENAI_MODEL required; select a tool-capable model explicitly",
  );
  return { baseURL, model };
}
export function checkAnswer(
  content: string,
  threshold: number,
  events: JsonObject[],
  baseline: JsonObject,
  boundary: boolean,
) {
  const answer = JSON.parse(content) as JsonObject;
  const checks: JsonObject = {
    status: answer.status,
    manual_review:
      "required: scope, rule interpretation, units, citations, unsupported claims",
  };
  assert(
    typeof answer.scope === "string" && typeof answer.definition === "string",
    "scope/definition required",
  );
  assert(
    Array.isArray(answer.evidence_ids) &&
      answer.evidence_ids.every((x) => typeof x === "string"),
    "citation list required",
  );
  assert(
    Array.isArray(answer.limitations) &&
      answer.limitations.every((x) => typeof x === "string"),
    "limitations list required",
  );
  if (boundary) {
    assert(
      answer.status === "unsupported",
      "unsupported scope must not become an answer",
    );
    assert(
      [answer.count, answer.total, answer.average].every((x) => x === null),
      "unsupported numerical claims refused",
    );
    return {
      ...checks,
      numerical_gate: "not applicable",
      semantic_refusal: "pending human review",
    };
  }
  assert(answer.status === "answered", "numerical scene must be answered");
  const day = (
    (baseline.cases as Record<string, JsonObject>)[String(threshold)]
      .daily as JsonObject[]
  ).find((d) => d.ds === "2026-03-10")!;
  assert(
    answer.count === String(day.record_count),
    "final record count disagrees with independent oracle",
  );
  assert(
    typeof answer.total === "string" &&
      /^\d+\.\d{2}$/.test(answer.total) &&
      answer.total === day.gaap_sum,
    "final exact SUM disagrees with oracle",
  );
  assert(
    typeof answer.average === "string" &&
      /^\d+(?:\.\d+)?$/.test(answer.average),
    "finite numeric mean string required",
  );
  const expected = Number(day.gaap_average);
  assert(
    Math.abs(Number(answer.average) - expected) <= 1e-10 ||
      answer.average === expected.toFixed(2),
    "final mean disagrees with oracle",
  );
  assert(answer.unit === "USD", "unit must be USD");
  const references = answer.evidence_ids as string[];
  assert(
    references.includes("ossie://population/concepts"),
    "formal rule resource citation required",
  );
  const supported = new Set<string>();
  for (const event of events.filter(
    (e) =>
      e.kind === "tool_response" && references.includes(String(e.evidence_id)),
  )) {
    const request = events.find(
      (e) => e.kind === "tool_request" && e.evidence_id === event.evidence_id,
    );
    if (request?.name !== "population_query") continue;
    const reply = event.result as JsonObject;
    const result = reply.result as JsonObject | undefined;
    if (reply.error || result?.isError) continue;
    const blocks = result?.content as { text?: string }[];
    if (!blocks?.[0]?.text) continue;
    const decoded = populationResult(blocks[0].text);
    const rows = resultRecords(decoded);
    for (const row of rows) {
      if (row.record_count === String(day.record_count)) supported.add("count");
      if (row.gaap_sum === day.gaap_sum) supported.add("total");
      if (
        typeof row.mean_gaap === "string" &&
        Math.abs(Number(row.mean_gaap) - expected) <= 1e-10
      )
        supported.add("average");
    }
  }
  assert(
    supported.size === 3,
    "final claims need cited executed query columns matching independent oracle",
  );
  return {
    ...checks,
    numerical_gate: "pass",
    executed_support:
      "matching cited values found; row/scope correspondence pending review",
    overall: "pending human review",
  };
}
export function experimentSummary(scenarios: JsonObject[], live: boolean) {
  return {
    scenarios,
    overall: scenarios.some((s) => s.overall === "fail")
      ? "fail"
      : live
        ? "pending human review"
        : "discovery-only; not Agent acceptance",
  };
}
export async function experiment(args: string[]) {
  const value = (name: string) => {
    const i = args.indexOf(name);
    assert(
      i >= 0 && args[i + 1] && !args[i + 1].startsWith("--"),
      `required ${name}`,
    );
    return args[i + 1];
  };
  assert(
    args.includes("--prepare") !== args.includes("--run"),
    "choose --prepare or --run",
  );
  const live = args.includes("--run");
  const out = resolve(value("--out")),
    cli = resolve(value("--cli")),
    extension = resolve(value("--extension"));
  assert(
    !existsSync(out),
    "output must be a fresh directory; never overwrite an experiment",
  );
  assert(
    existsSync(cli) && existsSync(extension),
    "CLI and artifact must exist",
  );
  const { baseURL, model } = experimentConfiguration(process.env);
  if (live)
    assert(
      args.includes("--token-budget-only"),
      "live run requires explicit token-only budget choice; monetary pricing unknown",
    );
  const key = live ? process.env.OPENAI_API_KEY : undefined;
  if (live) assert(key, "OPENAI_API_KEY required for --run");
  const metadata = spawnSync(
    cli,
    [
      "-json",
      "-c",
      "LOAD duckdb_mcp; SELECT version() AS version, (SELECT source_id FROM pragma_version()) AS source_id, extension_version, install_path FROM duckdb_extensions() WHERE extension_name='duckdb_mcp'",
    ],
    { encoding: "utf8", timeout: 10000 },
  );
  assert.equal(metadata.status, 0, "cannot identify stock CLI/MCP runtime");
  const runtime = JSON.parse(metadata.stdout)[0];
  assert(
    runtime.version === "v1.5.6" && runtime.source_id === "069cc9f9b5",
    "stock DuckDB 1.5.6 required",
  );
  const baseline = parse(join(EVIDENCE, "baseline-results.json"));
  assert.equal(
    digest(join(EVIDENCE, "gaap-original-475.csv")),
    baseline.fixture_sha256,
  );
  const prompts = parse(PROMPTS);
  const source = readFileSync(
    join(ROOT, "examples/population_server.sql"),
    "utf8",
  );
  assert(
    source.includes("INSTALL ossie FROM community;\nLOAD ossie;\n"),
    "exact source replacement required",
  );
  const git = (command: string[]) => {
    const r = spawnSync("git", command, { cwd: ROOT, encoding: "utf8" });
    assert.equal(r.status, 0, "git metadata unavailable");
    return r.stdout.trim();
  };
  mkdirSync(out, { recursive: true, mode: 0o700 });
  const save = (name: string, data: unknown) =>
    writeFileSync(join(out, name), JSON.stringify(data, null, 2) + "\n", {
      flag: "wx",
    });
  save("run.json", {
    mode: live ? "real-model" : "discovery-only",
    head: git(["rev-parse", "HEAD"]),
    dirty: git(["status", "--porcelain"]),
    baseURL,
    requested_model: model,
    key_variable: "OPENAI_API_KEY",
    api: "responses",
    endpoint: new URL("responses", baseURL).href,
    continuation:
      "store:true; previous_response_id only within each isolated scene",
    sampling: "provider default; effective unknown",
    backend_revision: "unknown",
    billing: "unknown; no monetary ceiling enforced",
    limits: LIMITS,
    maximum_model_requests: 24,
    maximum_requested_output_tokens: 49152,
    runtime: { ...runtime, mcp_sha256: digest(runtime.install_path) },
    hashes: shaFiles([
      cli,
      extension,
      PROMPTS,
      join(ROOT, "scripts/agent_loop.ts"),
      join(ROOT, "scripts/agent_acceptance.ts"),
      join(ROOT, "examples/population_server.sql"),
      join(EVIDENCE, "gaap-original-475.csv"),
      join(EVIDENCE, "baseline-results.json"),
    ]),
  });
  const temp = mkdtempSync(join(tmpdir(), "ossie-agent-"));
  const summaries: JsonObject[] = [];
  try {
    for (const scene of [
      { label: "baseline", threshold: 10, boundary: false },
      { label: "changed-rule", threshold: 20, boundary: false },
      { label: "unsupported-scope", threshold: 10, boundary: true },
    ]) {
      const directory = join(out, scene.label);
      mkdirSync(directory);
      const started = Date.now();
      const deadline = started + LIMITS.milliseconds;
      const events: JsonObject[] = [];
      const record = (data: JsonObject) => {
        const event = {
          ...data,
          timestamp: new Date().toISOString(),
          elapsed_ms: Date.now() - started,
        };
        events.push(structuredClone(event));
        appendFileSync(
          join(directory, "events.jsonl"),
          JSON.stringify(event) + "\n",
        );
      };
      const manifestPath = join(
        ROOT,
        `test/fixtures/population/bundle-${scene.threshold}.json`,
      );
      const manifest = parse(manifestPath);
      for (const kind of ["core", "ontology", "mapping"]) {
        assert.equal(
          digest(resolve(manifestPath, "..", manifest[kind].path)),
          manifest[kind].sha256,
          "definition hash mismatch",
        );
      }
      record({
        kind: "trusted_setup",
        manifest_sha256: digest(manifestPath),
        manifest,
        definition_hashes: {
          core: manifest.core.sha256,
          ontology: manifest.ontology.sha256,
          mapping: manifest.mapping.sha256,
        },
      });
      const setup = join(temp, scene.label + ".sql");
      writeFileSync(
        setup,
        source
          .replace(
            "INSTALL ossie FROM community;\nLOAD ossie;\n",
            `LOAD ${sqlString(extension)};\n`,
          )
          .replace("bundle-10.json", `bundle-${scene.threshold}.json`),
      );
      const server = new RpcProcess([
        cli,
        "-unsigned",
        "-bail",
        "-init",
        setup,
      ]);
      let id = 0;
      const rpc = async (method: string, params?: unknown) => {
        const request = { id: ++id, method, params };
        record({ kind: "rpc_request", request });
        const reply = await beforeDeadline(deadline, () =>
          server.request(id, method, params),
        );
        record({ kind: "rpc_response", reply });
        return reply;
      };
      try {
        const hello = await rpc("initialize", {
          protocolVersion: "2024-11-05",
          capabilities: {},
          clientInfo: { name: "ossie-real-agent", version: "1" },
        });
        assert(
          !hello.error && hello.result?.protocolVersion === "2024-11-05",
          "MCP negotiation failed",
        );
        server.send({ jsonrpc: "2.0", method: "notifications/initialized" });
        const toolsReply = await rpc("tools/list");
        assert(!toolsReply.error, "tools discovery failed");
        const tools = toolsReply.result?.tools as Tool[];
        assert.deepEqual(tools.map((t) => t.name).sort(), [
          "population_compile",
          "population_objects",
          "population_query",
        ]);
        const listed = await rpc("resources/list");
        assert(!listed.error, "resource discovery failed");
        const resources: JsonObject[] = [];
        let binding: JsonObject | undefined;
        const identityKeys = [
          "snapshot_id",
          "content_sha256",
          "core_sha256",
          "ontology_sha256",
          "mapping_sha256",
          "manifest_sha256",
        ];
        const uris = listed.result?.resources?.map((x) => x.uri).sort();
        assert.deepEqual(
          uris,
          ["capabilities", "concepts", "fields", "metrics"].map(
            (x) => "ossie://population/" + x,
          ),
        );
        for (const uri of uris!) {
          const reply = await rpc("resources/read", { uri });
          assert(
            !reply.error && !reply.result?.isError,
            "resource read failed",
          );
          resources.push({ evidence_id: uri, response: reply.result });
          const rows = JSON.parse(
            (reply.result?.contents as { text: string }[])[0].text,
          ) as JsonObject[];
          assert(rows.length > 0, "empty resource");
          for (const row of rows) {
            assert.equal(row.ontology_sha256, manifest.ontology.sha256);
            assert.equal(row.core_sha256, manifest.core.sha256);
            assert.equal(row.mapping_sha256, manifest.mapping.sha256);
            assert.equal(row.manifest_sha256, digest(manifestPath));
            const identity = Object.fromEntries(
              identityKeys.map((key) => [key, row[key]]),
            );
            assert(
              identityKeys.every(
                (key) =>
                  typeof identity[key] === "string" &&
                  String(identity[key]).length > 0,
              ),
              "incomplete discovery binding",
            );
            if (binding)
              assert.deepEqual(
                identity,
                binding,
                "discovery resources bind different snapshots",
              );
            else binding = identity;
          }
        }
        record({ kind: "discovered_context", tools, resources });
        if (!live) {
          summaries.push({
            scene: scene.label,
            status: "prepared",
            model_called: false,
          });
          continue;
        }
        assert(deadline > Date.now(), "scenario deadline exceeded");
        const final = await agentLoop({
          limits: { ...LIMITS, milliseconds: deadline - Date.now() },
          model,
          system: prompts.system,
          question: scene.boundary
            ? prompts.unsupported_scope
            : prompts.baseline_and_changed_rule,
          resources,
          tools,
          complete: responsesProvider(baseURL, key!),
          call: async (name, params) => {
            const reply = await rpc("tools/call", { name, arguments: params });
            if (
              !reply.error &&
              !reply.result?.isError &&
              name !== "population_compile"
            ) {
              const blocks = reply.result?.content as { text: string }[];
              const result = populationResult(blocks[0].text);
              assert.deepEqual(
                Object.fromEntries(
                  identityKeys.map((key) => [
                    key,
                    result[key as keyof typeof result],
                  ]),
                ),
                binding,
                "execution differs from discovered definition/data binding",
              );
            }
            return reply;
          },
          record,
        });
        let evaluation: JsonObject;
        try {
          evaluation = checkAnswer(
            final.content,
            scene.threshold,
            events,
            baseline,
            scene.boundary,
          );
        } catch (error) {
          evaluation = {
            numerical_gate: "fail",
            error: String(error),
            overall: "fail",
          };
        }
        writeFileSync(
          join(directory, "review.json"),
          JSON.stringify(
            {
              scene,
              final,
              evaluation,
              manual_checks: [
                "date/product/concept scope and no extra narrowing",
                "current formal predicate explained and resource citation",
                "response/column/row references support answer",
                "units and record versus entity scope",
                "no active/paid substitution",
                "compile/query hashes match if compile used",
              ],
              human_verdict: "pending",
            },
            null,
            2,
          ) + "\n",
          { flag: "wx" },
        );
        summaries.push({ scene: scene.label, ...evaluation });
      } catch (error) {
        writeFileSync(
          join(directory, "failure.json"),
          JSON.stringify({ error: String(error) }, null, 2) + "\n",
          { flag: "wx" },
        );
        throw error;
      } finally {
        await server.close();
      }
    }
    save("summary.json", experimentSummary(summaries, live));
    if (summaries.some((s) => s.overall === "fail"))
      throw new Error("one or more scenario numerical gates failed");
    console.log(
      live
        ? "Experiment recorded; human review required"
        : "Three isolated MCP discovery preparations passed; no model called",
    );
  } catch (error) {
    save("failure.json", { error: String(error), scenarios: summaries });
    throw error;
  } finally {
    rmSync(temp, { recursive: true, force: true });
  }
}
if (import.meta.main)
  experiment(process.argv.slice(2)).catch(() => {
    console.error(
      "Agent experiment stopped; inspect preserved output failure.json",
    );
    process.exitCode = 1;
  });

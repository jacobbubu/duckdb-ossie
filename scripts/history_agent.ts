/** Scripted Responses + actual history MCP, sealed evidence and strict offline sequential replay. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import {
  agentLoop,
  responsesProvider,
  type JsonObject,
  type Completion,
} from "./agent_loop.ts";
import { PrivateArchive, verifyArchive } from "./observation_agent_archive.ts";
import { completionFromProjection } from "./observation_agent_replay.ts";
import { ROOT, digest } from "./artifact_helpers.ts";
import {
  HistorySession,
  withHistorySession,
  historyRuntime,
  historyContent,
  HISTORY_TOOLS,
} from "./history_mcp.ts";
import type { RpcProcess } from "./mcp_check.ts";
import {
  historyResult,
  historyPlan,
  historyRequest,
  sameHistoryBinding,
  type HistoryRequest,
} from "./history_result.ts";
import { exact, object } from "./daily_state_result.ts";
import { factJSON as strictJSON } from "./fact_result.ts";
import {
  historyOracle,
  HISTORY_FIXTURE,
  HISTORY_OBSERVATIONS,
  HISTORY_RECORDS,
} from "./history_oracle.ts";
export const HISTORY_AGENT_LIMITS = {
  rounds: 3,
  calls: 2,
  milliseconds: 120000,
  outputTokens: 4096,
  inputBytes: 131072,
};
export const HISTORY_SYSTEM =
  'Use only discovered history tools/resources. Discover the exact relationship and typed roles first. Entire scope is inside String request_json: dates explicit all or inclusive canonical finite DATE range, products explicit all or subset. The project operator matches frozen observations against finite half-open history [start,end); gaps produce no links, different-target overlaps reject whole load, distinct same-target record_id witnesses do not duplicate links. No carry or qualification. Source/index counts are load metadata, not query counts. Compile binds projection but never scans/matches/computes links. Execute the requested scope to answer; preserve complete four-role links, integer-text counts, scope and six hashes; cite actual execution evidence. Return exactly {status:"answered",result:<complete native history result>,evidence_ids:[actual tool ID],limitations:[nonempty strings]}. Explain-only returns exactly {status:"not_executed",reason:<nonempty String>,evidence_ids:[actual compile ID],limitations:[nonempty strings]}, without computed results. No load/reload/arbitrary SQL, registry or generic upstream derivation claims. Do not infer business truth, completeness of real sources or model autonomy from this integration.';
export const HISTORY_SCENES = [
  {
    id: "all",
    dataset: "extended",
    dates: { mode: "all" },
    products: { mode: "all" },
    compileOnly: false,
  },
  {
    id: "date-range",
    dataset: "extended",
    dates: { mode: "range", start_date: "2024-02-29", end_date: "2024-03-03" },
    products: { mode: "all" },
    compileOnly: false,
  },
  {
    id: "product-Y",
    dataset: "extended",
    dates: { mode: "all" },
    products: { mode: "subset", values: ["Y"] },
    compileOnly: false,
  },
  {
    id: "gap",
    dataset: "extended",
    dates: { mode: "range", start_date: "2024-03-02", end_date: "2024-03-02" },
    products: { mode: "all" },
    compileOnly: false,
  },
  {
    id: "minimum",
    dataset: "extended",
    dates: {
      mode: "range",
      start_date: "5877642-06-23 (BC)",
      end_date: "5877642-06-23 (BC)",
    },
    products: { mode: "all" },
    compileOnly: false,
  },
  {
    id: "empty",
    dataset: "empty",
    dates: { mode: "all" },
    products: { mode: "all" },
    compileOnly: false,
  },
  {
    id: "compile-only",
    dataset: "extended",
    dates: { mode: "all" },
    products: { mode: "all" },
    compileOnly: true,
  },
] as const;
export type HistoryScene = (typeof HISTORY_SCENES)[number];
export const historySceneRequest = (s: HistoryScene): HistoryRequest =>
  structuredClone({ dates: s.dates, products: s.products }) as HistoryRequest;
export const historyQuestion = (s: HistoryScene) =>
  `${s.compileOnly ? "只解释，不执行，不输出计算后的链接或数量" : "执行并返回全部观察到目标的四角色关系集合及实际证据"}。日期${s.dates.mode === "all" ? "不限" : `${s.dates.start_date}至${s.dates.end_date}含首尾`}，${s.products.mode === "all" ? "全部产品" : "仅产品" + s.products.values.join(",")}。历史有效期[start,end)，缺口无链接；同目标重复见证只产生一条完整关系，记录ID不作为输出身份。不要延续或猜测关系。`;
export const HISTORY_AGENT_SOURCES = [
  HISTORY_FIXTURE,
  "examples/history_server.sql",
  "examples/history_request_notice.json",
  ...["core.json", "ontology.json", "mapping.json", "bundle.json"].map(
    (n) => "test/fixtures/population-history-program/" + n,
  ),
  ...[
    "history_agent",
    "history_mcp",
    "history_request_notice",
    "p1_agent_json",
    "history_result",
    "history_oracle",
    "relation_result",
    "daily_state_result",
    "daily_result",
    "daily_mcp",
    "fact_result",
    "agent_loop",
    "observation_agent_archive",
    "observation_agent_replay",
    "observation_mcp",
    "observation_evidence",
    "mcp_check",
    "artifact_helpers",
  ].map((n) => "scripts/" + n + ".ts"),
];
export function historyToolArgs(
  name: string,
  args: unknown,
  scene: HistoryScene,
  program: Record<string, unknown>,
) {
  assert(HISTORY_TOOLS.includes(name));
  if (scene.compileOnly)
    assert(name === HISTORY_TOOLS[1], "explain-only cannot execute");
  object(args);
  exact(args, ["relationship", "request_json"]);
  assert(
    args.relationship === program.relationship &&
      typeof args.request_json === "string",
  );
  assert.deepEqual(
    historyRequest(strictJSON(args.request_json, 16384)),
    historySceneRequest(scene),
  );
}
function decoded(
  name: string,
  reply: Awaited<ReturnType<HistorySession["call"]>>,
) {
  assert(HISTORY_TOOLS.includes(name));
  return name === HISTORY_TOOLS[0]
    ? historyResult(historyContent(reply))
    : historyPlan(historyContent(reply));
}
function fixtureBinding(program: Record<string, unknown>, scene: HistoryScene) {
  for (const [key, file] of [
    ["core_sha256", "core.json"],
    ["ontology_sha256", "ontology.json"],
    ["mapping_sha256", "mapping.json"],
    ["manifest_sha256", "bundle.json"],
  ])
    assert.equal(
      program[key],
      digest(join(ROOT, "test/fixtures/population-history-program", file)),
      key,
    );
  assert(
    program.root_concept === "DailyObservation" &&
      program.user_concept === "User" &&
      program.target_concept === "Team" &&
      program.relationship === "team_at_observation",
  );
  const sources = program.sources as Record<string, unknown>[];
  assert.equal(
    sources[0].rows,
    String(scene.dataset === "empty" ? 0 : HISTORY_OBSERVATIONS.length),
  );
  assert.equal(
    sources[1].rows,
    String(scene.dataset === "empty" ? 0 : HISTORY_RECORDS.length),
  );
}
export function auditHistoryAnswer(
  content: string,
  events: JsonObject[],
  scene: HistoryScene,
  program: Record<string, unknown>,
) {
  fixtureBinding(program, scene);
  const answer = strictJSON(content);
  object(answer);
  for (const e of events.filter((e) => e.kind === "tool_request")) {
    assert(typeof e.arguments_json === "string");
    const args = strictJSON(e.arguments_json, 131072);
    assert.deepEqual(args, e.arguments);
    historyToolArgs(String(e.name), args, scene, program);
  }
  assert(
    Array.isArray(answer.limitations) &&
      answer.limitations.length > 0 &&
      answer.limitations.every((v) => typeof v === "string" && v.length > 0),
  );
  assert(
    Array.isArray(answer.evidence_ids) &&
      answer.evidence_ids.length === 1 &&
      typeof answer.evidence_ids[0] === "string",
  );
  const id = answer.evidence_ids[0],
    requests = events.filter(
      (e) => e.kind === "tool_request" && e.evidence_id === id,
    ),
    responses = events.filter(
      (e) => e.kind === "tool_response" && e.evidence_id === id,
    );
  assert(requests.length === 1 && responses.length === 1);
  const request = requests[0],
    args = request.arguments;
  object(args);
  exact(args, ["relationship", "request_json"]);
  assert(
    args.relationship === program.relationship &&
      typeof args.request_json === "string",
  );
  assert.deepEqual(
    historyRequest(strictJSON(args.request_json, 16384)),
    historySceneRequest(scene),
  );
  const executed = events.filter(
    (e) => e.kind === "tool_request" && e.name === HISTORY_TOOLS[0],
  );
  if (scene.compileOnly) {
    exact(answer, ["status", "reason", "evidence_ids", "limitations"]);
    assert(
      answer.status === "not_executed" &&
        typeof answer.reason === "string" &&
        answer.reason.length > 0 &&
        request.name === HISTORY_TOOLS[1] &&
        executed.length === 0,
    );
    const plan = decoded(
      HISTORY_TOOLS[1],
      responses[0].result as Awaited<ReturnType<HistorySession["call"]>>,
    );
    sameHistoryBinding(plan, program);
    assert.deepEqual(plan.request, historySceneRequest(scene));
    return { status: "not_executed", computed: false };
  }
  exact(answer, ["status", "result", "evidence_ids", "limitations"]);
  assert(
    answer.status === "answered" &&
      request.name === HISTORY_TOOLS[0] &&
      executed.length === 1,
  );
  const result = historyResult(
    JSON.stringify([{ result_json: JSON.stringify(answer.result) }]),
  );
  assert.deepEqual(
    result,
    decoded(
      HISTORY_TOOLS[0],
      responses[0].result as Awaited<ReturnType<HistorySession["call"]>>,
    ),
  );
  sameHistoryBinding(result, program);
  assert.deepEqual(result.request, historySceneRequest(scene));
  assert.deepEqual(
    result.links,
    historyOracle(historySceneRequest(scene), scene.dataset === "empty"),
  );
  return {
    status: "answered",
    link_count: result.link_count,
    unit: "Observation/target link",
    independent_rows_verified: true,
  };
}
export function scriptedHistory() {
  const rounds = new Map<string, number>();
  return async (
    scene: HistoryScene,
    body: JsonObject,
    signal: AbortSignal,
  ): Promise<Completion> => {
    const n = (rounds.get(scene.id) ?? 0) + 1;
    rounds.set(scene.id, n);
    let output: JsonObject[];
    if (n === 1 || (n === 2 && !scene.compileOnly)) {
      output = [
        {
          type: "function_call",
          id: `item-${scene.id}-${n}`,
          call_id: `call-${scene.id}-${n}`,
          name: n === 1 ? HISTORY_TOOLS[1] : HISTORY_TOOLS[0],
          arguments: JSON.stringify({
            relationship: "team_at_observation",
            request_json: JSON.stringify(historySceneRequest(scene)),
          }),
          status: "completed",
        },
      ];
    } else {
      const input = body.input as { output: string }[];
      assert(input.length === 1);
      const tool = JSON.parse(input[0].output),
        limitations = [
          "Scripted integration from actual MCP output; no model autonomy, data completeness or business truth.",
        ];
      const answer = scene.compileOnly
        ? {
            status: "not_executed",
            reason:
              "Only the logical plan was inspected; no links or link counts were computed.",
            evidence_ids: [tool.evidence_id],
            limitations,
          }
        : {
            status: "answered",
            result: historyResult(historyContent(tool.mcp_response)),
            evidence_ids: [tool.evidence_id],
            limitations,
          };
      output = [
        {
          type: "message",
          id: `message-${scene.id}`,
          role: "assistant",
          status: "completed",
          content: [{ type: "output_text", text: JSON.stringify(answer) }],
        },
      ];
    }
    const parser = responsesProvider(
      "https://simulated.invalid/v1/",
      "synthetic-unused",
      async () =>
        new Response(
          JSON.stringify({
            id: `response-${scene.id}-${n}`,
            model: "simulated-history",
            status: "completed",
            usage: { input_tokens: 0, output_tokens: 0, total_tokens: 0 },
            output,
          }),
          { status: 200 },
        ),
    );
    return parser(body, signal);
  };
}
async function executeScene(
  session: HistorySession,
  scene: HistoryScene,
  record: (e: JsonObject) => void,
  complete: (b: JsonObject, s: AbortSignal) => Promise<Completion>,
  save: (n: string, v: unknown) => void,
) {
  const discovery = await session.discover(),
    context = {
      system: HISTORY_SYSTEM,
      question: historyQuestion(scene),
      tools: discovery.tools,
      resources: discovery.rows,
    };
  fixtureBinding(discovery.program, scene);
  save(scene.id + "/context.json", context);
  save(scene.id + "/setup.json", {
    binding: discovery.binding,
    dataset: scene.dataset,
  });
  const events: JsonObject[] = [];
  // Session events are recorded externally; final audit receives the full chronological list from caller.
  const final = await agentLoop({
    model: "simulated-history",
    ...context,
    limits: HISTORY_AGENT_LIMITS,
    complete,
    record: (e) => {
      if (e.kind === "tool_request") {
        assert(typeof e.arguments_json === "string");
        const args = strictJSON(e.arguments_json, 131072);
        assert.deepEqual(args, e.arguments);
        historyToolArgs(String(e.name), args, scene, discovery.program);
      }
      events.push(e);
      record(e);
    },
    call: async (name, args) => {
      historyToolArgs(name, args, scene, discovery.program);
      const reply = await session.call(name, args);
      sameHistoryBinding(decoded(name, reply), discovery.program);
      return reply;
    },
  });
  const evaluation = auditHistoryAnswer(
    final.content,
    events,
    scene,
    discovery.program,
  );
  record({ kind: "evaluation", value: evaluation });
  record({
    kind: "scene_end",
    status: "complete",
    calls: final.calls,
    rounds: final.rounds,
  });
  const review = { mode: "simulated", scene: scene.id, final, evaluation };
  save(scene.id + "/review.json", review);
  return {
    scene: scene.id,
    status: "complete",
    calls: final.calls,
    rounds: final.rounds,
    evaluation,
  };
}
export async function rehearseHistory(
  cli: string,
  artifact: string,
  out: string,
  complete = scriptedHistory(),
) {
  const archive = PrivateArchive.create(out),
    scenes: JsonObject[] = [];
  let current = "setup",
    requests = 0,
    status = "failed";
  archive.save("run.json", {
    schema: "ossie-history-agent-run-v1",
    mode: "simulated",
    model: "simulated-history",
    provider_config_read: false,
    real_model_requests: 0,
    budget: HISTORY_AGENT_LIMITS,
    source_hashes: Object.fromEntries(
      HISTORY_AGENT_SOURCES.map((n) => [n, digest(join(ROOT, n))]),
    ),
    runtime: historyRuntime(cli, artifact),
  });
  try {
    for (const scene of HISTORY_SCENES) {
      current = scene.id;
      archive.startEvents(scene.id);
      let sequence = 0;
      const record = (e: JsonObject) => {
        if (e.kind === "provider_request") requests++;
        archive.record(scene.id, {
          ...e,
          scene: scene.id,
          mode: "simulated",
          sequence: ++sequence,
        });
      };
      record({ kind: "scene_start", limits: HISTORY_AGENT_LIMITS });
      scenes.push(
        await withHistorySession(
          cli,
          artifact,
          scene.dataset,
          (s) =>
            executeScene(
              s,
              scene,
              record,
              (b, signal) => complete(scene, b, signal),
              (n, v) => archive.save(n, v),
            ),
          record,
        ),
      );
    }
    status = "complete";
  } catch (error) {
    archive.save("failure.json", {
      scene: current,
      category: error instanceof Error ? error.name : "Error",
      details:
        "Inspect immutable simulated events; no provider configuration read.",
    });
  }
  archive.save("summary.json", {
    schema: "ossie-history-agent-summary-v1",
    mode: "simulated",
    status,
    real_model_requests: 0,
    simulated_completion_requests: requests,
    scenes,
  });
  return {
    status,
    source_seal_sha256: archive.seal(),
    simulated_completion_requests: requests,
    scenes,
  };
}
const normalized = (v: unknown) => JSON.parse(JSON.stringify(v));
export async function replayHistory(
  path: string,
  expectedArtifactSha?: string,
) {
  const verified = verifyArchive(path),
    read = (n: string) =>
      JSON.parse(readFileSync(join(verified.path, n), "utf8")),
    run = read("run.json"),
    summary = read("summary.json");
  assert(
    run.schema === "ossie-history-agent-run-v1" &&
      run.mode === "simulated" &&
      run.model === "simulated-history" &&
      run.provider_config_read === false &&
      run.real_model_requests === 0,
  );
  assert.deepEqual(run.budget, HISTORY_AGENT_LIMITS);
  object(run.runtime);
  exact(run.runtime, [
    "version",
    "source_id",
    "extension_version",
    "install_path",
    "cli_sha256",
    "mcp_sha256",
    "extension_sha256",
    "server_sha256",
  ]);
  assert(
    run.runtime.version === "v1.5.6" && run.runtime.source_id === "069cc9f9b5",
  );
  assert(
    typeof run.runtime.extension_version === "string" &&
      run.runtime.extension_version.length > 0,
  );
  assert(
    typeof run.runtime.install_path === "string" &&
      run.runtime.install_path.length > 0,
  );
  // Archived paths are metadata only and never opened. Caller may bind a known local artifact digest.
  for (const k of [
    "cli_sha256",
    "mcp_sha256",
    "extension_sha256",
    "server_sha256",
  ])
    assert(
      typeof run.runtime[k] === "string" &&
        /^[a-f0-9]{64}$/.test(run.runtime[k]),
    );
  assert.equal(
    run.runtime.server_sha256,
    digest(join(ROOT, "examples/history_server.sql")),
  );
  if (expectedArtifactSha !== undefined)
    assert.equal(run.runtime.extension_sha256, expectedArtifactSha);
  assert.deepEqual(
    Object.keys(run.source_hashes).sort(),
    [...HISTORY_AGENT_SOURCES].sort(),
  );
  for (const name of HISTORY_AGENT_SOURCES)
    assert.equal(
      run.source_hashes[name],
      digest(join(ROOT, name)),
      "current source/fixture differs",
    );
  assert(
    summary.schema === "ossie-history-agent-summary-v1" &&
      summary.mode === "simulated" &&
      summary.status === "complete" &&
      summary.real_model_requests === 0,
  );
  assert.deepEqual(
    summary.scenes.map((s: JsonObject) => s.scene),
    HISTORY_SCENES.map((s) => s.id),
  );
  assert.deepEqual(
    Object.keys(verified.files).sort(),
    [
      "run.json",
      "summary.json",
      ...HISTORY_SCENES.flatMap((s) =>
        ["events.jsonl", "context.json", "setup.json", "review.json"].map(
          (n) => s.id + "/" + n,
        ),
      ),
    ].sort(),
  );
  let rounds = 0;
  const scenes: JsonObject[] = [];
  for (const scene of HISTORY_SCENES) {
    const events: JsonObject[] = readFileSync(
      join(verified.path, scene.id, "events.jsonl"),
      "utf8",
    )
      .trim()
      .split("\n")
      .map((s) => JSON.parse(s));
    events.forEach((e, i) => {
      assert(
        e.scene === scene.id &&
          e.mode === "simulated" &&
          e.sequence === i + 1 &&
          e.capture_redacted === undefined,
      );
    });
    let cursor = 0;
    const peek = (kind: string) => {
      const e = events[cursor];
      assert(e && e.kind === kind, `expected ${kind} at ${cursor}`);
      return e;
    };
    const consume = (e: JsonObject) => {
      const {
        scene: _,
        mode: __,
        sequence: ___,
        ...bare
      } = peek(String(e.kind));
      assert.deepEqual(bare, normalized(e), "ordered history event mismatch");
      cursor++;
    };
    consume({ kind: "scene_start", limits: HISTORY_AGENT_LIMITS });
    const fake = {
      request: async (id: number) => {
        const reply = peek("rpc_response").reply as JsonObject;
        assert(reply.id === id && reply.jsonrpc === "2.0");
        return reply;
      },
      send: () => {},
    } as unknown as RpcProcess;
    const entry = await executeScene(
      new HistorySession(fake, consume),
      scene,
      consume,
      async () =>
        completionFromProjection(
          peek("provider_response").response as unknown as Completion,
        ),
      (n, v) => assert.deepEqual(read(n), normalized(v)),
    );
    assert(cursor === events.length, "extra trailing events");
    assert.deepEqual(
      summary.scenes.find((s: JsonObject) => s.scene === scene.id),
      entry,
    );
    rounds += Number(entry.rounds);
    scenes.push(entry);
  }
  assert(summary.simulated_completion_requests === rounds);
  assert.equal(verifyArchive(path).seal_sha256, verified.seal_sha256);
  return {
    passed: true,
    mode: "simulated",
    source_seal_sha256: verified.seal_sha256,
    real_model_requests: 0,
    simulated_completion_requests: rounds,
    artifact_identity_checked: expectedArtifactSha !== undefined,
    scenes,
    limitations:
      "Exact ordered local byte correspondence and independent synthetic fixture result audit; no provider authenticity or model autonomy.",
  };
}

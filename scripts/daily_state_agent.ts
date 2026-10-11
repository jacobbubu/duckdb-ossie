/** Scripted Responses + actual state MCP, sealed evidence and strict offline sequential replay. */
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
  StateSession,
  withStateSession,
  stateRuntime,
  stateContent,
  STATE_TOOLS,
} from "./daily_state_mcp.ts";
import type { RpcProcess } from "./mcp_check.ts";
import {
  stateResult,
  statePlan,
  stateRequest,
  sameStateBinding,
  exact,
  object,
  type StateRequest,
} from "./daily_state_result.ts";
import { factJSON as strictJSON } from "./fact_result.ts";
import { stateOracle, STATE_FIXTURE } from "./daily_state_oracle.ts";
export const STATE_AGENT_LIMITS = {
  rounds: 3,
  calls: 2,
  milliseconds: 120000,
  outputTokens: 4096,
  inputBytes: 131072,
};
export const STATE_SYSTEM =
  'Use only discovered state tools/resources. Entire scope is inside String request_json. Discover qualification concept/current frozen rule and full captured User domain first. Any TRUE wins; nonempty all FALSE gives FALSE; otherwise UNKNOWN; no carry. Dates/products filter observations, never the User domain. Compile does not scan or calculate states, comparisons or totals. Execute the exact requested scope to answer; preserve full result, ordered rows, counts and User/day unit; cite actual execution evidence. Return exactly {status:"answered",result:<complete native state result>,evidence_ids:[actual tool ID],limitations:[nonempty strings]}. Explain-only returns exactly {status:"not_executed",reason:<nonempty String>,evidence_ids:[actual compile ID],limitations:[nonempty strings]}, with no computed data. No import/reload/arbitrary SQL, registered-user inference or historical rule/validity inference. Do not claim model autonomy, reality or data completeness from this integration.';
export const STATE_SCENES = [
  {
    id: "all",
    threshold: 10,
    dataset: "extended",
    product: null,
    end: "2026-02-01",
    compileOnly: false,
  },
  {
    id: "product-X",
    threshold: 10,
    dataset: "extended",
    product: "X",
    end: "2026-02-01",
    compileOnly: false,
  },
  {
    id: "missing-day",
    threshold: 10,
    dataset: "extended",
    product: null,
    end: "2026-02-02",
    compileOnly: false,
  },
  {
    id: "changed-rule",
    threshold: 20,
    dataset: "extended",
    product: null,
    end: "2026-02-01",
    compileOnly: false,
  },
  {
    id: "empty",
    threshold: 10,
    dataset: "empty",
    product: null,
    end: "2026-02-01",
    compileOnly: false,
  },
  {
    id: "compile-only",
    threshold: 10,
    dataset: "extended",
    product: null,
    end: "2026-02-01",
    compileOnly: true,
  },
] as const;
export type StateScene = (typeof STATE_SCENES)[number];
export const stateSceneRequest = (s: StateScene): StateRequest => ({
  start_date: "2026-01-31",
  end_date: s.end,
  products:
    s.product === null
      ? { mode: "all" }
      : { mode: "subset", values: [s.product] },
  user_domain: "captured_users",
  transitions: "within_request_days",
});
export const stateQuestion = (s: StateScene) =>
  `${s.compileOnly ? "只解释，不执行，不输出计算后的状态/比较/计数" : "执行并返回全部每日三值状态、相邻日比较、变化标记及统计"}。日期2026-01-31至${s.end}含首尾，${s.product === null ? "全部产品" : "产品" + s.product}。使用整个冻结观察User域，比较只含请求内相邻自然日（含未变化），不延续前日；给出实际规则和证据。`;
export const STATE_AGENT_SOURCES = [
  STATE_FIXTURE,
  "examples/daily_state_server.sql",
  ...[
    "core.json",
    "ontology.json",
    "ontology-20.json",
    "mapping.json",
    "bundle-10.json",
    "bundle-20.json",
  ].map((n) => "test/fixtures/population-observation/" + n),
  ...[
    "daily_state_agent",
    "daily_state_mcp",
    "daily_state_result",
    "daily_state_oracle",
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
function decoded(
  name: string,
  reply: Awaited<ReturnType<StateSession["call"]>>,
) {
  assert(STATE_TOOLS.includes(name));
  return name === STATE_TOOLS[0]
    ? stateResult(stateContent(reply))
    : statePlan(stateContent(reply));
}
export function auditStateAnswer(
  content: string,
  events: JsonObject[],
  scene: StateScene,
  program: Record<string, unknown>,
) {
  const answer = strictJSON(content);
  object(answer);
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
  exact(args, ["concept", "request_json"]);
  assert(
    args.concept === program.qualification_concept &&
      typeof args.request_json === "string",
  );
  assert.deepEqual(
    stateRequest(strictJSON(args.request_json, 16384)),
    stateSceneRequest(scene),
  );
  const executed = events.filter(
    (e) => e.kind === "tool_request" && e.name === STATE_TOOLS[0],
  );
  if (scene.compileOnly) {
    exact(answer, ["status", "reason", "evidence_ids", "limitations"]);
    assert(
      answer.status === "not_executed" &&
        typeof answer.reason === "string" &&
        answer.reason.length > 0 &&
        request.name === STATE_TOOLS[1] &&
        executed.length === 0,
    );
    const plan = decoded(
      STATE_TOOLS[1],
      responses[0].result as Awaited<ReturnType<StateSession["call"]>>,
    );
    sameStateBinding(plan, program);
    assert.deepEqual(plan.request, stateSceneRequest(scene));
    return { status: "not_executed", computed: false };
  }
  exact(answer, ["status", "result", "evidence_ids", "limitations"]);
  assert(
    answer.status === "answered" &&
      request.name === STATE_TOOLS[0] &&
      executed.length === 1,
  );
  const result = stateResult(
    JSON.stringify([{ result_json: JSON.stringify(answer.result) }]),
  );
  assert.deepEqual(
    result,
    decoded(
      STATE_TOOLS[0],
      responses[0].result as Awaited<ReturnType<StateSession["call"]>>,
    ),
  );
  sameStateBinding(result, program);
  assert.equal(
    result.declared_rule,
    `DailyObservation.classification_value >= ${scene.threshold}`,
  );
  assert.deepEqual(result.request, stateSceneRequest(scene));
  const oracle = stateOracle(
    stateSceneRequest(scene),
    scene.threshold,
    scene.dataset === "empty",
  );
  assert.deepEqual(result.states, oracle.states);
  assert.deepEqual(result.comparisons, oracle.comparisons);
  return {
    status: "answered",
    state_count: result.state_count,
    comparison_count: result.comparison_count,
    unit: "User/day",
    independent_rows_verified: true,
  };
}
export function scriptedState() {
  const rounds = new Map<string, number>();
  return async (
    scene: StateScene,
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
          name: n === 1 ? STATE_TOOLS[1] : STATE_TOOLS[0],
          arguments: JSON.stringify({
            concept: "QualifiedObservation",
            request_json: JSON.stringify(stateSceneRequest(scene)),
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
              "Only the logical plan was inspected; no states, comparisons or totals were computed.",
            evidence_ids: [tool.evidence_id],
            limitations,
          }
        : {
            status: "answered",
            result: stateResult(stateContent(tool.mcp_response)),
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
            model: "simulated-state",
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
  session: StateSession,
  scene: StateScene,
  record: (e: JsonObject) => void,
  complete: (b: JsonObject, s: AbortSignal) => Promise<Completion>,
  save: (n: string, v: unknown) => void,
) {
  const discovery = await session.discover(),
    context = {
      system: STATE_SYSTEM,
      question: stateQuestion(scene),
      tools: discovery.tools,
      resources: discovery.rows,
    };
  save(scene.id + "/context.json", context);
  save(scene.id + "/setup.json", {
    binding: discovery.binding,
    threshold: scene.threshold,
    dataset: scene.dataset,
  });
  const events: JsonObject[] = [];
  // Session events are recorded externally; final audit receives the full chronological list from caller.
  const final = await agentLoop({
    model: "simulated-state",
    ...context,
    limits: STATE_AGENT_LIMITS,
    complete,
    record: (e) => {
      events.push(e);
      record(e);
    },
    call: async (name, args) => {
      const reply = await session.call(name, args);
      sameStateBinding(decoded(name, reply), discovery.program);
      return reply;
    },
  });
  const evaluation = auditStateAnswer(
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
export async function rehearseState(
  cli: string,
  artifact: string,
  out: string,
  complete = scriptedState(),
) {
  const archive = PrivateArchive.create(out),
    scenes: JsonObject[] = [];
  let current = "setup",
    requests = 0,
    status = "failed";
  archive.save("run.json", {
    schema: "ossie-daily-state-agent-run-v1",
    mode: "simulated",
    model: "simulated-state",
    provider_config_read: false,
    real_model_requests: 0,
    budget: STATE_AGENT_LIMITS,
    source_hashes: Object.fromEntries(
      STATE_AGENT_SOURCES.map((n) => [n, digest(join(ROOT, n))]),
    ),
    runtime: stateRuntime(cli, artifact),
  });
  try {
    for (const scene of STATE_SCENES) {
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
      record({ kind: "scene_start", limits: STATE_AGENT_LIMITS });
      scenes.push(
        await withStateSession(
          cli,
          artifact,
          scene.threshold,
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
    schema: "ossie-daily-state-agent-summary-v1",
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
export async function replayState(path: string) {
  const verified = verifyArchive(path),
    read = (n: string) =>
      JSON.parse(readFileSync(join(verified.path, n), "utf8")),
    run = read("run.json"),
    summary = read("summary.json");
  assert(
    run.schema === "ossie-daily-state-agent-run-v1" &&
      run.mode === "simulated" &&
      run.model === "simulated-state" &&
      run.provider_config_read === false &&
      run.real_model_requests === 0,
  );
  assert.deepEqual(run.budget, STATE_AGENT_LIMITS);
  assert.deepEqual(
    Object.keys(run.source_hashes).sort(),
    [...STATE_AGENT_SOURCES].sort(),
  );
  for (const name of STATE_AGENT_SOURCES)
    assert.equal(
      run.source_hashes[name],
      digest(join(ROOT, name)),
      "current source/fixture differs",
    );
  assert(
    summary.schema === "ossie-daily-state-agent-summary-v1" &&
      summary.mode === "simulated" &&
      summary.status === "complete" &&
      summary.real_model_requests === 0,
  );
  assert.deepEqual(
    summary.scenes.map((s: JsonObject) => s.scene),
    STATE_SCENES.map((s) => s.id),
  );
  assert.deepEqual(
    Object.keys(verified.files).sort(),
    [
      "run.json",
      "summary.json",
      ...STATE_SCENES.flatMap((s) =>
        ["events.jsonl", "context.json", "setup.json", "review.json"].map(
          (n) => s.id + "/" + n,
        ),
      ),
    ].sort(),
  );
  let rounds = 0;
  const scenes: JsonObject[] = [];
  for (const scene of STATE_SCENES) {
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
      assert.deepEqual(bare, normalized(e), "ordered state event mismatch");
      cursor++;
    };
    consume({ kind: "scene_start", limits: STATE_AGENT_LIMITS });
    const fake = {
      request: async (id: number) => {
        const reply = peek("rpc_response").reply as JsonObject;
        assert(reply.id === id && reply.jsonrpc === "2.0");
        return reply;
      },
      send: () => {},
    } as unknown as RpcProcess;
    const entry = await executeScene(
      new StateSession(fake, consume),
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
    scenes,
    limitations:
      "Exact ordered local byte correspondence and independent synthetic fixture result audit; no provider authenticity or model autonomy.",
  };
}

/** Fixture oracle and per-claim execution audit; never supplied to the model. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT, digest } from "./artifact_helpers.ts";
import {
  populationResult,
  resultRecords,
  type PopulationResult,
} from "./population_result.ts";
export const OBSERVATION_PROFILE = "daily-observation-population-v1";
export const OBSERVATION_CSV = join(
  ROOT,
  "docs/research/ossie-req-001/multi-period-research/observations.csv",
);
export const METRIC_EXPRESSIONS = {
  observation_count: "COUNT(observations.user_id)",
  user_count: "COUNT(DISTINCT observations.user_id)",
  amount_value_count: "COUNT(observations.amount_usd)",
  amount_sum: "SUM(observations.amount_usd)",
  mean_observation_amount: "AVG(observations.amount_usd)",
} as const;
export type Metric = keyof typeof METRIC_EXPRESSIONS;
export const METRICS = Object.keys(METRIC_EXPRESSIONS) as Metric[];
export const BINDING_KEYS = [
  "snapshot_id",
  "content_sha256",
  "core_sha256",
  "ontology_sha256",
  "mapping_sha256",
  "manifest_sha256",
] as const;
export type Binding = Record<(typeof BINDING_KEYS)[number], string>;
export function binding(value: Record<string, unknown>): Binding {
  return Object.fromEntries(
    BINDING_KEYS.map((k) => {
      assert(
        typeof value[k] === "string" && value[k].length > 0,
        `missing binding ${k}`,
      );
      return [k, value[k]];
    }),
  ) as Binding;
}
export interface Observation {
  user: string;
  date: string;
  product: string;
  cents: bigint | null;
  classification: number;
}
export function observationInput(): Observation[] {
  assert.equal(
    digest(OBSERVATION_CSV),
    "bfd38a0130028fe5829aa45e40dce8ac12e228cc2cc61e2f08f293493764f441",
    "reviewed CSV changed",
  );
  const lines = readFileSync(OBSERVATION_CSV, "utf8").trim().split(/\r?\n/);
  assert.equal(
    lines.shift(),
    "observation_id,user_id,ds,product,amount_usd,classification_value",
  );
  const rows = lines.map((line) => {
    const cells = line.split(",");
    assert.equal(cells.length, 6);
    const [, user, date, product, amount, classification] = cells;
    assert(user && product && /^\d{4}-\d{2}-\d{2}$/.test(date));
    assert(amount === "" || /^\d+\.\d{2}$/.test(amount));
    assert(/^\d+$/.test(classification));
    return {
      user,
      date,
      product,
      cents: amount === "" ? null : BigInt(amount.replace(".", "")),
      classification: Number(classification),
    };
  });
  assert.equal(new Set(rows.map(tuple)).size, rows.length);
  return rows;
}
export const tuple = (r: Observation) =>
  JSON.stringify([r.user, r.date, r.product]);
function money(cents: bigint) {
  return `${cents / 100n}.${String(cents % 100n).padStart(2, "0")}`;
}
export function aggregate(rows: Observation[]): Record<Metric, string | null> {
  const values = rows.filter((r) => r.cents !== null);
  const sum = values.reduce((a, r) => a + r.cents!, 0n);
  return {
    observation_count: String(rows.length),
    user_count: String(new Set(rows.map((r) => r.user)).size),
    amount_value_count: String(values.length),
    amount_sum: values.length ? money(sum) : null,
    mean_observation_amount: values.length
      ? String(Number(sum) / 100 / values.length)
      : null,
  };
}
export function metricEqual(
  metric: Metric,
  actual: unknown,
  expected: string | null,
) {
  if (metric === "mean_observation_amount" && expected !== null) {
    assert(
      typeof actual === "string" &&
        Number.isFinite(Number(actual)) &&
        Math.abs(Number(actual) - Number(expected)) <= 1e-10,
      `wrong ${metric}`,
    );
  } else assert.equal(actual, expected, `wrong ${metric}`);
}
export const SCENES = [
  {
    id: "baseline",
    threshold: 10,
    question:
      "按当前正式定义，2026年1月31日至2月1日（含首尾、所有产品）的合格观察有多少条、涉及多少不同用户？金额非空有多少条、总额和每条非空观察平均金额是多少？说明单位和分母并引用依据。",
  },
  {
    id: "changed-rule",
    threshold: 20,
    question:
      "按当前正式定义，2026年1月31日至2月1日（含首尾、所有产品）的合格观察有多少条、涉及多少不同用户？金额非空有多少条、总额和每条非空观察平均金额是多少？说明单位和分母并引用依据。",
  },
  {
    id: "interval-users",
    threshold: 10,
    question:
      "按当前正式定义，分别给出2026年1月31日和2月1日每一天至少有一条合格观察涉及的不同用户数，以及整个区间至少有一条合格观察涉及的不同用户数（含首尾、所有产品）；解释每日人数相加是否代表区间人数，并引用依据。",
  },
  {
    id: "unsupported-every-day",
    threshold: 10,
    question:
      "2026年1月31日至2月1日（含首尾、所有产品）这个区间内，每一天至少有一条合格观察的不同用户有多少（各天产品可以不同）？请按每天都合格的口径回答并给出依据。",
  },
] as const;
export const OBSERVATION_SYSTEM =
  "你是有界数据助手。依据发现资源和实际执行结果回答。只回答当前已注册的指标和口径，不在客户端重新计算能力外指标替代。完整明细可用于推导已注册指标。编译不是执行。自行选择工具和参数，不擅自改变业务口径；能力不足时具体说明缺口，不给替代口径数值。最终只输出一个JSON对象，包含全部指定字段，不加代码围栏或额外键：status（answered或unsupported）、scope、definition、unit、observation_count、user_count、amount_value_count、amount_sum、mean_observation_amount、daily_user_counts、evidence_ids、limitations。scope和definition为非空字符串；definition说明实际正式规则、统计对象、人数/条数单位和金额分母。unit仅表示金额单位，从正式元数据读取；没有请求金额或目标不支持时为null。数字为字符串，未请求或不支持的项为null；daily_user_counts为日期到人数的对象或null；evidence_ids和limitations为字符串数组。数值字段保持执行精度，近似浮点值不称精确十进制，舍入展示仅放解释中并注明，不替换数值字段。evidence_ids必须包含ossie://population/concepts；每个数值还须引用实际成功执行返回的tool-N。解释和限制写入definition或limitations。";
export interface ExecutionEvidence {
  scene: string;
  id: string;
  name: string;
  args: Record<string, string>;
  content: string;
}
export interface Claim {
  evidence_id: string;
  metric: Metric;
  value: string | null;
  day?: string;
}
export interface ClaimContext {
  scene: string;
  threshold: number;
  binding: Binding;
  input: Observation[];
}
// Finite conjunction language only. Other valid SQL remains pending review.
interface ScopeWitness {
  rows: Observation[];
  lower: string | null;
  upper: string | null;
  narrowed: boolean;
}
function nextDate(value: string) {
  const date = new Date(value + "T00:00:00Z");
  assert(
    Number.isFinite(date.getTime()) &&
      date.toISOString().slice(0, 10) === value,
    "invalid date literal",
  );
  date.setUTCDate(date.getUTCDate() + 1);
  return date.toISOString().slice(0, 10);
}
function scopeWitness(
  filters: string[],
  input: Observation[],
): ScopeWitness | undefined {
  let rows = input,
    lower: string | null = null,
    upper: string | null = null,
    narrowed = false;
  for (const predicate of filters)
    for (const term of predicate.split(/\s+AND\s+/i)) {
      const date =
        /^(?:observations\.ds|DailyObservation\.date)\s*(>=|<=|=|<|>)\s*DATE\s*'(\d{4}-\d{2}-\d{2})'$/i.exec(
          term.trim(),
        );
      const text =
        /^(observations\.(?:product|user_id)|DailyObservation\.product)\s*=\s*'([^']*)'$/i.exec(
          term.trim(),
        );
      if (date) {
        const [, op, value] = date;
        const next = nextDate(value);
        const lo = op === ">" ? next : [">=", "="].includes(op) ? value : null;
        const hi = ["<=", "="].includes(op) ? next : op === "<" ? value : null;
        if (lo) lower = lower === null || lo > String(lower) ? lo : lower;
        if (hi) upper = upper === null || hi < String(upper) ? hi : upper;
        rows = rows.filter(
          (r) => (lo === null || r.date >= lo) && (hi === null || r.date < hi),
        );
      } else if (text) {
        narrowed = true;
        rows = rows.filter(
          (r) =>
            (text[1].toLowerCase().endsWith("user_id") ? r.user : r.product) ===
            text[2],
        );
      } else return undefined;
    }
  return { rows, lower, upper, narrowed };
}
export function selectedScope(filters: string[], input: Observation[]) {
  return scopeWitness(filters, input)?.rows;
}
function list(args: Record<string, string>, key: string): string[] {
  assert(typeof args[key] === "string", `missing ${key}`);
  const values: unknown = JSON.parse(args[key]);
  assert(
    Array.isArray(values) && values.every((v) => typeof v === "string"),
    `invalid ${key}`,
  );
  return values;
}
export function auditClaim(
  claim: Claim,
  evidence: ExecutionEvidence[],
  context: ClaimContext,
) {
  assert(METRICS.includes(claim.metric), "unregistered claim metric");
  assert(
    claim.value === null || typeof claim.value === "string",
    "invalid claim value",
  );
  assert(
    claim.day === undefined || ["2026-01-31", "2026-02-01"].includes(claim.day),
    "invalid claim date",
  );
  const matches = evidence.filter(
    (e) => e.scene === context.scene && e.id === claim.evidence_id,
  );
  assert.equal(
    matches.length,
    1,
    "unique same-scene execution citation required",
  );
  const event = matches[0];
  assert(
    ["population_query", "population_objects"].includes(event.name),
    "executed tool required",
  );
  assert.equal(event.args.concept, "QualifiedObservation", "wrong concept");
  const decoded = populationResult(event.content);
  assert.equal(decoded.profile, OBSERVATION_PROFILE);
  assert.equal(decoded.concept, event.args.concept);
  assert.deepEqual(
    binding(decoded as unknown as Record<string, unknown>),
    context.binding,
    "stale execution binding",
  );
  const filters = list(event.args, "filters");
  const witness = scopeWitness(filters, context.input);
  const selected = witness?.rows;
  const expectedScope = context.input.filter(
    (r) =>
      r.date >= "2026-01-31" &&
      r.date <= "2026-02-01" &&
      (!claim.day || r.date === claim.day),
  );
  let dims: string[] = [];
  if (event.name === "population_query") {
    assert.equal(decoded.request_kind, "metrics");
    assert(
      list(event.args, "metrics").includes(claim.metric),
      "metric not requested",
    );
    dims = list(event.args, "dimensions");
    assert(
      dims.length === 0 ||
        (dims.length === 1 && dims[0] === "observations.ds" && claim.day),
      "wrong grouping scope",
    );
    const column = decoded.columns.find((c) => c.name === claim.metric);
    assert(column, "requested metric column missing");
    assert.equal(
      column.bound_type,
      claim.metric === "amount_sum"
        ? "DECIMAL(38,2)"
        : claim.metric === "mean_observation_amount"
          ? "DOUBLE"
          : "BIGINT",
      "wrong metric bound type",
    );
  } else {
    assert.equal(decoded.request_kind, "objects");
    assert.deepEqual(
      decoded.columns.slice(0, 5).map((c) => [c.name, c.bound_type]),
      [
        ["observations.user_id", "VARCHAR"],
        ["observations.ds", "DATE"],
        ["observations.product", "VARCHAR"],
        ["observations.amount_usd", "DECIMAL(18,2)"],
        ["observations.classification_value", "INTEGER"],
      ],
    );
  }
  const qualifying = expectedScope.filter(
    (r) => r.classification >= context.threshold,
  );
  const expected = aggregate(qualifying)[claim.metric];
  metricEqual(claim.metric, claim.value, expected);
  const actualScope =
    dims.length || (event.name === "population_objects" && claim.day)
      ? selected?.filter((r) => r.date === claim.day)
      : selected;
  if (actualScope)
    assert.deepEqual(
      actualScope.map(tuple).sort(),
      expectedScope.map(tuple).sort(),
      "wrong request scope; matching values do not prove correspondence",
    );
  if (witness)
    assert(
      !witness.narrowed,
      "all products and Users required; no additional narrowing",
    );
  const records = resultRecords(decoded);
  if (event.name === "population_query") {
    const rows = dims.length
      ? records.filter((r) => r["observations.ds"] === claim.day)
      : records;
    assert.equal(rows.length, 1, "one matching aggregate row required");
    metricEqual(claim.metric, rows[0][claim.metric], expected);
  } else {
    const actual = records
      .map((r) => {
        assert(
          typeof r["observations.user_id"] === "string" &&
            typeof r["observations.ds"] === "string" &&
            typeof r["observations.product"] === "string",
        );
        return JSON.stringify([
          r["observations.user_id"],
          r["observations.ds"],
          r["observations.product"],
          r["observations.amount_usd"],
          r["observations.classification_value"],
        ]);
      })
      .sort();
    const wanted = (selected ?? context.input)
      .filter((r) => r.classification >= context.threshold)
      .map((r) =>
        JSON.stringify([
          r.user,
          r.date,
          r.product,
          r.cents === null ? null : money(r.cents),
          String(r.classification),
        ]),
      )
      .sort();
    assert.deepEqual(
      actual,
      wanted,
      "complete typed objects required before derivation",
    );
  }
  const intervalBounds =
    witness?.lower === "2026-01-31" && witness.upper === "2026-02-02";
  const dayBounds =
    claim.day &&
    witness?.lower === claim.day &&
    witness.upper === nextDate(claim.day);
  const exactBounds = claim.day
    ? dayBounds ||
      ((dims.length > 0 || event.name === "population_objects") &&
        intervalBounds)
    : intervalBounds;
  if (!witness || !exactBounds)
    return {
      status: "pending semantic review",
      reason: !witness
        ? "filter form outside bounded witness language"
        : "captured membership matches but date bounds are not proven equivalent",
      scene: event.scene,
      evidence_id: event.id,
      args: event.args,
    };
  return {
    status: "pass",
    scene: event.scene,
    evidence_id: event.id,
    metric: claim.metric,
    day: claim.day ?? null,
    args: event.args,
    binding: context.binding,
    scope_proof:
      "normalized literal date bounds and no product/User narrowing; independently verified hash-pinned membership",
    semantic_review: "required",
  };
}
export function assertResultBinding(
  result: PopulationResult,
  expected: Binding,
) {
  assert.deepEqual(
    binding(result as unknown as Record<string, unknown>),
    expected,
  );
}
/** Consume only successful recorded tool replies, preserving the actual request association. */
export function executionEvidence(
  scene: string,
  events: Record<string, unknown>[],
): ExecutionEvidence[] {
  const evidence: ExecutionEvidence[] = [];
  const seen = new Set<string>();
  for (const event of events.filter((e) => e.kind === "tool_response")) {
    assert(
      typeof event.evidence_id === "string" && !seen.has(event.evidence_id),
      "duplicate response evidence id",
    );
    seen.add(event.evidence_id);
    const requests = events.filter(
      (e) => e.kind === "tool_request" && e.evidence_id === event.evidence_id,
    );
    assert.equal(requests.length, 1, "unique tool request required");
    const request = requests[0];
    assert(
      request.scene === scene && event.scene === scene,
      "cross-scene transcript",
    );
    const reply = event.result as {
      error?: unknown;
      result?: { isError?: boolean; content?: { text?: string }[] };
    };
    if (reply.error || reply.result?.isError) continue;
    if (
      !["population_query", "population_objects"].includes(String(request.name))
    )
      continue;
    const blocks = reply.result?.content;
    assert(
      blocks?.length === 1 && typeof blocks[0].text === "string",
      "single successful carrier required",
    );
    const args = request.arguments as Record<string, string>;
    assert(
      args && Object.values(args).every((v) => typeof v === "string"),
      "actual string arguments required",
    );
    evidence.push({
      scene,
      id: event.evidence_id,
      name: String(request.name),
      args,
      content: blocks[0].text,
    });
  }
  return evidence;
}
export function checkObservationAnswer(
  content: string,
  events: Record<string, unknown>[],
  context: ClaimContext,
) {
  const answer = JSON.parse(content) as Record<string, unknown>;
  assert(answer && typeof answer === "object" && !Array.isArray(answer));
  const keys = [
    "status",
    "scope",
    "definition",
    "unit",
    ...METRICS,
    "daily_user_counts",
    "evidence_ids",
    "limitations",
  ];
  assert.deepEqual(
    Object.keys(answer).sort(),
    keys.sort(),
    "complete observation answer fields required",
  );
  for (const key of ["scope", "definition"])
    assert(typeof answer[key] === "string" && answer[key].trim().length > 0);
  for (const key of ["evidence_ids", "limitations"])
    assert(
      Array.isArray(answer[key]) &&
        answer[key].every((v) => typeof v === "string"),
    );
  const references = answer.evidence_ids as string[];
  const allowedResources = [
    "concepts",
    "metrics",
    "fields",
    "capabilities",
  ].map((k) => "ossie://population/" + k);
  assert(
    references.includes(allowedResources[0]),
    "formal concept citation required",
  );
  const evidence = executionEvidence(context.scene, events);
  for (const reference of references)
    assert(
      allowedResources.includes(reference) ||
        evidence.some((e) => e.id === reference),
      "unknown or unsuccessful execution citation",
    );
  if (context.scene === "unsupported-every-day") {
    assert.equal(answer.status, "unsupported");
    for (const key of [...METRICS, "daily_user_counts", "unit"])
      assert.equal(answer[key], null, "unsupported target claims must be null");
    return {
      numerical_gate: "not applicable",
      semantic_review:
        "required: registered capability refusal; raw data may support client calculations",
      overall: "pending semantic review",
    };
  }
  assert.equal(answer.status, "answered");
  const interval = context.scene === "interval-users";
  assert(
    interval || ["baseline", "changed-rule"].includes(context.scene),
    "unknown acceptance scene",
  );
  assert.equal(
    context.threshold,
    context.scene === "changed-rule" ? 20 : 10,
    "scene rule mismatch",
  );
  const claims: Omit<Claim, "evidence_id">[] = [];
  if (interval) {
    for (const metric of METRICS.filter((m) => m !== "user_count"))
      assert.equal(answer[metric], null, "unrequested claims must be null");
    assert.equal(answer.unit, null);
    const daily = answer.daily_user_counts as Record<string, unknown>;
    assert(daily && typeof daily === "object" && !Array.isArray(daily));
    assert.deepEqual(Object.keys(daily).sort(), ["2026-01-31", "2026-02-01"]);
    for (const day of Object.keys(daily)) {
      assert(typeof daily[day] === "string");
      claims.push({ metric: "user_count", value: daily[day] as string, day });
    }
    assert(typeof answer.user_count === "string");
    claims.push({ metric: "user_count", value: answer.user_count });
  } else {
    assert.equal(answer.unit, "USD");
    assert.equal(answer.daily_user_counts, null);
    for (const metric of METRICS) {
      assert(typeof answer[metric] === "string");
      claims.push({ metric, value: answer[metric] as string });
    }
  }
  const audited = claims.map((claim) => {
    const expected = aggregate(
      context.input.filter(
        (r) =>
          r.classification >= context.threshold &&
          r.date >= "2026-01-31" &&
          r.date <= "2026-02-01" &&
          (!claim.day || r.date === claim.day),
      ),
    )[claim.metric];
    metricEqual(claim.metric, claim.value, expected);
    const pending: unknown[] = [];
    const errors: string[] = [];
    for (const candidate of evidence.filter((e) => references.includes(e.id))) {
      try {
        const result = auditClaim(
          { ...claim, evidence_id: candidate.id },
          evidence,
          context,
        );
        if (result.status === "pass") return result;
        pending.push(result);
      } catch (error) {
        errors.push(String(error));
      }
    }
    if (pending.length)
      return { status: "pending semantic review", claim, candidates: pending };
    throw new Error(
      `No cited execution supports ${claim.metric}/${claim.day ?? "interval"}: ${errors.join("; ")}`,
    );
  });
  return {
    numerical_gate: "pass",
    executed_support: audited.every((a) => a.status === "pass")
      ? "pass"
      : "pending semantic review",
    claims: audited,
    semantic_review:
      "required: scope/rule/unit/denominator/explanation and witness-only scope proof",
    overall: "pending semantic review",
  };
}

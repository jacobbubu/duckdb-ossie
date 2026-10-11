import { test, expect } from "bun:test";
import {
  aggregate,
  observationInput,
  auditClaim,
  checkObservationAnswer,
  executionEvidence,
  METRICS,
  OBSERVATION_PROFILE,
  type ExecutionEvidence,
  type ClaimContext,
  type Metric,
} from "../../scripts/observation_evidence.ts";
import { preparationArguments } from "../../scripts/agent_observation_acceptance.ts";
const input = observationInput();
const hashes = {
  snapshot_id: "snapshot",
  content_sha256: "content",
  core_sha256: "core",
  ontology_sha256: "ontology",
  mapping_sha256: "mapping",
  manifest_sha256: "manifest",
};
const range = [
  "observations.ds >= DATE '2026-01-31' AND observations.ds < DATE '2026-02-02'",
];
const context: ClaimContext = {
  scene: "baseline",
  threshold: 10,
  binding: hashes,
  input,
};
function carrier(
  names: string[],
  types: string[],
  rows: (string | null)[][],
  kind = "metrics",
) {
  return JSON.stringify([
    {
      result_json: JSON.stringify({
        schema_version: "ossie-population-result-v1",
        request_kind: kind,
        concept: "QualifiedObservation",
        executed: true,
        profile: OBSERVATION_PROFILE,
        ...hashes,
        ordering: "unspecified",
        paging: false,
        truncated: false,
        columns: names.map((name, i) => ({
          ordinal: i + 1,
          name,
          bound_type: types[i],
          encoding:
            types[i] === "BIGINT" || types[i] === "INTEGER"
              ? "integer-text"
              : types[i].startsWith("DECIMAL")
                ? "decimal-text"
                : types[i] === "DOUBLE"
                  ? "ieee754-text"
                  : types[i] === "DATE"
                    ? "date-text"
                    : "string",
        })),
        rows,
        row_count: rows.length,
      }),
    },
  ]);
}
function query(
  id = "q",
  scene = "baseline",
  metrics: Metric[] = METRICS,
): ExecutionEvidence {
  const expected = aggregate(input.filter((r) => r.classification >= 10));
  return {
    id,
    scene,
    name: "population_query",
    args: {
      concept: "QualifiedObservation",
      metrics: JSON.stringify(metrics),
      dimensions: "[]",
      filters: JSON.stringify(range),
    },
    content: carrier(
      metrics,
      metrics.map((m) =>
        m === "amount_sum"
          ? "DECIMAL(38,2)"
          : m === "mean_observation_amount"
            ? "DOUBLE"
            : "BIGINT",
      ),
      [metrics.map((m) => expected[m])],
    ),
  };
}
function objects(scene = "baseline"): ExecutionEvidence {
  return {
    id: "objects",
    scene,
    name: "population_objects",
    args: { concept: "QualifiedObservation", filters: JSON.stringify(range) },
    content: carrier(
      [
        "observations.user_id",
        "observations.ds",
        "observations.product",
        "observations.amount_usd",
        "observations.classification_value",
      ],
      ["VARCHAR", "DATE", "VARCHAR", "DECIMAL(18,2)", "INTEGER"],
      input
        .filter((r) => r.classification >= 10)
        .map((r) => [
          r.user,
          r.date,
          r.product,
          r.cents === null
            ? null
            : `${r.cents / 100n}.${String(r.cents % 100n).padStart(2, "0")}`,
          String(r.classification),
        ]),
      "objects",
    ),
  };
}
function mutate(e: ExecutionEvidence, fn: (result: any) => void) {
  const outer = JSON.parse(e.content);
  const result = JSON.parse(outer[0].result_json);
  fn(result);
  outer[0].result_json = JSON.stringify(result);
  return { ...e, content: JSON.stringify(outer) };
}
function events(evidence: ExecutionEvidence[]) {
  return evidence.flatMap((e) => [
    {
      kind: "tool_request",
      scene: e.scene,
      evidence_id: e.id,
      name: e.name,
      arguments: e.args,
    },
    {
      kind: "tool_response",
      scene: e.scene,
      evidence_id: e.id,
      result: { result: { content: [{ text: e.content }] } },
    },
  ]);
}
function answer(refs = ["q"]) {
  return {
    status: "answered",
    scope: "2026-01-31 through 2026-02-01, all products",
    definition: "current qualifying observations",
    unit: "USD",
    ...aggregate(input.filter((r) => r.classification >= 10)),
    daily_user_counts: null,
    evidence_ids: ["ossie://population/concepts", ...refs],
    limitations: ["synthetic data; observation mean"],
  };
}
const claim = { evidence_id: "q", metric: "user_count" as const, value: "3" };
test("answer protocol rejects fences, extra keys and empty required descriptions", () => {
  const content = JSON.stringify(answer());
  expect(
    checkObservationAnswer(content, events([query()]), context).overall,
  ).toBe("pending semantic review");
  for (const invalid of [
    "```json\n" + content + "\n```",
    JSON.stringify({ ...answer(), extra: "unregistered field" }),
    JSON.stringify({ ...answer(), scope: "" }),
    JSON.stringify({ ...answer(), definition: " " }),
  ])
    expect(() =>
      checkObservationAnswer(invalid, events([query()]), context),
    ).toThrow();
});
test("formal concept and actual execution citations are separately required", () => {
  for (const refs of [["q"], ["ossie://population/concepts"]])
    expect(() =>
      checkObservationAnswer(
        JSON.stringify({ ...answer(), evidence_ids: refs }),
        events([query()]),
        context,
      ),
    ).toThrow();
});
test("unit is monetary USD for amount answers and null for count-only answers", () => {
  expect(() =>
    checkObservationAnswer(
      JSON.stringify({ ...answer(), unit: "人" }),
      events([query()]),
      context,
    ),
  ).toThrow();
  const e = objects("interval-users");
  const counts = {
    ...answer([e.id]),
    ...Object.fromEntries(
      METRICS.map((m) => [m, m === "user_count" ? "3" : null]),
    ),
    unit: null,
    daily_user_counts: { "2026-01-31": "1", "2026-02-01": "3" },
    definition:
      "distinct Users with qualifying observations; people; daily counts overlap",
  };
  const ctx = { ...context, scene: e.scene };
  expect(
    checkObservationAnswer(JSON.stringify(counts), events([e]), ctx).overall,
  ).toBe("pending semantic review");
  expect(() =>
    checkObservationAnswer(
      JSON.stringify({ ...counts, unit: "人" }),
      events([e]),
      ctx,
    ),
  ).toThrow();
});
test("rounded display may explain but cannot replace the executed numeric field", () => {
  const expected = aggregate(input.filter((r) => r.classification >= 20));
  const e = mutate(query("q", "changed-rule"), (r) => {
    r.rows = [METRICS.map((m) => expected[m])];
  });
  const ctx = { ...context, scene: e.scene, threshold: 20 };
  const result = {
    ...answer(),
    ...expected,
    limitations: [
      "display rounded to 5.67 USD; numeric field keeps execution precision",
    ],
  };
  expect(
    checkObservationAnswer(JSON.stringify(result), events([e]), ctx)
      .numerical_gate,
  ).toBe("pass");
  expect(() =>
    checkObservationAnswer(
      JSON.stringify({ ...result, mean_observation_amount: "5.67" }),
      events([e]),
      ctx,
    ),
  ).toThrow("wrong mean_observation_amount");
});
test("oracle distinguishes observation, User and nonnull denominators", () => {
  expect(aggregate(input.filter((r) => r.classification >= 10))).toEqual({
    observation_count: "6",
    user_count: "3",
    amount_value_count: "5",
    amount_sum: "31.00",
    mean_observation_amount: "6.2",
  });
  expect(aggregate(input.filter((r) => r.classification >= 20))).toEqual({
    observation_count: "3",
    user_count: "1",
    amount_value_count: "3",
    amount_sum: "17.00",
    mean_observation_amount: String(17 / 3),
  });
  expect(aggregate(input.filter((r) => r.user === "C"))).toEqual({
    observation_count: "1",
    user_count: "1",
    amount_value_count: "0",
    amount_sum: null,
    mean_observation_amount: null,
  });
});
test("correct same-scope metrics may be supported by separate actual replies", () => {
  const evidence = METRICS.map((m) => query(m, "baseline", [m]));
  const evaluation = checkObservationAnswer(
    JSON.stringify(answer(METRICS)),
    events(evidence),
    context,
  );
  expect(evaluation.executed_support).toBe("pass");
  expect(evaluation.overall).toBe("pending semantic review");
});
test("equal User number in product X does not prove all-product scope", () => {
  const e = query();
  e.args.filters = "[\"DailyObservation.product = 'X'\"]";
  expect(() => auditClaim(claim, [e], context)).toThrow("wrong request scope");
});
test("wider date range with identical members and missing date bounds remain pending", () => {
  for (const filters of [
    [],
    [
      "observations.ds >= DATE '2026-01-01' AND observations.ds < DATE '2026-03-01'",
    ],
  ]) {
    const e = query();
    e.args.filters = JSON.stringify(filters);
    expect(auditClaim(claim, [e], context).status).toBe(
      "pending semantic review",
    );
  }
});
test("inclusive and exclusive equivalent date bounds pass", () => {
  const e = query();
  e.args.filters = JSON.stringify([
    "DailyObservation.date >= DATE '2026-01-31'",
    "DailyObservation.date <= DATE '2026-02-01'",
  ]);
  expect(auditClaim(claim, [e], context).status).toBe("pass");
});
test("unknown filter cannot hide wrong metric, grouping, final value or result value", () => {
  const base = query();
  base.args.filters = "[\"(observations.ds >= DATE '2026-01-31')\"]";
  expect(auditClaim(claim, [base], context).status).toBe(
    "pending semantic review",
  );
  const metric = structuredClone(base);
  metric.args.metrics = '["observation_count"]';
  const group = structuredClone(base);
  group.args.dimensions = '["observations.product"]';
  const wrongResult = mutate(base, (r) => (r.rows[0][1] = "4"));
  for (const e of [metric, group, wrongResult])
    expect(() => auditClaim(claim, [e], context)).toThrow();
  expect(() => auditClaim({ ...claim, value: "4" }, [base], context)).toThrow(
    "wrong user_count",
  );
});
test("wrong concept, cross-scene citation, duplicate id and stale hashes fail", () => {
  const wrong = query();
  wrong.args.concept = "DailyObservation";
  expect(() => auditClaim(claim, [wrong], context)).toThrow("wrong concept");
  expect(() =>
    auditClaim(claim, [query("q", "changed-rule")], context),
  ).toThrow();
  expect(() => auditClaim(claim, [query(), query()], context)).toThrow();
  expect(() =>
    auditClaim(
      claim,
      [mutate(query(), (r) => (r.ontology_sha256 = "stale"))],
      context,
    ),
  ).toThrow("stale execution binding");
});
test("raw captured counts, wrong average denominator and fake citations are refused", () => {
  for (const change of [
    { observation_count: "8" },
    { user_count: "4" },
    { mean_observation_amount: String(31 / 3) },
    { evidence_ids: ["ossie://population/concepts", "fake"] },
  ]) {
    expect(() =>
      checkObservationAnswer(
        JSON.stringify({ ...answer(), ...change }),
        events([query()]),
        context,
      ),
    ).toThrow();
  }
});
test("complete two-day objects can derive both daily counts and interval distinct", () => {
  const ctx = { ...context, scene: "interval-users" };
  const e = objects(ctx.scene);
  for (const [day, value] of [
    ["2026-01-31", "1"],
    ["2026-02-01", "3"],
  ])
    expect(
      auditClaim(
        { evidence_id: e.id, metric: "user_count", day, value },
        [e],
        ctx,
      ).status,
    ).toBe("pass");
  const result = {
    ...answer([e.id]),
    unit: null,
    observation_count: null,
    amount_value_count: null,
    amount_sum: null,
    mean_observation_amount: null,
    daily_user_counts: { "2026-02-01": "3", "2026-01-31": "1" },
  };
  expect(
    checkObservationAnswer(JSON.stringify(result), events([e]), ctx)
      .executed_support,
  ).toBe("pass");
  expect(() =>
    checkObservationAnswer(
      JSON.stringify({ ...result, user_count: "4" }),
      events([e]),
      ctx,
    ),
  ).toThrow();
});
test("incomplete, duplicate, wrong typed or null-to-zero objects refuse derivation", () => {
  const e = objects();
  for (const bad of [
    mutate(e, (r) => {
      r.rows.pop();
      r.row_count--;
    }),
    mutate(e, (r) => (r.rows[1] = r.rows[0])),
    mutate(e, (r) => {
      r.columns[3].bound_type = "VARCHAR";
      r.columns[3].encoding = "string";
    }),
    mutate(e, (r) => {
      const row = r.rows.find((v: any) => v[3] === null);
      row[3] = "0.00";
    }),
  ])
    expect(() =>
      auditClaim({ ...claim, evidence_id: e.id }, [bad], context),
    ).toThrow();
});
test("grouped rows bind date keys, not order or values from another row", () => {
  const e = query("daily", "interval-users", ["user_count"]);
  e.args.dimensions = '["observations.ds"]';
  e.content = carrier(
    ["observations.ds", "user_count"],
    ["DATE", "BIGINT"],
    [
      ["2026-02-01", "3"],
      ["2026-01-31", "1"],
    ],
  );
  const ctx = { ...context, scene: e.scene };
  expect(
    auditClaim(
      {
        evidence_id: e.id,
        metric: "user_count",
        day: "2026-01-31",
        value: "1",
      },
      [e],
      ctx,
    ).status,
  ).toBe("pass");
  expect(() =>
    auditClaim(
      {
        evidence_id: e.id,
        metric: "user_count",
        day: "2026-01-31",
        value: "1",
      },
      [mutate(e, (r) => (r.rows[1][1] = "3"))],
      ctx,
    ),
  ).toThrow("wrong user_count");
});
test("failed RPC or isError with a successful-looking carrier cannot become evidence", () => {
  for (const failure of [
    { error: { message: "failed" } },
    { result: { isError: true, content: [{ text: query().content }] } },
  ]) {
    const records = events([query()]);
    records[1].result = failure as any;
    expect(executionEvidence("baseline", records)).toHaveLength(0);
    expect(() =>
      checkObservationAnswer(JSON.stringify(answer()), records, context),
    ).toThrow();
  }
  const duplicate = events([query()]);
  expect(() =>
    executionEvidence("baseline", [...duplicate, duplicate[1]]),
  ).toThrow();
  const cross = events([query()]);
  cross[1].scene = "changed-rule";
  expect(() => executionEvidence("baseline", cross)).toThrow("cross-scene");
});
test("unsupported may follow successful probing but must not return target numbers", () => {
  const ctx = { ...context, scene: "unsupported-every-day" };
  const e = query("probe", ctx.scene);
  const result = {
    ...answer([e.id]),
    status: "unsupported",
    unit: null,
    ...Object.fromEntries(METRICS.map((m) => [m, null])),
    daily_user_counts: null,
  };
  expect(
    checkObservationAnswer(JSON.stringify(result), events([e]), ctx).overall,
  ).toBe("pending semantic review");
  expect(() =>
    checkObservationAnswer(
      JSON.stringify({ ...result, user_count: "3" }),
      events([e]),
      ctx,
    ),
  ).toThrow();
});
test("external claims require runtime metric/date/value validation", () => {
  for (const change of [
    { metric: "mean_user_total" },
    { day: "2026-01-30" },
    { value: 3 },
  ])
    expect(() =>
      auditClaim({ ...claim, ...change } as any, [query()], context),
    ).toThrow();
});
test("prepare driver has no live mode or model configuration arguments", () => {
  expect(() =>
    preparationArguments([
      "--run",
      "--cli",
      "x",
      "--extension",
      "y",
      "--out",
      "z",
    ]),
  ).toThrow("live mode is not implemented");
  expect(() =>
    preparationArguments([
      "--prepare",
      "--cli",
      "x",
      "--extension",
      "y",
      "--out",
      "z",
      "--model",
      "test",
    ]),
  ).toThrow();
  expect(
    preparationArguments([
      "--prepare",
      "--cli",
      "x",
      "--extension",
      "y",
      "--out",
      "z",
    ]).out.endsWith("/z"),
  ).toBe(true);
});

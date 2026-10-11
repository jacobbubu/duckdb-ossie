/** Every-day membership and byte refusal controls against a stock DuckDB artifact. */
import { readFileSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { join, resolve } from "node:path";
import {
  Database,
  ROOT,
  assert,
  digest,
  emitReport,
  temporary,
} from "./artifact_helpers.ts";
import { populationError } from "./population_error.ts";
const RESEARCH = join(
  ROOT,
  "docs/research/ossie-req-001/multi-period-research/every-day",
);
const FIXTURE = join(ROOT, "test/fixtures/population-observation");
const QUERY = "SELECT result_json FROM ossie_daily_users_json(?,?)";
interface Case {
  id: string;
  start: string;
  end: string;
  products: string[] | null;
  threshold: number;
  dataset: string;
  expected_ids: string[];
}
export async function verifyDailyUsers(artifact: string) {
  const db = await Database.open();
  const checks: string[] = [],
    refusals: unknown[] = [],
    results: unknown[] = [];
  const load = (threshold = 10) =>
    db.run("CALL ossie_bundle_load(?)", [
      join(FIXTURE, `bundle-${threshold}.json`),
    ]);
  const request = (
    start = "2026-01-31",
    end = "2026-02-01",
    values: string[] | null = null,
  ) => ({
    start_date: start,
    end_date: end,
    products: values === null ? { mode: "all" } : { mode: "subset", values },
  });
  const query = async (text: string, concept = "QualifiedObservation") =>
    String((await db.one(QUERY, [concept, text]))[0]);
  const compile = async (text: string, concept = "QualifiedObservation") =>
    String(
      (
        await db.one("SELECT plan_json FROM ossie_daily_users_compile(?,?)", [
          concept,
          text,
        ])
      )[0],
    );
  function checkPlan(
    plan: Record<string, unknown>,
    result?: Record<string, unknown>,
  ) {
    assert.equal(plan.schema_version, "ossie-daily-user-set-plan-v1");
    for (const flag of [
      "executed",
      "physical_plan",
      "final_output_binding",
      "sql_replayable",
      "members_computed",
    ])
      assert.equal(plan[flag], false, flag);
    assert.equal(plan.input_projection_binding, true);
    assert(!("members" in plan) && !("member_count" in plan));
    assert.equal(plan.plan_json_utf8_byte_limit, 1048576);
    const identities = plan.bound_identity as {
      role: string;
      bound_type: string;
    }[];
    assert.deepEqual(
      identities.map((v) => [v.role, v.bound_type]),
      [
        ["user", "VARCHAR"],
        ["date", "DATE"],
        ["product", "VARCHAR"],
      ],
    );
    const operators = plan.operators as {
      operator: string;
      required_count?: string;
      qualification?: string;
      byte_limit?: number;
    }[];
    assert.deepEqual(
      operators.map((v) => v.operator),
      [
        "qualified_observation_projection",
        "inclusive_date_filter",
        "explicit_product_scope",
        "distinct_user_date",
        "date_count_equals_required_calendar_days",
        "complete_members_order",
        "bounded_complete_JSON",
      ],
    );
    assert.equal(operators[0]!.qualification, "TRUE only");
    assert.equal(operators[4]!.required_count, plan.required_day_count);
    assert.equal(operators[6]!.byte_limit, 1048576);
    assert(
      typeof plan.qualified_projection_sql === "string" &&
        plan.qualified_projection_sql.includes("WHERE"),
    );
    if (result)
      for (const key of [
        "concept",
        "user_concept",
        "request",
        "required_day_count",
        "eligibility_semantics",
        "qualification_rule",
        "profile",
        "snapshot_id",
        "content_sha256",
        "core_sha256",
        "ontology_sha256",
        "mapping_sha256",
        "manifest_sha256",
      ])
        assert.deepEqual(plan[key], result[key], `compile/execute ${key}`);
  }
  async function refusal(
    label: string,
    action: () => Promise<unknown>,
    code: string,
  ) {
    let caught: unknown;
    try {
      await action();
    } catch (error) {
      caught = error;
    }
    assert(caught, `${label}: accepted`);
    const decoded = populationError(String(caught));
    assert.equal(decoded?.code, code, `${label}: ${String(caught)}`);
    refusals.push({ label, error: decoded });
    // Every recoverable refusal leaves the same published bundle usable.
    assert.equal(
      JSON.parse(await query(JSON.stringify(request()))).schema_version,
      "ossie-daily-user-set-result-v1",
    );
  }
  try {
    const engine = await db.loadArtifact(artifact);
    await db.run(
      "CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER)",
    );
    const cases: Case[] = JSON.parse(
      readFileSync(join(RESEARCH, "cases.json"), "utf8"),
    );
    for (const c of cases) {
      await db.run("DELETE FROM observations");
      if (c.dataset !== "empty")
        await db.run(
          "INSERT INTO observations SELECT * FROM read_csv(?,header=true)",
          [
            join(
              c.dataset === "original" ? join(RESEARCH, "..") : RESEARCH,
              "observations.csv",
            ),
          ],
        );
      await load(c.threshold);
      const text = await query(
        JSON.stringify(request(c.start, c.end, c.products)),
      );
      const actual = JSON.parse(text);
      const planText = await compile(
        JSON.stringify(request(c.start, c.end, c.products)),
      );
      const plan = JSON.parse(planText);
      checkPlan(plan, actual);
      assert.deepEqual(actual.members, c.expected_ids, c.id);
      assert.equal(actual.schema_version, "ossie-daily-user-set-result-v1");
      assert.equal(actual.request_kind, "daily_users");
      assert.equal(actual.user_concept, "User");
      assert.equal(actual.concept, "QualifiedObservation");
      const normalized = request(
        c.start,
        c.end,
        c.products === null
          ? null
          : [...c.products].sort((a, b) =>
              Buffer.compare(Buffer.from(a), Buffer.from(b)),
            ),
      );
      assert.deepEqual(actual.request, normalized, `${c.id}: scope echo`);
      assert.deepEqual(
        actual.qualification_rule,
        {
          concept: "QualifiedObservation",
          declared_sql: `DailyObservation.classification_value >= ${c.threshold}`,
        },
        `${c.id}: formal rule echo`,
      );

      assert.equal(actual.member_count, String(c.expected_ids.length));
      const days = (Date.parse(c.end) - Date.parse(c.start)) / 86400000 + 1;
      assert.equal(actual.required_day_count, String(days));
      assert.equal(actual.executed, true);
      assert.equal(actual.complete, true);
      assert.equal(actual.truncated, false);
      const state = (
        await db.records("SELECT * FROM ossie_population_capabilities()")
      )[0];
      for (const key of [
        "profile",
        "snapshot_id",
        "content_sha256",
        "core_sha256",
        "ontology_sha256",
        "mapping_sha256",
        "manifest_sha256",
      ])
        assert.equal(actual[key], state[key], `${c.id}: ${key}`);
      // Independent plain SQL relational division, not the product implementation's count algorithm.
      const productFilter =
        c.products === null
          ? "true"
          : `w.product IN (${c.products.map((p) => `'${p.replaceAll("'", "''")}'`).join(",")})`;
      const oracle = await db.rows(
        `SELECT DISTINCT o.user_id FROM observations o WHERE NOT EXISTS (SELECT 1 FROM range(DATE '${c.start}', DATE '${c.end}' + INTERVAL 1 DAY, INTERVAL 1 DAY) d(day) WHERE NOT EXISTS (SELECT 1 FROM observations w WHERE w.user_id=o.user_id AND w.ds=d.day AND ${productFilter} AND w.classification_value>=${c.threshold})) ORDER BY o.user_id`,
      );
      assert.deepEqual(
        oracle.map((row) => row[0]),
        c.expected_ids,
        `${c.id}: oracle`,
      );
      checks.push(c.id);
      results.push({
        case: c.id,
        result_json: text,
        plan_json: planText,
        independent_members: oracle.map((row) => row[0]),
      });
    }
    // Reload original eight-row batch for resource boundaries and empty-match controls.
    await db.run("DELETE FROM observations");
    await db.run(
      "INSERT INTO observations SELECT * FROM read_csv(?,header=true)",
      [join(RESEARCH, "..", "observations.csv")],
    );
    await load();
    const capRow = (
      await db.records("SELECT * FROM ossie_population_capabilities()")
    )[0];
    assert.equal(capRow.api_version, "2.2.0");
    const cap = JSON.parse(String(capRow.capabilities_json));
    assert.equal(cap.daily_user_set.enabled, true);
    assert.equal(cap.daily_user_set.mcp_example_registered, false);
    assert.deepEqual(cap.daily_user_set.supported_concepts, [
      "QualifiedObservation",
    ]);
    assert.equal(cap.daily_user_set.compile_supported, true);
    assert.equal(
      cap.daily_user_set.plan_schema,
      "ossie-daily-user-set-plan-v1",
    );
    checks.push(
      "capability operation, version, singleton target and MCP boundary",
    );
    const shape = JSON.parse(
      readFileSync(
        join(RESEARCH, "interface-design/request.examples.json"),
        "utf8",
      ),
    );
    for (const [i, value] of shape.invalid_shape.entries())
      await refusal(
        `invalid shape ${i}`,
        () => query(JSON.stringify(value)),
        "INVALID_REQUEST",
      );
    for (const [label, text] of [
      [
        "duplicate escaped root key",
        '{"start_date":"2026-01-31","\\u0073tart_date":"2026-01-31","end_date":"2026-02-01","products":{"mode":"all"}}',
      ],
      [
        "duplicate nested key",
        '{"start_date":"2026-01-31","end_date":"2026-02-01","products":{"mode":"all","mode":"all"}}',
      ],
      ["trailing JSON", JSON.stringify(request()) + " null"],
      ["invalid day", JSON.stringify(request("2026-02-30", "2026-03-01"))],
      [
        "invalid leap date",
        JSON.stringify(request("2025-02-29", "2025-03-01")),
      ],
      ["year zero", JSON.stringify(request("0000-01-01", "0000-01-02"))],
      ["inverted dates", JSON.stringify(request("2026-02-01", "2026-01-31"))],
    ])
      await refusal(label, () => query(text), "INVALID_REQUEST");
    for (const concept of ["User", "DailyObservation", "unknown"])
      await refusal(
        `unsupported ${concept}`,
        () => query(JSON.stringify(request()), concept),
        "UNSUPPORTED_CAPABILITY",
      );
    await query(JSON.stringify(request("2024-01-01", "2024-12-31")));
    checks.push("366 inclusive days with leap year accepted");
    for (const endpoint of ["0001-01-01", "9999-12-31"]) {
      const result = JSON.parse(
        await query(JSON.stringify(request(endpoint, endpoint))),
      );
      assert.deepEqual(result.members, []);
      assert.equal(result.required_day_count, "1");
    }
    checks.push("strict finite Gregorian year endpoints accepted");

    for (const [label, text, code] of [
      [
        "compile duplicate decoded key",
        '{"start_date":"2026-01-31","\\u0073tart_date":"2026-01-31","end_date":"2026-02-01","products":{"mode":"all"}}',
        "INVALID_REQUEST",
      ],
      [
        "compile invalid day",
        JSON.stringify(request("2026-02-30", "2026-03-01")),
        "INVALID_REQUEST",
      ],
      [
        "compile 367 days",
        JSON.stringify(request("2024-01-01", "2025-01-01")),
        "RESOURCE_LIMIT_EXCEEDED",
      ],
      [
        "compile 16KiB+1",
        JSON.stringify(request()) +
          " ".repeat(16385 - Buffer.byteLength(JSON.stringify(request()))),
        "RESOURCE_LIMIT_EXCEEDED",
      ],
    ])
      await refusal(label!, () => compile(text!), code!);
    await refusal(
      "compile unsupported User",
      () => compile(JSON.stringify(request()), "User"),
      "UNSUPPORTED_CAPABILITY",
    );
    const escapedPlan = JSON.parse(
      await compile(
        JSON.stringify(
          request(undefined, undefined, ['quote"\\', "\u0000p", "汉"]),
        ),
      ),
    );
    checkPlan(escapedPlan);
    assert.deepEqual(escapedPlan.request.products.values, [
      "\u0000p",
      'quote"\\',
      "汉",
    ]);
    checks.push("compile strict request parity and raw product escaping");
    await refusal(
      "367 days",
      () => query(JSON.stringify(request("2024-01-01", "2025-01-01"))),
      "RESOURCE_LIMIT_EXCEEDED",
    );
    await query(
      JSON.stringify(
        request(
          undefined,
          undefined,
          Array.from({ length: 64 }, (_, i) => `P${i}`),
        ),
      ),
    );
    checks.push("64 subset products accepted");
    await refusal(
      "65 products",
      () =>
        query(
          JSON.stringify(
            request(
              undefined,
              undefined,
              Array.from({ length: 65 }, (_, i) => `P${i}`),
            ),
          ),
        ),
      "RESOURCE_LIMIT_EXCEEDED",
    );
    await query(
      JSON.stringify(request(undefined, undefined, ["x".repeat(256)])),
    );
    checks.push("256 decoded product bytes accepted");
    await query(
      JSON.stringify(request(undefined, undefined, ["汉".repeat(85) + "x"])),
    );
    checks.push("256 UTF-8 multibyte product bytes accepted");
    for (const [label, value] of [
      ["257 product bytes", "x".repeat(257)],
      ["258 multibyte product bytes", "汉".repeat(86)],
    ])
      await refusal(
        label,
        () => query(JSON.stringify(request(undefined, undefined, [value]))),
        "RESOURCE_LIMIT_EXCEEDED",
      );
    const base = JSON.stringify(request());
    await query(base + " ".repeat(16384 - Buffer.byteLength(base)));
    checks.push("exact16KiB request accepted");
    await refusal(
      "16KiB+1 request",
      () => query(base + " ".repeat(16385 - Buffer.byteLength(base))),
      "RESOURCE_LIMIT_EXCEEDED",
    );
    await refusal(
      "simultaneous64x256 byte products exceed request limit",
      () =>
        query(
          JSON.stringify(
            request(
              undefined,
              undefined,
              Array.from(
                { length: 64 },
                (_, i) => `${String(i).padStart(2, "0")}${"x".repeat(254)}`,
              ),
            ),
          ),
        ),
      "RESOURCE_LIMIT_EXCEEDED",
    );
    await refusal(
      "SQL NULL",
      () =>
        db.rows(
          "SELECT * FROM ossie_daily_users_json('QualifiedObservation',NULL)",
        ),
      "INVALID_REQUEST",
    );
    await db.run("DELETE FROM observations");
    const rawUsers = ["a", "A", "汉", "😀", "z\u0000r", 'quote"\\', "\u0001"];
    for (const user of rawUsers)
      for (const day of ["2026-01-31", "2026-02-01"]) {
        await db.run(
          "INSERT INTO observations VALUES(?,?,CAST(? AS DATE),?,NULL,10)",
          ["raw", user, day, "\u0000p"],
        );
      }
    await db.run("SET GLOBAL default_collation='nocase'");
    await load();
    const expectedRaw = [...rawUsers].sort((a, b) =>
      Buffer.compare(Buffer.from(a), Buffer.from(b)),
    );
    const rawResult = JSON.parse(
      await query(JSON.stringify(request(undefined, undefined, ["\u0000p"]))),
    );
    assert.deepEqual(rawResult.members, expectedRaw);
    assert.deepEqual(rawResult.request.products.values, ["\u0000p"]);
    checks.push(
      "raw Unicode/case/quote/backslash/control/NUL members and products retain binary identity under caller nocase",
    );
    await db.run("SET GLOBAL default_collation='binary'");
    await db.run("DELETE FROM observations");
    await db.run(
      "INSERT INTO observations SELECT 'domain', 'D', d::DATE, 'P'||i::VARCHAR,NULL,10 FROM range(65) p(i), range(DATE '2026-01-31',DATE '2026-02-02',INTERVAL 1 DAY) dates(d)",
    );
    await load();
    assert.deepEqual(
      JSON.parse(await query(JSON.stringify(request()))).members,
      ["D"],
    );
    checks.push("all permits source product domain greater than64");
    // Actual large success/refusal, including escaping. Whole captured input remains <16MiB.
    await db.run("DELETE FROM observations");
    await db.run(
      "INSERT INTO observations VALUES('large',repeat('x',1047000),DATE '2026-01-31','X',NULL,10)",
    );
    await load();
    const large = await query(
      JSON.stringify(request("2026-01-31", "2026-01-31")),
    );
    assert(Buffer.byteLength(large) <= 1048576);
    assert.equal(JSON.parse(large).members[0].length, 1047000);
    checks.push("large complete member succeeds below1MiB");
    const sequence = Number(JSON.parse(large).snapshot_id.split(":").at(-1));
    const growth = String(sequence + 1).length - String(sequence).length;
    const exactLength = 1048576 - (Buffer.byteLength(large) - 1047000) - growth;
    await db.run(`UPDATE observations SET user_id=repeat('x',${exactLength})`);
    await load();
    const exact = await query(
      JSON.stringify(request("2026-01-31", "2026-01-31")),
    );
    assert.equal(Buffer.byteLength(exact), 1048576);
    assert.equal(JSON.parse(exact).members[0].length, exactLength);
    checks.push("exact1MiB complete result accepted including metadata");
    const nextGrowth =
      String(sequence + 2).length - String(sequence + 1).length;
    await db.run(
      `UPDATE observations SET user_id=repeat('x',${exactLength + 1 - nextGrowth})`,
    );
    await load();
    let exactFailure: unknown;
    try {
      await query(JSON.stringify(request("2026-01-31", "2026-01-31")));
    } catch (error) {
      exactFailure = error;
    }
    const exactError = populationError(String(exactFailure));
    assert.equal(exactError?.code, "RESOURCE_LIMIT_EXCEEDED");
    assert.equal(exactError?.object, "daily_user_set.result_json_utf8_bytes");
    assert.equal(exactError?.reason, "maximum=1048576 actual=1048577");
    const oversizedPlan = JSON.parse(
      await compile(JSON.stringify(request("2026-01-31", "2026-01-31"))),
    );
    checkPlan(oversizedPlan);
    checks.push(
      "compile succeeds without computing oversized execution members",
    );
    refusals.push({
      label: "exact1MiB+1 complete result refuses",
      error: exactError,
    });
    await db.run("UPDATE observations SET user_id=repeat('x',1048576)");
    await load();
    // Cannot use helper recovery with same oversized member/range: validate empty result instead.
    let failure: unknown;
    try {
      await query(JSON.stringify(request("2026-01-31", "2026-01-31")));
    } catch (error) {
      failure = error;
    }
    assert.equal(
      populationError(String(failure))?.code,
      "RESOURCE_LIMIT_EXCEEDED",
    );
    refusals.push({
      label: "result includes metadata and refuses above1MiB",
      error: populationError(String(failure)),
    });
    assert.deepEqual(
      JSON.parse(
        await query(JSON.stringify(request("2026-02-01", "2026-02-01"))),
      ).members,
      [],
    );
    checks.push("result budget refusal retains bundle");
    await db.run("UPDATE observations SET user_id=repeat(chr(1),180000)");
    await load();
    failure = undefined;
    try {
      await query(JSON.stringify(request("2026-01-31", "2026-01-31")));
    } catch (error) {
      failure = error;
    }
    assert.equal(
      populationError(String(failure))?.code,
      "RESOURCE_LIMIT_EXCEEDED",
    );
    refusals.push({
      label: "control escaping exceeds1MiB",
      error: populationError(String(failure)),
    });
    // Legal rule whitespace enlarges declared_sql without changing the formal AST.
    // This exercises the plan's own metadata cap without broadening supported rules.
    await db.run("DELETE FROM observations");
    await db.run(
      "INSERT INTO observations VALUES('budget','B',DATE '2026-01-31','X',NULL,10)",
    );
    await temporary("ossie-daily-plan-", async (directory) => {
      const manifest = JSON.parse(
        readFileSync(join(FIXTURE, "bundle-10.json"), "utf8"),
      );
      const ontology = JSON.parse(
        readFileSync(join(FIXTURE, "ontology.json"), "utf8"),
      );
      for (const section of ["core", "mapping"])
        writeFileSync(
          join(directory, manifest[section].path),
          readFileSync(join(FIXTURE, manifest[section].path)),
        );
      async function loadPadded(padding: number) {
        ontology.ontology.at(-1).derived_by = [
          "DailyObservation.classification_value >=" +
            " ".repeat(padding) +
            "10",
        ];
        const text = JSON.stringify(ontology);
        writeFileSync(join(directory, "ontology.json"), text);
        manifest.ontology.sha256 = createHash("sha256")
          .update(text)
          .digest("hex");
        writeFileSync(join(directory, "bundle.json"), JSON.stringify(manifest));
        await db.run("CALL ossie_bundle_load(?)", [
          join(directory, "bundle.json"),
        ]);
      }
      await loadPadded(1);
      const padding =
        16384 - Buffer.byteLength("DailyObservation.classification_value >=10");
      await loadPadded(padding);
      const maximumRulePlan = await compile(JSON.stringify(request()));
      checkPlan(JSON.parse(maximumRulePlan));
      assert.equal(
        Buffer.byteLength(
          JSON.parse(maximumRulePlan).qualification_rule.declared_sql,
        ),
        16384,
      );
      results.push({
        case: "maximum supported rule in plan",
        rule_padding: padding,
        plan_bytes: Buffer.byteLength(maximumRulePlan),
        plan_sha256: createHash("sha256").update(maximumRulePlan).digest("hex"),
        ontology_sha256: manifest.ontology.sha256,
      });
      checks.push("16KiB supported rule metadata compiles without truncation");
      let failure: unknown;
      try {
        await loadPadded(padding + 1);
      } catch (error) {
        failure = error;
      }
      const error = populationError(String(failure));
      assert.equal(error?.code, "RULE_REJECTED");
      assert.equal(await compile(JSON.stringify(request())), maximumRulePlan);
      refusals.push({
        label: "16KiB+1 rule rejected at loader before plan encoding",
        error,
      });
      checks.push("failed oversized rule load retains complete prior plan");
      await load();
      checkPlan(JSON.parse(await compile(JSON.stringify(request()))));
      checks.push(
        "supported rule budget refuses at loader and valid reload recovers",
      );
    });
    await db.run("DROP TABLE observations");
    checkPlan(JSON.parse(await compile(JSON.stringify(request()))));
    checks.push("compile uses captured bundle after source table dropped");
    return {
      passed: true,
      scope: "daily SQL execution and logical compile only; not all P1",
      engine,
      artifact_sha256: digest(artifact),
      checks,
      refusals,
      results,
      input_sha256: digest(join(RESEARCH, "observations.csv")),
      limitations:
        "No native cancellation, MCP or full legacy regression proof in this report; hard time/memory/wire caps and exact1MiB plan encoding boundary unclaimed; oversized rule injection is rejected by existing16KiB loader rule limit.",
    };
  } finally {
    db.close();
  }
}
if (import.meta.main)
  emitReport(await verifyDailyUsers(resolve(process.argv[2] ?? "")));

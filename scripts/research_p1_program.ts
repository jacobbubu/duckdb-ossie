/** Ordinary SQL + independent integer/set oracle research; never claims new plugin support. */
import { readFileSync, writeFileSync } from "node:fs";
import { join, relative, resolve } from "node:path";
import {
  Database,
  ROOT,
  assert,
  digest,
  sqlString,
} from "./artifact_helpers.ts";
import { fixedSchemaValidator, SPEC_COMMIT } from "./check_fixed_schema.ts";
import { populationError } from "./population_error.ts";
const DIR = join(ROOT, "docs/research/ossie-req-001/p1-program-research");
const FIXTURE = join(ROOT, "test/fixtures/population-observation");
const OBS = join(
  ROOT,
  "docs/research/ossie-req-001/multi-period-research/every-day/observations.csv",
);
const csv = (path: string) =>
  readFileSync(path, "utf8")
    .trim()
    .split("\n")
    .slice(1)
    .map((row) => row.split(","));
const cents = (text: string) =>
  text === "" ? null : BigInt(text.replace(".", ""));
const money = (value: bigint | null) =>
  value === null
    ? null
    : `${value < 0n ? "-" : ""}${(value < 0n ? -value : value) / 100n}.${String((value < 0n ? -value : value) % 100n).padStart(2, "0")}`;
export async function researchP1(artifact: string) {
  const db = await Database.open(),
    checks: string[] = [],
    failures: unknown[] = [];
  try {
    const engine = await db.loadArtifact(artifact);
    const obs = csv(OBS).map(([, user, date, product, , classification]) => ({
      user,
      date,
      product,
      classification: classification === "" ? null : Number(classification),
    }));
    const links = (threshold: number) =>
      obs
        .filter(
          (r) => r.classification !== null && r.classification >= threshold,
        )
        .map((r) => [r.user, r.date, r.product, r.user])
        .sort((a, b) =>
          Buffer.compare(
            Buffer.from(JSON.stringify(a)),
            Buffer.from(JSON.stringify(b)),
          ),
        );
    assert.equal(
      new Set(obs.map((r) => JSON.stringify([r.user, r.date, r.product]))).size,
      obs.length,
    );
    await db.run(
      "CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER)",
    );
    await db.run(
      "INSERT INTO observations SELECT * FROM read_csv(?,header=true)",
      [OBS],
    );
    const relationshipResults: unknown[] = [];
    for (const threshold of [10, 20]) {
      const rows = await db.rows(
        `WITH qualified_user AS (SELECT DISTINCT user_id,ds,product,user_id AS target_user FROM observations WHERE classification_value>=${threshold}), audited_user AS (SELECT * FROM qualified_user) SELECT user_id,ds::VARCHAR,product,target_user FROM audited_user`,
      );
      const actual = rows
        .map((r) => r.map((v) => String(v)))
        .sort((a, b) =>
          Buffer.compare(
            Buffer.from(JSON.stringify(a)),
            Buffer.from(JSON.stringify(b)),
          ),
        );
      assert.deepEqual(actual, links(threshold));
      assert.equal(actual.length, threshold === 10 ? 18 : 7);
      relationshipResults.push({ threshold, links: actual });
    }
    const duplicate = await db.one(
      "SELECT count(*) FROM (SELECT user_id,ds,product,user_id AS target_user FROM observations WHERE classification_value>=10 UNION SELECT user_id,ds,product,user_id FROM observations WHERE classification_value>=10)",
    );
    assert.equal(duplicate[0], 18);
    checks.push(
      "typed observation-to-User full links, two-level dependency and set duplicate derivation oracle",
    );
    assert.equal(links(10).filter((r) => r[0] === "K").length, 4);
    assert(!links(10).some((r) => r[0] === "G" && r[1] === "2026-02-01"));
    checks.push(
      "same User across date/product preserves root identities; FALSE/NULL missing links do not become null targets",
    );
    const guard = "classification_value IS NULL OR classification_value >= 0";
    const violations = await db.one(
      `SELECT count(*) FILTER(WHERE (${guard}) IS FALSE),count(*) FILTER(WHERE (${guard}) IS NULL) FROM observations`,
    );
    assert.deepEqual(violations, [0, 0]);
    await db.run(
      "INSERT INTO observations VALUES('invalid','BAD',DATE '2026-02-01','X',NULL,-1)",
    );
    assert.deepEqual(
      await db.one(
        `SELECT count(*) FILTER(WHERE (${guard}) IS FALSE),count(*) FILTER(WHERE (${guard}) IS NULL) FROM observations`,
      ),
      [1, 0],
    );
    assert.deepEqual(
      await db.one(
        `SELECT count(*) FROM observations WHERE classification_value>=10 AND (${guard}) IS FALSE`,
      ),
      [0],
    );
    assert.deepEqual(
      await db.one(
        "SELECT count(*) FILTER(WHERE (classification_value >= 0) IS NULL) FROM observations",
      ),
      [1],
    );
    checks.push(
      "requires full root population violation is hidden by qualification WHERE; explicit NULL guard is total, unguarded comparison has UNKNOWN",
    );
    await db.run("DELETE FROM observations WHERE user_id='BAD'");
    await db.run("CALL ossie_bundle_load(?)", [
      join(FIXTURE, "bundle-10.json"),
    ]);
    const candidate = JSON.parse(
      readFileSync(join(FIXTURE, "ontology.json"), "utf8"),
    );
    const root = candidate.ontology.find(
      (c: any) => c.concept === "DailyObservation",
    );
    root.requires = [
      "DailyObservation.classification_value IS NULL OR DailyObservation.classification_value >= 0",
    ];
    root.relationships.push(
      {
        name: "qualified_user",
        roles: [{ concept: "User" }],
        multiplicity: "ManyToOne",
        verbalizes: ["{DailyObservation} qualifies for {User}"],
        derived_by: [
          "DailyObservation.classification_value >= 10 AND DailyObservation.user(User)",
        ],
      },
      {
        name: "audited_user",
        roles: [{ concept: "User" }],
        multiplicity: "ManyToOne",
        verbalizes: ["{DailyObservation} confirms {User}"],
        derived_by: ["DailyObservation.qualified_user(User)"],
      },
    );
    const schema = fixedSchemaValidator()("ontology", candidate);
    assert(schema.accepted, JSON.stringify(schema.errors));
    writeFileSync(
      join(DIR, "candidate-ontology.json"),
      JSON.stringify(candidate, null, 2) + "\n",
    );
    const manifest = JSON.parse(
      readFileSync(join(FIXTURE, "bundle-10.json"), "utf8"),
    );
    for (const kind of ["core", "mapping"])
      manifest[kind].path = relative(DIR, join(FIXTURE, manifest[kind].path));
    manifest.ontology.path = "candidate-ontology.json";
    manifest.ontology.sha256 = digest(join(DIR, "candidate-ontology.json"));
    writeFileSync(
      join(DIR, "candidate-current-profile-bundle.json"),
      JSON.stringify(manifest, null, 2) + "\n",
    );
    const snapshot = await db.one(
      "SELECT snapshot_id FROM ossie_population_capabilities()",
    );
    let failed: unknown;
    try {
      await db.run("CALL ossie_bundle_load(?)", [
        join(DIR, "candidate-current-profile-bundle.json"),
      ]);
    } catch (e) {
      failed = e;
    }
    assert(failed);
    const error = populationError(String(failed));
    assert.equal(error?.code, "UNSUPPORTED_CAPABILITY");
    assert.deepEqual(
      await db.one("SELECT snapshot_id FROM ossie_population_capabilities()"),
      snapshot,
    );
    failures.push({
      label:
        "schema-accepted ontology candidate rejected by existing profile; old snapshot retained",
      error,
    });
    // Isolate relationship and window refusal from the root requires refusal.
    const relationOnly = structuredClone(candidate);
    relationOnly.ontology.find(
      (c: any) => c.concept === "DailyObservation",
    ).requires = [];
    const windowCore = JSON.parse(
      readFileSync(join(FIXTURE, "core.json"), "utf8"),
    );
    windowCore.datasets[0].fields.find(
      (f: any) => f.name === "classification_value",
    ).expression.dialects[0].expression =
      "LAG(classification_value,1,NULL) OVER (PARTITION BY user_id,product ORDER BY ds)";
    for (const [label, kind, document] of [
      ["relationship-only", "ontology", relationOnly],
      ["window-only", "core", windowCore],
    ] as const) {
      const validation = fixedSchemaValidator()(kind, document);
      assert(validation.accepted, JSON.stringify(validation.errors));
      const filename = `${label}-${kind}.json`;
      writeFileSync(
        join(DIR, filename),
        JSON.stringify(document, null, 2) + "\n",
      );
      const variant = JSON.parse(
        readFileSync(join(FIXTURE, "bundle-10.json"), "utf8"),
      );
      for (const component of ["core", "ontology", "mapping"])
        variant[component].path = relative(
          DIR,
          join(FIXTURE, variant[component].path),
        );
      variant[kind].path = filename;
      variant[kind].sha256 = digest(join(DIR, filename));
      const bundlePath = join(DIR, `${label}-bundle.json`);
      writeFileSync(bundlePath, JSON.stringify(variant, null, 2) + "\n");
      let rejected: unknown;
      try {
        await db.run("CALL ossie_bundle_load(?)", [bundlePath]);
      } catch (e) {
        rejected = e;
      }
      assert(rejected, `${label} must reject in existing profile`);
      const refusal = populationError(String(rejected));
      assert.equal(
        refusal?.code,
        label === "window-only" ? "RULE_REJECTED" : "UNSUPPORTED_CAPABILITY",
      );
      assert.deepEqual(
        await db.one("SELECT snapshot_id FROM ossie_population_capabilities()"),
        snapshot,
      );
      failures.push({
        label,
        schema: validation,
        error: refusal,
        old_snapshot_retained: true,
      });
    }
    checks.push(
      "separate schema-valid relationship-only and window-only existing-profile refusals retain snapshot",
    );
    // DuckDB parser tree is evidence of syntax only, not ontology binder/execution.
    const parser: unknown[] = [];
    for (const expression of [
      "DailyObservation.classification_value >= 10 AND DailyObservation.user(User)",
      "DailyObservation.qualified_user(User)",
      "LAG(revenue_usd,1,NULL) OVER (PARTITION BY user_id ORDER BY ds)",
      "SUM(revenue_usd) OVER (PARTITION BY user_id ORDER BY ds ROWS BETWEEN 1 PRECEDING AND 0 FOLLOWING)",
    ]) {
      const parsed = JSON.parse(
        String(
          (
            await db.one(
              `SELECT json_serialize_sql(${sqlString("SELECT " + expression)})`,
            )
          )[0],
        ),
      );
      assert.equal(parsed.error, false);
      parser.push({ expression, tree: parsed });
    }
    checks.push(
      "fixed schema accepts declared relationships/requires; current plugin rejects; parser acceptance separately recorded",
    );
    await db.run(
      "CREATE TABLE revenue(user_id VARCHAR,ds DATE,revenue_usd DECIMAL(18,2))",
    );
    await db.run(
      "CREATE TABLE cost(user_id VARCHAR,ds DATE,product VARCHAR,cost_usd DECIMAL(18,2))",
    );
    await db.run("INSERT INTO revenue SELECT * FROM read_csv(?,header=true)", [
      join(DIR, "revenue.csv"),
    ]);
    await db.run("INSERT INTO cost SELECT * FROM read_csv(?,header=true)", [
      join(DIR, "cost.csv"),
    ]);
    const revenue = csv(join(DIR, "revenue.csv")),
      cost = csv(join(DIR, "cost.csv"));
    const sum = (rows: string[][], index: number) => {
      const values = rows
        .map((r) => cents(r[index]))
        .filter((v): v is bigint => v !== null);
      return values.length ? values.reduce((a, b) => a + b, 0n) : null;
    };
    const users = [
      ...new Set(
        revenue.map((r) => r[0]).filter((u) => cost.some((c) => c[0] === u)),
      ),
    ].sort();
    const expected = users.map((u) => {
      const r = sum(
          revenue.filter((r) => r[0] === u),
          2,
        ),
        c = sum(
          cost.filter((r) => r[0] === u),
          3,
        );
      return [
        u,
        money(r),
        money(c),
        money(r === null || c === null ? null : r - c),
      ];
    });
    assert.deepEqual(expected, [
      ["A", "375.00", "85.00", "290.00"],
      ["B", "80.00", "30.00", "50.00"],
      ["C", null, null, null],
    ]);
    const grouped = await db.rows(
      "WITH r AS (SELECT user_id,sum(revenue_usd) AS revenue FROM revenue GROUP BY user_id),c AS (SELECT user_id,sum(cost_usd) AS cost FROM cost GROUP BY user_id) SELECT r.user_id,revenue,cost,revenue-cost FROM r JOIN c USING(user_id) ORDER BY user_id",
    );
    assert.deepEqual(grouped, expected);
    const fanout = await db.rows(
      "SELECT r.user_id,sum(revenue_usd),sum(cost_usd) FROM revenue r JOIN cost c ON r.user_id=c.user_id AND r.ds=c.ds GROUP BY r.user_id ORDER BY r.user_id",
    );
    assert.deepEqual(fanout[0], ["A", "525.00", "85.00"]);
    assert(!expected.some((r) => r[0] === "D" || r[0] === "E"));
    checks.push(
      "separate User/date revenue and User/date/product cost aggregate to common User before combine; raw join inflates375 to525, common-domain D/E absence and NULL retained",
    );
    const rich = new Set(
      expected
        .filter(
          (r) =>
            r[1] !== null && BigInt(String(r[1]).replace(".", "")) >= 10000n,
        )
        .map((r) => r[0]),
    );
    const cheap = new Set(
      expected
        .filter(
          (r) => r[2] !== null && BigInt(String(r[2]).replace(".", "")) < 8000n,
        )
        .map((r) => r[0]),
    );
    const cohorts = {
      intersection: [...rich].filter((u) => cheap.has(u)),
      union: [...new Set([...rich, ...cheap])].sort(),
      exclude: [...rich].filter((u) => !cheap.has(u)),
    };
    assert.deepEqual(cohorts, {
      intersection: [],
      union: ["A", "B"],
      exclude: ["A"],
    });
    const sqlCohorts = await db.one(
      "WITH r AS (SELECT user_id,sum(revenue_usd) AS revenue FROM revenue GROUP BY user_id),c AS (SELECT user_id,sum(cost_usd) AS cost FROM cost GROUP BY user_id),v AS (SELECT * FROM r JOIN c USING(user_id)) SELECT (SELECT count(*) FROM v WHERE revenue>=100 AND cost<80),(SELECT count(*) FROM v WHERE revenue>=100 OR cost<80),(SELECT count(*) FROM v WHERE revenue>=100 AND NOT(cost<80))",
    );
    assert.deepEqual(sqlCohorts, [0, 2, 1]);
    checks.push(
      "cohort intersection/union/exclusion independent full User IDs plus SQL counts",
    );
    const window = await db.rows(
      "SELECT user_id,ds::VARCHAR,revenue_usd,LAG(revenue_usd,1,NULL) OVER(PARTITION BY user_id ORDER BY ds),SUM(revenue_usd) OVER(PARTITION BY user_id ORDER BY ds ROWS BETWEEN 1 PRECEDING AND 0 FOLLOWING) FROM revenue ORDER BY user_id,ds",
    );
    const windowExpected = revenue
      .map((r) => {
        const partition = revenue
          .filter((x) => x[0] === r[0])
          .sort((a, b) => a[1].localeCompare(b[1]));
        const pos = partition.findIndex((x) => x[1] === r[1]);
        const previous = pos ? cents(partition[pos - 1][2]) : null;
        return [
          r[0],
          r[1],
          money(cents(r[2])),
          money(previous),
          money(sum(partition.slice(Math.max(0, pos - 1), pos + 1), 2)),
        ];
      })
      .sort(
        (a, b) =>
          String(a[0]).localeCompare(String(b[0])) ||
          String(a[1]).localeCompare(String(b[1])),
      );
    assert.deepEqual(window, windowExpected);
    assert.deepEqual(window[2], [
      "A",
      "2026-02-01",
      "50.00",
      "200.00",
      "250.00",
    ]);
    checks.push(
      "same raw User across January/February: LAG and two observed-row SUM independent cents/ordering/NULL oracle; rows are not calendar days",
    );
    return {
      passed: true,
      scope: "finite P1 design research only, no new plugin support",
      engine,
      artifact_sha256: digest(artifact),
      spec_commit: SPEC_COMMIT,
      checks,
      refusals: failures,
      candidate_schema: schema,
      relationshipResults,
      parser_only: parser,
      mixed_grain: {
        expected,
        fanout,
        cohorts,
        domain:
          "intersection of separately aggregated User populations, all products/no allocation; candidate synthetic measure",
      },
      window_observed_rows: window,
      source_hashes: Object.fromEntries(
        [
          OBS,
          join(DIR, "revenue.csv"),
          join(DIR, "cost.csv"),
          join(DIR, "candidate-ontology.json"),
          join(
            ROOT,
            "docs/research/ossie-req-001/sources/apache-ossie/ontology/ontology.md",
          ),
          join(
            ROOT,
            "docs/research/ossie-req-001/sources/apache-ossie/core-spec/expression_language.md",
          ),
        ].map((p) => [relative(ROOT, p), digest(p)]),
      ),
      limitations: [
        "Schema/parser/ordinary SQL proofs separate from plugin binding/execution",
        "Relationship SQL manually lowers candidate; candidate ontology rules were not executed by plugin",
        "Cohort domain is common observed User population; NULL is not coalesced to zero or absent fact",
        "No product allocation/filter on unmapped revenue dimension; no observed status/history semantics assumed",
        "Window ROWS counts observed rows, not natural days or state changes",
      ],
    };
  } finally {
    db.close();
  }
}
if (import.meta.main) {
  const report = await researchP1(resolve(process.argv[2]));
  writeFileSync(
    resolve(process.argv[3]),
    JSON.stringify(report, null, 2) + "\n",
  );
  console.log(
    `PASS P1 design research ${report.checks.length} checks; no new plugin support`,
  );
}

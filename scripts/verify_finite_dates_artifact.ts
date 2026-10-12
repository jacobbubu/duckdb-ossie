/** Real finite-DATE boundary correspondence through the frozen relation result carrier. */
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { Database, ROOT, digest, assert } from "./artifact_helpers.ts";
import { relationDate, relationResult } from "./relation_result.ts";
import { factProgram, factValue } from "./fact_result.ts";
import { populationError } from "./population_error.ts";
const [artifactArg, outArg] = process.argv.slice(2);
assert(artifactArg && outArg && process.argv.length === 4);
const artifact = resolve(artifactArg),
  out = resolve(outArg);
assert(!existsSync(out), "fresh output directory required");
mkdirSync(out, { recursive: true });
const db = await Database.open();
try {
  const engine = await db.loadArtifact(artifact);
  await db.run(
    "CREATE TABLE observations(observation_id VARCHAR,user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER)",
  );
  const offsets = [-2147483648, -2147483646, -100000000, 0, 2147483646];
  for (const [i, day] of offsets.entries())
    await db.run(
      `INSERT INTO observations VALUES ('o${i}','u',DATE '1970-01-01'+(${day})::INTEGER,'p${i}',1.00,10)`,
    );
  const actual = await db.rows(
    "SELECT ds::VARCHAR,isfinite(ds),date_diff('day',DATE '1970-01-01',ds)::VARCHAR FROM observations ORDER BY ds",
  );
  await db.run("CALL ossie_bundle_load(?)", [
    join(ROOT, "test/fixtures/population-relation-program/bundle.json"),
  ]);
  const [raw] = await db.one(
    "SELECT result_json FROM ossie_relation_objects_json('audited_user')",
  );
  const value = JSON.parse(String(raw));
  const report = {
    scope:
      "finite DATE relation identities and fact display/window/plan/decoder correspondence; not generic historical execution",
    engine,
    artifact_sha256: digest(artifact),
    source_sha256: {
      "scripts/verify_finite_dates_artifact.ts": digest(
        join(ROOT, "scripts/verify_finite_dates_artifact.ts"),
      ),
      "scripts/relation_result.ts": digest(
        join(ROOT, "scripts/relation_result.ts"),
      ),
    },
    actual,
    result: value,
    expected_days: offsets,
  };
  writeFileSync(
    join(out, "actual.json"),
    JSON.stringify(report, null, 2) + "\n",
  );
  const result = relationResult(JSON.stringify([{ result_json: raw }]));
  assert.deepEqual(
    result.links,
    actual.map((r, i) => ["u", String(r[0]), "p" + i, "u"]),
  );
  for (const [i, row] of actual.entries()) {
    assert.equal(row[1], true);
    assert.equal(relationDate(row[0]), offsets[i]);
    assert.equal(row[2], String(offsets[i]));
  }
  for (const s of ["5877642-06-24 (BC)", "5881580-07-11", "5877642-06-22 (BC)"])
    assert.throws(() => relationDate(s));
  await db.run(
    "CREATE TABLE revenue(who VARCHAR,day DATE,value DECIMAL(18,2))",
  );
  await db.run(
    "CREATE TABLE cost(who VARCHAR,day DATE,item VARCHAR,value DECIMAL(18,2))",
  );
  for (const [i, day] of offsets.entries())
    await db.run(
      `INSERT INTO revenue VALUES ('u',DATE '1970-01-01'+(${day})::INTEGER,${i + 1}.01)`,
    );
  await db.run("CALL ossie_bundle_load(?)", [
    join(ROOT, "test/fixtures/population-fact-program/bundle.json"),
  ]);
  const [programRaw] = await db.one("SELECT * FROM ossie_fact_program()");
  const program = factProgram(JSON.stringify([{ program_json: programRaw }]));
  const windows: unknown[] = [],
    refused: unknown[] = [];
  for (let i = 0; i < actual.length; i++) {
    const date = String(actual[i][0]);
    const request = {
      input_scope: "entire_capture",
      windows: ["previous", "rolling"],
      display: { mode: "date_range", start_date: date, end_date: date },
    };
    const [rawResult] = await db.one(
      "SELECT * FROM ossie_fact_windows_json(?)",
      [JSON.stringify(request)],
    );
    const [rawPlan] = await db.one(
      "SELECT * FROM ossie_fact_windows_compile(?)",
      [JSON.stringify(request)],
    );
    const result = factValue(
      JSON.stringify([{ result_json: rawResult }]),
      request,
      program,
      true,
      false,
    );
    const plan = factValue(
      JSON.stringify([{ plan_json: rawPlan }]),
      request,
      program,
      true,
      true,
    );
    const sum = (i + 1) * 100 + 1 + (i > 0 ? i * 100 + 1 : 0),
      money = (n: number) =>
        `${Math.floor(n / 100)}.${String(n % 100).padStart(2, "0")}`;
    const expected = [
      ["u", date, i === 0 ? null : money(i * 100 + 1), money(sum)],
    ];
    assert.deepEqual(result.rows, expected);
    assert.equal(result.source_captured_rows, "5");
    windows.push({ request, result, plan, expected });
  }
  for (const date of [
    "-infinity",
    "infinity",
    "5877642-06-24 (BC)",
    "5881580-07-11",
    "5877642-06-22 (BC)",
  ])
    for (const fn of [
      "ossie_fact_windows_json",
      "ossie_fact_windows_compile",
    ]) {
      let caught = false;
      try {
        await db.one(`SELECT * FROM ${fn}(?)`, [
          JSON.stringify({
            input_scope: "entire_capture",
            windows: ["previous"],
            display: { mode: "date_range", start_date: date, end_date: date },
          }),
        ]);
      } catch (e) {
        assert.equal(populationError(String(e))?.code, "INVALID_REQUEST");
        caught = true;
      }
      assert(caught);
      refused.push({ fn, date });
    }
  writeFileSync(
    join(out, "verification.json"),
    JSON.stringify(
      {
        ...report,
        passed: true,
        fact_windows: windows,
        fact_date_refusals: refused,
        refused: [
          "negative infinity sentinel",
          "positive infinity sentinel",
          "below INT32_MIN",
        ],
      },
      null,
      2,
    ) + "\n",
  );
  console.log(
    "PASS actual frozen finite DATE links and fact windows including INT32_MIN; full rows/plan/decoder and infinity/out-of-range refusals",
  );
} finally {
  await db.close();
}

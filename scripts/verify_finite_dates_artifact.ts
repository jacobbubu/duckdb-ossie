/** Real finite-DATE boundary correspondence through the frozen relation result carrier. */
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { Database, ROOT, digest, assert } from "./artifact_helpers.ts";
import { relationDate, relationResult } from "./relation_result.ts";
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
      "complete finite Date native/decoder correspondence, not historical relation execution",
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
  writeFileSync(
    join(out, "verification.json"),
    JSON.stringify(
      {
        ...report,
        passed: true,
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
    "PASS actual frozen finite DATE links including INT32_MIN; decoded epoch/native equality and infinity/out-of-range refusals",
  );
} finally {
  await db.close();
}

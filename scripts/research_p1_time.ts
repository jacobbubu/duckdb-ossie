/** Conditional ordinary-SQL research; no temporal plugin support or business approval implied. */
import { writeFileSync } from "node:fs";
import { join, relative, resolve } from "node:path";
import { Database, ROOT, assert, digest } from "./artifact_helpers.ts";

export async function researchTime(artifact: string) {
  const db = await Database.open();
  try {
    const engine = await db.loadArtifact(artifact);
    await db.run("CREATE TABLE witnesses(mask INTEGER, value BOOLEAN)");
    const truth = [true, false, null];
    const expected = Array.from({ length: 8 }, (_, mask) => {
      const values = truth.filter((_, bit) => mask & (1 << bit));
      return [
        mask,
        values.includes(true)
          ? "TRUE"
          : values.length && values.every((v) => v === false)
            ? "FALSE"
            : "UNKNOWN",
      ];
    });
    for (let mask = 1; mask < 8; mask++) {
      for (let bit = 0; bit < 3; bit++) {
        if (mask & (1 << bit)) {
          const literal = ["TRUE", "FALSE", "NULL"][bit]!;
          await db.run(`INSERT INTO witnesses VALUES (${mask},${literal})`);
        }
      }
    }
    const actual = await db.rows(`
      SELECT m.mask, CASE
        WHEN count(*) FILTER (WHERE w.value IS TRUE)>0 THEN 'TRUE'
        WHEN count(w.mask)>0 AND count(*) FILTER (WHERE w.value IS FALSE)=count(w.mask) THEN 'FALSE'
        ELSE 'UNKNOWN' END AS state
      FROM range(8) m(mask) LEFT JOIN witnesses w ON m.mask=w.mask
      GROUP BY m.mask ORDER BY m.mask`);
    assert.deepEqual(actual, expected);
    const boolOr = await db.rows(
      "SELECT mask, bool_or(value) FROM witnesses GROUP BY mask ORDER BY mask",
    );
    // FALSE + NULL differs: ignoring NULL would erase uncertainty in this candidate.
    assert.deepEqual(
      boolOr.find((row) => row[0] === 6),
      [6, false],
    );
    assert.deepEqual(expected[6], [6, "UNKNOWN"]);
    assert.deepEqual(
      await db.rows("SELECT DATE '2024-02-28'+i::INTEGER FROM range(3) t(i)"),
      [["2024-02-28"], ["2024-02-29"], ["2024-03-01"]],
    );

    await db.run(
      "CREATE TABLE history(record_id VARCHAR, user_id VARCHAR, starts DATE, ends DATE, target_id VARCHAR)",
    );
    await db.run(`INSERT INTO history VALUES
      ('h1','A','2024-02-28','2024-03-01','X'),
      ('h2','A','2024-03-01','2024-03-02','Y'),
      ('h3','B','2024-02-28','2024-03-02','X'),
      ('h4','B','2024-02-29','2024-03-01','Y'),
      ('h5','C','2024-02-28','2024-03-01','X'),
      ('h6','C','2024-02-28','2024-03-01','X')`);
    const records = (
      await db.rows(
        "SELECT record_id,user_id,starts::VARCHAR,ends::VARCHAR,target_id FROM history ORDER BY record_id",
      )
    ).map((row) => row.map(String));
    const overlaps = records.flatMap((a, i) =>
      records
        .slice(i + 1)
        .filter(
          (b) =>
            a[1] === b[1] && a[4] !== b[4] && a[2]! < b[3]! && b[2]! < a[3]!,
        )
        .map((b) => [a[0], b[0]]),
    );
    assert.deepEqual(overlaps, [["h3", "h4"]]);
    assert.deepEqual(
      await db.rows(`SELECT a.record_id,b.record_id FROM history a
      JOIN history b ON a.record_id<b.record_id AND a.user_id=b.user_id
      AND a.target_id<>b.target_id AND a.starts<b.ends AND b.starts<a.ends
      ORDER BY a.record_id,b.record_id`),
      overlaps,
    );
    // There are no B facts: conflict checks restricted to facts would hide h3/h4.
    await db.run("CREATE TABLE facts(fact_id VARCHAR,user_id VARCHAR,ds DATE)");
    await db.run(`INSERT INTO facts VALUES
      ('f1','A','2024-02-28'),('f2','A','2024-03-01'),
      ('f3','A','2024-03-02'),('f4','C','2024-02-29')`);
    const facts = (
      await db.rows(
        "SELECT fact_id,user_id,ds::VARCHAR FROM facts ORDER BY fact_id",
      )
    ).map((row) => row.map(String));
    const links = facts.flatMap((f) =>
      [
        ...new Set(
          records
            .filter((h) => h[1] === f[1] && h[2]! <= f[2]! && f[2]! < h[3]!)
            .map((h) => h[4]),
        ),
      ].map((target) => [f[0], target]),
    );
    const sqlLinks =
      await db.rows(`SELECT DISTINCT f.fact_id,h.target_id FROM facts f
      JOIN history h ON f.user_id=h.user_id AND h.starts<=f.ds AND f.ds<h.ends
      ORDER BY f.fact_id,h.target_id`);
    assert.deepEqual(sqlLinks, links);
    assert.deepEqual(links, [
      ["f1", "X"],
      ["f2", "Y"],
      ["f4", "X"],
    ]);
    const bag = await db.one(`SELECT count(*) FROM facts f JOIN history h
      ON f.user_id=h.user_id AND h.starts<=f.ds AND f.ds<h.ends`);
    assert.equal(bag[0], 4);
    const budget = (users: bigint, days: bigint) => ({
      states: String(users * days),
      transitions: String(users * (days - 1n)),
      combined: String(users * (2n * days - 1n)),
    });
    assert.deepEqual(budget(27n, 366n), {
      states: "9882",
      transitions: "9855",
      combined: "19737",
    });
    const sources = [
      "scripts/research_p1_time.ts",
      "docs/research/ossie-req-001/sources/apache-ossie/ontology/ontology.md",
      "docs/research/ossie-req-001/sources/apache-ossie/core-spec/expression_language.md",
    ];
    return {
      kind: "conditional-time-design-research",
      plugin_temporal_support_proven: false,
      business_policy_approved: false,
      engine,
      artifact_sha256: digest(artifact),
      source_hashes: Object.fromEntries(
        sources.map((p) => [
          relative(ROOT, join(ROOT, p)),
          digest(join(ROOT, p)),
        ]),
      ),
      truth_subsets: actual,
      false_null_bool_or_counterexample: boolOr.find((row) => row[0] === 6),
      full_history_conflicts_without_fact_witness: overlaps,
      illustrative_links_from_nonconflicting_fact_users: links,
      history_source_rows: records.length,
      matched_bag_rows: bag[0],
      set_links: links.length,
      budget_counterexample: budget(27n, 366n),
      limitations: [
        "Ordinary SQL plus independent enumerations, not native loader/binder/query acceptance",
        "Daily truth reduction and finite history conflict policy remain conditional on human decisions",
        "History contains a full-batch conflict; candidate loader would refuse it rather than return these illustrative links",
        "Distinct record IDs preserve identical interval records in this example; this does not approve a source identity contract",
        "No proof of execution memory, sweep algorithm, product scope, or runtime response limits",
      ],
    };
  } finally {
    db.close();
  }
}
if (import.meta.main) {
  assert(
    process.argv.length === 4,
    "Usage: research_p1_time.ts ARTIFACT OUTPUT",
  );
  writeFileSync(
    resolve(process.argv[3]!),
    JSON.stringify(await researchTime(resolve(process.argv[2]!)), null, 2) +
      "\n",
    { flag: "wx" },
  );
  console.log(
    "PASS conditional time SQL/enumeration research; no temporal plugin acceptance",
  );
}

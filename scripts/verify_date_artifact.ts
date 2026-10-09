/** Hand-derived Gregorian DATE controls; no JS Date or timestamp conversion. */
import { join, resolve, relative } from "node:path";
import { readFileSync, writeFileSync, readdirSync } from "node:fs";
import { DuckDBDateValue } from "@duckdb/node-api";
import {
  Database,
  ROOT,
  assert,
  digest,
  json,
  temporary,
  emitReport,
} from "./artifact_helpers.ts";
const FIX = join(ROOT, "test/fixtures/population-dates");
interface Core {
  name: string;
  datasets: {
    fields: {
      name: string;
      datatype: string;
      expression: { dialects: { expression: string }[] };
    }[];
  }[];
}
interface Ontology {
  name: string;
  ontology: { derived_by?: string[] }[];
}
type Docs = [Core, Ontology, { name: string }];
function definitions(): Docs {
  return [
    json<Core>(join(FIX, "core.json")),
    json<Ontology>(join(FIX, "ontology.json")),
    json<{ name: string }>(join(FIX, "mapping.json")),
  ];
}
function files(directory: string, docs: Docs) {
  const entries: Record<string, unknown> = {
    profile: "gaap-scalar-population-v1",
  };
  for (const [index, kind] of ["core", "ontology", "mapping"].entries()) {
    const path = join(directory, kind + ".json");
    writeFileSync(path, JSON.stringify(docs[index], null, 2) + "\n");
    entries[kind] = {
      path: kind + ".json",
      name: docs[index].name,
      sha256: digest(path),
    };
  }
  const path = join(directory, "bundle.json");
  writeFileSync(path, JSON.stringify(entries, null, 2) + "\n");
  return path;
}
export async function verify(artifact: string) {
  const con = await Database.open();
  try {
    const version = await con.loadArtifact(artifact);
    const rawInfinity = (
      await con.connection.runAndReadAll(
        "SELECT DATE 'infinity', DATE '-infinity'",
      )
    ).getRows()[0];
    const rawInfinityDisplay = rawInfinity.map((value) => {
      assert(value instanceof DuckDBDateValue);
      assert.equal(value.isFinite, false);
      return {
        days: value.days,
        is_finite: value.isFinite,
        client_to_string: value.toString(),
      };
    });
    const host = await con.one(
      "SELECT DATE '2024-02-29'+1, DATE '2024-03-01'-DATE '2024-02-28', typeof(DATE '2024-02-29'+1), typeof(DATE '2024-02-29'+INTERVAL 1 DAY), TRY_CAST('2023-02-29' AS DATE), CAST('2024-02-29 23:30:00-08' AS DATE)",
    );
    assert.deepEqual(host, [
      "2024-03-01",
      2,
      "DATE",
      "TIMESTAMP",
      null,
      "2024-02-29",
    ]);
    await con.run(readFileSync(join(FIX, "rows.sql"), "utf8"));
    const load = (path: string) => con.one("CALL ossie_bundle_load(?)", [path]);
    const baseline = await load(join(FIX, "bundle.json"));
    assert.equal(baseline[3], 11);
    const snapshot = () =>
      con.one(
        "SELECT snapshot_id, content_sha256 FROM ossie_concepts() WHERE concept='User'",
      );
    const ids = async (concept = "User", filters: string[] = []) =>
      (
        await con.rows(
          "SELECT entity_id FROM ossie_objects(?,?) ORDER BY entity_id",
          [concept, filters],
        )
      ).map((row) => String(row[0]));
    const query = (concept = "User", filters: string[] = []) =>
      con.one("SELECT * FROM ossie_concept_query(?,?,?,?)", [
        concept,
        ["record_count", "gaap_sum"],
        [],
        filters,
      ]);
    const feb =
      "User.date >= DATE '2024-02-01' AND User.date < DATE '2024-03-01'";
    assert.deepEqual(await ids("User", [feb]), [
      "feb_low",
      "feb_start",
      "leap_day",
    ]);
    assert.deepEqual(await query("User", [feb]), [3, "12.00"]);
    assert.deepEqual(await ids("ValidUser"), ["feb_start", "leap_day"]);
    assert.deepEqual(await query("ValidUser"), [2, "8.00"]);
    assert.deepEqual(
      await ids("ValidUser", ["User.date >= DATE '2024-03-01'"]),
      [],
    );
    assert.deepEqual(
      await query("ValidUser", ["User.date >= DATE '2024-03-01'"]),
      [0, null],
    );
    assert.deepEqual(
      await ids("User", [
        "User.date BETWEEN DATE '2024-02-01' AND DATE '2024-03-01'",
      ]),
      ["feb_low", "feb_start", "leap_day", "march_start"],
    );
    assert.deepEqual(
      await ids("User", [
        "User.date >= DATE '2023-12-31' AND User.date < DATE '2024-01-01'",
      ]),
      ["year_end"],
    );
    assert.deepEqual(await ids("User", ["User.date = DATE '2024-01-01'"]), [
      "year_start",
    ]);
    assert.deepEqual(await ids("User", ["User.date = DATE '2025-02-28'"]), [
      "nonleap_feb",
    ]);
    for (const [lower, upper, count, sum] of [
      ["2024-01-01", "2024-02-01", 1, "2.00"],
      ["2024-02-01", "2024-03-01", 3, "12.00"],
      ["2024-03-01", "2024-04-01", 1, "6.00"],
    ] as const)
      assert.deepEqual(
        await query("User", [
          `User.date >= DATE '${lower}' AND User.date < DATE '${upper}'`,
        ]),
        [count, sum],
      );
    const grouped = await con.rows(
      "SELECT * FROM ossie_concept_query(?,?,?,?) ORDER BY 1",
      ["ValidUser", ["record_count", "gaap_sum"], ["dws_gaap_di.ds"], []],
    );
    assert.deepEqual(grouped, [
      ["2024-02-01", 1, "3.00"],
      ["2024-02-29", 1, "5.00"],
    ]);
    assert.deepEqual(await ids("User", ["User.date IS NULL"]), ["null_date"]);
    assert.deepEqual(await ids("User", ["User.date = NULL"]), []);
    assert.deepEqual(await ids("User", ["User.date > DATE '2025-12-31'"]), [
      "positive_infinity",
    ]);
    assert.deepEqual(await ids("User", ["User.date < DATE '2023-01-01'"]), [
      "negative_infinity",
    ]);
    const nativeDates = await con.rows(
      'SELECT entity_id, "dws_gaap_di.ds" FROM ossie_objects(?,?) ORDER BY entity_id',
      [
        "User",
        [
          "User.date = DATE 'infinity' OR User.date = DATE '-infinity' OR User.date IS NULL",
        ],
      ],
    );
    assert.deepEqual(nativeDates, [
      ["negative_infinity", "-infinity"],
      ["null_date", null],
      ["positive_infinity", "infinity"],
    ]);
    assert.deepEqual(await ids("User", ["User.date + 1 = DATE '2024-03-01'"]), [
      "leap_day",
    ]);
    assert.deepEqual(await ids("User", ["User.date - DATE '2024-02-28' = 2"]), [
      "march_start",
    ]);
    assert.deepEqual(
      await ids("User", ["User.date = CAST('2024-02-29' AS DATE)"]),
      ["leap_day"],
    );
    assert.deepEqual(await ids("User", ["User.date = '2024-02-29'"]), [
      "leap_day",
    ]);
    assert.deepEqual(
      await ids("User", ["User.date = TRY_CAST('2023-02-29' AS DATE)"]),
      [],
    );
    // DuckDB accepts a date prefix; this is not conversion of the offset instant.
    assert.deepEqual(
      await ids("User", ["User.date = CAST('2024-02-29 23:30:00-08' AS DATE)"]),
      ["leap_day"],
    );
    assert.deepEqual(await ids("User", ["1 + User.date = DATE '2024-03-01'"]), [
      "leap_day",
    ]);
    assert.deepEqual(await ids("User", ["User.date - 1 = DATE '2024-02-29'"]), [
      "march_start",
    ]);
    for (const text of [
      "2024/02/29",
      "2024-02-29garbage",
      "2024-02-29 23:30:00-12:00",
    ]) {
      for (const operand of [
        `CAST('${text}' AS DATE)`,
        `'${text}'`,
        `TRY_CAST('${text}' AS DATE)`,
      ])
        assert.deepEqual(await ids("User", [`User.date = ${operand}`]), [
          "leap_day",
        ]);
    }
    assert.deepEqual(await con.one("SELECT CAST('epoch' AS DATE)"), [
      "1970-01-01",
    ]);
    for (const timezone of ["Pacific/Honolulu", "Asia/Shanghai"]) {
      await con.run(`SET TimeZone = '${timezone}'`);
      assert.deepEqual(await con.one("SELECT current_setting('TimeZone')"), [
        timezone,
      ]);
      assert.deepEqual(await ids("User", [feb]), [
        "feb_low",
        "feb_start",
        "leap_day",
      ]);
      assert.deepEqual(await query("User", [feb]), [3, "12.00"]);
      assert.deepEqual(
        await ids("User", ["User.date = '2024-02-29 23:30:00-12:00'"]),
        ["leap_day"],
      );
      assert.deepEqual(await query("ValidUser"), [2, "8.00"]);
    }
    await con.run("SET TimeZone = 'UTC'");
    const rejected: string[] = [];
    for (const [name, filter, reason] of [
      [
        "numeric_interval_constructor",
        "User.date + INTERVAL 1 DAY = DATE '2024-03-01'",
        "unsupported function",
      ],
      [
        "quoted_interval_constructor",
        "User.date + INTERVAL '1 day' = DATE '2024-03-01'",
        "unsupported cast",
      ],
      ["invalid_calendar", "User.date = CAST('2023-02-29' AS DATE)", "date"],
      ["invalid_month", "User.date = CAST('2024-13-01' AS DATE)", "date"],
      ["current_date", "User.date = CURRENT_DATE", "qualified"],
      ["current_date_call", "User.date = CURRENT_DATE()", "unsupported"],
      ["relative_today", "User.date = CAST('today' AS DATE)", "date"],
      ["relative_now", "User.date = CAST('now' AS DATE)", "date"],
      [
        "timestamp_literal",
        "User.date = TIMESTAMP '2024-02-29 00:00:00'",
        "unsupported cast",
      ],
      ["time_literal", "TIME '12:00:00' IS NOT NULL", "unsupported cast"],
      [
        "nested_timestamp_cast",
        "User.date = CAST(CAST('2024-02-29 00:00:00' AS TIMESTAMP) AS DATE)",
        "unsupported cast",
      ],
      [
        "timestamp",
        "User.date = CAST('2024-02-29 00:00:00' AS TIMESTAMP)",
        "unsupported cast",
      ],
      ["time", "CAST('12:00:00' AS TIME) IS NOT NULL", "unsupported cast"],
      [
        "timezone_timestamp",
        "CAST('2024-02-29 00:00:00+00' AS TIMESTAMPTZ) IS NOT NULL",
        "unsupported cast",
      ],
      [
        "date_trunc",
        "DATE_TRUNC('month',User.date) IS NOT NULL",
        "unsupported",
      ],
      ["numeric_date", "CAST(1 AS DATE) = User.date", "cast"],
    ]) {
      await assert.rejects(ids("User", [filter]), (error: Error) => {
        assert(
          error.message.toLowerCase().includes(reason),
          `${name}: ${error.message}`,
        );
        return true;
      });
      assert.deepEqual(await ids("ValidUser"), ["feb_start", "leap_day"]);
      assert.deepEqual(await snapshot(), baseline.slice(1, 3));
      rejected.push(name);
    }
    await temporary("ossie-date-", async (directory) => {
      const invalid = definitions();
      invalid[0].datasets[0].fields.find(
        (field) => field.name === "ds",
      )!.expression.dialects[0].expression = "CAST(product AS DATE)";
      await assert.rejects(load(files(directory, invalid)), (error: Error) => {
        assert(error.message.includes("missing_capability"));
        return true;
      });
      assert.deepEqual(await ids("ValidUser"), ["feb_start", "leap_day"]);
      assert.deepEqual(await snapshot(), baseline.slice(1, 3));
      rejected.push("invalid_field_cast_atomicity");
      const safe = definitions();
      safe[0].datasets[0].fields.find(
        (field) => field.name === "ds",
      )!.expression.dialects[0].expression = "TRY_CAST(product AS DATE)";
      const safeBatch = await load(files(directory, safe));
      assert.equal((await ids("User", ["User.date IS NULL"])).length, 11);
      assert.deepEqual(await query("ValidUser"), [0, null]);
      const interval = definitions();
      interval[0].datasets[0].fields.find(
        (field) => field.name === "ds",
      )!.expression.dialects[0].expression = "ds + INTERVAL 1 DAY";
      await assert.rejects(load(files(directory, interval)), (error: Error) => {
        assert(error.message.includes("unsupported function"));
        return true;
      });
      assert.deepEqual(await query("ValidUser"), [0, null]);
      rejected.push("interval_date_field");
      assert.deepEqual(await snapshot(), safeBatch.slice(1, 3));
      const shifted = definitions();
      shifted[0].datasets[0].fields.find(
        (field) => field.name === "ds",
      )!.expression.dialects[0].expression = "ds + 1";
      await load(files(directory, shifted));
      assert.deepEqual(await ids("User", ["User.date = DATE '2024-03-01'"]), [
        "leap_day",
      ]);
    });
    const restored = await load(join(FIX, "bundle.json"));
    assert.equal(restored[2], baseline[2]);
    for (const type of ["TIME", "TIMESTAMP", "TIMESTAMPTZ"]) {
      await con.run(
        `ALTER TABLE dws_gaap_di ADD COLUMN unsupported_temporal ${type}`,
      );
      await assert.rejects(load(join(FIX, "bundle.json")), (error: Error) => {
        assert(error.message.includes("unsupported physical column type"));
        return true;
      });
      assert.deepEqual(await ids("ValidUser"), ["feb_start", "leap_day"]);
      assert.deepEqual(await snapshot(), restored.slice(1, 3));
      await con.run("ALTER TABLE dws_gaap_di DROP COLUMN unsupported_temporal");
      rejected.push(`physical_${type.toLowerCase()}`);
    }
    const compiled = await con.one(
      "SELECT * FROM ossie_concept_compile(?,?,?,?)",
      ["User", ["record_count"], [], [feb]],
    );
    assert.equal(compiled[2], restored[1]);
    assert.equal(compiled[3], restored[2]);
    assert.equal(compiled.at(-1), false);
    const report = {
      scope:
        "Existing population DATE boundary and conversion controls; no timestamp/timezone execution expansion",
      runtime: `Bun ${Bun.version}`,
      duckdb_version: version[0],
      duckdb_source_id: version[1],
      artifact_sha256: digest(resolve(artifact)),
      script_sha256: digest(import.meta.path),
      helper_sha256: digest(join(ROOT, "scripts/artifact_helpers.ts")),
      rows: 11,
      host_boundary: host,
      half_open_calendar_hand_derived: true,
      between_includes_upper: true,
      host_interval_timestamp_does_not_prove_profile_acceptance: true,
      object_metric_population_matches: true,
      sparse_grouping: true,
      null_and_infinity_native: true,
      raw_client_infinity_display: rawInfinityDisplay,
      string_prefix_is_not_instant_conversion: true,
      timezone_configurations_checked: ["Pacific/Honolulu", "Asia/Shanghai"],
      cast_failure_try_cast_null: true,
      failed_load_preserves_snapshot: true,
      rejected_controls: rejected,
      inputs_sha256: Object.fromEntries(
        readdirSync(FIX)
          .sort()
          .map((n) => [relative(ROOT, join(FIX, n)), digest(join(FIX, n))]),
      ),
      population_source_sha256: digest(
        join(ROOT, "src/population/population.cpp"),
      ),
    };
    emitReport(report);
    return report;
  } finally {
    con.close();
  }
}
if (import.meta.main) {
  assert(process.argv[2], "artifact required");
  await verify(process.argv[2]);
}

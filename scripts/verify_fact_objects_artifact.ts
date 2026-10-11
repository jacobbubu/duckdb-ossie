/** Actual production fact API execution and strict request/resource refusals; no model calls. */
import { readFileSync, writeFileSync, mkdirSync, existsSync } from "node:fs";
import { join, resolve } from "node:path";
import {
  Database,
  ROOT,
  assert,
  digest,
  sqlString,
} from "./artifact_helpers.ts";
import { populationError } from "./population_error.ts";
const [artifactArg, outArg] = process.argv.slice(2);
assert(
  artifactArg && outArg && process.argv.length === 4,
  "ARTIFACT NEW_OUTPUT_DIRECTORY",
);
const artifact = resolve(artifactArg),
  out = resolve(outArg);
assert(!existsSync(out), "fresh directory");
mkdirSync(out, { recursive: true });
const fixture = join(ROOT, "test/fixtures/population-fact-program"),
  db = await Database.open();
const checks: string[] = [],
  refusals: unknown[] = [];
const all = {
  domain: "common",
  input_scope: "entire_capture",
  metrics: [
    "balance",
    "income",
    "expense",
    "revenue_rows",
    "revenue_values",
    "cost_rows",
    "cost_values",
  ],
  cohort: { operation: "all" },
};
const win = {
  input_scope: "entire_capture",
  windows: ["previous", "rolling"],
  display: { mode: "all" },
};
async function call(fn: string, request: unknown = undefined): Promise<any> {
  const rows = await db.rows(
    "SELECT * FROM " + fn + (request === undefined ? "()" : "(?)"),
    request === undefined
      ? []
      : [typeof request === "string" ? request : JSON.stringify(request)],
  );
  assert.equal(rows.length, 1);
  assert.equal(rows[0].length, 1);
  assert.equal(typeof rows[0][0], "string");
  const bytes = rows[0][0] as string;
  assert(Buffer.byteLength(bytes) <= 1048576);
  return JSON.parse(bytes);
}
async function users(request: unknown = all) {
  return call("ossie_fact_users_json", request);
}
async function windows(request: unknown = win) {
  return call("ossie_fact_windows_json", request);
}
async function load() {
  await db.run("CALL ossie_bundle_load(?)", [join(fixture, "bundle.json")]);
}
async function reject(
  name: string,
  fn: string,
  request: unknown,
  stage?: string,
) {
  let caught = false;
  try {
    await call(fn, request);
  } catch (e) {
    const envelope = populationError(
      e instanceof Error ? e.message : String(e),
    );
    assert(envelope, "structured error");
    if (stage) assert.equal(envelope.stage, stage);
    refusals.push({ name, error: envelope });
    caught = true;
  }
  assert(caught, "expected refusal " + name);
}
const pred = (predicate: string) => ({ operation: "predicate", predicate });
const set = (operation: string, left: unknown, right: unknown) => ({
  operation,
  children: [left, right],
});
const oracle = [
  ["A", "290.00", "375.00", "85.00", "4", "4", "6", "6"],
  ["B", "50.00", "80.00", "30.00", "2", "2", "2", "2"],
  ["C", null, null, null, "1", "0", "1", "0"],
];
try {
  await db.run("LOAD " + sqlString(artifact));
  await db.run(readFileSync(join(fixture, "rows.sql"), "utf8"));
  await load();
  const initial = await users();
  assert.deepEqual(initial.rows, oracle);
  assert.equal(initial.common_user_count, "3");
  assert.equal(initial.selected_user_count, "3");
  assert.equal(initial.row_count, "3");
  checks.push(
    "independent integer-cents and complete User/count oracle A/B/C; D/E excluded",
  );
  assert.deepEqual(
    initial.columns.map((c: any) => c.bound_type),
    [
      "VARCHAR",
      "DECIMAL(38,2)",
      "DECIMAL(38,2)",
      "DECIMAL(38,2)",
      "BIGINT",
      "BIGINT",
      "BIGINT",
      "BIGINT",
    ],
  );
  assert.equal(initial.columns[1].unit, "currency:USD:scale2");
  checks.push(
    "actual output types and USD scale2 units; NULL vs zero preserved",
  );
  const empty = await users({ ...all, cohort: pred("income>1000000") });
  assert.deepEqual(empty.rows, []);
  assert.equal(empty.common_user_count, "3");
  assert.equal(empty.selected_user_count, "0");
  checks.push(
    "empty selected cohort still carries actual pre-cohort common denominator3",
  );
  const unprojected = await users({
    ...all,
    metrics: ["BALANCE"],
    cohort: pred("expense>50"),
  });
  assert.deepEqual(unprojected.rows, [["A", "290.00"]]);
  assert.deepEqual(unprojected.selected, ["balance"]);
  checks.push(
    "condition may use unprojected metric; case-insensitive selection returns canonical column name",
  );
  for (const [op, expected] of [
    ["intersection", ["A"]],
    ["union", ["A", "B"]],
    ["difference", ["B"]],
  ] as const) {
    const r = await users({
      ...all,
      metrics: ["income"],
      cohort: set(op, pred("income>0"), pred("expense>50")),
    });
    assert.deepEqual(
      r.rows.map((x: any) => x[0]),
      expected,
    );
    assert.equal(r.common_user_count, "3");
  }
  checks.push(
    "complete intersection/union/ordered difference identities within row-presence common domain",
  );
  const w = await windows({
    ...win,
    display: {
      mode: "date_range",
      start_date: "2026-02-01",
      end_date: "2026-02-01",
    },
  });
  assert.deepEqual(w.rows, [
    ["A", "2026-02-01", "200.00", "250.00"],
    ["B", "2026-02-01", "80.00", "80.00"],
    ["C", "2026-02-01", null, null],
    ["D", "2026-02-01", null, "40.00"],
  ]);
  assert.equal(w.source_captured_rows, "8");
  assert.deepEqual(
    w.columns.map((c: any) => c.bound_type),
    ["VARCHAR", "DATE", "DECIMAL(18,2)", "DECIMAL(38,2)"],
  );
  checks.push(
    "whole capture sparse-row windows computed before display; noncommon D remains, NULL and actual precisions preserved",
  );
  const whole = await windows();
  assert.equal(whole.rows.length, 8);
  assert.deepEqual(whole.source_identity_fields, ["user_id", "ds"]);
  checks.push(
    "window scope is single fact complete8 rows rather than common3 Users",
  );
  const discovery = await call("ossie_fact_program");
  assert.equal(discovery.query_executed, false);
  assert.equal(discovery.counts_computed, false);
  assert.equal(discovery.mcp_registered, false);
  assert.equal(discovery.functions.length, 5);
  assert.deepEqual(
    discovery.sources.map((s: any) => s.captured_rows),
    ["8", "10"],
  );
  assert.deepEqual(
    discovery.sources.map((s: any) => s.identity_fields),
    [
      ["user_id", "ds"],
      ["user_id", "ds", "product"],
    ],
  );
  assert.equal(discovery.metrics.length, 7);
  assert.equal(discovery.windows.length, 2);
  assert.equal(discovery.limits.regular_metrics, 32);
  checks.push(
    "discovery declares both complete grains/actual types/original definitions and honest limits without computed common counts",
  );
  for (const [fn, req] of [
    ["ossie_fact_users_compile", all],
    ["ossie_fact_windows_compile", win],
  ] as const) {
    const p = await call(fn, req);
    assert.equal(p.executed, false);
    assert.equal(p.members_computed, false);
    assert.equal(p.counts_computed, false);
    assert.equal(p.sql_replayable, false);
    assert.equal(p.physical_plan, false);
    for (const key of [
      "rows",
      "row_count",
      "common_user_count",
      "selected_user_count",
    ])
      assert(!(key in p));
    assert.equal(p.content_sha256, initial.content_sha256);
    if (p.common_count_sql) {
      assert(!/sum|income|expense|balance/i.test(p.common_count_sql));
      assert(/count_star\(\)/i.test(p.common_count_sql));
    }
  }
  checks.push(
    "compile has actual typed schema but no computed counts/members; denominator SQL only distinct User reductions",
  );
  for (const [name, req] of [
    ["missing explicit scope", { ...all, input_scope: undefined }],
    ["wrong scope", { ...all, input_scope: "filtered" }],
    ["wrong domain", { ...all, domain: "union" }],
    ["missing cohort", { ...all, cohort: undefined }],
    ["unknown key", { ...all, filters: [] }],
    ["empty metrics", { ...all, metrics: [] }],
    ["nonstring selection", { ...all, metrics: [1] }],
    ["duplicate case name", { ...all, metrics: ["income", "INCOME"] }],
    ["unknown name", { ...all, metrics: ["missing"] }],
    ["window as regular", { ...all, metrics: ["previous"] }],
    ["name budget", { ...all, metrics: ["x".repeat(129)] }],
    [
      "all with predicate",
      { ...all, cohort: { operation: "all", predicate: "income>0" } },
    ],
    [
      "unary difference",
      {
        ...all,
        cohort: { operation: "difference", children: [pred("income>0")] },
      },
    ],
    [
      "ternary union",
      {
        ...all,
        cohort: {
          operation: "union",
          children: [pred("income>0"), pred("income>0"), pred("income>0")],
        },
      },
    ],
    [
      "predicate with children",
      { ...all, cohort: { ...pred("income>0"), children: [] } },
    ],
    [
      "unknown operation",
      { ...all, cohort: { operation: "not", children: [pred("income>0")] } },
    ],
    ["expression not comparison", { ...all, cohort: pred("income") }],
    ["arbitrary function", { ...all, cohort: pred("abs(income)>0") }],
    ["cross metric comparison", { ...all, cohort: pred("income>expense") }],
    ["NULL comparison", { ...all, cohort: pred("income>NULL") }],
    ["float comparison", { ...all, cohort: pred("income>1e0") }],
    ["scale3 comparison", { ...all, cohort: pred("income>0.001") }],
    ["decimal count comparison", { ...all, cohort: pred("revenue_rows>1.5") }],
    ["unknown predicate metric", { ...all, cohort: pred("missing>0") }],
    ["window predicate", { ...all, cohort: pred("previous>0") }],
    ["sql statement injection", { ...all, cohort: pred("income>0;SELECT 1") }],
    ["NUL predicate", { ...all, cohort: pred("income>0\u0000") }],
    ["comparison alias", { ...all, cohort: pred("income>0 AS hidden") }],
  ] as [string, unknown][]) {
    await reject(name, "ossie_fact_users_json", req);
  }
  for (const [name, raw] of [
    [
      "duplicate root key",
      '{"domain":"common","domain":"common","input_scope":"entire_capture","metrics":["income"],"cohort":{"operation":"all"}}',
    ],
    [
      "escaped duplicate key",
      '{"domain":"common","input_scope":"entire_capture","metrics":["income"],"cohort":{"operation":"all","operatio\\u006e":"all"}}',
    ],
    ["non-object", "[]"],
    ["YAML refusal", "domain: common"],
  ])
    await reject(name, "ossie_fact_users_json", raw);
  let deep: any = { operation: "all" };
  for (let i = 0; i < 16; i++)
    deep = set("difference", deep, { operation: "all" });
  await reject(
    "cohort depth17",
    "ossie_fact_users_json",
    { ...all, cohort: deep },
    "resource",
  );
  function tree(level: number): any {
    return level === 0
      ? { operation: "all" }
      : set("union", tree(level - 1), tree(level - 1));
  }
  await reject(
    "cohort127 nodes",
    "ossie_fact_users_json",
    { ...all, cohort: tree(6) },
    "resource",
  );
  const exact =
    JSON.stringify(all) +
    " ".repeat(16384 - Buffer.byteLength(JSON.stringify(all)));
  assert.equal(Buffer.byteLength(exact), 16384);
  assert.deepEqual((await users(exact)).rows, oracle);
  await reject(
    "request16385 bytes",
    "ossie_fact_users_json",
    exact + " ",
    "resource",
  );
  checks.push(
    "exact16384-byte strict JSON request accepted;16385 refused before parsing/copying",
  );
  for (const [name, req] of [
    ["missing display", { ...win, display: undefined }],
    ["wrong input scope", { ...win, input_scope: "common" }],
    ["regular as window", { ...win, windows: ["income"] }],
    ["duplicate windows", { ...win, windows: ["previous", "PREVIOUS"] }],
    [
      "all with dates",
      { ...win, display: { mode: "all", start_date: "2026-01-01" } },
    ],
    [
      "range missing end",
      { ...win, display: { mode: "date_range", start_date: "2026-01-01" } },
    ],
    [
      "infinite date",
      {
        ...win,
        display: {
          mode: "date_range",
          start_date: "-infinity",
          end_date: "2026-01-01",
        },
      },
    ],
    [
      "noncanonical date",
      {
        ...win,
        display: {
          mode: "date_range",
          start_date: "2026-1-1",
          end_date: "2026-01-01",
        },
      },
    ],
    [
      "reversed date",
      {
        ...win,
        display: {
          mode: "date_range",
          start_date: "2026-02-01",
          end_date: "2026-01-01",
        },
      },
    ],
    ["implicit all", { ...win, display: {} }],
  ] as [string, unknown][])
    await reject(name, "ossie_fact_windows_json", req);
  await db.run(
    'CREATE MACRO count_star() AS 42::BIGINT;CREATE MACRO "-"(a,b) AS 99::DECIMAL(38,2);CREATE MACRO "+"(a,b) AS 98::DECIMAL(38,2);CREATE MACRO sum(x) AS 97::DECIMAL(38,2);CREATE MACRO count(x) AS 96::BIGINT',
  );
  const macroMany = await users();
  assert.deepEqual(macroMany.rows, oracle);
  assert.equal(macroMany.common_user_count, "3");
  await db.run(
    "DELETE FROM revenue;DELETE FROM cost;INSERT INTO revenue VALUES('A','2026-01-01',100);INSERT INTO cost VALUES('A','2026-01-01','X',10)",
  );
  await load();
  const macroOne = await users();
  assert.deepEqual(macroOne.rows, [
    ["A", "90.00", "100.00", "10.00", "1", "1", "1", "1"],
  ]);
  assert.equal(macroOne.common_user_count, "1");
  assert.deepEqual((await windows()).rows, [
    ["A", "2026-01-01", null, "100.00"],
  ]);
  await db.run("DELETE FROM cost");
  await load();
  const macroEmpty = await users();
  assert.deepEqual(macroEmpty.rows, []);
  assert.equal(macroEmpty.common_user_count, "0");
  checks.push(
    "same-name count_star/SUM/COUNT/arithmetic macros cannot change multiple/single/empty common counts or metric/window values",
  );
  await db.run(
    'DROP MACRO count_star;DROP MACRO "-";DROP MACRO "+";DROP MACRO sum;DROP MACRO count;DROP TABLE revenue;DROP TABLE cost',
  );
  await db.run(readFileSync(join(fixture, "rows.sql"), "utf8"));
  await load();
  const swappedManifest = JSON.parse(
    readFileSync(join(fixture, "bundle.json"), "utf8"),
  );
  swappedManifest.program.facts.reverse();
  for (const kind of ["core", "ontology", "mapping"])
    swappedManifest[kind].path = join(fixture, kind + ".json");
  const swappedPath = join(out, "swapped-bundle.json");
  writeFileSync(swappedPath, JSON.stringify(swappedManifest) + "\n");
  await db.run("CALL ossie_bundle_load(?)", [swappedPath]);
  const swapped = await call("ossie_fact_program");
  assert.deepEqual(
    swapped.sources.map((s: any) => [
      s.dataset,
      s.source,
      s.captured_rows,
      s.identity_fields,
    ]),
    [
      ["cost", "memory.main.cost", "10", ["user_id", "ds", "product"]],
      ["revenue", "memory.main.revenue", "8", ["user_id", "ds"]],
    ],
  );
  const oldByDataset = new Map(
    discovery.sources.map((s: any) => [s.dataset, s]),
  );
  for (const s of swapped.sources) {
    const original: any = oldByDataset.get(s.dataset);
    assert.equal(s.content_sha256, original.content_sha256);
    assert.deepEqual(s.fields, original.fields);
  }
  assert.deepEqual(
    swapped.windows.map((w: any) => w.dataset),
    ["revenue", "revenue"],
  );
  assert.deepEqual((await users()).rows, oracle);
  assert.deepEqual((await windows()).rows, whole.rows);
  checks.push(
    "swapped fact order preserves dataset/source/child hash/rows/fields/window source and both complete execution oracles",
  );
  await load();
  await db.run(
    "INSERT INTO revenue VALUES('N','2026-02-01',150);INSERT INTO cost VALUES('N','2026-02-01','X',NULL)",
  );
  assert.deepEqual((await users()).rows, oracle);
  await load();
  const diff = await users({
    ...all,
    metrics: ["income"],
    cohort: set("difference", pred("income>0"), pred("expense>0")),
  });
  assert.deepEqual(diff.rows, [["N", "150.00"]]);
  assert.equal(diff.common_user_count, "4");
  checks.push(
    "explicit reload and UNKNOWN RHS difference retain N rather than nullable SQL NOT dropping it",
  );
  await db.run(
    "INSERT INTO revenue VALUES('a','2026-01-01',1);INSERT INTO cost VALUES('a','2026-01-01','X',1);SET default_collation='nocase';SET default_order='DESC'",
  );
  await load();
  const raw = await users();
  assert.deepEqual(
    raw.rows.map((r: any) => r[0]),
    ["A", "B", "C", "N", "a"],
  );
  checks.push(
    "public private binary Engine preserves A/a under caller nocase and descending defaults",
  );
  await db.run("DROP TABLE revenue;DROP TABLE cost");
  assert.equal((await users()).common_user_count, "5");
  assert.equal((await windows()).rows.length, 10);
  checks.push(
    "public current frozen queries survive deletion of original source tables",
  );
  await db.run(readFileSync(join(fixture, "rows.sql"), "utf8"));
  await db.run("DELETE FROM revenue;DELETE FROM cost");
  await load();
  const e = await users();
  assert.deepEqual(e.rows, []);
  assert.equal(e.common_user_count, "0");
  assert.equal(e.columns[1].bound_type, "DECIMAL(38,2)");
  assert.deepEqual((await windows()).rows, []);
  checks.push(
    "empty dual captures preserve exact columns and zero domain/window outputs",
  );
  await db.run(
    "INSERT INTO revenue VALUES('BC',DATE '0001-01-01 (BC)',1);INSERT INTO cost VALUES('BC',DATE '0001-01-01 (BC)','X',1)",
  );
  await load();
  const bc = await windows({
    ...win,
    display: {
      mode: "date_range",
      start_date: "0001-01-01 (BC)",
      end_date: "0001-01-01 (BC)",
    },
  });
  assert.deepEqual(bc.rows, [["BC", "0001-01-01 (BC)", null, "1.00"]]);
  checks.push("canonical BC DATE display and lossless output round-trip");
  await db.run(
    "DELETE FROM revenue;DELETE FROM cost;INSERT INTO revenue VALUES('wide',DATE '5877642-06-25 (BC)',1),('wide',DATE '10000-01-01',2),('wide',DATE '5881580-07-10',3);INSERT INTO cost SELECT who,day,'X',value FROM revenue",
  );
  await load();
  const wide = await windows({
    ...win,
    display: {
      mode: "date_range",
      start_date: "5877642-06-25 (BC)",
      end_date: "5881580-07-10",
    },
  });
  assert.deepEqual(wide.rows, [
    ["wide", "5877642-06-25 (BC)", null, "1.00"],
    ["wide", "10000-01-01", "1.00", "3.00"],
    ["wide", "5881580-07-10", "2.00", "5.00"],
  ]);
  checks.push(
    "canonical min/max finite DATE and year10000 preserve chronological window/display order",
  );
  assert.equal(
    (await users({ ...all, cohort: tree(5) })).selected_user_count,
    "1",
  );
  let depth16: any = { operation: "all" };
  for (let i = 0; i < 15; i++)
    depth16 = set("union", depth16, { operation: "all" });
  assert.equal(
    (await users({ ...all, cohort: depth16 })).selected_user_count,
    "1",
  );
  await reject(
    "cohort exact65 nodes",
    "ossie_fact_users_json",
    { ...all, cohort: set("union", tree(5), { operation: "all" }) },
    "resource",
  );
  checks.push(
    "cohort63 nodes and depth16 accepted;65 nodes and depth17 refused",
  );
  await db.run(
    "DELETE FROM revenue;DELETE FROM cost;INSERT INTO revenue VALUES('x','2026-01-01',1);INSERT INTO cost VALUES('x','2026-01-01','X',1)",
  );
  await load();
  const sizeRequest = { ...all, metrics: ["income"] };
  const seedText = (
    await db.one("SELECT * FROM ossie_fact_users_json(?)", [
      JSON.stringify(sizeRequest),
    ])
  )[0] as string;
  const seed = JSON.parse(seedText);
  const oldSeq = Number(seed.snapshot_id.split(":").at(-1));
  const nextSnapshotBytes = String(oldSeq + 1).length - String(oldSeq).length;
  const exactUserLength =
    1048576 - Buffer.byteLength(seedText) + 1 - nextSnapshotBytes;
  await db.run(
    `UPDATE revenue SET who=repeat('x',${exactUserLength});UPDATE cost SET who=repeat('x',${exactUserLength})`,
  );
  await load();
  const exactText = (
    await db.one("SELECT * FROM ossie_fact_users_json(?)", [
      JSON.stringify(sizeRequest),
    ])
  )[0] as string;
  assert.equal(Buffer.byteLength(exactText), 1048576);
  assert.equal(JSON.parse(exactText).rows[0][0].length, exactUserLength);
  await db.run("UPDATE revenue SET who=who||'x';UPDATE cost SET who=who||'x'");
  await load();
  await reject(
    "complete JSON exact1MiB plus1 byte",
    "ossie_fact_users_json",
    sizeRequest,
    "resource",
  );
  checks.push(
    "complete user JSON exact1048576 bytes accepted;1048577 bytes refused without truncation",
  );
  await db.run(
    "DELETE FROM revenue;DELETE FROM cost;INSERT INTO revenue VALUES(repeat('x',550000),'2026-01-01',1);INSERT INTO cost VALUES(repeat('x',550000),'2026-01-01','X',1)",
  );
  await load();
  const large = await users({ ...all, metrics: ["income"] });
  assert.equal(large.rows[0][0].length, 550000);
  checks.push(
    "complete long raw User encoded without truncation while under1MiB",
  );
  await db.run(
    "UPDATE revenue SET who=repeat(chr(92),550000);UPDATE cost SET who=repeat(chr(92),550000)",
  );
  await load();
  await reject(
    "escaped identity complete JSON over1MiB",
    "ossie_fact_users_json",
    { ...all, metrics: ["income"] },
    "resource",
  );
  await reject(
    "escaped window identity over1MiB",
    "ossie_fact_windows_json",
    win,
    "resource",
  );
  checks.push(
    "JSON escaping contributes to byte budget and refuses both complete responses",
  );
  await db.run(
    "DELETE FROM revenue;DELETE FROM cost;INSERT INTO revenue SELECT 'u'||lpad(i::VARCHAR,5,'0'),'2026-01-01',1 FROM range(10000)t(i);INSERT INTO cost SELECT who,day,'X',value FROM revenue",
  );
  await load();
  const full = await users({ ...all, metrics: ["income"] });
  assert.equal(full.rows.length, 10000);
  assert.equal(full.common_user_count, "10000");
  assert.deepEqual(full.rows[0], ["u00000", "1.00"]);
  assert.deepEqual(full.rows[9999], ["u09999", "1.00"]);
  const wf = await windows();
  assert.equal(wf.rows.length, 10000);
  checks.push(
    "complete10000 Users/windows returned with first/last identities and correct pre-cohort denominator",
  );
  await db.run("DELETE FROM revenue;DELETE FROM cost");
  const definitions = Object.fromEntries(
    ["core", "ontology", "mapping"].map((kind) => [
      kind,
      JSON.parse(readFileSync(join(fixture, kind + ".json"), "utf8")),
    ]),
  );
  const baseManifest = JSON.parse(
    readFileSync(join(fixture, "bundle.json"), "utf8"),
  );
  const regular = definitions.core.metrics.filter(
    (m: any) => !["previous", "rolling"].includes(m.name),
  );
  const windowDefs = definitions.core.metrics.filter((m: any) =>
    ["previous", "rolling"].includes(m.name),
  );
  while (regular.length < 32)
    regular.push({
      name:
        regular.length === 31 ? "m" + "x".repeat(127) : "m" + regular.length,
      datatype: "Decimal",
      expression: {
        dialects: [
          { dialect: "OSSIE_SQL_2026", expression: "SUM(revenue.amount)" },
        ],
      },
    });
  while (windowDefs.length < 16)
    windowDefs.push({
      name: "w" + windowDefs.length,
      datatype: "Decimal",
      expression: {
        dialects: [
          {
            dialect: "OSSIE_SQL_2026",
            expression:
              "SUM(revenue.amount) OVER(PARTITION BY revenue.user_id ORDER BY revenue.ds ROWS BETWEEN 0 PRECEDING AND CURRENT ROW)",
          },
        ],
      },
    });
  async function variant(metrics: any[], windows: string[], label: string) {
    const docs = structuredClone(definitions);
    docs.core.metrics = metrics;
    const manifest = structuredClone(baseManifest);
    manifest.program.windows = windows;
    for (const kind of ["core", "ontology", "mapping"]) {
      const path = join(out, label + "-" + kind + ".json");
      writeFileSync(path, JSON.stringify(docs[kind]) + "\n");
      manifest[kind] = { name: docs[kind].name, path, sha256: digest(path) };
    }
    const path = join(out, label + "-bundle.json");
    writeFileSync(path, JSON.stringify(manifest) + "\n");
    return path;
  }
  const fullDefinitions = await variant(
    [...regular, ...windowDefs],
    windowDefs.map((m: any) => m.name),
    "max",
  );
  await db.run("CALL ossie_bundle_load(?)", [fullDefinitions]);
  const all32 = { ...all, metrics: regular.map((m: any) => m.name) };
  const all16 = { ...win, windows: windowDefs.map((m: any) => m.name) };
  const maxUsers = await users(all32),
    maxWindows = await windows(all16);
  assert.equal(maxUsers.columns.length, 33);
  assert.equal(maxWindows.columns.length, 18);
  assert.deepEqual(maxUsers.rows, []);
  assert.deepEqual(maxWindows.rows, []);
  assert.equal(maxUsers.columns.at(-1).name.length, 128);
  checks.push(
    "actual maximum32 regular and16 window declarations/selected outputs including128-byte identifier accepted with exact empty schema",
  );
  await reject(
    "selected33 regular entries",
    "ossie_fact_users_compile",
    { ...all32, metrics: [...all32.metrics, "missing"] },
    "resource",
  );
  await reject(
    "selected17 windows",
    "ossie_fact_windows_compile",
    { ...all16, windows: [...all16.windows, "missing"] },
    "resource",
  );
  for (const [name, path] of [
    [
      "declared33 regular",
      await variant(
        [...regular, { ...regular[7], name: "overflow" }],
        [],
        "regular-over",
      ),
    ],
    [
      "declared17 windows",
      await variant(
        [
          ...regular.slice(0, 7),
          ...windowDefs,
          { ...windowDefs[2], name: "window_over" },
        ],
        [...windowDefs.map((m: any) => m.name), "window_over"],
        "window-over",
      ),
    ],
  ]) {
    let caught = false;
    try {
      await db.run("CALL ossie_bundle_load(?)", [path]);
    } catch (error) {
      const envelope = populationError(
        error instanceof Error ? error.message : String(error),
      );
      assert(envelope);
      refusals.push({ name, error: envelope });
      caught = true;
    }
    assert(caught, name);
  }
  assert.equal((await users(all32)).columns.length, 33);
  checks.push(
    "33rd regular/17th window declaration refused atomically, original maximum program remains queryable",
  );
  const plusDefs = structuredClone([...regular, ...windowDefs]);
  plusDefs[0].expression.dialects[0].expression = "income+expense";
  const plusPath = await variant(
    plusDefs,
    windowDefs.map((m: any) => m.name),
    "plus",
  );
  await db.run(
    "INSERT INTO revenue VALUES('A','2026-01-01',100);INSERT INTO cost VALUES('A','2026-01-01','X',10);CREATE MACRO \"+\"(a,b) AS 98::DECIMAL(38,2)",
  );
  await db.run("CALL ossie_bundle_load(?)", [plusPath]);
  assert.deepEqual((await users({ ...all, metrics: ["balance"] })).rows, [
    ["A", "110.00"],
  ]);
  const active16 = await windows(all16);
  assert.deepEqual(active16.rows, [
    ["A", "2026-01-01", null, "100.00", ...Array(14).fill("100.00")],
  ]);
  checks.push(
    "all16 public window outputs execute with14 zero-preceding frames selecting only current observation",
  );
  await db.run('DROP MACRO "+"');
  checks.push(
    "derived amount addition remains110 under same-name plus macro returning98",
  );

  const boundedMetrics = structuredClone(definitions.core.metrics);
  for (let i = 0; i < 10; i++)
    boundedMetrics.push({
      name: "d" + i,
      datatype: "Decimal",
      expression: {
        dialects: [
          {
            dialect: "OSSIE_SQL_2026",
            expression: i === 0 ? "income" : "d" + (i - 1) + "+d" + (i - 1),
          },
        ],
      },
    });
  const boundedPath = await variant(
    boundedMetrics,
    ["previous", "rolling"],
    "bounded-money",
  );
  await db.run(
    "DELETE FROM revenue;DELETE FROM cost;INSERT INTO revenue SELECT 'A',DATE '2000-01-01'+i::INTEGER,9999999999999999.99::DECIMAL(18,2) FROM range(10000)t(i);INSERT INTO cost SELECT who,day,'X',value FROM revenue",
  );
  await db.run("CALL ossie_bundle_load(?)", [boundedPath]);
  const cents = (10n ** 18n - 1n) * 10000n * 512n;
  const centsText = (v: bigint) =>
    (v / 100n).toString() + "." + (v % 100n).toString().padStart(2, "0");
  const maximum = await users({
    ...all,
    metrics: ["d9", "revenue_rows", "cost_rows"],
  });
  assert.deepEqual(maximum.rows, [["A", centsText(cents), "10000", "10000"]]);
  assert.equal(maximum.columns[1].bound_type, "DECIMAL(38,2)");
  assert.equal(maximum.common_user_count, "1");
  checks.push(
    "max10000 rows per fact and DECIMAL18,2 values execute1023-node/512-leaf derived money exactly against independent bigint cents",
  );
  const tooDeep = structuredClone(boundedMetrics);
  tooDeep.push({
    name: "d10",
    datatype: "Decimal",
    expression: {
      dialects: [{ dialect: "OSSIE_SQL_2026", expression: "d9+d9" }],
    },
  });
  const expandedPath = await variant(
    tooDeep,
    ["previous", "rolling"],
    "money-expanded-over",
  );
  let expansionRefused = false;
  try {
    await db.run("CALL ossie_bundle_load(?)", [expandedPath]);
  } catch (error) {
    const envelope = populationError(
      error instanceof Error ? error.message : String(error),
    );
    assert(envelope);
    refusals.push({
      name: "2047-node monetary expansion refused before overflowing subtree copy",
      error: envelope,
    });
    expansionRefused = true;
  }
  assert(expansionRefused);
  assert.deepEqual((await users({ ...all, metrics: ["d9"] })).rows, [
    ["A", centsText(cents)],
  ]);
  checks.push(
    "next2047-node amount expansion refused while prior maximum exact-money snapshot remains intact",
  );
  await db.run("ALTER TABLE revenue ALTER COLUMN value TYPE DECIMAL(38,2)");
  let wideMoneyRefused = false;
  try {
    await db.run("CALL ossie_bundle_load(?)", [boundedPath]);
  } catch (error) {
    const envelope = populationError(
      error instanceof Error ? error.message : String(error),
    );
    assert(envelope);
    refusals.push({
      name: "sourceDECIMAL38,2 outside accepted money type refused",
      error: envelope,
    });
    wideMoneyRefused = true;
  }
  assert(wideMoneyRefused);
  assert.deepEqual((await users({ ...all, metrics: ["d9"] })).rows, [
    ["A", centsText(cents)],
  ]);
  checks.push(
    "wider source Decimal refuses loading instead of silently expanding accepted measure domain",
  );
  writeFileSync(
    join(out, "verification.json"),
    JSON.stringify(
      {
        passed: true,
        scope:
          "production dedicated fact users/windows/compile/program; no MCP or real Agent acceptance",
        artifact_sha256: digest(artifact),
        checks,
        refusals,
        sources: Object.fromEntries(
          [
            "scripts/verify_fact_objects_artifact.ts",
            "src/population/population.cpp",
            "src/population/fact_program.cpp",
          ].map((p) => [p, digest(join(ROOT, p))]),
        ),
      },
      null,
      2,
    ) + "\n",
  );
  console.log(
    `PASS fact objects ${checks.length} checks/${refusals.length} refusals`,
  );
} finally {
  db.close();
}

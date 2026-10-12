/** Complete independent witness enumeration against native historical SQL and bound plan. */
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { Database, ROOT, digest, assert } from "./artifact_helpers.ts";
import { populationError } from "./population_error.ts";
import { relationDate } from "./relation_result.ts";
const [a, o] = process.argv.slice(2);
assert(a && o && process.argv.length === 4);
const artifact = resolve(a),
  out = resolve(o);
assert(!existsSync(out));
mkdirSync(out, { recursive: true });
const db = await Database.open();
const checks: string[] = [],
  refusals: unknown[] = [],
  cases: unknown[] = [];
type Obs = { user: string; date: string; product: string };
type History = {
  id: string;
  user: string;
  target: string;
  start: string;
  end: string;
};
const obs: Obs[] = [
  { user: "u", date: "2024-02-28", product: "X" },
  { user: "u", date: "2024-02-29", product: "X" },
  { user: "u", date: "2024-02-29", product: "Y" },
  { user: "u", date: "2024-03-01", product: "X" },
  { user: "u", date: "2024-03-02", product: "X" },
  { user: "u", date: "2024-03-03", product: "X" },
  { user: "absent", date: "2024-03-01", product: "X" },
];
const hist: History[] = [
  { id: "a", user: "u", target: "T", start: "2024-02-28", end: "2024-03-01" },
  { id: "b", user: "u", target: "T", start: "2024-02-29", end: "2024-03-01" },
  { id: "c", user: "u", target: "V", start: "2024-03-01", end: "2024-03-02" },
  { id: "d", user: "u", target: "T", start: "2024-03-03", end: "2024-03-04" },
];
for (const user of ["U", "汉字", 'q"\\\n', "nul\u0000id"]) {
  obs.push({ user, date: "2024-02-29", product: "X" });
  hist.push({
    id: user,
    user,
    target: user,
    start: "2024-02-28",
    end: "2024-03-01",
  });
}
obs.push(
  { user: "wide", date: "10000-01-01", product: "X" },
  { user: "BC", date: "0001-01-01 (BC)", product: "X" },
  { user: "min", date: "5877642-06-23 (BC)", product: "X" },
);
hist.push(
  {
    id: "wide",
    user: "wide",
    target: "W",
    start: "9999-12-31",
    end: "10000-01-02",
  },
  {
    id: "BC",
    user: "BC",
    target: "B",
    start: "0002-12-31 (BC)",
    end: "0001-01-02 (BC)",
  },
  {
    id: "min",
    user: "min",
    target: "M",
    start: "5877642-06-23 (BC)",
    end: "5877642-06-25 (BC)",
  },
);
const all = { dates: { mode: "all" }, products: { mode: "all" } };
const load = () =>
  db.run("CALL ossie_bundle_load(?)", [
    join(ROOT, "test/fixtures/population-history-program/bundle.json"),
  ]);
const raw = async (r: unknown, fn = "ossie_history_links_json") =>
  String(
    (
      await db.one(`SELECT * FROM ${fn}('team_at_observation',?)`, [
        typeof r === "string" ? r : JSON.stringify(r),
      ])
    )[0],
  );
const json = async (r: unknown, fn?: string) => JSON.parse(await raw(r, fn));
const compare = (a: string[], b: string[]) => {
  for (let i = 0; i < 4; i++) {
    const c =
      i === 1
        ? relationDate(a[i]) - relationDate(b[i])
        : Buffer.compare(Buffer.from(a[i]), Buffer.from(b[i]));
    if (c) return c;
  }
  return 0;
};
function oracle(r: any) {
  const rows: string[][] = [];
  for (const w of obs) {
    if (
      r.dates.mode !== "all" &&
      (relationDate(w.date) < relationDate(r.dates.start_date) ||
        relationDate(w.date) > relationDate(r.dates.end_date))
    )
      continue;
    if (r.products.mode !== "all" && !r.products.values.includes(w.product))
      continue;
    const targets = new Set(
      hist
        .filter(
          (h) =>
            h.user === w.user &&
            relationDate(h.start) <= relationDate(w.date) &&
            relationDate(w.date) < relationDate(h.end),
        )
        .map((h) => h.target),
    );
    assert(targets.size <= 1);
    for (const target of targets)
      rows.push([w.user, w.date, w.product, target]);
  }
  return rows.sort(compare);
}
async function reject(label: string, r: unknown, code: string, fn?: string) {
  try {
    await raw(r, fn);
  } catch (e) {
    assert.equal(populationError(String(e))?.code, code, label);
    refusals.push({ label, code });
    return;
  }
  throw Error("expected refusal " + label);
}
try {
  const engine = await db.loadArtifact(artifact);
  await db.run(
    "CREATE TABLE observations(user_id VARCHAR,ds DATE,product VARCHAR)",
  );
  await db.run(
    "CREATE TABLE history(record_id VARCHAR,user_id VARCHAR,team_id VARCHAR,starts DATE,ends DATE)",
  );
  for (const w of obs)
    await db.run(
      "INSERT INTO observations VALUES (?,DATE '1970-01-01'+?::INTEGER,?)",
      [w.user, String(relationDate(w.date)), w.product],
    );
  for (const h of hist)
    await db.run(
      "INSERT INTO history VALUES (?,?,?,DATE '1970-01-01'+?::INTEGER,DATE '1970-01-01'+?::INTEGER)",
      [
        h.id,
        h.user,
        h.target,
        String(relationDate(h.start)),
        String(relationDate(h.end)),
      ],
    );
  await load();
  for (const request of [
    all,
    {
      dates: {
        mode: "range",
        start_date: "2024-02-29",
        end_date: "2024-03-01",
      },
      products: { mode: "subset", values: ["Y", "X"] },
    },
    {
      dates: { mode: "all" },
      products: { mode: "subset", values: ["absent"] },
    },
    {
      dates: {
        mode: "range",
        start_date: "5877642-06-23 (BC)",
        end_date: "5877642-06-23 (BC)",
      },
      products: { mode: "all" },
    },
    {
      dates: {
        mode: "range",
        start_date: "9999-12-31",
        end_date: "10000-01-02",
      },
      products: { mode: "all" },
    },
  ]) {
    const result = await json(request),
      plan = await json(request, "ossie_history_links_compile");
    assert.deepEqual(result.links, oracle(request));
    assert.equal(result.link_count, String(result.links.length));
    assert(
      result.executed && result.complete && !result.paging && !result.truncated,
    );
    assert(
      !plan.executed &&
        !plan.links_computed &&
        !plan.physical_plan &&
        !plan.sql_replayable &&
        plan.input_projection_binding,
    );
    assert.deepEqual(plan.input_types, ["VARCHAR", "DATE", "VARCHAR"]);
    assert(!/\bWHERE\b/i.test(plan.projection_sql));
    assert.deepEqual(plan.request, result.request);
    for (const key of [
      "snapshot_id",
      "content_sha256",
      "core_sha256",
      "ontology_sha256",
      "mapping_sha256",
      "manifest_sha256",
      "relationship",
      "target_concept",
    ])
      assert.equal(plan[key], result[key]);
    cases.push({ request, plan, result, independent_links: oracle(request) });
  }
  checks.push(
    "complete independent links, duplicate same-target witnesses set-dedup, two products, start/end, gaps, case/NUL/wide/BC/min DATE; bound plans same scope/trace",
  );
  for (const fn of ["ossie_history_links_json", "ossie_history_links_compile"])
    for (const [label, r, code] of [
      ["missing dates", { products: { mode: "all" } }, "INVALID_REQUEST"],
      ["extra field", { ...all, carry: true }, "INVALID_REQUEST"],
      [
        "duplicate decoded key",
        '{"dates":{"mode":"all"},"\\u0064ates":{"mode":"all"},"products":{"mode":"all"}}',
        "INVALID_REQUEST",
      ],
      [
        "unknown date mode",
        { ...all, dates: { mode: "as_of" } },
        "UNSUPPORTED_CAPABILITY",
      ],
      [
        "infinity",
        {
          ...all,
          dates: {
            mode: "range",
            start_date: "-infinity",
            end_date: "2024-03-01",
          },
        },
        "INVALID_REQUEST",
      ],
      [
        "noncanonical date",
        {
          ...all,
          dates: {
            mode: "range",
            start_date: "2024-2-28",
            end_date: "2024-03-01",
          },
        },
        "INVALID_REQUEST",
      ],
      [
        "reversed range",
        {
          ...all,
          dates: {
            mode: "range",
            start_date: "2024-03-02",
            end_date: "2024-03-01",
          },
        },
        "INVALID_REQUEST",
      ],
      [
        "product number",
        { ...all, products: { mode: "subset", values: [1] } },
        "INVALID_REQUEST",
      ],
      [
        "empty subset",
        { ...all, products: { mode: "subset", values: [] } },
        "INVALID_REQUEST",
      ],
      [
        "duplicate products",
        { ...all, products: { mode: "subset", values: ["X", "X"] } },
        "INVALID_REQUEST",
      ],
      [
        "65products",
        {
          ...all,
          products: {
            mode: "subset",
            values: Array.from({ length: 65 }, (_, i) => String(i)),
          },
        },
        "RESOURCE_LIMIT_EXCEEDED",
      ],
      [
        "257bytes",
        { ...all, products: { mode: "subset", values: ["x".repeat(257)] } },
        "RESOURCE_LIMIT_EXCEEDED",
      ],
      [
        "16385bytes",
        JSON.stringify(all) +
          " ".repeat(16385 - Buffer.byteLength(JSON.stringify(all))),
        "RESOURCE_LIMIT_EXCEEDED",
      ],
    ] as [string, unknown, string][])
      await reject(fn + ":" + label, r, code, fn);
  const padded =
    JSON.stringify(all) +
    " ".repeat(16384 - Buffer.byteLength(JSON.stringify(all)));
  assert.deepEqual((await json(padded)).links, oracle(all));
  checks.push("original16KiB request accepted; +1 refused before parsing");
  await db.run(
    'PREPARE old_links AS SELECT result_json FROM ossie_history_links_json(\'team_at_observation\',\'{"dates":{"mode":"all"},"products":{"mode":"all"}}\')',
  );
  await db.run(
    'PREPARE old_plan AS SELECT plan_json FROM ossie_history_links_compile(\'team_at_observation\',\'{"dates":{"mode":"all"},"products":{"mode":"all"}}\')',
  );
  const old = await raw(all),
    oldPlan = await raw(all, "ossie_history_links_compile");
  await db.run(
    "UPDATE history SET team_id='NEW' WHERE record_id IN ('a','b','d')",
  );
  await load();
  assert.notEqual(await raw(all), old);
  await db.run("DROP TABLE history");
  await db.run("DROP TABLE observations");
  assert.equal(String((await db.one("EXECUTE old_links"))[0]), old);
  assert.equal(String((await db.one("EXECUTE old_plan"))[0]), oldPlan);
  await json(all);
  checks.push(
    "prepared complete links and plan retain old parent across reload and both table drops; fresh snapshot changes",
  );
  await db.run(
    "CREATE TABLE observations(user_id VARCHAR,ds DATE,product VARCHAR)",
  );
  await db.run(
    "CREATE TABLE history(record_id VARCHAR,user_id VARCHAR,team_id VARCHAR,starts DATE,ends DATE)",
  );
  await db.run(
    "INSERT INTO observations SELECT 'u',DATE '2024-02-29','p'||i FROM range(10000) t(i)",
  );
  await db.run(
    "INSERT INTO history VALUES ('only','u','T',DATE '2024-02-28',DATE '2024-03-01')",
  );
  await load();
  const large = await json(all);
  assert.equal(large.link_count, "10000");
  assert.deepEqual(
    large.links,
    Array.from({ length: 10000 }, (_, i) => [
      "u",
      "2024-02-29",
      "p" + i,
      "T",
    ]).sort(compare),
  );
  checks.push("complete10000 full link identities, not count-only");
  await db.run("DELETE FROM observations");
  await db.run("DELETE FROM history");
  const id = "u".repeat(1040000);
  await db.run("INSERT INTO observations VALUES (?,DATE '2024-02-29','X')", [
    id,
  ]);
  await db.run(
    "INSERT INTO history VALUES ('one',?,'T',DATE '2024-02-28',DATE '2024-03-01')",
    [id],
  );
  await load();
  const scope = {
    dates: { mode: "all" },
    products: {
      mode: "subset",
      values: [
        "X",
        ...Array.from(
          { length: 63 },
          (_, i) => "p" + String(i).padStart(2, "0"),
        ),
      ],
    },
  };
  let remaining = 1048576 - Buffer.byteLength(await raw(scope));
  assert(remaining > 0);
  for (let i = 1; i < scope.products.values.length && remaining; i++) {
    const n = Math.min(
      remaining,
      256 - Buffer.byteLength(scope.products.values[i]),
    );
    scope.products.values[i] += "a".repeat(n);
    remaining -= n;
  }
  assert.equal(remaining, 0);
  assert(Buffer.byteLength(JSON.stringify(scope)) <= 16384);
  assert.equal(Buffer.byteLength(await raw(scope)), 1048576);
  scope.products.values[63] += "b";
  await reject("complete1MiB+1 bytes", scope, "RESOURCE_LIMIT_EXCEEDED");
  await json(all);
  checks.push(
    "exact complete1MiB including escaped metadata/scope and+1 rejection, recovery",
  );
  // One large target shared by many roots must not be copied per output witness before JSON refuses.
  await db.run("DELETE FROM observations");
  await db.run(
    "INSERT INTO observations SELECT ?,DATE '2024-02-29','p'||i FROM range(1000) t(i)",
    ["u"],
  );
  await db.run("DELETE FROM history");
  await db.run(
    "INSERT INTO history VALUES ('large','u',?,DATE '2024-02-28',DATE '2024-03-01')",
    ["t".repeat(1040000)],
  );
  await load();
  await reject(
    "large shared target rejects bounded JSON without join bag",
    all,
    "RESOURCE_LIMIT_EXCEEDED",
  );
  checks.push(
    "large target referenced in immutable index, not duplicated across link materialization",
  );
  await db.run("DELETE FROM observations");
  await db.run("DELETE FROM history");
  await load();
  const empty = await json(all);
  assert.equal(empty.link_count, "0");
  assert.deepEqual(empty.links, []);
  checks.push("empty source/link result with full provenance");
  writeFileSync(
    join(out, "verification.json"),
    JSON.stringify(
      {
        passed: true,
        scope:
          "historical SQL/plan complete synthetic oracle; MCP/offline/Linux/tidy remain required",
        engine,
        artifact_sha256: digest(artifact),
        checks,
        refusals,
        cases,
        source_sha256: {
          "scripts/verify_history_links_artifact.ts": digest(
            join(ROOT, "scripts/verify_history_links_artifact.ts"),
          ),
        },
        real_model_requests: 0,
      },
      null,
      2,
    ) + "\n",
  );
  console.log(
    `PASS history links ${checks.length} controls/${refusals.length} refusals`,
  );
} finally {
  await db.close();
}

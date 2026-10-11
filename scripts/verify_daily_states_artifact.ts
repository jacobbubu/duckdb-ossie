/** Complete independent User/day and adjacent-pair oracle for the real native state interfaces. */
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
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
  "ARTIFACT FRESH_OUTPUT_DIRECTORY",
);
const artifact = resolve(artifactArg),
  out = resolve(outArg);
assert(!existsSync(out), "fresh directory required");
mkdirSync(out, { recursive: true });
type Observation = {
  user: string;
  date: string;
  product: string;
  value: number | null;
};
const rows: Observation[] = [];
const add = (
  user: string,
  date: string,
  product: string,
  value: number | null,
) => rows.push({ user, date, product, value });
for (let mask = 0; mask < 8; mask++) {
  add(`mask${mask}`, "2023-01-01", "outside", 10);
  [10, 0, null].forEach((v, bit) => {
    if (mask & (1 << bit)) add(`mask${mask}`, "2024-02-28", `p${bit}`, v);
  });
}
for (const a of [10, 0, null])
  for (const b of [10, 0, null]) {
    const user = `pair-${a}-${b}`;
    add(user, "2024-02-28", "X", a);
    add(user, "2024-02-29", "X", b);
  }
for (const user of ["Case", "case", "汉字", 'q"\\\n', "a\u0000b"])
  add(user, "2024-02-28", "X", 10);
add("BC-only", "0001-01-01 (BC)", "X", 10);
add("wide-only", "10000-01-01", "X", 10);
const request = {
  start_date: "2024-02-28",
  end_date: "2024-03-01",
  products: { mode: "all" },
  user_domain: "captured_users",
  transitions: "within_request_days",
};
const byteOrder = (a: string, b: string) =>
  Buffer.compare(Buffer.from(a), Buffer.from(b));
const db = await Database.open(),
  checks: string[] = [],
  refusals: unknown[] = [];
const fixture = join(ROOT, "test/fixtures/population-observation");
const load = async (threshold = 10) =>
  db.run("CALL ossie_bundle_load(?)", [
    join(fixture, `bundle-${threshold}.json`),
  ]);
const raw = async (r: unknown, fn = "ossie_daily_states_json") => {
  const [text] = await db.one(`SELECT * FROM ${fn}('QualifiedObservation',?)`, [
    typeof r === "string" ? r : JSON.stringify(r),
  ]);
  assert.equal(typeof text, "string");
  return text as string;
};
const query = async (r: unknown) => JSON.parse(await raw(r));
async function reject(label: string, r: unknown, code: string, fn?: string) {
  try {
    await raw(r, fn);
  } catch (error) {
    const envelope = populationError(String(error));
    assert(envelope, "structured error required");
    assert.equal(envelope.code, code);
    refusals.push({ label, code });
    return;
  }
  throw Error("expected refusal: " + label);
}
function oracle(r: typeof request, threshold: number) {
  const days: string[] = [];
  for (
    let t = Date.parse(r.start_date + "T00:00:00Z"),
      end = Date.parse(r.end_date + "T00:00:00Z");
    t <= end;
    t += 86400000
  )
    days.push(new Date(t).toISOString().slice(0, 10));
  const users = [...new Set(rows.map((x) => x.user))].sort(byteOrder);
  const states: unknown[][] = [],
    comparisons: unknown[][] = [];
  for (const user of users) {
    const values = days.map((date) => {
      const observed = rows.filter(
        (x) =>
          x.user === user &&
          x.date === date &&
          (r.products.mode === "all" ||
            (r.products as { values?: string[] }).values!.includes(x.product)),
      );
      const truth = observed.map((x) =>
        x.value === null ? null : x.value >= threshold,
      );
      return truth.includes(true)
        ? "TRUE"
        : truth.length && truth.every((x) => x === false)
          ? "FALSE"
          : "UNKNOWN";
    });
    days.forEach((date, i) => states.push([user, date, values[i]]));
    for (let i = 1; i < days.length; i++)
      comparisons.push([
        user,
        days[i - 1],
        days[i],
        values[i - 1],
        values[i],
        values[i - 1] !== values[i],
      ]);
  }
  return { states, comparisons, users };
}
async function compare(r: typeof request, threshold = 10) {
  const actual = await query(r),
    expected = oracle(r, threshold);
  assert.equal(actual.schema_version, "ossie-daily-states-result-v1");
  assert.deepEqual(actual.states, expected.states);
  assert.deepEqual(actual.comparisons, expected.comparisons);
  assert.equal(actual.user_count, String(expected.users.length));
  assert.equal(actual.state_count, String(expected.states.length));
  assert.equal(actual.comparison_count, String(expected.comparisons.length));
  assert.equal(
    actual.combined_object_count,
    String(expected.states.length + expected.comparisons.length),
  );
  assert.deepEqual(actual.state_totals, {
    unit: "User/day",
    TRUE: String(expected.states.filter((x) => x[2] === "TRUE").length),
    FALSE: String(expected.states.filter((x) => x[2] === "FALSE").length),
    UNKNOWN: String(expected.states.filter((x) => x[2] === "UNKNOWN").length),
  });
  assert.equal(
    actual.changed_comparison_count,
    String(expected.comparisons.filter((x) => x[5] === true).length),
  );
  assert.equal(actual.carry_forward, false);
  assert.equal(actual.paging, false);
  assert.equal(actual.truncated, false);
  assert.equal(actual.complete, true);
  return actual;
}
try {
  const engine = await db.loadArtifact(artifact);
  await db.run(
    "CREATE TABLE observations(user_id VARCHAR,ds DATE,product VARCHAR,amount_usd DECIMAL(18,2),classification_value INTEGER)",
  );
  for (const r of rows)
    await db.run(
      `INSERT INTO observations VALUES (?,CAST(? AS DATE),?,1,${r.value === null ? "NULL" : r.value})`,
      [r.user, r.date, r.product],
    );
  await load();
  const initial = await compare(request);
  assert.equal(
    initial.states.find(
      (x: unknown[]) => x[0] === "mask6" && x[1] === "2024-02-28",
    )[2],
    "UNKNOWN",
  );
  assert.equal(
    new Set(
      initial.comparisons
        .filter(
          (x: unknown[]) =>
            String(x[0]).startsWith("pair-") && x[1] === "2024-02-28",
        )
        .map((x: unknown[]) => JSON.stringify(x.slice(3, 5))),
    ).size,
    9,
  );
  checks.push(
    "eight truth subsets, all nine adjacent status pairs and unchanged, leap/month boundary; complete independent rows",
  );
  await compare({
    ...request,
    products: { mode: "subset", values: ["X"] },
  } as typeof request);
  const unknown = await compare({
    ...request,
    products: { mode: "subset", values: ["absent"] },
  } as typeof request);
  assert(unknown.states.every((x: unknown[]) => x[2] === "UNKNOWN"));
  await compare({ ...request, end_date: request.start_date });
  checks.push(
    "raw/case/NUL/UTF8 identity; outside-date/product/BC/wide-date Users retained; single-day no comparisons",
  );
  const plan = JSON.parse(await raw(request, "ossie_daily_states_compile"));
  for (const k of [
    "snapshot_id",
    "content_sha256",
    "core_sha256",
    "ontology_sha256",
    "mapping_sha256",
    "manifest_sha256",
  ])
    assert.equal(plan[k], initial[k]);
  assert.equal(plan.executed, false);
  assert.equal(plan.states_computed, false);
  assert.equal(plan.comparisons_computed, false);
  assert.deepEqual(plan.input_types, ["VARCHAR", "DATE", "VARCHAR", "BOOLEAN"]);
  assert(!plan.projection_sql.includes("WHERE"));
  const program = JSON.parse(
    (await db.one("SELECT * FROM ossie_daily_states_program()"))[0] as string,
  );
  assert.equal(program.query_executed, false);
  assert.equal(program.mcp_registered, false);
  assert.equal(program.captured_user_count, initial.user_count);
  checks.push(
    "compile full BOOLEAN projection without qualifying WHERE; actual shared bindings and nonexecuting discovery",
  );
  for (const [label, r, code] of [
    [
      "missing domain",
      {
        start_date: request.start_date,
        end_date: request.end_date,
        products: request.products,
      },
      "INVALID_REQUEST",
    ],
    [
      "other domain",
      { ...request, user_domain: "observed_users" },
      "UNSUPPORTED_CAPABILITY",
    ],
    [
      "external predecessor",
      { ...request, transitions: "with_predecessor" },
      "UNSUPPORTED_CAPABILITY",
    ],
    ["unknown key", { ...request, carry_forward: true }, "INVALID_REQUEST"],
    [
      "duplicate key",
      JSON.stringify(request).replace(
        '"user_domain":',
        '"user_domain":"captured_users","user_domain":',
      ),
      "INVALID_REQUEST",
    ],
    ["bad date", { ...request, start_date: "2024-02-30" }, "INVALID_REQUEST"],
    ["reverse date", { ...request, end_date: "2024-02-27" }, "INVALID_REQUEST"],
    ["infinity", { ...request, start_date: "infinity" }, "INVALID_REQUEST"],
    [
      "empty subset",
      { ...request, products: { mode: "subset", values: [] } },
      "INVALID_REQUEST",
    ],
    [
      "duplicate product",
      { ...request, products: { mode: "subset", values: ["X", "X"] } },
      "INVALID_REQUEST",
    ],
    [
      "too many days",
      { ...request, end_date: "2025-03-01" },
      "RESOURCE_LIMIT_EXCEEDED",
    ],
  ] as const)
    await reject(label, r, code);
  const padded = JSON.stringify(request).padEnd(16384, " ");
  assert.deepEqual((await query(padded)).states, initial.states);
  await reject(
    "original request 16KiB+1",
    padded + " ",
    "RESOURCE_LIMIT_EXCEEDED",
  );
  await reject(
    "old daily interface rejects new shape",
    request,
    "INVALID_REQUEST",
    "ossie_daily_users_json",
  );
  const oldReq = {
    start_date: request.start_date,
    end_date: request.end_date,
    products: request.products,
  };
  const old = JSON.parse(await raw(oldReq, "ossie_daily_users_json"));
  const trueDaily = oracle(request, 10);
  const expectedOld = trueDaily.users.filter((user) =>
    trueDaily.states.filter((x) => x[0] === user).every((x) => x[2] === "TRUE"),
  );
  assert.deepEqual(old.members, expectedOld);
  checks.push(
    "old daily behavior unchanged; strict original request keys and exact16KiB boundary",
  );
  await load(20);
  await compare(request, 20);
  await db.run("CREATE MACRO count(x) AS 999");
  await compare(request, 20);
  checks.push(
    "new frozen threshold20 changes semantic rows; caller count macro cannot alter private domain",
  );
  await db.run("DELETE FROM observations");
  await load();
  const empty = await query(request);
  assert.deepEqual(empty.states, []);
  assert.deepEqual(empty.comparisons, []);
  await db.run(
    "INSERT INTO observations SELECT printf('%05d',i),DATE '2024-02-28','X',1,10 FROM range(10000)t(i)",
  );
  await load();
  const large = await query({ ...request, end_date: request.start_date });
  assert.equal(large.state_count, "10000");
  assert.equal(large.states.length, 10000);
  assert.deepEqual(
    large.states,
    Array.from({ length: 10000 }, (_, i) => [
      String(i).padStart(5, "0"),
      request.start_date,
      "TRUE",
    ]),
  );
  await reject(
    "combined object gate before scan/allocation",
    request,
    "RESOURCE_LIMIT_EXCEEDED",
  );
  await reject(
    "same gate at compile",
    request,
    "RESOURCE_LIMIT_EXCEEDED",
    "ossie_daily_states_compile",
  );
  await db.run("DELETE FROM observations WHERE user_id>='03333'");
  await load();
  const two = { ...request, end_date: "2024-02-29" };
  assert.equal((await query(two)).combined_object_count, "9999");
  await db.run(
    "INSERT INTO observations VALUES ('extra',DATE '2024-02-28','X',1,10)",
  );
  await load();
  await reject(
    "3334 Users two days total10002",
    two,
    "RESOURCE_LIMIT_EXCEEDED",
  );
  checks.push(
    "empty domain, complete10000 unique Users, two-day9999 and10002 combined budget including unchanged comparisons",
  );
  await db.run("DELETE FROM observations");
  await db.run(
    "INSERT INTO observations VALUES (?,DATE '2024-02-28','X',1,10)",
    ["u".repeat(1040000)],
  );
  await load();
  const products = Array.from(
    { length: 64 },
    (_, i) => `p${String(i).padStart(3, "0")}`,
  );
  const byteReq = {
    ...request,
    end_date: request.start_date,
    products: { mode: "subset", values: products },
  };
  const baseline = await raw(byteReq);
  let remaining = 1048576 - Buffer.byteLength(baseline);
  assert(remaining >= 0 && remaining <= 64 * 252);
  for (let i = 0; i < products.length; i++) {
    const n = Math.min(252, remaining);
    products[i] += "z".repeat(n);
    remaining -= n;
  }
  assert.equal(remaining, 0);
  assert.equal(Buffer.byteLength(await raw(byteReq)), 1048576);
  const index = products.findIndex((p) => p.length < 256);
  assert(index >= 0);
  products[index] += "z";
  await reject(
    "complete JSON1MiB+1 including request escaping",
    byteReq,
    "RESOURCE_LIMIT_EXCEEDED",
  );
  checks.push(
    "exact1MiB complete response including scope metadata and+1 refusal; eligible object count does not guarantee encoding fits",
  );
  const result = {
    passed: true,
    scope:
      "Daily states native SQL/plan/discovery; MCP/offline and history still required",
    engine,
    artifact_sha256: digest(artifact),
    checks,
    refusals,
    verifier_sha256: digest(import.meta.path),
    real_model_requests: 0,
  };
  writeFileSync(
    join(out, "verification.json"),
    JSON.stringify(result, null, 2) + "\n",
    { flag: "wx" },
  );
  console.log(
    `PASS daily states ${checks.length} controls/${refusals.length} refusals`,
  );
} finally {
  db.close();
}

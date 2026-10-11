/** Research oracle only: intersects per-day User sets; never imports production code. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
const base = "docs/research/ossie-req-001/multi-period-research/";
function csv(path: string) {
  return readFileSync(path, "utf8")
    .trimEnd()
    .split("\n")
    .slice(1)
    .map((line) => {
      const [id, user, day, product, amount, classification] = line.split(",");
      return {
        id,
        user,
        day,
        product,
        amount,
        classification: classification === "" ? null : Number(classification),
      };
    });
}
const original = csv(base + "observations.csv"),
  extended = csv(base + "every-day/observations.csv");
assert.equal(extended.length, 23);
assert.deepEqual(extended.slice(0, 8), original);
assert.equal(
  new Set(extended.map((o) => JSON.stringify([o.user, o.day, o.product]))).size,
  23,
);
const cases = JSON.parse(readFileSync(base + "every-day/cases.json", "utf8"));
const sql = JSON.parse(
  readFileSync(base + "every-day/sql-results.raw.json", "utf8"),
);
const results = cases.map((c: any) => {
  const rows =
    c.dataset === "original" ? original : c.dataset === "empty" ? [] : extended;
  const dates: string[] = [];
  for (
    let d = new Date(c.start + "T00:00:00Z");
    d <= new Date(c.end + "T00:00:00Z");
    d.setUTCDate(d.getUTCDate() + 1)
  )
    dates.push(d.toISOString().slice(0, 10));
  assert(dates.length > 0);
  const daily = dates.map(
    (day) =>
      new Set(
        rows
          .filter(
            (o) =>
              o.day === day &&
              o.classification !== null &&
              o.classification >= c.threshold &&
              (c.products === null || c.products.includes(o.product)),
          )
          .map((o) => o.user),
      ),
  );
  const ids = [...daily[0]]
    .filter((user) => daily.every((set) => set.has(user)))
    .sort();
  assert.deepEqual(ids, c.expected_ids);
  const actual = sql.find((r: any) => r.id === c.id);
  assert.deepEqual(actual.eligible_ids, ids);
  assert.equal(actual.user_count, ids.length);
  assert.equal(actual.required_days, dates.length);
  return {
    id: c.id,
    ids,
    required_days: dates.length,
    daily_user_counts: daily.map((set) => set.size),
  };
});
// Same numbers do not establish identical populations or quantifier order.
const dates = ["2026-01-31", "2026-02-01"],
  products = ["X", "Y"];
const users = [...new Set(extended.map((o) => o.user))];
const qualified = (user: string, day: string, product?: string) =>
  extended.some(
    (o) =>
      o.user === user &&
      o.day === day &&
      (!product || o.product === product) &&
      o.classification !== null &&
      o.classification >= 10,
  );
const sameProduct = users
  .filter((u) => products.some((p) => dates.every((d) => qualified(u, d, p))))
  .sort();
const everyProduct = users
  .filter((u) => dates.every((d) => products.every((p) => qualified(u, d, p))))
  .sort();
const allObservations = users
  .filter(
    (u) =>
      dates.every((d) => qualified(u, d)) &&
      extended
        .filter((o) => o.user === u && dates.includes(o.day))
        .every((o) => o.classification !== null && o.classification >= 10),
  )
  .sort();
assert.deepEqual(sameProduct, ["B", "F", "H", "K"]);
assert.deepEqual(everyProduct, ["K"]);
assert.deepEqual(allObservations, ["B", "E", "H", "K"]);
console.log(
  JSON.stringify(
    {
      scope:
        "research only; hand-listed members, ordinary SQL division and independent daily set intersection",
      cases: results,
      alternative_quantifiers: {
        same_product_every_day: sameProduct,
        every_product_every_day: everyProduct,
        all_observations_qualify: allObservations,
      },
      real_model_requests: 0,
      ossie_loaded: false,
    },
    null,
    2,
  ),
);

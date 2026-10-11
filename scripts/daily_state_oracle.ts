/** Independent fixture calculation, for verification only; not given to completions. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT } from "./artifact_helpers.ts";
import type { StateRequest } from "./daily_state_result.ts";
export const STATE_FIXTURE =
  "docs/research/ossie-req-001/multi-period-research/every-day/observations.csv";
export function stateOracle(r: StateRequest, threshold: number, empty = false) {
  const lines = readFileSync(join(ROOT, STATE_FIXTURE), "utf8")
    .trim()
    .split("\n");
  assert(
    lines.shift() ===
      "observation_id,user_id,ds,product,amount_usd,classification_value",
  );
  const rows = empty
    ? []
    : lines.map((line) => {
        const cells = line.split(",");
        assert(
          cells.length === 6 && !line.includes('"'),
          "this fixture oracle accepts unquoted CSV only",
        );
        return {
          user: cells[1],
          date: cells[2],
          product: cells[3],
          value: cells[5] === "" ? null : Number(cells[5]),
        };
      });
  const users = [...new Set(rows.map((v) => v.user))].sort((a, b) =>
      Buffer.compare(Buffer.from(a), Buffer.from(b)),
    ),
    dates: string[] = [];
  for (
    let t = Date.parse(r.start_date + "T00:00:00Z");
    t <= Date.parse(r.end_date + "T00:00:00Z");
    t += 86400000
  )
    dates.push(new Date(t).toISOString().slice(0, 10));
  const states: unknown[][] = [],
    comparisons: unknown[][] = [];
  for (const user of users) {
    const status = dates.map((date) => {
      const obs = rows.filter(
        (row) =>
          row.user === user &&
          row.date === date &&
          (r.products.mode === "all" ||
            r.products.values.includes(row.product)),
      );
      if (obs.some((row) => row.value !== null && row.value >= threshold))
        return "TRUE";
      if (
        obs.length > 0 &&
        obs.every((row) => row.value !== null && row.value < threshold)
      )
        return "FALSE";
      return "UNKNOWN";
    });
    dates.forEach((d, i) => states.push([user, d, status[i]]));
    for (let d = 1; d < dates.length; d++)
      comparisons.push([
        user,
        dates[d - 1],
        dates[d],
        status[d - 1],
        status[d],
        status[d - 1] !== status[d],
      ]);
  }
  return { states, comparisons };
}

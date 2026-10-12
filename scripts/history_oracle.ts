/** Independent per-record witness enumeration; no interval merging or binary lookup. */
import assert from "node:assert/strict";
import { relationDate, compareRelationLinks } from "./relation_result.ts";
import type { HistoryRequest } from "./history_result.ts";
export const HISTORY_FIXTURE =
  "test/fixtures/population-history-program/rows.sql";
export const HISTORY_OBSERVATIONS = [
  ["A", "2024-02-28", "X"],
  ["A", "2024-02-29", "X"],
  ["A", "2024-02-29", "Y"],
  ["A", "2024-03-01", "X"],
  ["A", "2024-03-02", "X"],
  ["A", "2024-03-03", "X"],
  ["B", "2024-02-29", "X"],
  ["absent", "2024-02-29", "X"],
  ["a", "2024-02-29", "X"],
  ["界", "2024-02-29", "X"],
  ["N\0U", "2024-02-29", "X"],
  ["wide", "10000-01-01", "X"],
  ["bc", "0001-01-01 (BC)", "X"],
  ["minimum", "5877642-06-23 (BC)", "X"],
];
export const HISTORY_RECORDS = [
  ["a", "A", "T", "2024-02-28", "2024-03-01"],
  ["b", "A", "T", "2024-02-29", "2024-03-01"],
  ["c", "A", "V", "2024-03-01", "2024-03-02"],
  ["d", "A", "T", "2024-03-03", "2024-03-04"],
  ["e", "B", "T", "2024-02-28", "2024-03-01"],
  ["f", "a", "t", "2024-02-28", "2024-03-01"],
  ["g", "界", "队", "2024-02-28", "2024-03-01"],
  ["h", "N\0U", "T\0M", "2024-02-28", "2024-03-01"],
  ["i", "wide", "W", "10000-01-01", "10000-01-02"],
  ["j", "bc", "BC", "0001-01-01 (BC)", "0001-01-02 (BC)"],
  ["unobserved", "outside", "O", "2023-01-01", "2023-01-02"],
  ["k", "minimum", "MIN", "5877642-06-23 (BC)", "5877642-06-25 (BC)"],
];
export function historyOracle(request: HistoryRequest, empty = false) {
  if (empty) return [];
  const result: string[][] = [];
  for (const [user, date, product] of HISTORY_OBSERVATIONS) {
    const day = relationDate(date);
    if (
      request.dates.mode === "range" &&
      (day < relationDate(request.dates.start_date) ||
        day > relationDate(request.dates.end_date))
    )
      continue;
    if (
      request.products.mode === "subset" &&
      !request.products.values.includes(product)
    )
      continue;
    const targets = new Set(
      HISTORY_RECORDS.filter(
        (r) =>
          r[1] === user &&
          relationDate(r[3]) <= day &&
          day < relationDate(r[4]),
      ).map((r) => r[2]),
    );
    assert(targets.size <= 1, "oracle fixture conflict");
    for (const target of targets) result.push([user, date, product, target]);
  }
  return result.sort(compareRelationLinks);
}

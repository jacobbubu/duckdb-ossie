import { test, expect } from "bun:test";
import { DuckDBDateValue } from "@duckdb/node-api";
import { cell } from "../../scripts/artifact_helpers.ts";

test("native DATE adapter preserves finite dates, infinity and NULL", () => {
  expect(
    cell(DuckDBDateValue.fromParts({ year: 2024, month: 2, day: 29 })),
  ).toBe("2024-02-29");
  expect(cell(DuckDBDateValue.PosInf)).toBe("infinity");
  expect(cell(DuckDBDateValue.NegInf)).toBe("-infinity");
  expect(cell(null)).toBe(null);
});

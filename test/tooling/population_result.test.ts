import { test, expect } from "bun:test";
import {
  populationResult,
  resultRecords,
  type PopulationResult,
} from "../../scripts/population_result.ts";
const base: PopulationResult = {
  schema_version: "ossie-population-result-v1",
  request_kind: "metrics",
  concept: "User",
  executed: true,
  profile: "test",
  snapshot_id: "snapshot",
  content_sha256: "content",
  core_sha256: "core",
  ontology_sha256: "ontology",
  mapping_sha256: "mapping",
  manifest_sha256: "manifest",
  ordering: "unspecified",
  paging: false,
  truncated: false,
  columns: [
    {
      ordinal: 1,
      name: "total",
      bound_type: "DECIMAL(38,2)",
      encoding: "decimal-text",
    },
    {
      ordinal: 2,
      name: "count",
      bound_type: "HUGEINT",
      encoding: "integer-text",
    },
  ],
  rows: [["9007199254740992.01", "9241386435364257792"]],
  row_count: 1,
};
const carrier = (r: unknown) =>
  JSON.stringify([{ result_json: JSON.stringify(r) }]);
test("decoder uses DuckDB ASCII case rules for output names", () => {
  const unicode = {
    ...base,
    columns: [
      { ...base.columns[0], name: "Ä" },
      { ...base.columns[1], name: "ä" },
    ],
  };
  expect(populationResult(carrier(unicode)).columns.map((c) => c.name)).toEqual(
    ["Ä", "ä"],
  );
  expect(() =>
    populationResult(
      carrier({
        ...base,
        columns: [
          { ...base.columns[0], name: "SUM" },
          { ...base.columns[1], name: "sum" },
        ],
      }),
    ),
  ).toThrow();
});
test("ordinary JSON decoding retains exact integer/Decimal strings", () => {
  const r = populationResult(carrier(base));
  expect(resultRecords(r)).toEqual([
    { total: "9007199254740992.01", count: "9241386435364257792" },
  ]);
});
test("typed decoder refuses numeric cells and mismatched type/encoding", () => {
  expect(() =>
    populationResult(carrier({ ...base, rows: [[9007199254740992, "1"]] })),
  ).toThrow();
  expect(() =>
    populationResult(
      carrier({
        ...base,
        columns: [
          { ...base.columns[0], encoding: "integer-text" },
          base.columns[1],
        ],
      }),
    ),
  ).toThrow();
});
test("empty typed result keeps schema, snapshot and null cells stay null", () => {
  const empty = populationResult(carrier({ ...base, rows: [], row_count: 0 }));
  expect(empty.snapshot_id).toBe("snapshot");
  expect(empty.columns).toEqual(base.columns);
  expect(
    resultRecords(populationResult(carrier({ ...base, rows: [[null, null]] }))),
  ).toEqual([{ total: null, count: null }]);
});
test("typed decoder refuses malformed carrier, unknown schema and broken row width", () => {
  expect(() => populationResult("[]")).toThrow();
  expect(() =>
    populationResult(carrier({ ...base, schema_version: "future" })),
  ).toThrow();
  expect(() => populationResult(carrier({ ...base, rows: [["1"]] }))).toThrow();
  expect(() => populationResult(carrier({ ...base, row_count: 2 }))).toThrow();
});

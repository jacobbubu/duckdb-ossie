/** Decode the adapter carrier without converting numeric cells to JS Number. */
import assert from "node:assert/strict";
export interface PopulationResult {
  schema_version: "ossie-population-result-v1";
  request_kind: "objects" | "metrics";
  concept: string;
  executed: true;
  profile: string;
  snapshot_id: string;
  content_sha256: string;
  core_sha256: string;
  ontology_sha256: string;
  mapping_sha256: string;
  manifest_sha256: string;
  ordering: "unspecified";
  paging: false;
  truncated: false;
  columns: {
    ordinal: number;
    name: string;
    bound_type: string;
    encoding: string;
  }[];
  rows: (string | boolean | null)[][];
  row_count: number;
}
function encoding(type: string) {
  if (["TINYINT", "SMALLINT", "INTEGER", "BIGINT", "HUGEINT"].includes(type))
    return "integer-text";
  if (/^DECIMAL\(\d+,\d+\)$/.test(type)) return "decimal-text";
  if (["FLOAT", "DOUBLE"].includes(type)) return "ieee754-text";
  if (type === "DATE") return "date-text";
  if (type === "VARCHAR") return "string";
  if (type === "BOOLEAN") return "boolean";
  throw Error("unsupported bound result type: " + type);
}
export function populationResult(contentText: string): PopulationResult {
  const carrier = JSON.parse(contentText);
  assert(
    Array.isArray(carrier) &&
      carrier.length === 1 &&
      typeof carrier[0].result_json === "string",
    "single result_json carrier required",
  );
  const result = JSON.parse(carrier[0].result_json) as PopulationResult;
  assert(
    result.schema_version === "ossie-population-result-v1",
    "unknown result schema",
  );
  assert(["objects", "metrics"].includes(result.request_kind));
  assert(
    result.executed === true &&
      result.paging === false &&
      result.truncated === false &&
      result.ordering === "unspecified",
  );
  for (const key of [
    "concept",
    "profile",
    "snapshot_id",
    "content_sha256",
    "core_sha256",
    "ontology_sha256",
    "mapping_sha256",
    "manifest_sha256",
  ] as const)
    assert(
      typeof result[key] === "string" && result[key].length > 0,
      "missing result provenance: " + key,
    );
  assert(Array.isArray(result.columns) && Array.isArray(result.rows));
  assert(
    Number.isSafeInteger(result.row_count) &&
      result.row_count === result.rows.length,
  );
  const names = new Set<string>();
  result.columns.forEach((c, i) => {
    assert(
      c.ordinal === i + 1 &&
        typeof c.name === "string" &&
        typeof c.bound_type === "string",
    );
    // DuckDB folds ASCII only; Unicode case variants can be distinct identifiers.
    const folded = c.name.replace(/[A-Z]/g, (letter) => letter.toLowerCase());
    assert(!names.has(folded), "duplicate result column");
    names.add(folded);
    assert(c.encoding === encoding(c.bound_type), "encoding/type mismatch");
  });
  for (const row of result.rows) {
    assert(
      Array.isArray(row) && row.length === result.columns.length,
      "result row width mismatch",
    );
    row.forEach((v, i) => {
      if (v === null) return;
      const c = result.columns[i];
      if (c.encoding === "boolean") assert(typeof v === "boolean");
      else {
        assert(
          typeof v === "string",
          "numeric and text cells must remain strings",
        );
        if (c.encoding === "integer-text") assert(/^-?(?:0|[1-9]\d*)$/.test(v));
        if (c.encoding === "decimal-text") assert(/^-?\d+(?:\.\d+)?$/.test(v));
        if (c.encoding === "ieee754-text")
          assert(
            ["NaN", "+Infinity", "-Infinity"].includes(v) ||
              /^-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?$/.test(v),
          );
      }
    });
  }
  return result;
}
export function resultRecords(
  result: PopulationResult,
): Record<string, string | boolean | null>[] {
  return result.rows.map((row) =>
    Object.fromEntries(result.columns.map((c, i) => [c.name, row[i]])),
  );
}

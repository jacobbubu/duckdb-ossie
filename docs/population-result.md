# Typed population result

`ossie_objects_json(concept, filters)` and
`ossie_concept_query_json(concept, metrics, dimensions, filters)` are optional
adapters. Each returns exactly one VARCHAR column, `result_json`, with one row
after successful execution, including an empty object/grouped result. They use
the same QueryAST, filters, checked grain and frozen data as the native APIs.
`include_trace => true` adds the same trace projections. Native object/query
column names, types and computation remain unchanged.

The project schema is `ossie-population-result-v1`; it is not an Ossie or MCP
standard. Project population API 2.0.0 adds these functions and marks the
incompatible MCP success-format change. The example MCP object and
query tools now use them, which changes their successful result format. Tool
names, arguments, discovery resources and the compile tool format remain unchanged.
Consumers of the earlier row-object arrays must update their decoding.

## Envelope

The result includes `schema_version`, `request_kind` (`objects` or `metrics`),
`concept`, `executed=true`, `profile`, and the actual bound `snapshot_id`,
`content_sha256`, `core_sha256`, `ontology_sha256`, `mapping_sha256` and
`manifest_sha256`. Metadata comes from the immutable bundle used by this query,
not a separate discovery call. Old prepared queries retain their old bundle
across reload; failed loads do not replace it.

`columns` is ordered. Each entry has one-based `ordinal`, output `name`, actual
DuckDB `bound_type` and `encoding`. The types/names come from preparing the same
generated statement; execution verifies its result schema matches. The output
is not inferred from declared Ossie datatype or the metric name. Duplicate
output names, including case variants, refuse as they do in the native API;
the adapter checks them because the outer binder sees only `result_json`.

`rows` is an array of cell arrays in column order. `row_count` counts fully
fetched/encoded output rows, not imported rows or aggregate denominators.
`ordering="unspecified"`, `paging=false` and `truncated=false` describe the
complete successful response. There is no new sorting, paging or truncation.
Any query/fetch/encoding error returns a failure, not a partial success envelope.
Empty results retain columns and all hashes; empty ungrouped COUNT/SUM/AVG retain
their native zero/NULL semantics.

| Actual bound type | Encoding | Non-NULL cell |
|---|---|---|
| TINYINT/SMALLINT/INTEGER/BIGINT/HUGEINT | `integer-text` | Signed base-10 integer string, for small and large values alike |
| DECIMAL(p,s) | `decimal-text` | Native decimal string with scale, preserving stored/computed digits |
| FLOAT/DOUBLE | `ieee754-text` | Approximate native floating result as text; special strings `NaN`, `+Infinity`, `-Infinity`, and `-0` for signed zero |
| BOOLEAN | `boolean` | JSON true/false |
| DATE | `date-text` | Native date text, including `infinity`/`-infinity` |
| VARCHAR | `string` | Length-aware JSON-escaped text |

Every SQL NULL becomes JSON null. Unsupported bound types refuse. Integer and
Decimal strings preserve exact values for ordinary JSON parsers, but downstream
conversion to JavaScript Number can still lose precision. Use BigInt or a decimal
arithmetic library when calculation is needed. FLOAT/DOUBLE text does not make
division/AVG exact Decimal, preserve NaN payload bits, or introduce rounding/unit
conversion.

## MCP decoding

MCP content remains JSON text produced by duckdb_mcp. Parse it once to obtain the
single-row carrier, then parse the `result_json` string:

```ts
const carrier = JSON.parse(contentText);
const result = JSON.parse(carrier[0].result_json);
// Numeric result.rows cells are strings. Read columns[i].bound_type/encoding.
```

A decoder should retain the numeric strings and reject type/encoding or
row-width mismatches. Discovery/compile responses keep their earlier formats.

The adapter fetches chunks without making another complete Value-row copy, but
materializes the complete JSON string. MCP adds another escaping layer. Capture
limits do not bound output or peak memory; no universal output-size promise is
made. Cancellation
is checked during and after encoding, and the existing pending-task draining is
retained. There is no pagination.

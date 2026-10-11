# Population over MCP

[population_server.sql](../examples/population_server.sql) exposes the frozen
population APIs through three separate tools and four readable discovery resources.
It imports the reviewed 475-row GAAP fixture and loads the threshold-10 manifest
before starting stdio. The ordinary core-model server remains a separate example. The new daily
observation profile has a separate [observation server](../examples/observation_server.sql)
and [no-model preparation guide](agent-observation-acceptance.md); its concepts
resource uses v2. The legacy server below keeps its current v1 discovery.

Run from the repository root with a stock DuckDB 1.5.6 CLI:

```sh
duckdb -bail -init examples/population_server.sql
```

The community Ossie artifact must contain the population APIs. To use an unpublished
local build, copy the script and replace its `INSTALL ossie` / `LOAD ossie` lines with
`LOAD '/absolute/path/to/ossie.duckdb_extension';`, then pass `-unsigned` to the CLI.
`-bail` is required: a failed operator import/load must stop before the server starts.
The actual-artifact verifier uses this path, without installing Ossie from the registry.

The operator owns the marked setup block, manifest, physical import and allowed
metric/dimension vocabulary. Replace the setup and definitions for another profile;
the tool templates discover vocabulary from the bound bundle without GAAP names.
Loading a reviewed bundle is not authorization to serve unreviewed data or definitions.
This example does not implement user identity, tenant permissions or a deployment.
No tool imports, reloads or changes rules. Restart with another trusted setup to
change the rule. Both single-table and bounded fact-dimension profiles use the same
three tool templates.

## Calls and results

Read `ossie://population/concepts` with `resources/read`. Its JSON content comes
from `ossie_concepts()` and contains executable concepts, parent, original rule,
identity, profile, original source and definition/data snapshot hashes. It is an
operator-startup resource. Also read `ossie://population/metrics`,
`ossie://population/fields` and `ossie://population/capabilities`, backed by the
three [population discovery functions](population-discovery.md). Use metric `name`
and field `qualified_name` exactly; the latter includes SQL identifier quoting.
`object_column` is the literal key in object results. Compare snapshot and
definition hashes across resources and calls. The separate
`ossie_metrics()` state is not the population bundle and is not published here.

All tool arguments are required. `concept` is an exact discovered name; each list
is a **JSON-encoded string array**. Send the string `"[]"` for an empty list. Example
`tools/call` parameters:

```json
{
  "name": "population_query",
  "arguments": {
    "concept": "ValidUser",
    "metrics": "[\"record_count\",\"gaap_sum\",\"mean_gaap\"]",
    "dimensions": "[\"dws_gaap_di.ds\"]",
    "filters": "[\"User.product = 'X'\",\"User.date >= DATE '2026-03-01' AND User.date < DATE '2026-04-01'\"]"
  }
}
```

| Tool | Required arguments | Result |
|---|---|---|
| `population_objects` | concept, filters | Typed result envelope: IDs, logical fields, actual types and snapshot hashes |
| `population_query` | concept, metrics, dimensions, filters | Typed result envelope: aggregates/group keys, actual types and snapshot hashes |
| `population_compile` | concept, metrics, dimensions, filters | Diagnostic SQL, versioned plan_json, definition/snapshot hashes, execution target, `executed=false` |

Empty metrics select the object path only for compile and require empty dimensions.
All filters retain the [profile AST restrictions](population-profile.md) and can
only narrow the formal concept rule. JSON encoding preserves commas, semicolons
and escaped quotes inside a single predicate; lists are never delimiter-split.
The extension helper rejects malformed JSON, wrong top-level types and every non-string
or NULL element. There is no invalid-input fallback to an unfiltered query.

The observed duckdb_mcp build does not enforce every schema type. Native JSON
arrays are serialized to the same bytes and accepted if they contain only strings;
unused extra arguments are ignored. An extra `threshold` or `sql` argument has no
binding and cannot replace a rule or execute SQL. This is the observed transport
behavior, not a strict JSON Schema validation guarantee.

Object/query results use [ossie-population-result-v1](population-result.md).
MCP content is JSON text containing one carrier row with a `result_json` string;
parse that string to obtain ordered `columns` and cell-array `rows`. Numeric cells
remain strings with actual bound types, SQL NULL stays JSON null, and Boolean
stays true/false. This is a change from the earlier arrays of row objects; tool
names and arguments remain the same. Discovery and compile formats are unchanged.

These templates add no sorting, paging or truncation. Historical raw-format
completeness was verified for fixed fixtures, including a
synthetic 10,000-object response with 23,267,891 content bytes and 33,367,965
JSON-RPC line bytes. Its escaped payloads and all fields/IDs were checked; a
10,000-group aggregate response was also checked. These are successful observations
of the recorded MCP build, not output maxima or guarantees for all legal models.
The 16 MiB canonical input limit does not bound JSON output or peak memory.
New encoded response sizes are recorded separately by the typed verifier and
must not be inferred from those historical byte counts. Groups are sparse.
Empty grouped/object envelopes have `rows=[]` and `row_count=0`; an
empty ungrouped aggregate has COUNT zero and SUM/AVG NULL. Nonempty rows carry
`_ossie_snapshot_id`, `_ossie_content_sha256`, `_ossie_ontology_sha256`. Every
successful execution envelope, including empty results, carries all definition
and snapshot hashes from the actual bound query.
Separate compile/query calls are not an atomic operation across an operator reload.
The compile tool enables `include_trace` to show the same trace projections as query/object tools.
Compile SQL requires an internal AST dependency and cannot be replayed as ordinary
SQL. The [machine-readable plan](population-plan.md) describes the generated semantic AST and its definition/capture provenance; it is not a physical plan.

The earlier raw JSON numeric format was **not a lossless ordinary-JSON client
contract**. For the recorded build, the precision probe found exact raw integer/DECIMAL(38,2)
tokens, including BIGINT boundaries and widened HUGEINT SUM. Loss occurred when
ordinary `JSON.parse` converted those tokens to JavaScript Number: for example,
`9007199254740993` became `9007199254740992`, and adjacent cent amounts above 2^53
became the same value. This evidence does not attribute loss to the MCP serializer.
The current typed adapter prevents that initial Number conversion by encoding
integer/Decimal cells as strings consistently, with precision/scale in the actual
bound type. Converting these strings to Number later can still lose precision.
FLOAT/DOUBLE cells also use strings, with explicit NaN/infinity/signed-zero
representation; they remain approximate. Fixture AVG uses DOUBLE with 1e-10
tolerance, and string encoding does not make it exact Decimal.
Native extension arithmetic/types remain as documented in the profile.

Errors may be a JSON-RPC `error` or tool `result.isError`; inspect both. Recoverable
population callback and JSON-list failures expose the versioned
[error contract](population-errors.md), including object/stage/reason and nullable
missing capability. SQL signature/output binding, missing MCP arguments, unknown
tools and cancellation/serious native failures remain external boundaries.
All built-in query/export/describe/list_tables/database_info/execute calls and
unpublished load/reload tools are refused by the demonstrated server configuration.

## Evidence

```sh
bun scripts/verify_population_mcp.ts /abs/path/ossie.duckdb_extension --output results.json
```

The verifier speaks real stdio JSON-RPC to a stock CLI, reads actual resources,
checks the exclusive three-tool schema/list, independent threshold-10/20 object
IDs, totals, all days and date/region/type groups, inherited drill filters and
trace/compile hashes. It also checks a hand-derived joined fixture, hostile inputs,
recovery, and fail-fast operator startup. The same CI MCP job runs it against the
built artifact; remote execution is not claimed by local wiring.

[Local results](research/ossie-req-001/r7-population-mcp-results.json) bind the script,
server, fixtures and actual extension to hashes, and record observed MCP build
`6f9c4ba` and its artifact hash. The registry dependency is observed, not pinned
by this repository. No evidence here proves autonomous Agent interpretation or
complete P0/A1–A8. The PRAGMA resource argument order is documented by the
[upstream server reference](https://duckdb-mcp.readthedocs.io/en/latest/reference/server/)
and verified with actual `resources/read`, rather than only counting resources.

[Transport research results](research/ossie-req-001/r7-transport-artifact-results.json)
separate raw-token comparisons from ordinary JSON round-trip differences, and
record large-response hashes/bytes, full object/group comparisons, fixed-startup
empty-result association and input-limit refusals. Reproduce with:

```sh
bun scripts/verify_population_transport.ts /abs/path/ossie.duckdb_extension --output results.json
```

The raw research verifier deliberately selects the original native SQL functions
to preserve that comparison. It records decoding discrepancies as findings;
successful research does not mean the earlier numeric contract passed. The new
`verify_typed_population_artifact.ts` instead validates the repaired adapter through
ordinary JSON parsing, exact numeric strings, non-finite values and new large
responses. See [the typed contract](population-result.md) for compatibility and
the difference between exact Decimal and approximate floating-point text.

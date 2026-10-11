# Frozen bundle discovery

The population APIs now expose original model vocabulary and successful load
preflight evidence from the same immutable bundle as concept execution. These
functions are independent of `ossie_metrics()`/`ossie_fields()`, which describe
the separately loaded core model.

```sql
SELECT * FROM ossie_population_metrics();
SELECT * FROM ossie_population_fields();
SELECT * FROM ossie_population_capabilities();
```

All three require a loaded bundle. Each bind captures one bundle; prepared
discovery retains it after reload. Discovery neither reads definition files again
nor prepares or executes business queries. Failed loads preserve the old bundle.
Every row carries `profile`, `snapshot_id`, `content_sha256`, `core_sha256`,
`ontology_sha256`, `mapping_sha256`, and `manifest_sha256`.

Metrics return `name`, declared `datatype`, actual `bound_type`, `description`,
declared selected `expression`, `selected_dialect`, `available_dialects_json`,
`synonyms_json`, `examples_json`, `instructions`, `ai_context_json`,
`custom_extensions_json`, `load_preflight_validated`, and `execution_verified`.
Fields return `dataset`, `name`, `qualified_name`, `object_column`, declared
`datatype`, actual `bound_type`, `label`, `description`, the same expression and
AI metadata, `is_time`, `is_time_explicit`, `is_computed`, and validation flags.
Absent optional scalar metadata is an empty string; absent synonyms/examples
are `[]`. Raw AI context/custom extensions are empty strings when absent,
otherwise preserved JSON; they remain descriptive and never authorize execution.

Use metric `name` and field `qualified_name` in requests. Qualified names are
serialized column-reference ASTs, including identifier quoting where needed.
For example, `dimension."label"` references field `label`; `object_column` is
the literal output key `dimension.label`, not a SQL expression. Separate dataset
and field columns retain provenance even when names contain dots or quotes.
Joined bundles retain original namespaces and computed definitions, rather than
generated flattened names. No ontology attribute catalogue is added here;
formal concept rules and identities remain in `ossie_concepts()` for scalar/join
profiles and [`ossie_concepts_v2()`](population-observation.md) for compound
observation identities. Field declarations do not establish grouping capability;
the observation profile permits only its date/product fields as dimensions.

`load_preflight_validated=true` means fields passed the existing combined field
projection type binding, and each metric passed individual no-group binding,
strict AST checks and declared-type compatibility before publication. `bound_type`
comes from that production preflight, not from the logical type label. For GAAP,
SUM is `DECIMAL(38,2)`, COUNT is `BIGINT`, and AVG is `DOUBLE`.
`execution_verified=false` means discovery does not certify execution for the
data or arbitrary combined requests. Runtime arithmetic can still fail; duplicate
output names and other request combinations still require request binding.
Unsupported declarations reject the whole load; no unavailable-definition list
is synthesized. Time flags describe declared/defaulted roles, not calendar support.

Capabilities return one row with `api_version=2.2.0`,
`discovery_schema=ossie-population-discovery-v1`,
`plan_schema=ossie-population-plan-v1` for scalar/join profiles (`plan-v2` for
the observation profile), `ossie_spec_version=0.2.0.dev0`,
`capabilities_json`, and the shared trace columns. These are separate identities:

- API version describes this project's public population SQL contract and its
  documented MCP example. Compatible additive functions/columns use a minor
  version; removals, renamed columns, type or behavioral incompatibilities use a
  major version; compatible corrections use a patch. Consumers read by column
  name and accept additive metadata. This establishes the current baseline,
  including eleven-column compile; it does not version the separate core APIs.
  Version 2.0.0 marks the MCP object's/query's switch from row-object arrays to
  typed envelopes, even though the optional SQL adapters are additive and the
  existing native SQL schemas remain unchanged. The result envelope is separately
  identified as `ossie-population-result-v1`; consumers must decode the new carrier.
- Discovery and plan schema identifiers version their documented metadata shapes.
- Ossie version identifies the accepted document format. Profile identifiers
  identify bounded execution semantics; neither implies full Ossie conformance.
- Engine version/source ID identify the actual DuckDB runtime; definition hashes
  and snapshot identify loaded inputs. None is a build-revision identifier.

`capabilities_json` separates supported profiles from the active profile column,
lists the active concepts, checked capture-join policy, input/request limits,
frozen source/reload behavior, sparse/unsorted/unpaged results and compile-stage
limits. MCP list encoding and transport uncertainty are explicitly properties of
the example adapter; their presence does not mean an MCP server is installed or
running. The [error contract](population-errors.md) covers recoverable population
callback failures; `error_contract` lists external boundaries and serious native
errors that remain outside the envelope. `mcp_example.result_schema` identifies
the [typed result adapter](population-result.md), and `numeric_encoding` is
`typed strings`. Integer/Decimal cells preserve native values without Number
conversion; floating results remain approximate. The retained broad
`exact_numeric_transport_verified=false` flag does not certify arbitrary client
arithmetic or all numeric types as exact. Arbitrary ontology execution, cross-call
reload atomicity and unbounded transport size/truncation guarantees are not
advertised as implemented. The 16 MiB canonical limit is not peak memory.

GAAP remains the primary requirement fixture. The generic fact/dimension fixture,
reversed datasets and computed/quoted names test discovery-driven requests without
GAAP vocabulary. Stock artifact controls are in `verify_discovery_artifact.ts`;
actual MCP resource contents and resource-driven requests are checked by
`verify_population_mcp.ts`. Separate resource calls can observe different bundles
across operator reload; consumers must compare hashes. The example exposes no reload.

API2.2.0 adds the separate `daily_user_set` operation declaration. See
[the daily User set contract](population-daily-users.md) for its independent result
schema, exact UTF-8 budgets and singleton qualified concept. The generic
results/compile metadata above continues to describe existing objects/metrics;
daily SQL ordering differs. Its dedicated logical compile declares actual bound
identity types and the ordered C++ reduction, without calculating members or
promising execution success. A separate daily-only example now publishes two tools and a daily-surface
resource describing exposure; the observation example remains unchanged. The native
`mcp_example_registered=false` field is an unverified example registration
declaration, not an inspection of a running server. Actual tools/list is authoritative.

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
formal concept rules and identities remain in `ossie_concepts()`.

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

Capabilities return one row with `api_version=1.0.0`,
`discovery_schema=ossie-population-discovery-v1`,
`plan_schema=ossie-population-plan-v1`, `ossie_spec_version=0.2.0.dev0`,
`capabilities_json`, and the shared trace columns. These are separate identities:

- API version describes this project's public population SQL contract and its
  documented MCP example. Compatible additive functions/columns use a minor
  version; removals, renamed columns, type or behavioral incompatibilities use a
  major version; compatible corrections use a patch. Consumers read by column
  name and accept additive metadata. This establishes the current baseline,
  including eleven-column compile; it does not version the separate core APIs.
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
running. Uniform errors, arbitrary ontology execution, cross-call reload atomicity,
exact numeric MCP transport and unbounded transport size/truncation guarantees
are not advertised as implemented. The 16 MiB canonical limit is not peak memory.

GAAP remains the primary requirement fixture. The generic fact/dimension fixture,
reversed datasets and computed/quoted names test discovery-driven requests without
GAAP vocabulary. Stock artifact controls are in `verify_discovery_artifact.ts`;
actual MCP resource contents and resource-driven requests are checked by
`verify_population_mcp.ts`. Separate resource calls can observe different bundles
across operator reload; consumers must compare hashes. The example exposes no reload.

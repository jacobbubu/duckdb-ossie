# Single-table population execution

`gaap-scalar-population-v1` implements the first local OSSIE-REQ-001 execution
slice against pinned Ossie `0.2.0.dev0` and DuckDB 1.5.6. It provides one root
EntityType and one derived EntityType with scalar attributes and a scalar
membership rule. It does not represent full Ossie ontology conformance.

## Load a definition bundle

The implementation manifest is separate from the three standard documents:

```json
{
  "profile": "gaap-scalar-population-v1",
  "core": {"path": "gaap.core.json", "name": "gaap", "sha256": "<exact file SHA-256>"},
  "ontology": {"path": "gaap.ontology.json", "name": "gaap_population", "sha256": "<exact file SHA-256>"},
  "mapping": {"path": "gaap.mapping.json", "name": "gaap_mapping", "sha256": "<exact file SHA-256>"}
}
```

Use the actual names from the documents. Relative paths resolve against the
manifest directory. Only explicit local paths are supported. The loader reads
each document once, rejects NUL-containing strings, verifies its bytes against the manifest SHA-256, and parses
those same bytes. It checks cross-document names and exact versions. The checked
[threshold-10 manifest](../test/fixtures/population/bundle-10.json) references the
[reviewed standard definitions](research/ossie-req-001/proposed-definitions/).

First create the native table named by the core source. The GAAP fixture uses
`memory.main.dws_gaap_di` with DATE, VARCHAR and DECIMAL(18,2) columns. Then:

```sql
CALL ossie_bundle_load('test/fixtures/population/bundle-10.json');
SELECT * FROM ossie_concepts();
SELECT * FROM ossie_objects('ValidUser', ['User.product = ''X''']);
SELECT * FROM ossie_concept_query(
  'ValidUser', ['record_count', 'gaap_value_count', 'gaap_sum', 'mean_gaap'],
  ['dws_gaap_di.ds'], ['User.product = ''X''']);
SELECT * FROM ossie_concept_compile('ValidUser', ['mean_gaap'], [], []);
```

Loading requires autocommit and a fully qualified, committed native table.
TEMP tables, views, external sources and generated source columns are refused.
A separate connection reads metadata and rows in the same source transaction.
After whole-batch checks, loading atomically publishes a frozen typed collection.
Failed or interrupted loads retain the previous bundle. `EXPLAIN` does not load.
If failure aborts an explicit caller transaction, roll it back before querying.

The current bundle is database-wide, independently of the ordinary `ossie_load`
model. Source updates become visible only on explicit reload. Each request binds
to a strong reference to its exact bundle and data collection; an already prepared
request keeps that reference across reload. Closing the database releases its
state. Frozen scans carry the collection as an AST dependency; no global history
of old batches is retained merely for lookup.

## Supported semantics and refusals

The root has a single String identity relationship, OneToOne multiplicity, mapped
to a plain, declared single-column primary key. All IDs must be non-NULL and unique
in the entire batch, before any request filters. Identical duplicate rows are also
rejected. OneToOne scalar attributes must be unique among non-NULL values. The
child inherits identity and attributes, with exactly one parent and one scalar
`derived_by` expression. Independent child mapping or overrides are refused.

The mapping has one root referent and one matching root link tree. Every declared
scalar relationship, including identity, must be mapped exactly once to a declared
logical field. Supported targets are String, Date, Decimal, Integer, Float and
Boolean. Source/field types and aggregate result types are checked without implicit
unit conversion. Descriptive extensions do not create execution rules.

Rules and filters support qualified mapped attributes or declared fields, scalar
constants/comparisons, AND/OR/NOT, BETWEEN, IS NULL/IS NOT NULL, COALESCE, CASE,
`+`, `-`, `*`, NULLIF, explicitly protected `/`, and a bounded set of explicit casts. Predicate and conditional
positions must bind as BOOLEAN; numeric truthiness is rejected. Only TRUE creates
membership; NULL does not. Metrics require SUM, COUNT or AVG over declared fields,
with combinations of those aggregates permitted. Nested aggregates, user functions,
qualified functions, subqueries, windows, parameters and unsupported operators are
refused, including when hidden in computed fields. After validation, function ASTs
bind explicitly to engine functions so caller macros cannot shadow AVG or other
allowed functions. Request filters can only narrow population.

Division must have a direct denominator AST of `NULLIF(expression, numeric zero constant)`.
The guard is checked before and after field expansion; `a / safe_field` is rejected even
if that field would expand to NULLIF. Numeric `0` and `0.00` work; strings, Boolean,
NULL, computed zero, CAST wrappers and CASE guards are outside this first slice.
NULLIF itself permits any two accepted scalar arguments, not only comparison to zero.
Core OSSIE_SQL_2026 NULLIFZERO lowering is supported; the alias is not accepted in
ontology rules or request filters, which have no dialect lowering step.

For this pinned DuckDB 1.5.6 profile, integer or DECIMAL division yields DOUBLE,
including `SUM(x) / NULLIF(SUM(y),0)`. This is approximate arithmetic, distinct
from exact DECIMAL storage/addition/SUM. A zero or NULL guarded denominator yields
NULL; opposite signs may cancel an aggregate denominator to zero. Row ratios and
a ratio of sums are different formulas and are verified separately. A declared
Decimal metric cannot silently accept a DOUBLE result. A zero guard does **not**
ensure finite values: floating-point overflow can still yield Infinity/NaN and
participate in predicates under DuckDB semantics. No implicit rounding, denominator
replacement or business-unit conversion is introduced.

The [fixed Ossie expression source](research/ossie-req-001/sources/apache-ossie/core-spec/expression_language.md)
defines / and NULLIF but leaves Decimal precision/scale and zero-division results
unspecified. DuckDB documents [approximate Decimal division](https://duckdb.org/docs/current/sql/data_types/numeric)
and [IEEE zero division](https://duckdb.org/docs/current/sql/dialect/postgresql_compatibility);
our pinned 1.5.6 artifact controls establish the behavior stated here. This is an explicit implementation restriction,
not a claim of full OSSIE_SQL_2026 compliance. The existing core APIs retain their
host arithmetic behavior; this stricter population guard does not rewrite them.
The [synthetic expression verifier](../scripts/verify_expression_artifact.ts) uses
hand-derived ratios, NULL/zero/empty groups, native types, rejected formulas,
caller macro isolation and an explicit non-finite control.

Nonempty `requires` anywhere in the supported ontology closure is refused. So are
explicit value-type components, multiple inheritance, multiple datasets, core joins
and missing/ambiguous identity mappings. The error includes `object`, `stage`,
`reason` and `missing_capability` identifying this bounded profile.

Objects expose `entity_id`, `record_id` and all declared logical fields. In this
single-row-per-entity profile the first two IDs coincide. Missing scalar values
remain SQL NULL. Metrics use the identical bound population predicate and typed
batch. AVG excludes NULL metric values from its denominator. Empty ungrouped
COUNT is zero, SUM/AVG is NULL; empty grouped queries return no rows. Group output
is sparse, with no implicit calendar, sorting, paging or truncation.

Both objects and metric queries accept `include_trace => true`, adding
`_ossie_snapshot_id`, `_ossie_content_sha256`, `_ossie_ontology_sha256` from the
actual bound bundle. Discovery includes all definition hashes and original source.
Content hashes frame physical column names/types and row values, plus row/column
counts; they identify the ordered captured observation batch, not a universal
order-independent dataset digest. Snapshot IDs additionally distinguish reloads.

`ossie_concept_compile` exposes that same query AST as diagnostic SQL with profile,
definition/snapshot hashes, execution target and `executed=false`. Empty metrics
select the object path, with no dimensions. The SQL uses an internal frozen-row
reader whose AST dependency cannot be expressed as SQL text. Replaying the text
is refused. A separate query after reload may bind a different snapshot; use
execution trace to associate actual results with their batch.

## Limits and evidence

Definitions are at most 1 MiB each. A captured batch is at most 10,000 rows and
256 physical columns, with at most 16 MiB of framed canonical values. There are
1–256 logical fields, 1–64 metrics, at most 64 request filters and 256 requested
dimensions. Each rule/filter is at most 16 KiB of SQL and the checked expression
AST is bounded to 1,024 nodes and depth 32. Physical types are VARCHAR without
custom collation, BOOLEAN, signed 8/16/32/64-bit integers, DECIMAL, FLOAT, DOUBLE
and DATE. These are acceptance limits; initial source materialization and query
execution use the host DuckDB memory budget. Overflow is an error, never truncation.
Queries serialize per bundle and forward caller cancellation during execution.

The [independent artifact verifier](../scripts/verify_population_artifact.ts)
loads the real extension into an independently installed stock DuckDB Node binding under Bun. It compares
full object sets, totals, every daily and region/type group, and group drill-down
against an independent CSV/bigint-cents oracle at thresholds 10 and 20, plus hand-derived
NULL/identity boundary controls. It also exercises rejection, atomic reload,
prepared snapshot retention, cancellation and concurrent callers. Reproducible
hashes and results are in [r3-population-artifact-results.json](research/ossie-req-001/r3-population-artifact-results.json).
The current [research record](ossie-execution-research.md) tracks remaining work;
[Expression evidence](research/ossie-req-001/r3-expression-artifact-results.json) records
this slice's actual arithmetic and rejection controls. This slice does not complete P0, multi-table safe joins, calendar or A1–A8 acceptance.

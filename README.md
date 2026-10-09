# duckdb_ossie

[![Main Extension Distribution Pipeline](https://github.com/iqea-ai/duckdb-ossie/actions/workflows/MainDistributionPipeline.yml/badge.svg?branch=main)](https://github.com/iqea-ai/duckdb-ossie/actions/workflows/MainDistributionPipeline.yml)
[![Community Extension](https://img.shields.io/badge/community--extensions-ossie-blue)](https://github.com/duckdb/community-extensions/blob/main/extensions/ossie/description.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

The [Apache Ossie](https://github.com/apache/ossie) (incubating) extension for DuckDB. It reads
Ossie semantic model files — YAML or JSON — and answers semantic queries against the tables in
DuckDB, from a single binary, with no infrastructure. This extension also exposes the
semantic layer via MCP.

**This extension works with DuckDB v1.5.6.**

This local implementation targets the root-level core model at Apache Ossie
[`8dd6732`](https://github.com/apache/ossie/tree/8dd6732da354f22ca71f16d82626a46039ecdcd9),
whose version is `0.2.0.dev0`. This is a pinned development snapshot. Other version
strings, missing versions and legacy `semantic_model` wrappers are rejected.
The core loader refuses ontology and mapping document markers. The separate
`ossie_bundle_load` path executes a bounded single-table population profile, described
in [the profile contract](docs/population-profile.md). See the [bounded implementation record](docs/ossie-execution-research.md).

Ossie is a vendor-neutral file format for semantic models: `datasets` bound to physical tables,
`fields`, declared `relationships`, and `metrics` written as aggregate expressions. It describes
data in place and never transforms it. What the format deliberately leaves open is the *query*
those definitions go into — the joins, the `GROUP BY`, and the grain — because those depend on the
question being asked, and the file does not know the question.

**Every other shipped Ossie implementation is a converter**, translating definitions into some
vendor's semantic layer so that vendor's engine can run them. This one executes the query itself:
it plans the joins, derives the grain, and emits SQL that DuckDB runs. It is the first Ossie
implementation that answers a question rather than translating one.

### Conformance

Claims about a file format should be checkable, so:

- Conformance fixtures are checked against the pinned [`core-spec/ossie-schema.json`](test/fixtures/conformance/ossie-schema.json).
  The runtime loader performs bounded validation of versions, document shape, logical
  datatypes, expressions and references; it is not a complete JSON Schema validator.
- The suite includes six models from `apache/ossie`, vendored verbatim: five published by *other*
  Ossie implementers — Databricks, GoodData, NVIDIA, Omni and OrionBelt — and the spec's own TPC-DS
  example. Five load and answer queries. The Databricks model carries only `DATABRICKS`
  expressions, which this extension does not execute
- Generated SQL is diffed against hand-written TPC-DS equivalents with symmetric `EXCEPT` over
  `dsdgen(sf = 0.01)` data, so correctness is measured against the numbers rather than against our
  own output. This currently covers `total_sales` and `books_sales`. The remaining metrics are
  exercised by the golden-SQL, grain or vocabulary tests, none of which can catch a wrong *number*

Models use the flat document shape of Ossie 0.2.0.dev0, one model per file, with expressions in
`OSSIE_SQL_2026` (the spec's own expression language) or `ANSI_SQL`. What is not yet supported is
listed in [docs/limitations.md](docs/limitations.md): table-backed sources only, and metrics that
span more than one grain or use a window function are refused rather than answered wrongly.

```sql
CALL ossie_load('model.json', rebind => MAP{'tpcds.public': 'tpcds.main'});

SELECT * FROM ossie_metrics();                       -- discover the vocabulary
SELECT * FROM ossie_query(['total_sales'],           -- ask a question
                          ['item.i_brand'],
                          ['date_dim.d_year = 2001']);
```

```
┌───────────────────┬───────────────┐
│   item.i_brand    │  total_sales  │
├───────────────────┼───────────────┤
│ amalgimporto #2   │     709288.31 │
│ edu packamalg #2  │     565064.01 │
└───────────────────┴───────────────┘
```

## Why refusals matter

The primary consumer here is an AI agent, which cannot check the number it gets back. A plausible
wrong answer is therefore worse than an error, and the extension is built to refuse rather than
guess. Ask for `store_productivity` — which sums a fact column and a dimension column — and you get:

```
ossie: these metrics aggregate at 2 different grains (store, store_sales).
Computing them together needs one aggregate per grain, which is not supported yet
```

rather than a number roughly a thousand times too large. [docs/limitations.md](docs/limitations.md)
records every such refusal and why it exists.

## Functions

| Function | Purpose |
|---|---|
| `ossie_load(path, ...)` | Parse and validate a model. Named args: `rebind`, `validate_sources`, `allow_filter_functions` |
| `ossie_datasets()` | Datasets, their sources, keys, and whether each source resolves |
| `ossie_fields()` | Fields with datatype, `is_time`, `is_computed`, expression, synonyms |
| `ossie_metrics()` | Metrics with datatype, expression, synonyms |
| `ossie_relationships()` | Relationships with derived cardinality |
| `ossie_metadata()` | Descriptions, field labels, AI instructions/synonyms/examples, preserved JSON and expression dialects across all model scopes |
| `ossie_query(metrics, dimensions, filters)` | Compile the request and execute it |
| `ossie_compile(metrics, dimensions, filters)` | Return the SQL without running it |
| `ossie_bundle_load(manifest_path)` | Validate a hash-bound core/ontology/mapping bundle and freeze a committed observation batch |
| `ossie_concepts()` | Discover executable concepts, identity, rule and definition/snapshot hashes |
| `ossie_objects(concept, filters)` | Enumerate the objects and logical fields in a frozen concept population |
| `ossie_concept_query(concept, metrics, dimensions, filters)` | Calculate metrics over that same population |
| `ossie_concept_compile(concept, metrics, dimensions, filters)` | Return diagnostic SQL and pinned hashes without executing the request |

`rebind` remaps warehouse-qualified prefixes onto the local catalog: a model saying
`tpcds.public.store_sales` needs `rebind => MAP{'tpcds.public': 'tpcds.main'}` to find local tables.

`ossie_metadata()` returns `kind`, optional field `dataset`, `name`, `description`,
`label`, `instructions`, `synonyms`, `examples`, `ai_context`, `custom_extensions`,
`expression_dialect`, `available_dialects`, and `is_time_explicit`. The JSON columns
preserve the original JSON values, including extra AI context keys; whitespace is
normalized. A string AI context is exposed as instructions and also retained as JSON.
Vendor extensions are descriptive only: units are preserved with their vendor source,
and are not inferred, converted or executed. Available dialects record the definition;
`expression_dialect` identifies the one actually selected. Fields and metrics are
executable through the existing query vocabulary; other rows describe model structure.

Temporal logical types default to `is_time=true`; an explicit `dimension.is_time`
overrides that default, including opting an audit timestamp out. This role does not
cast physical columns. Natural-language metadata never creates a calculation rule.

`ossie_compile` is a first-class output, not a debug aid — it is the useful surface wherever DuckDB
is not the executor. It runs the identical compiler `ossie_query` does, so the SQL you inspect is
the SQL that would run.

The population APIs implement `gaap-scalar-population-v1`: one root entity and one
scalar derived entity, on a single committed native table. Loading checks identity
across the entire captured batch. Source updates require explicit reload; prepared
requests retain their original bundle. Objects and metrics use the same population
predicate. Default groups are sparse, AVG counts non-NULL inputs, and empty SUM/AVG
returns NULL. Full ontology and calendar completion remain later work. A bounded frozen
fact-dimension profile and a separate population MCP example are now available;
see [the profile contract](docs/population-profile.md) for the manifest, limits
and SQL examples. `ossie_concept_compile` SQL is diagnostic and
requires an internal frozen-data dependency; it cannot be replayed as ordinary SQL. Its
[versioned plan_json](docs/population-plan.md) describes the generated AST and
immutable definition/capture provenance; it is not a physical plan.

## Serving a model to an AI agent

[examples/server.sql](examples/server.sql) publishes the model over MCP:

```sh
duckdb -init examples/server.sql
```

Point Claude Desktop at it:

```json
{"mcpServers": {"ossie": {"command": "duckdb",
                          "args": ["-init", "/abs/path/to/server.sql"]}}}
```

The agent gets a `semantic_query` tool plus `metrics` and `dimensions` resources to discover names
from. Within that tool it can reach nothing but the model's own vocabulary: filters are allowlisted,
subqueries are refused outright, and no argument lets a caller widen that.

That holds for the connection as a whole, not just the tool, because `server.sql` disables every
built-in `duckdb_mcp` tool when it starts the server — `query`, `export`, `list_tables`, `describe`
and `database_info`. `tools/list` returns only `semantic_query`, and calling any of the others is
refused with *Tool not found*. `scripts/mcp_check.ts` asserts this on every push.

> **Writing your own server?** Disabling `query` is not sufficient on its own — `export` also takes
> an arbitrary SQL argument and reaches the same data. Disable all five, or point the server only at
> a database you are willing to expose in full.

The frozen population APIs have a separate [MCP server example](examples/population_server.sql)
with concept/metric/field/capability discovery and object/query/compile tools.
The [population discovery contract](docs/population-discovery.md) separates declared
types from actual load-preflight types and versions this project's API independently
of the Ossie format. See [its calling contract](docs/population-mcp.md)
and [versioned error boundaries](docs/population-errors.md)
for explicit JSON list encoding, trusted operator setup, trace, refusals and numeric transport limits.

## Building & Contributing

No vcpkg and no build-time dependency fetching. JSON parsing uses the yyjson DuckDB already vendors;
YAML support is a single vendored header ([third_party/fkyaml](third_party/fkyaml), MIT).

```sh
git submodule update --init --recursive
make
```

Submodules are pinned by recorded commit (DuckDB `v1.5.6`). DuckDB's parser and planner internals
are not a stable API, so the pin is deliberate and upgrades are expected to need work. The first
build compiles DuckDB from source and takes a while; later builds are incremental.

```sh
make test
```

For the architecture and how to work on the compiler, see
[docs/architecture.md](docs/architecture.md).

Issues and pull requests are welcome — most of all a semantic model this extension reads wrongly,
since models written by other implementers are the only ones that can catch a disagreement about
the format. [CONTRIBUTING.md](CONTRIBUTING.md) covers the build, the invariants a change must hold
to, when a differential test is required, and the exact `clang_format` version CI expects.

If the extension is useful to you, a ⭐ on [the repository](https://github.com/iqea-ai/duckdb-ossie)
helps others find it.

## License

MIT. See [LICENSE](LICENSE), and [NOTICE](NOTICE) for third-party content redistributed with this
repository — the fkYAML submodule (MIT) and the Apache-2.0 licensed Ossie models used as conformance
test data.

This project is an independent implementation that reads the Apache Ossie file format. It is not
affiliated with, endorsed by, or an official product of the Apache Software Foundation. "Apache
Ossie" and "Apache" are trademarks of the Apache Software Foundation.

Project quality commands and the core model/policy capture contract are documented
in [docs/quality-controls.md](docs/quality-controls.md).

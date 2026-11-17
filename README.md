# duckdb_ossie

[![Main Extension Distribution Pipeline](https://github.com/iqea-ai/duckdb-ossie/actions/workflows/MainDistributionPipeline.yml/badge.svg?branch=main)](https://github.com/iqea-ai/duckdb-ossie/actions/workflows/MainDistributionPipeline.yml)
[![Community Extension](https://img.shields.io/badge/community--extensions-ossie-blue)](https://github.com/duckdb/community-extensions/blob/main/extensions/ossie/description.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

The [Apache Ossie](https://github.com/apache/ossie) (incubating) extension for DuckDB. It reads
Ossie semantic model files — YAML or JSON — and answers semantic queries against the tables in
DuckDB, from a single binary, with no infrastructure. This extension also exposes the
semantic layer via MCP.

**This extension works with DuckDB v2.0.0.** On DuckDB v1.5.6, `INSTALL ossie FROM community` gives the 0.2.0 build.

> **This repository is ahead of the community registry.** On DuckDB v2.0.0,
> `INSTALL ossie FROM community` gives the 0.3.0 build of
> [iqea-ai/duckdb-ossie](https://github.com/iqea-ai/duckdb-ossie), which this repository was forked
> from. What [CHANGELOG.md](CHANGELOG.md) lists under *Unreleased* is not in that build: it has seven
> `ossie_*` functions where this repository has seventeen, and its `ossie_query` has no
> `metric_filters` argument. To use what this README describes, build the extension and load the
> file, as in [Running this repository's build](#running-this-repositorys-build).

Ossie is a vendor-neutral file format for semantic models: `datasets` bound to physical tables,
`fields`, declared `relationships`, and `metrics` written as aggregate expressions. It describes
data in place and never transforms it. What the format deliberately leaves open is the *query*
those definitions go into — the joins, the `GROUP BY`, and the grain — because those depend on the
question being asked, and the file does not know the question.

**The converters in apache/ossie translate** definitions into some vendor's semantic layer so that
vendor's engine can run them. This one executes the query itself: it plans the joins, derives the
grain, and emits SQL that DuckDB runs.

### Conformance

Claims about a file format should be checkable, so:

- Models are validated against the format's own [`core-spec/ossie-schema.json`](https://github.com/apache/ossie/blob/main/core-spec/ossie-schema.json)
- The suite includes fifteen models from `apache/ossie`, vendored verbatim: the spec's own examples
  (TPC-DS, flights, renamed fields) and its compliance models (wide-table retail and stores, the
  `scott` query model, the portability ladder m0–m3). Twelve load and answer queries. The three
  refusals are the portability models that name a column without its dataset, which the spec
  forbids
- Generated SQL is diffed against hand-written TPC-DS equivalents with symmetric `EXCEPT` over
  `dsdgen(sf = 0.01)` data, so correctness is measured against the numbers rather than against our
  own output. This currently covers `total_sales`, `store_productivity` and
  `customer_lifetime_value`. The remaining metrics are exercised by the golden-SQL, grain or
  vocabulary tests, none of which can catch a wrong *number*

Models use the document shape of Ossie 0.3.0.dev0, one model per file, with expressions in
`OSSIE_SQL_2026` (the spec's own expression language, written as `expression.sql`) or `ANSI_SQL`.
Dataset-scoped metrics are addressed as `dataset.metric`, and a model-scoped metric may combine them
with `AGG(dataset.metric)`. What is not yet supported is
listed in [docs/limitations.md](docs/limitations.md): table-backed sources only, and metrics that
use a window function are refused rather than answered wrongly.

```sql
CALL ossie_load('model.json', rebind => MAP{'tpcds.public': 'tpcds.main'});

SELECT * FROM ossie_metrics();                       -- discover the vocabulary
SELECT * FROM ossie_query(['total_sales'],           -- ask a question
                          ['item.i_brand'],
                          ['date_dim.d_year = 2001']);
```

```
┌───────────────────┬───────────────┐
│      i_brand      │  total_sales  │
├───────────────────┼───────────────┤
│ amalgimporto #2   │     709288.31 │
│ edu packamalg #2  │     565064.01 │
└───────────────────┴───────────────┘
```

## Why refusals matter

The primary consumer here is an AI agent, which cannot check the number it gets back. A plausible
wrong answer is therefore worse than an error, and the extension is built to refuse rather than
guess. Ask for `total_sales` grouped by `store.s_state` on a model where the sales-to-store
relationship is declared many-to-many, and you get:

```
ossie: E_NO_PATH: dimension "store.s_state" is a field of "store", which metric
"total_sales" cannot reach from "store_sales": joining "store" repeats each
"store_sales" row once per match, which would inflate every aggregate.
Relationship "store_sales_to_store" is many_to_many
```

rather than a number inflated by the number of matches. Where the model *does* determine the
answer the extension does the work instead: `store_productivity`, which sums a fact column and a
dimension column, is aggregated one grain at a time and the ratio taken over the results, rather
than summing employee counts once per sale. [docs/limitations.md](docs/limitations.md) records
every refusal and why it exists.

## Functions

| Function | Purpose |
|---|---|
| `ossie_load(path, ...)` | Parse and validate a model. Named args: `rebind`, `validate_sources`, `allow_filter_functions`, `left_joins` |
| `ossie_datasets()` | Datasets, their sources, keys, and whether each source resolves |
| `ossie_fields()` | Fields with datatype, `is_time`, `is_computed`, expression, synonyms |
| `ossie_metrics()` | Metrics with datatype, expression, synonyms |
| `ossie_relationships()` | Relationships with derived cardinality |
| `ossie_metadata()` | Descriptions, labels, AI instructions/synonyms/examples, preserved JSON and expression dialects |
| `ossie_query(metrics, dimensions, filters)` | Compile the request and execute it. Named arg: `metric_filters` |
| `ossie_query(query)` | The same, with the request written as one JSON object, which can also name its output columns |
| `ossie_compile(metrics, dimensions, filters)` | Return the SQL without running it. Named arg: `metric_filters` |
| `ossie_compile(query)` | The same, for a JSON query |

A request names metrics as `metric` or `dataset.metric` and dimensions as `dataset.field`; each
output column takes the final part of its name (`item.i_brand` becomes `i_brand`), and two entries
that would share one are refused, as the Wide Table specification requires. `metrics` may be empty:
`ossie_query([], ['item.i_brand'])` returns the distinct brands, read from the one dimension dataset
that joins the others without repeating its rows. Each entry of `filters` is a condition on the rows
of one dataset, applied before aggregation; a condition on a metric goes in
`metric_filters => ['total_sales > 1000']` and is applied after.

The same request can be written as one JSON object, the query form of the Wide Table specification,
and that form is where an output column gets a name of the caller's choosing:

```sql
SELECT * FROM ossie_query('{
  "metrics":    [{"metric": "total_sales", "alias": "sales"}],
  "dimensions": ["item.i_brand"],
  "filter":     "date_dim.d_year = 2001"
}');
```

An entry of `metrics` or `dimensions` is a reference, or an object holding the reference under
`metric` or `field` with an `alias` beside it; `filter` is one condition. A key the form does not
define, or a `null`, is refused rather than ignored. `metric_filters` stays a named argument.

`rebind` remaps warehouse-qualified prefixes onto the local catalog: a model saying
`tpcds.public.store_sales` needs `rebind => MAP{'tpcds.public': 'tpcds.main'}` to find local tables.

`ossie_compile` is a first-class output, not a debug aid — it is the useful surface wherever DuckDB
is not the executor. It runs the identical compiler `ossie_query` does, so the SQL you inspect is
the SQL that would run.

## Population profiles

A separate, opt-in path executes bounded *population* profiles: a hash-bound core/ontology/mapping
bundle is loaded with `ossie_bundle_load(manifest_path)`, the committed source batch is captured and
frozen, and the frozen population is then queried through its own functions: `ossie_program()`
describes the loaded profile, `ossie_population_compile(request_json)` and
`ossie_population_json(request_json)` take one JSON request whose `kind` selects objects, metrics,
daily_users, daily_states, relation_links, fact_users, fact_windows or history_links, and
`ossie_concepts()`, `ossie_objects(...)` and `ossie_concept_query(...)` return native tables for the
objects/metrics profiles. These profiles never
relax the generic core refusals above; what each one supports, and refuses, is written down in
[docs/population-profile.md](docs/population-profile.md), with the discovery, plan, result and
error contracts in the neighbouring `docs/population-*.md` files. The frozen profiles are also
served over MCP by [examples/population_server.sql](examples/population_server.sql) and the other
`examples/*_server.sql` files.

## Serving a model to an AI agent

[examples/server.sql](examples/server.sql) publishes the model over MCP:

```sh
duckdb -init examples/server.sql
```

As written, the script installs `ossie` from the registry, and with the 0.3.0 build there every
`semantic_query` call fails, because the tool passes `metric_filters`. Until the registry carries this
repository's code, serve the build from this tree instead:
[Running this repository's build](#running-this-repositorys-build) has the two commands.

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
refused with *Tool not found*. `scripts/mcp_check.py` asserts this on every push.

> **Writing your own server?** Disabling `query` is not sufficient on its own — `export` also takes
> an arbitrary SQL argument and reaches the same data. Disable all five, or point the server only at
> a database you are willing to expose in full.

## Building & Contributing

No vcpkg and no build-time dependency fetching. JSON parsing uses the yyjson DuckDB already vendors;
YAML support is a single vendored header ([third_party/fkyaml](third_party/fkyaml), MIT).

```sh
git submodule update --init --recursive
make
```

Submodules are pinned by recorded commit (DuckDB `v2.0.0`). DuckDB's parser and planner internals
are not a stable API, so the pin is deliberate and upgrades are expected to need work. The first
build compiles DuckDB from source and takes a while; later builds are incremental.

```sh
make test
```

### Running this repository's build

`make` leaves the extension at `build/release/extension/ossie/ossie.duckdb_extension`. Two things
about that file differ from a registry build. It is not signed, so DuckDB has to be started with
`-unsigned`. And a local build is stamped with DuckDB's commit rather than with the release name
`v2.0.0`, so the released DuckDB v2.0.0 CLI refuses it until told to accept the mismatch. On macOS
that CLI also refuses a relative path, so give the path in full:

```sh
duckdb -unsigned
```

```sql
SET allow_extensions_metadata_mismatch = true;
LOAD '/abs/path/to/duckdb-ossie/build/release/extension/ossie/ossie.duckdb_extension';
```

The shell the build produces, `./build/release/duckdb -unsigned`, was built from the same commit and
loads the file without the `SET`.

The scripts under `examples/` install `ossie` from the registry. To run one against the local build,
swap those two lines for the two above. From the repository root:

```sh
EXT="$PWD/build/release/extension/ossie/ossie.duckdb_extension"
sed -e "s|^INSTALL ossie FROM community;\$|SET allow_extensions_metadata_mismatch = true;|" \
    -e "s|^LOAD ossie;\$|LOAD '$EXT';|" examples/server.sql > /tmp/server.sql
duckdb -unsigned -init /tmp/server.sql
```

The same two substitutions work for every `examples/*_server.sql`.

For the architecture and how to work on the compiler, see
[docs/architecture.md](docs/architecture.md).

Issues and pull requests are welcome — most of all for semantic models this extension misinterpets,
since models written by other implementers are the only ones that can catch a disagreement about
the format. Refer to [CONTRIBUTING.md](CONTRIBUTING.md).

If the extension is useful to you, a ⭐ on [the repository](https://github.com/iqea-ai/duckdb-ossie)
helps others find it.

## License

MIT. See [LICENSE](LICENSE), and [NOTICE](NOTICE) for third-party content redistributed with this
repository — the fkYAML submodule (MIT) and the Apache-2.0 licensed Ossie models used as conformance
test data.

This project is an independent implementation that reads the Apache Ossie file format. It is not
affiliated with, endorsed by, or an official product of the Apache Software Foundation. "Apache
Ossie" and "Apache" are trademarks of the Apache Software Foundation.

# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The version recorded here is the **extension version** (what the
[community-extensions descriptor](https://github.com/duckdb/community-extensions/blob/main/extensions/ossie/description.yml)
pins). The DuckDB version each release targets is noted separately.

## [Unreleased]

### Changed
- **A refusal the Wide Table specification defines carries its error code, and a request with
  several faults gets the one the specification lists first.** A message now reads
  `ossie: E_NO_PATH: dimension "orders.order_id" is a field of "orders", which metric ...`: the
  code from Section 8 of the specification, then the text. Code that matched a whole message, or
  its start, must allow for the code; the text after it is unchanged except where noted here.
  The checks run in the specification's order, so a request with an unknown name and a filter on
  two datasets is refused for the name. What else changed with it:
  - A dimension that names a metric is `E_METRIC_IN_DIMENSIONS` and a metric that names a field is
    `E_FIELD_IN_METRICS`, each saying where the entry belongs. Both used to be reported as a name
    the model does not declare.
  - A model-scoped metric that reads a field outside every aggregate (`SUM(orders.amount) -
    orders.order_id`) is refused, naming the field. `ossie_compile` used to return SQL for it,
    which DuckDB then refused to bind.
  - A dimension or filter on a dataset a metric cannot reach names that metric, the dataset it
    aggregates and the relationship that stops it, and tells a dimension no requested metric
    reaches (`E_NO_PATH`) from one only some reach (`E_PARTIAL_DIMENSION_REACHABILITY`). The old
    texts (`joining "store" repeats each "store_sales" row ...` on its own, and `... which "store"
    cannot join without repeating its rows`) are gone; the first survives as the reason at the end
    of the new message.
  - A request that cannot be read (a dimension that is not a name, a filter that does not parse or
    uses what the filter policy forbids) is refused before any name in it is looked up.
  Refusals the specification has no code for carry none. `docs/limitations.md` lists the codes.
- **Breaking: three metric shapes the Wide Table specification refuses are refused.** In a
  model-scoped metric each aggregate runs over the one dataset its arguments read, and a request
  for a metric that names no such dataset fails with `E_AMBIGUOUS_MEASURE_GRAIN`. These used to be
  answered:
  - An aggregate whose arguments read several datasets (`SUM(store_sales.ss_quantity *
    item.i_current_price)`), which was aggregated over the dataset that determines the others.
  - An aggregate whose `FILTER` reads a dataset its arguments do not (`SUM(orders.amount) FILTER
    (WHERE customers.region = 'west')`).
  - An aggregate whose arguments read no dataset (`COUNT(*)`, `COUNT(1)`, `COUNT(*) FILTER (WHERE
    orders.status = 'completed')`), which counted the rows of the grain the request's other
    metrics shared.
  The model still loads; the refusal comes when the metric is requested. A metric declared on a
  dataset is not affected and is where such a count belongs: `orders.completed_count` declared as
  `COUNT(*) FILTER (WHERE status = 'completed')` counts `orders`, alone, beside metrics of other
  datasets, and in a metric filter. An aggregate written directly in `metric_filters` follows the
  same rule, so `'COUNT(*) > 1'` is refused where it used to count the request's grain; name a
  dataset's metric (`'customers.customer_count > 1'`). `AGG(dataset.metric)` is refused at load
  when the dataset metric's aggregate has no column in its arguments, now also when it has a
  `FILTER` that names one. Population bundles are unchanged: a bundle designates its fact grain,
  and an aggregate over it may still read a dataset the fact rows determine.
- **Breaking: `filters` take conditions on rows; a condition on a metric goes in `metric_filters`.**
  Each entry of `filters` is a filter as the Wide Table specification defines one: applied before
  aggregation, reading the fields of one dataset, with no metric, aggregate or window function.
  `ossie_query` and `ossie_compile` gain the named parameter `metric_filters => [...]` for
  conditions applied after aggregation, which is where `'total_sales > 100'` now goes; in `filters`
  it is refused, and the message names the parameter. An entry reading two datasets
  (`'orders.amount > 100 AND customers.region = ''EAST'''`) is refused as well. Pass each dataset's
  condition as its own entry; entries are combined with AND, as before. A row condition joining two
  datasets with OR can no longer be written. The MCP example's `semantic_query` tool takes
  `metric_filters` beside `filters`.
- **Breaking: output columns are named without the dataset.** `ossie_query` and `ossie_compile`
  name each column by the final part of its reference, as the Wide Table specification's output
  name: `customers.region` is the column `region`, `orders.total_revenue` is `total_revenue`, where
  they used to be `"customers.region"` and `"orders.total_revenue"`. SQL that reads the result by
  the quoted qualified name must drop the dataset. Two entries with one output name, compared
  without case, are refused naming both, where they used to produce two columns of one name.
  Population results are unchanged: their columns were never qualified this way.
- **Breaking: models are Ossie 0.3.0.dev0 documents (apache/ossie da73e0e).** `version` must be
  `"0.3.0.dev0"`; a 0.2.0.dev0 document is refused at load naming the supported string. What changed
  in the format, and is now read:
  - `expression.sql` is the OSSIE_SQL_2026 text; `dialects` is optional beside it and carries only
    other dialects. An `expression` with neither, an empty `dialects` array, or only dialects this
    extension cannot execute is refused naming the field or metric.
  - `custom_extensions` is now `nonportable_extensions`, and each entry's `data` is an object rather
    than a string. `vendor_name` is any nonempty string; the old fixed vendor list is gone. The
    payload is surfaced verbatim by `ossie_metadata()` and never interpreted.
  - A dataset-scoped metric's expression names its dataset's fields by bare name and a source
    column as `source.column`; a model-scoped metric qualifies every column as `dataset.field`.
    Relationship `from_columns`/`to_columns` name fields. A model whose metric names a bare column
    is refused with the qualified spelling in the message.
  - The vendored conformance models are re-taken from that commit: the per-converter fixtures the
    previous snapshot carried no longer exist upstream, and the spec's examples and compliance
    models (retail, stores, scott, portability m0–m3) take their place. Twelve of fifteen load; the
    three `*_bare` portability models are refused correctly. `test/fixtures/conformance/README.md`
    has the table.
- All fixtures, examples and the descriptor's inline model are 0.3.0.dev0 documents, and the
  population bundle manifests re-pin the hashes of the migrated files.
- CI's `mcp-round-trip` job runs again and `CI gate` requires it. The community registry now serves
  duckdb_mcp for DuckDB v2.0.0 (teaguesterling/duckdb_mcp#103, closed 2026-10-28).
- The population surface is built against DuckDB v2.0.0 alongside the core, on the same parse-tree
  API port as 0.3.0. `duckdb_functions()` names every population parameter too.
- **A prepared `ossie_query` or `ossie_compile` follows the model current at `EXECUTE`.** DuckDB 2.0
  rebinds a prepared statement whose plan scans a catalog table, which the SQL these two compile
  does. Population requests read a frozen capture, so a prepared one keeps its snapshot as before.
  Recorded in docs/limitations.md.
- A string with trailing text after the date (`'2024-02-29garbage'`) is refused where DuckDB 2.0's
  own `CAST` refuses it; 1.5.6 accepted it. The population profile document no longer claims
  trailing text is accepted.
- The isolated finite DATE minimum (day -2147483648, `5877642-06-23 (BC)`) is emitted as
  `DATE '1970-01-01' + CAST(-2147483648 AS INTEGER)` in compiled SQL. DuckDB renders that day but
  refuses to parse it back, and DuckDB 2.0 renders a DATE constant as text, so the rendered SQL of a
  display range ending there now replays where it used to fail.
- `scripts/full_functionality_check.sh`'s fallback loads the build tree's own `ossie` and `tpcds`
  artifacts into the build-tree shell. DuckDB 2.0 no longer links extensions into that shell, so the
  fallback had been asserting against a shell with nothing registered.
- The C++ contract tests link `ossie` explicitly, since DuckDB 2.0 links extensions per target and
  resolves the default set only after every extension is added.
- A population expression test that defined a macro named `nullif` now drops it first; `nullif` is a
  reserved word in DuckDB 2.0.
- **A join path is searched the way the Wide Table specification defines one.** A path crosses a
  relationship only toward its one side and visits no dataset twice, and only the datasets a
  request joins are checked. A request used to be refused as ambiguous when a second route existed
  that no path could take: on the specification's `stores` model, `store_returns` by `date_dim.year`
  was refused because `store_sales` reaches `date_dim` as well and shares `item` with it. Such
  requests are answered now. Two real paths are still refused, including when one is shorter.
- **A count over a group its grain has no row for is 0.** When metrics at two grains are joined on
  the dimension values, a group only one grain produced left the other grain's `COUNT`,
  `COUNT(*)`, `COUNT(DISTINCT ...)` and `APPROX_COUNT_DISTINCT` NULL. They are 0 now, in the result
  and in a filter on the metric; other aggregates stay NULL, as they are over no rows.

- **Breaking: a name in a field expression is a column of the source, also where another field has
  that name.** Ossie 0.3 resolves every name in a field expression to a column of the dataset's
  `source` and has no field defined over another field. `ossie_load` used to put the other field's
  expression in place of the name, so with a field `amount` defined as `amount_cents / 100.0` and a
  field `doubled` defined as `amount * 2`, `doubled` read the field. It reads the source's `amount`
  column now, as the specification says, and the two give different numbers wherever the field is
  not simply its column. A model that relied on the old reading has to write the expression out.
  Where the source is in the catalog at load and has no column of that name, the model is refused,
  naming the field and the name; where it is not, DuckDB's binder reports the missing column when a
  query reads the field. Two fields named after each other's columns are no longer a cycle.

### Added
- **`ossie_sql(query)` runs a `SELECT` written over the model's relations, the Relational Query
  Interface of the Ossie specification.** A dataset is the relation `model.dataset` and its fields
  are its columns; `AGG(measure)` evaluates one of its dataset-scoped metrics over the rows of a
  group, in `SELECT`, `HAVING` or `ORDER BY`:
  `SELECT * FROM ossie_sql('SELECT deptno, AGG(sum_sal) FROM scott_model.emp GROUP BY deptno')`.
  The rest of the query is DuckDB SQL, so it can ask what a request cannot: a filter that holds a
  subquery, a group that is an expression, `ROLLUP`, an aggregate over measure values, a join the
  model has no relationship for. Over a join a measure counts each row of its own dataset once,
  whatever the join repeats or pads, with the rows told apart by the dataset's `primary_key`; a
  dataset that declares none has no measure there. A block that does not aggregate passes a measure
  on unevaluated, by name, renamed or within `*`, through a subquery in `FROM` or a CTE, and the
  block that reads it evaluates it over the rows that came through:
  `SELECT deptno, AGG(payroll) FROM (SELECT deptno, sum_sal AS payroll FROM scott_model.emp WHERE
  sal >= 2000) GROUP BY deptno`. Anywhere else a bare measure is refused, as the specification
  requires: as an operand or an argument, in `WHERE`, as a key to group by or join on, in a block
  that aggregates, under `DISTINCT` or a set operator. A query that would return a measure
  unevaluated is refused too, so `SELECT *` over a relation that has measures is, and a window
  measure is refused where `AGG` names it. Each refusal says what the measure is and where it
  stands.
  The query runs with the caller's rights and reads any table the caller can, so the MCP example
  does not expose it. See [docs/limitations.md](docs/limitations.md#the-relational-query-interface).
- **apache/ossie's wide-table compliance cases run against the extension.** The 46 cases of
  `compliance/wide_table` are vendored with their data, `scripts/wide_table_compliance.py` submits
  each query as written through `ossie_query(query)` and classifies the outcome, and
  `scripts/full_functionality_check.sh` fails when a case leaves its pinned status. All 46 pass.
  The copy is apache/ossie `e4f545a`, where a case's expected result is `expected.txt`, a table as
  upstream's `ost` runner prints it; the worked example that commit adds to the specification
  (Appendix B.6, `store_productivity` over two grains) is a test here and gives the 30 it states.
- **apache/ossie's query compliance cases are vendored and measured.** The 32 cases of
  `compliance/query` are written for the Relational Query Interface, SQL with `AGG()` over a
  model's relations, and `scripts/query_compliance.py` measures each two ways. As written, through
  `ossie_sql`: all 32 return the expected rows. As one `ossie_query` request asking the same
  question: 12 have such a request, and all 12 return the expected rows; 20 have none, each for a
  stated
  reason. Both statuses are pinned and `scripts/full_functionality_check.sh` checks them.
- **A request can be written as the Wide Table specification's JSON query, and can name its output
  columns.** `ossie_query(query)` and `ossie_compile(query)` take one string holding an object with
  `metrics`, `dimensions` and `filter`. An entry is a reference, or an object that adds an alias:
  `{"metric": "orders.total_revenue", "alias": "revenue"}` in `metrics`,
  `{"field": "customers.region", "alias": "area"}` in `dimensions`. The alias is the column's name
  in the result and counts as its output name, so two references that end in the same name can be
  requested together once one has an alias. A key or property the form does not define, a `null`,
  or a value of the wrong type is refused with `E_INVALID_QUERY`. The list form is unchanged and
  has no aliases; `metric_filters` is a named argument of both.
- **A request can have dimensions and no metric.** `ossie_query([], ['customers.region'])` returns
  the distinct regions, where it used to be refused. The rows come from the one dimension dataset
  that joins every other without repeating its own rows, which the Wide Table specification calls
  the anchor; a request where no dataset or more than one qualifies is refused, as is a filter on a
  dataset the anchor cannot reach that way. A metric filter that names a metric takes the place of
  the anchor: `ossie_query([], ['item.i_brand'], metric_filters => ['total_sales > 1000'])` reads
  the rows `total_sales` aggregates. A request with neither metrics nor dimensions is still refused.
- **Dataset-scoped metrics.** A dataset's `metrics` are loaded as `dataset.metric` and requested,
  filtered on and described under that name; their expressions are resolved against the dataset's
  fields and its `source.column` references. A model-scoped metric may evaluate one with
  `AGG(dataset.metric)`, which inlines the dataset metric's expression so each side of a ratio keeps
  its own grain. `AGG` is refused in a field or a dataset metric, with anything but one
  `dataset.metric` argument, naming a metric that is not dataset-scoped, or naming one whose
  aggregate has no column (`COUNT(*)`), since inlined into another grain it would count the wrong
  rows.
- **A filter on a metric is evaluated at that metric's grain.** `['orders.total_revenue'], [],
  ['customers.customer_count > 1']` aggregates `customers` in its own subquery and filters on that
  result, where it used to be folded into the `orders` HAVING clause and count the wrong rows.
- **Population profiles.** `ossie_bundle_load(manifest_path)` loads a hash-bound core, ontology and
  mapping bundle and captures the committed source batch, so a population is queried as frozen data
  through `ossie_program()`, `ossie_population_compile(request_json)` and
  `ossie_population_json(request_json)`, with `ossie_concepts()`, `ossie_concept_query()`,
  `ossie_objects()` and `ossie_metadata()` describing it. Contracts in docs/population-profile.md
  and the neighbouring `docs/population-*.md`; served over MCP by `examples/population_server.sql`
- **Metrics that aggregate at more than one grain are computed, not refused.** `store_productivity`
  (`SUM(store_sales.ss_ext_sales_price) / NULLIF(SUM(store.s_number_employees), 0)`) now aggregates
  each side over its own dataset and takes the ratio over the results, rather than refusing. Each
  grain gets a grouped subquery; the subqueries are joined NULL-safely on the dimension values, so a
  group only one side produced is kept; row filters apply inside every subquery and metric filters
  to the joined result. A filter or dimension a grain cannot reach without fanning out is refused,
  naming the grain, since applying it to one side of a ratio only would be a different metric.
  Checked against hand-written SQL over TPC-DS data in `ossie_differential.test`
- **`ossie_load(..., left_joins => true)` joins dimensions with `LEFT JOIN`** instead of the default
  `INNER`, so a fact row whose foreign key is NULL or dangling is kept under NULL dimension values
  rather than dropped, and a total no longer shrinks when a dimension is added. Ossie declares no
  join type, so the policy is set per load like `allow_filter_functions`. Fan-out and ambiguous-path
  refusals are unchanged

### Fixed
- **`CURRENT_DATE`, `CURRENT_TIMESTAMP` and `CURRENT_TIME` work, with and without parentheses.** The
  language writes each both ways; only `CURRENT_DATE()` ran. Without parentheses the name was taken
  for a column: a field failed in DuckDB's binder when queried, a metric was refused at load for an
  undeclared or unqualified name, and a filter got `E_NAME_NOT_FOUND`. `CURRENT_TIMESTAMP()` and
  `CURRENT_TIME()` loaded and then failed as functions DuckDB does not have. All six now give the
  `DATE`, `TIMESTAMP` and `TIME` the language names, read off `get_current_timestamp()`, and a
  filter may use them without `allow_filter_functions`. An `ANSI_SQL` expression may write the bare
  names too, with DuckDB's own types. A column that has one of these names is written in quotes.
- **`IF(condition, true_result, false_result)` loads.** It was refused as a function the language
  does not define; it is `IFF` under its other name and becomes the same `CASE`.
- **`DATE_TRUNC` and `DATEADD` keep a `DATE` a `DATE`.** DuckDB's `date_trunc` and `date_add`
  return a `TIMESTAMP` at midnight for a `DATE` input; the language keeps the input type. Year,
  quarter, month, week and day parts now lower to the extension's own `ossie_date_trunc` and
  `ossie_dateadd`, so a daily field groups and compares as a `DATE`. The compiled SQL names those
  functions, so it runs only where the extension is loaded. `DATEADD` no longer emits an `INTERVAL`,
  so it sidesteps the optimizer defect noted under 0.2.0
- **A function outside OSSIE_SQL_2026 is refused at load, not passed to DuckDB.** An
  `OSSIE_SQL_2026` field, metric or request filter that calls a function the language does not
  list (`MD5`, `STRING_AGG`, an `INTERVAL` literal, `TO_CHAR`) fails at load naming it, and
  suggests an `ANSI_SQL` variant. Previously it reached DuckDB's binder and meant whatever DuckDB
  said. `REGEXP_COUNT`, `TRUNCATE`, `APPROX_PERCENTILE`, `PERCENTILE_CONT`/`DISC`, `EXTRACT` and
  `POSITION` gained lowerings so that the listed functions all load
- **Quoted column names match exactly.** When a dataset's source is a table or view in the catalog,
  a quoted column reference in a field or metric that matches a column only by ignoring case is
  refused at load with the column's real spelling, as the language requires. A source that is not
  present at load is not checked

### Docs
- `docs/limitations.md` now labels each place where the extension departs from Ossie 0.3 rather
  than describing it as though the specification left the point open: the `INNER` default against
  Layer 3's `LEFT JOIN`, name resolution for keys and relationship columns, `AGG()` of a
  `COUNT(*)` measure, `REGEXP_LIKE`, window functions, the filter function policy, and Layer 2's
  query-defined measures and C6 measure discovery. Added: what the loader does not check --
  identifiers over 128 characters, subqueries and `::` casts in expressions, model legality
  reported only when a query asks -- and the asymmetry of a quoted metric reference in a request.
- `docs/ossie-sql-compatibility.md` is re-baselined from the 0.2 snapshot to the 0.3 spec, with the
  behaviours that change re-verified here (true division, `ROUND` ties away from zero,
  `FILTER (WHERE ...)`, `source.column`), and the dialect-name sentence corrected to the open
  vocabulary the loader actually uses.

## [0.3.0] - 2026-10-21

Targets DuckDB **v2.0.0**. DuckDB v1.5.6 keeps the 0.2.0 build.

### Changed
- Built against DuckDB v2.0.0 and extension-ci-tools `v2.0-cyanoptera`. Every file that touches a
  parse tree or registers a function changed for DuckDB 2.0's API.
- `ossie_load`'s keyword parameters (`rebind`, `validate_sources`, `allow_filter_functions`) now
  declare their defaults. DuckDB 2.0 makes a keyword parameter without a default required.
- `ossie_compile` is marked fallible, so its refusals still reach the caller as messages. DuckDB 2.0
  turns a throw from an unmarked scalar function into an INTERNAL error.
- Golden SQL gained parentheses around joins, from DuckDB 2.0's printer.

### Fixed
- `duckdb_functions()` lists every ossie parameter by name again. DuckDB 2.0 reads names from the
  signature, so without this change each function showed `col0`, `col1`, ... to an agent.
  `ossie_load` now lists all four parameters, where 1.5 showed only `path`.

### Known issues
- The MCP surface (`examples/server.sql`) needs `duckdb_mcp`, which has no DuckDB 2.0 build in the
  community registry yet. CI's `mcp-round-trip` job is off until it does.
- The DuckDB interval defect listed under 0.2.0 is still present in v2.0.0.

## [0.2.0] - 2026-10-10

Targets DuckDB **v1.5.6**. Version 0.2.0: catches up with two changes to the Ossie spec, one of them
breaking, that 0.1.1 predates.

### Changed
- **Breaking: models use the flat document shape of Ossie 0.2.0.dev0.** Each document now holds one
  model at its root (apache/ossie cc919e9). The `semantic_model` array is no longer read: a document
  that still has it is refused at load with the migration spelled out. 0.1.1 could read none of the
  models upstream now publishes, including its own TPC-DS example
- Built against DuckDB v1.5.6 and extension-ci-tools v1.5.6. No source change was needed: every
  changed header this extension includes came from DuckDB's additive "2.0 API spellings" backport,
  and the test suite passes unchanged (210 assertions in 12 test cases)
- CI installs the stock DuckDB CLI pinned to the version the extension is built against, rather
  than whatever `install.duckdb.org` calls latest. On DuckDB's release day the `artifact-load` and
  `mcp-round-trip` jobs had gone red with no change here: a v1.5.5-built artifact cannot load in a
  v1.5.6 CLI, and the registry had nothing to serve for v1.5.6 yet
- CI's MCP round trip runs the extension that CI run built, not the copy in the community registry,
  which only ever serves code that has already merged. Installing from the registry is checked by a
  separate daily workflow that cannot block a merge

### Added
- **`OSSIE_SQL_2026`, the spec's own expression language** (apache/ossie be2c73f), executed alongside
  `ANSI_SQL`; an expression carrying both runs the `OSSIE_SQL_2026` variant. Functions DuckDB spells
  differently or lacks (`NVL`, `IFF`, `NVL2`, `ZEROIFNULL`, `NULLIFZERO`, `CHARINDEX`, `STARTSWITH`,
  `ENDSWITH`, `TO_DATE`, `TO_TIMESTAMP`, `TIMESTAMP_NTZ`, and `DATEADD`/`DATEDIFF`/`DATE_TRUNC` with a
  bare date part) are lowered to equivalent DuckDB trees at load. `CONCAT`, `GREATEST` and `LEAST`
  return NULL when any argument is NULL, as the language's `||` does, unlike DuckDB's own. Constructs
  whose result the language leaves undefined, such as `REGEXP_LIKE`, are refused at load, by name
- Request filters are read in the same language: `NVL`, `DATEADD(day, 30, d)` and the rest work in a
  filter, and `CONCAT`, `GREATEST` and `LEAST` there return NULL when any argument is NULL, as in a field.
  Filters are still checked against `allow_filter_functions` as written, before any rewriting
- Upstream's `examples/tpcds_semantic_model.yaml` joins the vendored conformance models, which are
  re-vendored from one upstream commit (8dd6732) under a directory per converter

### Fixed
- **Window-function metrics are refused instead of computed.** Their rows depend on the dimensions
  requested, which the metric cannot express: grouped by date and item category, upstream's
  `cumulative_sales` ran across categories, and 10,047 of 10,049 rows differed from a per-category
  running total. 0.1.1 passed such metrics through; they now fail when queried, naming the metric

### Known issues
- A DuckDB optimizer defect, present in 1.5.5 and 1.5.6 without this extension, treats
  `INTERVAL 30 DAY` and `INTERVAL 1 MONTH` as the same expression, so a query asking for both gets
  one answer twice. See docs/limitations.md

## [0.1.1] - 2026-09-07

Targets DuckDB **v1.5.5**.

Both fixes came from writing an MCP client and driving the published server the way Claude Desktop
does. Neither the test suite nor the end-to-end functionality check covered that path.

### Fixed
- `examples/server.sql` never loaded the extension it demonstrates. It loaded `duckdb_mcp` but not
  `ossie`, and called `dsdgen` without loading `tpcds`, so it only worked from a build tree where
  both are statically linked. Anyone following the README after installing from the registry hit
  `Catalog Error: Table Function with name ossie_load does not exist!` on the first statement that
  mattered. All three extensions are now installed and loaded explicitly

### Changed
- Closed a hole behind a security claim. The documentation stated that an agent connected to the MCP
  server "can reach nothing but the model's own vocabulary", which was false: `duckdb_mcp` also
  publishes generic `query`, `export`, `list_tables`, `describe` and `database_info` tools, and
  reading a table the semantic model never declares confirmed an agent could reach it.
  `examples/server.sql` now disables all five when starting the server, so `tools/list` returns only
  `semantic_query` and calling anything else is refused. Disabling `query` alone would not have been
  enough — `export` takes an arbitrary SQL argument and reaches the same data. The claim is now true
  and, more importantly, asserted by CI rather than written down. Reported upstream as
  teaguesterling/duckdb_mcp#75; the flags existed but were absent from that project's tool table
- Dropped `-unsigned` from the documented MCP invocation and the Claude Desktop config snippet. It
  was left over from before `ossie` was in the registry, when the example could only run against a
  locally built artifact. Community extensions are signed, so the flag did nothing but disable
  signature verification for the whole session — including every session started by that pasted
  config. `scripts/full_functionality_check.sh` keeps it, because it loads the local build
  deliberately

### Added
- `scripts/mcp_check.py`, an end-to-end MCP check that speaks JSON-RPC over stdio to the published
  server and asserts the full round trip: handshake, tool discovery, vocabulary discovery, real rows
  from `semantic_query`, and a multi-grain refusal arriving as a readable error
- A `MCP round trip` CI job that runs it on every push. It deliberately uses a stock DuckDB CLI
  rather than the build tree, because static linking in the build tree is what hid the bug above:
  CI was green on a `server.sql` that failed for everyone installing from the registry
- A `CI gate` job aggregating the build matrix, the MCP round trip and the quality checks into one
  stable check name, so branch protection can require it without depending on matrix job names

## [0.1.0] - 2026-08-27

Targets DuckDB **v1.5.5**.

First release. Ossie is a vendor-neutral file format for semantic models, and every shipped
implementation of it is a *converter* — it translates definitions into some vendor's semantic layer
so that vendor's engine can run them. This extension is an engine: it compiles a semantic request
against the model and runs the SQL on DuckDB.

### Added
- `ossie_load(path, ...)` parses and validates an Ossie model, in either YAML or JSON, holding it in the database's
  `ObjectCache` rather than per connection so a model loaded by an init script is visible to an MCP
  server answering on its own connection. Named arguments: `rebind` remaps warehouse-qualified
  source prefixes onto the local catalog, `validate_sources` requires every dataset's table to
  exist (reporting all failures at once), and `allow_filter_functions` sets the filter policy at
  load so a caller supplying filters cannot widen its own policy
- `ossie_query(metrics, dimensions, filters)` compiles the request and executes it. The statement is
  returned from `bind_replace` as a `SubqueryRef`, which runs before binding — so DuckDB plans and
  optimizes the generated query as though it had been typed by hand, and join reordering, filter
  pushdown, and projection pruning are inherited rather than reimplemented
- `ossie_compile(metrics, dimensions, filters)` returns the same SQL as text without running it,
  for use where DuckDB is not the executor. It runs the identical compiler, so the SQL you inspect
  is the SQL that would run
- `ossie_datasets()`, `ossie_fields()`, `ossie_metrics()`, and `ossie_relationships()` expose the
  model's vocabulary, including `ai_context` synonyms so an agent can discover names, and each
  relationship's cardinality derived from the endpoints' declared keys
- Grain analysis: each aggregate's grain is the dataset that functionally determines the others it
  references, and that dataset becomes the root of the join tree. Every aggregate in a query must
  agree on one grain
- Join planning by breadth-first search over the relationship graph, with two refusals: reaching a
  required dataset by a second, non-tree edge (two routes that could produce different numbers),
  and any edge that repeats rows of the grain and would inflate every aggregate
- Filter predicates are compiled into `WHERE` or `HAVING` depending on whether they contain an
  aggregate, and are allowlisted by node type — operators always, named function calls only under
  `allow_filter_functions`, subqueries never
- Expressions are parsed into `ParsedExpression` trees at load and rewritten as trees through to
  emission; no SQL is ever built by string concatenation
- `examples/server.sql` publishes a model to an AI agent over MCP via the `duckdb_mcp` community
  extension, exposing a `semantic_query` tool plus `metrics` and `dimensions` resources
- YAML and JSON are both read natively. The Ossie ecosystem publishes models as YAML — the
  apache/ossie repository holds 51 YAML files to 6 JSON, and every one of those JSON files is a
  schema or a converter's output rather than a model. DuckDB vendors no YAML parser, so fkYAML is
  vendored as a single unmodified header and YAML is converted to JSON before parsing, which leaves
  every validation guard and the compiler unaware of how a model was serialized
- Conformance fixtures: five models taken verbatim from apache/ossie, plus the official
  `core-spec/ossie-schema.json`. Every other fixture here was written alongside the compiler and so
  shares its assumptions; these are the only ones that can detect a divergence between this
  implementation's reading of the format and anyone else's. Four of the five load, and the fifth is
  refused correctly because it carries only `DATABRICKS` expressions
- `scripts/full_functionality_check.sh`, an end-to-end check driven through a stock DuckDB CLI
  against the loadable artifact rather than the build tree

### Known limitations
Every refusal and its reasoning is recorded in [docs/limitations.md](docs/limitations.md). The ones
most likely to affect you:
- **`ANSI_SQL` expressions only.** A field or metric carrying only a `SNOWFLAKE`, `DATABRICKS`, or
  `MDX` expression fails at load rather than being guessed at
- **Metrics spanning more than one grain are refused** rather than answered wrongly. Computing them
  correctly needs one aggregate per grain joined on shared dimensions, which is not implemented
- **All joins are emitted as `INNER`.** Ossie declares a relationship's endpoints but not its join
  type, so a fact row with a `NULL` foreign key is dropped once its dimension is joined, and a
  metric total can shrink when a dimension is added. There is no override yet
- **One semantic model per file.** The format permits an array; metrics are addressed by bare name
  with no way to say which model is meant
- **A column named by a model expression but not declared as a field is passed through** to DuckDB's
  binder rather than refused, because the format does not require the declaration and real models
  rely on that. The cost is that a typo in a model expression surfaces when a query touches it
  instead of at load
- **No row cap.** `ossie_query` takes metrics, dimensions, and filters and nothing else, so a
  high-cardinality dimension returns every row

[Unreleased]: https://github.com/iqea-ai/duckdb-ossie/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/iqea-ai/duckdb-ossie/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/iqea-ai/duckdb-ossie/compare/v0.1.1...v0.2.0
[0.1.1]: https://github.com/iqea-ai/duckdb-ossie/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/iqea-ai/duckdb-ossie/releases/tag/v0.1.0

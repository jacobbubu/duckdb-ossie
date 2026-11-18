# What duckdb_ossie refuses, and why

A refusal is a feature here. An agent querying this extension cannot check the answer it gets
back, so a plausible wrong number is worse than an error. Everything below is a case where the
model, the request, or the format underdetermines the query — and the extension declines rather
than picking.

## Bounded population profile

The separate `ossie_bundle_load` path has a stricter expression and source contract
than the live core APIs described below. Its single-table profile and bounded
fact/dimension profile, whole-batch identity/key checks, frozen data lifetime and
explicit refusals are listed in [population-profile.md](population-profile.md).
The fact/dimension profile validates both captured inputs and an explicit
`INNER`/`missing=reject` policy before publishing a frozen result. General ontology
execution, arbitrary joins, mixed-grain aggregates and calendar completion remain
outside these profiles. The live core join/filter behavior below is a separate
contract; it does not describe the stricter population path.

The opt-in [two-fact profile](population-facts.md) has a separate contract: independently
reduce User/date and User/date/product facts before combining same-unit SUM measures,
apply bounded TRUE cohorts, or compute explicit User/date observation windows before
display filtering. Its dedicated SQL query/compile/discovery functions do not relax the
generic core refusals below. The generic core limits below still apply.

## Loader string arrays

AI `synonyms`/`examples`, dataset `primary_key` and relationship
`from_columns`/`to_columns` accept string members only in JSON and YAML.
Numbers, Booleans, null and containers are refused, with a zero-based member
index; a failed load preserves the previous model or frozen bundle. Quote
numeric or Boolean-looking text when it is intended as a string. `unique_keys`
already rejects non-string column members. AI strings preserve their bytes,
including control characters; this does not grant those characters permission
in executable expressions or identifiers. The loader is not a full JSON Schema
validator. Single-value string fields also require actual strings: required
fields must be present, and optional fields may be omitted but cannot explicitly
contain null, numbers, Booleans or containers. Quote numeric or Boolean-looking
text in JSON/YAML. Missing optional values retain their existing empty or SQL
NULL discovery representation; other field-specific checks still apply.

Core descriptive strings and unique-key members are read using their actual
length, so NUL does not truncate metadata. Model expression variants containing
NUL are refused before SQL parsing because DuckDB otherwise parses only the
prefix. The bounded population profiles continue to reject NUL in their
complete definition documents.

Core model, dataset, field, metric and relationship names, dataset source and
relationship endpoints must be nonempty strings. Whitespace is not trimmed.
Unknown properties in the core model, datasets, fields, metrics, relationships,
dimension objects, expressions and dialect variants are refused. Property keys
are case-sensitive and matched in full. AI context objects still permit and
preserve additional descriptive properties. `nonportable_extensions` must be
arrays of objects containing only `vendor_name`, a nonempty string, and `data`,
an object. Any vendor name is accepted; the data object is preserved without
further validation. Extensions are descriptive and do not execute vendor code.

## Grain

**Metrics that aggregate at more than one grain** are computed one grain at a time.
`store_productivity` is `SUM(store_sales.ss_ext_sales_price) / NULLIF(SUM(store.s_number_employees),
0)`; joining `store` to `store_sales` would replicate each store's employee count once per sale
line and inflate the denominator by roughly the number of sales per store. Instead each grain gets
its own grouped subquery, the subqueries are joined on the dimension values (`FULL JOIN ... ON a
IS NOT DISTINCT FROM b`, so a group only one side produced is kept), and the metric expression is
evaluated over the aggregate columns. For a group one side did not produce, that side's aggregates
take the value they have over no rows: 0 for `COUNT`, `COUNT(*)`, `COUNT(DISTINCT ...)` and
`APPROX_COUNT_DISTINCT`, NULL for the rest. What this implies for a request:

- A row filter is applied inside *every* grain's subquery, so each grain must reach the filter's
  dataset without fanning out. `store_productivity` filtered by `date_dim.d_year = 2001` is refused:
  `store` cannot reach `date_dim` except through `store_sales`, and a filter that silently applied
  to one side of the ratio only would be a different metric. The same holds for dimensions.
- A metric filter (`metric_filters => ['total_sales > 100']`) applies to the joined result.
- Population requests are single-grain by construction and refuse multi-grain metrics.

**Which dataset an aggregate runs over.** A metric declared on a dataset runs over that dataset's
rows, whatever its expression: `orders.completed_count`, declared as `COUNT(*) FILTER (WHERE status
= 'completed')`, counts `orders`. In a model-scoped metric each aggregate runs over the one dataset
its arguments read, and three shapes that name no such dataset are refused with
`E_AMBIGUOUS_MEASURE_GRAIN`, as the Wide Table specification requires:

- An aggregate whose arguments read several datasets: `SUM(store_sales.ss_quantity *
  item.i_current_price)`. An `ORDER BY` inside the call counts as an argument. The specification
  defers this shape rather than calling it wrong. Declare the product as a field of `store_sales`
  if the source has it, or aggregate each dataset on its own.
- An aggregate whose `FILTER` reads a dataset its arguments do not: `SUM(orders.amount) FILTER
  (WHERE customers.region = 'west')`. Pass the condition as a row filter of the request instead.
- An aggregate whose arguments read no dataset: `COUNT(*)`, `COUNT(1)`, and `COUNT(*) FILTER (WHERE
  orders.status = 'completed')`, where the `FILTER` does not stand in for the arguments. Against
  the TPC-DS model a bare `COUNT(*)` could be 2.9M (sales), 100K (customers) or 12 (stores), with
  nothing to choose between them, and the other metrics of a request do not choose either. Declare
  the count as a metric of the dataset it counts and request `dataset.metric`, or write
  `COUNT(store_sales.ss_item_sk)`.

An aggregate written directly in a `metric_filters` entry is held to the same rule, so
`metric_filters => ['COUNT(*) > 1']` is refused and `['customers.customer_count > 1']` is the way to
write it.

`AGG(dataset.metric)` inlines the dataset metric's expression into the model-scoped metric, and the
inlined aggregates are then read by the rule above rather than kept with the dataset that declared
them. A dataset metric whose aggregate has no column in its arguments therefore cannot be named in
`AGG` and is refused at load; request it directly.

Population requests check each metric against the grain the bundle designates instead: there an
aggregate over the fact rows may read a dataset those rows determine, as in `SUM(CASE WHEN
dimension.eligible THEN fact.amount END)`.

**Window-function metrics.** A metric such as `SUM(SUM(store_sales.ss_ext_sales_price)) OVER (ORDER
BY date_dim.d_date ...)` fixes its partition and order in the model, but the rows it runs over are
the groups of whatever dimensions the request names. Asked for by date and item category over
TPC-DS data, that running total ran across categories, and 10,047 of 10,049 rows differed from a
per-category running total; ties on date also leave the order, and so the value, arbitrary. Any
metric containing a window function (`OVER`) is refused when queried, naming the metric and the
function. The model still loads. Upstream's own `examples/tpcds_semantic_model.yaml` has three such
metrics.

**A field outside every aggregate.** A model-scoped metric such as `SUM(orders.amount) -
orders.order_id` has no one value for a group of orders, so it is refused when queried, naming the
field. A dataset-scoped metric is not checked this way; DuckDB's binder refuses a column that is
neither grouped nor aggregated.

**Fan-out joins.** If reaching a dataset repeats rows of the grain — a many-to-many relationship,
or traversing a many-to-one edge backwards — every aggregate in the query would be inflated. Such
a join is refused whether it was pulled in by a metric, a dimension, or a filter. For a dimension
or a filter the message names the entry, a metric that cannot reach its dataset, the dataset that
metric aggregates, and the relationship that would repeat rows. A metric over two grains must reach
the dataset from both.

## Joins

**Ambiguous paths.** A path runs from the grain to a required dataset, crossing each relationship
only toward the side that matches at most one row and visiting no dataset twice. When two such
paths exist they can produce different numbers, so the request is refused with the endpoints named
rather than resolved to the shorter one or to whichever was found first. Only the datasets a
request joins are checked, and a route that would fan out is not a path: `store_returns` reaches
`date_dim` one way even where `store_sales` reaches it too and both facts share `item`.

**Join type is not declared.** Ossie describes a relationship's endpoints and columns but not
whether it is inner or outer. By default joins are emitted as `INNER`, which matches hand-written
SQL but means a fact row whose foreign key is `NULL` or matches no dimension row is dropped once
that dimension is joined, so a metric can return a smaller total once a dimension or filter is
added. Loading with `left_joins => true` emits `LEFT JOIN` from the grain to every dimension
instead: such rows survive, grouped under `NULL` dimension values, and the grouped total equals
the ungrouped one. The policy applies to the whole model for that load, like
`allow_filter_functions`; there is no per-relationship declaration, because the model format has
nowhere to put one. A filter on a dimension column still excludes the unmatched rows, as it does in
plain SQL (`WHERE store.s_state = 'TN'` is false for a `NULL` state), and the fan-out and
ambiguous-path checks are unchanged, since a `LEFT JOIN` repeats rows exactly as an `INNER` one does.

## Filters

**A filter reads the rows of one dataset.** Each entry of `filters` is applied before aggregation
and may name the fields of one dataset only, which is the Wide Table specification's filter. An
entry naming a metric, or containing an aggregate, is refused with a message pointing at
`metric_filters`, the named parameter whose entries are applied after aggregation; an entry there
that names no metric and contains no aggregate is refused the other way. A window function is
refused in both. An entry reading two datasets is refused: pass each dataset's condition as its own
entry, and the entries are combined with AND. A row condition that joins two datasets with OR has no
spelling, since the specification has one filter over one dataset and entries only ever combine
with AND.

Filter predicates, of either kind, arrive per request rather than from the model, so their contents
are allowlisted. Operators are always permitted, including arithmetic, `||` and `LIKE`. Named
function calls are refused unless the model was loaded with `allow_filter_functions => true` — note
that `SIMILAR TO` desugars to a `regexp_full_match` call and so falls under that flag, while `LIKE`
does not. Subqueries are refused unconditionally and no option enables them, since they could read
tables the model never declared.

## The JSON query form

**`ossie_query(query)` and `ossie_compile(query)` take the Wide Table specification's JSON query and
nothing more.** The object has `metrics` and `dimensions`, each an array, and `filter`, one
expression string. An entry of an array is a reference string, or an object with the reference under
`metric` (in `metrics`) or `field` (in `dimensions`) and optionally an `alias`. Everything else is
refused with `E_INVALID_QUERY`, as the specification requires, rather than skipped: a key or
property outside those, a key written in another case or given twice, a `null` anywhere, a value of
the wrong type, text that is not JSON, and a NULL argument.

**An alias is one identifier.** It is written the way a model writes a name, bare or in double
quotes, so `"alias": "net_sales"` and `"alias": "\"net sales\""` are accepted and `"net sales"`,
`"a.b"`, a reserved word such as `"order"` and the empty string are refused. It names the result
column and nothing else: `filter` and `metric_filters` go on reading fields and metrics by their
own names, and an alias collides with another output name the way any two output names do.

**The two forms differ in three ways.** Aliases exist only in the JSON form. The JSON form has one
`filter`, as the specification does, where the list form takes several entries combined with AND.
And `metric_filters` is this extension's parameter, not part of the specification's query, so it is
not a key of the object; pass it as the named argument in either form.

## The Relational Query Interface

**`ossie_sql(query)` takes one `SELECT` over the model's relations and evaluates a measure where
`AGG` names it; a measure cannot yet be handed from one query block to another unevaluated.** The
Ossie specification defines
a second way to query a model: SQL that reads the model's datasets as relations and evaluates a
measure with `AGG()`, as in `SELECT deptno, AGG(sum_sal) FROM scott_model.emp GROUP BY deptno`. A
dataset is the relation `model.dataset`. Its fields are its columns, read by name with the field's
expression as their value, and `AGG(measure)` or `AGG(relation.measure)` evaluates one of its
dataset-scoped metrics over the rows of a group, in `SELECT`, `HAVING` or `ORDER BY`. A model-scoped
metric belongs to no relation, as the specification has it, so only `ossie_query` evaluates one.

**Over a join, a measure is over the rows of its own dataset, each once.** In `SELECT e.job,
AGG(d.dept_count), AGG(e.emp_count) FROM scott_model.emp e JOIN scott_model.dept d ON e.deptno =
d.deptno GROUP BY e.job` the join repeats a department for each of its employees, and
`AGG(d.dept_count)` still counts it once; a plain `count(d.deptno)` counts every joined row, as SQL
does. The join's type and condition are the query's. Rows that the join or `WHERE` removes are out
of every measure, a row an outer join pads adds nothing, and a group whose measure reaches no row of
its dataset keeps its row, with a count of 0 and a sum of NULL. The other side of the join can be a
table or a subquery. Three things follow from how this is done:

- **The dataset needs a `primary_key`.** The key is what tells two rows of the dataset apart once
  the join has repeated them, so `AGG` of a measure whose dataset declares none is refused wherever
  the `FROM` clause is more than one model relation. A key may name fields or columns of the source.
  Such a dataset can still be joined for the measures of the others, and alone in `FROM` it needs no
  key.
- **The key is taken at its word.** Nothing checks that it is unique or filled in. Two rows that
  share a key count twice if they differ in a column the measure reads and once if they do not. A
  row whose key is NULL is in no measure over a join, because that is what a padded row looks like,
  though alone in `FROM` it counts.
- **It costs memory and time.** For each group the distinct rows of the measure's dataset are
  collected, their key and the columns the measure reads, before the measure is computed over them.
  Summing a 2-million-row dataset through a join of 10 million rows took about five times as long as
  the same question written by hand with `DISTINCT`. A join that repeats nothing is not recognised
  and pays the same; only a block whose `FROM` is one model relation aggregates directly.

Two parts of the interface are missing, and each is refused by a message that says so rather than
answered some other way:

- **A measure passed through unevaluated.** A measure has a value only inside `AGG`, in the query
  block whose `FROM` names its relation. A bare measure column is refused, so a subquery or CTE
  cannot hand a measure to the block around it, and `*` over a relation that has measures is refused
  because the specification counts the measures among its columns. `*` over a relation without
  measures is its fields.
- **A window measure**, one whose expression holds a window function. `ossie_query` refuses it too.

**An unaliased `AGG(measure)` in the `SELECT` list is named for the measure.** The specification
gives no rule, and the name DuckDB would give is the measure's definition. Anything built on a
measure, such as `AGG(sum_sal) * 2`, takes DuckDB's name for the expression; give it an alias.

**The query is DuckDB SQL, not `OSSIE_SQL_2026`, and nothing in it is translated.** Only model
relations and `AGG` calls are replaced, before DuckDB binds the statement. `model.dataset` names the
dataset even where the database has a table of that name in a schema of the model's name; that table
is still reached through its catalog, as `memory.model.dataset`. A column of a dataset's source that
no field declares is not a column of the relation, though a measure may read it. A dataset that
declares no field cannot be read, and `AT` and column aliases on a model relation are refused.

**`ossie_sql` is not a sandbox.** It accepts one statement, a `SELECT` with no statement that writes
inside it, but that `SELECT` runs with the caller's rights: it reads any table and calls any function
the caller could. `allow_filter_functions` does not govern it. It is for a caller who could have
typed the SQL, and the MCP example exposes `semantic_query` and not this.

**There is no `ossie_compile` for it and no `DESCRIBE` of a relation.** The statement that runs
cannot be printed, and `DESCRIBE scott_model.emp` looks for a table. `DESCRIBE SELECT * FROM
ossie_sql('...')` does describe a query's result.

Upstream's 32 query compliance cases are the measure of this: 31 run as written and give the expected
rows, and one is refused, for a measure passed through a CTE.
`ossie_query` asks a narrower set of questions, with the joins planned from the model's
relationships: 12 of the 32 can be asked as one request, and all 12 give the expected rows. The
[conformance fixtures](../test/fixtures/conformance/README.md#query-compliance-cases) list which and
why.

## Error codes

**A refusal the Wide Table specification defines carries its code.** The message is `ossie: `, the
code from Section 8 of the specification, then the text: `ossie: E_NO_PATH: dimension
"orders.order_id" is a field of "orders", which metric "customers.customer_count" cannot reach from
"customers": ...`. A request with several faults gets the code listed first below, which is the
specification's order, so which fault is reported does not depend on how the compiler happens to be
written.

| | code | raised when |
|---|---|---|
| 1 | `E_INVALID_QUERY` | a dimension is not a name; a filter does not parse, is not one predicate, or uses a function, subquery or expression the filter policy refuses; a JSON query is not the object the specification describes |
| 2 | `E_EMPTY_AGGREGATION_QUERY` | the request has no metric and no dimension |
| 3 | `E_NAME_NOT_FOUND` | a metric, dataset or field is not in the model, or a field is not written `dataset.field` |
| 4 | `E_METRIC_IN_DIMENSIONS` | a dimension names a metric |
| 5 | `E_FIELD_IN_METRICS` | a metric names a field |
| 6 | `E_AGGREGATE_IN_WHERE` | a filter names a metric or contains an aggregate |
| 7 | `E_WINDOW_IN_WHERE` | a filter contains a window function |
| 8 | `E_CROSS_DATASET_FILTER` | a filter reads the fields of two datasets |
| 9 | `E_NAME_COLLISION` | two output columns have one name |
| 10 | `E_UNAGGREGATED_FINER_GRAIN_REFERENCE` | a model-scoped metric reads a field outside every aggregate |
| 11 | `E_DEFERRED_WINDOWED_METRIC` | a metric contains a window function |
| 12 | `E_AMBIGUOUS_MEASURE_GRAIN` | an aggregate of a model-scoped metric, or one written in a metric filter, has arguments that read several datasets or none, or a `FILTER` that reads a dataset its arguments do not |
| 13 | `E_NO_PATH` | no requested metric reaches a dimension's dataset |
| 14 | `E_PARTIAL_DIMENSION_REACHABILITY` | some requested metrics reach a dimension's dataset and others do not |
| 15 | `E_NO_ANCHOR` | a request without metrics has no dimension dataset that reaches all the others |
| 16 | `E_UNSUPPORTED_FILTER_PATH` | a metric, or the anchor of a request without metrics, does not reach a filter's dataset |
| 17 | `E_AMBIGUOUS_PATH` | two join paths lead to a dataset the request joins, or two datasets qualify as the anchor |

**Refusals the specification does not define carry no code.** `metric_filters` is this extension's
parameter, so a metric filter with no metric and no aggregate, or with a window function, is refused
as `ossie: metric filter ...`. So are the refusals of the population surface and of `ossie_load`.

## Model conformance

The [function and window matrix](ossie-sql-compatibility.md) separates pinned
language definitions from each profile's AST boundaries. It does not claim
complete OSSIE_SQL_2026 conformance.

**The pre-0.2 `semantic_model` array.** Ossie 0.2.0.dev0 puts exactly one model at the root of each
document and removed the `semantic_model` array rather than deprecating it. A document that still
wraps its model is refused at load with the migration spelled out: move the model's properties to
the top level, delete the wrapper, and split a multi-model file into one file per model.

**Two executable dialects.** An expression runs if it has `sql` (the OSSIE_SQL_2026 text) or an
`ANSI_SQL` entry in `dialects`; when it has both, the OSSIE_SQL_2026 text wins, since the spec
defines what it means. A `dialects` entry named `OSSIE_SQL_2026` beside `sql` is ignored. A field or
metric carrying only a `SNOWFLAKE`, `DATABRICKS` or `MDX` expression fails at load rather than
being guessed at. Dialect names are an open vocabulary matched exactly and case-sensitively:
`ansi_sql` is recorded as another vendor's dialect, not run as `ANSI_SQL`.

**OSSIE_SQL_2026 constructs with no defined result.** The expression language
(`core-spec/expression_language.md`) is lowered to DuckDB at load. Where it leaves a result
undefined and engines disagree, the expression is refused at load, naming the field or metric:

- `REGEXP_LIKE`: Snowflake requires the pattern to match the whole string, Databricks and
  BigQuery any part of it, and the language does not say. Use `LIKE`, or give the expression an
  `ANSI_SQL` variant that says which is meant (for example `regexp_full_match`).
- `TO_DATE` or `TO_TIMESTAMP` with a format argument, and `TO_CHAR`: format models differ between
  engines, and both forms are `EXPERIMENTAL` in the language. The one-argument ISO-8601 forms of
  `TO_DATE` and `TO_TIMESTAMP` are supported; for `TO_CHAR`, give the expression an `ANSI_SQL`
  variant that uses `strftime`.
- A date part outside year, quarter, month, week, day, hour, minute and second, or a function called
  with a different number of arguments than the language defines.
- A function the language does not list, including `INTERVAL` literals (which DuckDB parses as a
  call to `to_days` and the like) and window functions outside its ranking, offset and aggregate
  set. DuckDB has `MD5`, `STRING_AGG` and hundreds more, but an `OSSIE_SQL_2026` expression that
  calls one would mean something on this engine and nothing on another. Write it as an `ANSI_SQL`
  variant, which reaches DuckDB as written. The same check applies to request filters, after
  `allow_filter_functions` has judged the filter.

**OSSIE_SQL_2026 choices where DuckDB differs.** DuckDB's `concat`, `greatest` and `least` skip
NULL arguments. The language's own `||` and standard SQL return NULL when any argument is NULL, so
`CONCAT`, `GREATEST` and `LEAST` do too here. DuckDB's `date_trunc` and `date_add` turn a `DATE` into
a `TIMESTAMP` at midnight; the language keeps the input type, so `DATE_TRUNC` and `DATEADD` with a
year, quarter, month, week or day part are lowered to this extension's `ossie_date_trunc` and
`ossie_dateadd`, which return `DATE` for `DATE` and `TIMESTAMP` for `TIMESTAMP`. The compiled SQL
that `ossie_compile` prints therefore names those two functions, and runs only where the extension
is loaded. An hour, minute or second part cannot keep a `DATE`, and uses DuckDB's `date_add`.

Request filters are read in the same language, whatever dialect the model's own expressions use: a filter
is lowered exactly as an `OSSIE_SQL_2026` field is, after `allow_filter_functions` has judged the filter
as written. So `NVL` and `DATEADD(day, 30, d)` work in a filter, and `CONCAT(a, b) IS NULL` is true when
either is NULL, as it would be in a field.

**Quoted identifiers match exactly, when the source can be seen.** The language makes a quoted
identifier exact: `"region"` does not name a column created as `"Region"`. DuckDB matches quoted
names case-insensitively, so `ossie_load` checks instead: for every dataset whose source is a table
or view in the catalog, a quoted column reference in a field, or a dataset-qualified one in a metric,
that matches a column only by ignoring case is refused, naming the column's real spelling and
suggesting the unquoted form. A dataset whose source is absent at load (the usual case with
`ossie_compile` against a remote engine) is not checked, and DuckDB's own matching applies at query
time. The check reads the expression text, so a name written both quoted and unquoted in one
expression counts as quoted. Model-level names (datasets, fields, metrics) are case-insensitive as
before, whether or not they are quoted.

**Table sources only.** `source` may name a query rather than a table under the spec. Such a
source can neither be prefix-rebound nor bound as a table reference, so it is refused at load.

**Unqualified columns in metrics.** A metric must write `store_sales.ss_ext_sales_price` rather
than `ss_ext_sales_price`. A bare column cannot say which dataset it belongs to, and a metric's
dataset is its grain. Note this is a requirement about *qualification*, not about declaration: the
column itself need not appear in the dataset's `fields`.

**Columns that are not declared fields are passed through, not refused.** The format requires
neither that a relationship's columns be declared fields nor that a field or metric expression
reference only declared fields, and real third-party models rely on both. Such a name is treated as
a physical column and resolved by DuckDB's binder. The tradeoff is deliberate and has a cost: a
genuine typo in a model expression is no longer caught by `ossie_load`, and instead surfaces as a
binder error when a query touches it. That message names the column and the table, so it is a worse
message arriving later — not a wrong answer.

## Prepared statements

**A prepared `ossie_query` or `ossie_compile` follows the model current at `EXECUTE`, not at
`PREPARE`.** DuckDB 2.0 rebinds any prepared statement whose plan scans a catalog table, and the
SQL these two functions compile scans the model's sources, so after another `ossie_load` the next
`EXECUTE` compiles against the new model and the policy loaded with it. The population functions
(`ossie_program`, `ossie_population_compile`, `ossie_population_json`, `ossie_objects`, ...) read a
frozen capture rather than a catalog table, so a prepared population request keeps the snapshot it
was bound to, as the population documents describe. Prepare neither if you need one model pinned
across loads; re-prepare after the load you want.

## Known DuckDB defects

**Intervals of 30 days and one month are treated as the same expression.** DuckDB's optimizer
folds `INTERVAL 30 DAY` and `INTERVAL 1 MONTH` to constants, compares them with a month normalised
to 30 days, and merges the two expressions as duplicates, so a query that asks for both gets
whichever comes first, twice. Adding a month to 2024-01-31 should give 2024-02-29, and does when
asked alone. This is DuckDB 1.5.5, 1.5.6 and 2.0.0 with no extension loaded, and it affects `ANSI_SQL`
models (`d + INTERVAL 30 DAY` beside `d + INTERVAL 1 MONTH`). `OSSIE_SQL_2026` models are no longer
exposed: `DATEADD(day, 30, d)` and `DATEADD(month, 1, d)` lower to `ossie_dateadd` with the part as
a string argument, which the optimizer cannot mistake for the same expression. Disabling the
`common_subexpressions` optimizer gives the right answer for `ANSI_SQL`. Minimal repro:

```sql
CREATE TABLE t AS SELECT DATE '2024-01-31' AS d;
SELECT d + INTERVAL 30 DAY, d + INTERVAL 1 MONTH FROM t;   -- 2024-03-01 | 2024-03-01
```

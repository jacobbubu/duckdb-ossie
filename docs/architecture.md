# Architecture

Orientation for working on the extension. For what it refuses and why, see
[limitations.md](limitations.md); for the phase roadmap, see
[../ossie-duckdb-plan.md](../ossie-duckdb-plan.md).

## The shape of the problem

An Ossie file gives you leaf expressions — `SUM(store_sales.ss_ext_sales_price)` — but never the
query they belong to. Search the reference model for `GROUP BY`, `WHERE` or `JOIN` and you find
nothing, because those depend on the question. The same metric definition compiles to different SQL
depending on which dimensions were asked for, so the join set is a function of the request and
cannot be decided at load time.

That split is the whole architecture: **load time** builds a validated model, **request time**
compiles a query against it.

## Layout

```
src/
  model/       parser.cpp     JSON -> Model, expressions parsed to trees here
               validate.cpp   structural checks over the parsed model
               graph.cpp      relationship cardinality
               catalog.cpp    source name splitting, catalog lookup, quoted-column check
               dialect.cpp    OSSIE_SQL_2026 -> DuckDB trees; refuses what the language omits
  compile/     compile.cpp    resolve -> grain -> plan joins -> rewrite -> emit
  functions/   load.cpp       ossie_load, plus OssieState
               describe.cpp   the four description table functions
               compile.cpp    ossie_compile (scalar)
               query.cpp      ossie_query (bind_replace)
               dates.cpp      ossie_date_trunc / ossie_dateadd, DATE in, DATE out
```

## Load time

`ossie_load` parses JSON with the yyjson DuckDB vendors, then:

1. **Parse every expression into a tree.** Field and metric fragments go through
   `Parser::ParseExpressionList` *at load*, so a malformed metric fails here rather than at some
   later query. The tree is retained; the text is kept only for messages and introspection.
2. **Validate structurally** — relationship endpoints resolve, join columns are declared fields,
   metric columns are qualified and exist. Key columns are deliberately *not* checked against
   declared fields, because the reference model keys `store_sales` on a column it never exposes.
3. **Derive cardinality** for each relationship by matching `to_columns` against the target's
   `primary_key` and every `unique_keys` entry, by set equality.

The result lands in `OssieState`, held in the database's `ObjectCache` rather than per connection —
an MCP server answers on its own connection and must see a model loaded by the init script.

## Request time

`Compile()` in `compile/compile.cpp` runs five stages, and `CompileToSQL()` is a wrapper that
renders the result with `ToString()`. Both `ossie_query` and `ossie_compile` go through it, so the
SQL you inspect is the SQL that runs.

**Resolve.** Metric names look up in the model. Dimensions and filters are parsed with
`Parser::ParseExpressionList` rather than split on `.`, so quoted identifiers behave and a request
never becomes spliced SQL.

**Check.** A request is refused in the order of the Wide Table specification's error table, and
`Compile` is laid out in that order with the row numbers in its comments: read every entry before
looking up any name, look up every name before judging any filter, judge the filters before the
metrics, and the metrics before the join graph. A refusal the specification defines starts with its
code (`ossie: E_NO_PATH: ...`). Keep a new check in its row's place; the order is what makes a
request with two faults report the same one on every engine.

**Grain.** Each aggregate's grain is the dataset that functionally determines the others it
references — the one whose rows are counted once while the rest are replicated onto them. When
every aggregate in a query agrees on one grain, that grain becomes the root of the join tree. When
they do not, each grain gets its own grouped subquery, rooted at that grain, and the subqueries are
joined on the dimension values; the metric expressions then run over the hoisted aggregate columns.
An aggregate with no column reference, like `COUNT(*)`, inherits the grain when exactly one exists.
A request with no metric has no aggregate to take a grain from, so its root is the *anchor*: the one
dimension dataset that reaches every other dimension dataset without repeating its own rows. Zero or
several such datasets are refused, and so is a filter on a dataset the anchor cannot reach that way;
the filter has no say in which dataset is the anchor.
`CompileOptions` lets a caller extend this path without a second compiler: `shared_keys` declare a
bare dimension name that resolves to a different field on each grain, `required_grains` force a
grain subquery for a dataset that contributes only row presence, `grain_join` picks INNER over the
default OUTER join, and `population_sources` substitute a frozen `TableRef` for a dataset's declared
source. The population fact profile builds its per-User reductions and common-domain count this way.

**Plan joins.** For each required dataset, search the relationship graph from the root for paths to
it. A path crosses a relationship only *toward* its unique side, where each row matches at most one;
travelling away from it repeats rows of the grain and would inflate every aggregate. A path visits
no dataset twice. Exactly one path is joined. Two could produce different numbers, so the request is
refused, whichever is shorter. None is refused too, naming the hop that fans out when the datasets
are connected at all.

**Rewrite.** Each `dataset.field` reference is replaced by the field's own expression, with its
inner columns qualified to the bound alias. This substitution is unconditional: a field's name is
model-level and need not exist on the table — `customer_full_name` is really
`c_first_name || ' ' || c_last_name`.

**Emit.** A `SelectNode` with nested `JoinRef`s, `GROUP BY` over the dimensions, the entries of
`filters` in `WHERE` and the entries of `metric_filters` in `HAVING`. Which clause a predicate goes
to is the caller's statement, not an inference: a filter that names a metric or contains an
aggregate is refused, and so is a metric filter that does neither. Each output
column is aliased to the final part of its reference, `region` for `customers.region`, which is the
Wide Table specification's output name; two entries with one output name, compared without case,
are refused before anything is planned.

`ossie_query` returns the statement wrapped in a `SubqueryRef` from `bind_replace`, which runs
before binding — so DuckDB plans and optimizes the generated query as though it had been typed by
hand. Join reordering, filter pushdown and projection pruning come free and are not reimplemented.

## Conventions worth knowing

**Never splice SQL.** Everything is a `ParsedExpression` tree from load to emit. Identifier quoting
and operator precedence then come from DuckDB's printer rather than from us.

**Aliases are dataset names.** `FROM tpcds.main.item AS item` keeps emitted SQL readable against
the model. It assumes a dataset appears at most once per query; joining one twice would need
aliases keyed by join path.

**Filters are allowlisted, model expressions are not.** Model expressions come from whoever defined
the semantic layer, who already chose which tables to expose. Filters arrive per request from the
agent, so their node types are checked: operators always, named function calls only under
`allow_filter_functions`, subqueries never. The flag lives on `ossie_load` so a caller supplying
filters cannot widen its own policy.

**Refuse rather than guess.** Where the model underdetermines the query, the correct behaviour is
an error naming the offending object. Error text is part of the interface — an agent reads it and
retries — so refusals are tested on their message, not just on the fact that they threw.

## Tests

| Layer | File | Catches |
|---|---|---|
| Load and validation | `ossie_load`, `ossie_validate` | malformed models, structural errors |
| Description | `ossie_describe`, `ossie_relationships` | vocabulary surface, cardinality |
| Golden SQL | `ossie_compile`, `ossie_grain` | compiler output and refusals |
| Differential | `ossie_differential` | **grain bugs** |

The differential tests run generated SQL and hand-written TPC-DS equivalents over `dsdgen` data and
diff the results with symmetric `EXCEPT`. They are the only layer that catches a wrong *number* —
a golden-file test passes just as happily on SQL that is valid and incorrect. One of them pins the
`INNER` join behaviour: grouping by `store.s_state` drops exactly the sales whose foreign key is
NULL, so a change in join type will fail a test rather than silently move a total. Its twin pins
`left_joins => true`: the same sales reappear as the `NULL` group and the grouped total equals the
ungrouped one.

Formatting matches DuckDB's, enforced in CI:

```sh
python3 -m venv build/fmtvenv
build/fmtvenv/bin/pip install "black>=24" "clang_format==11.0.1" cmake-format
PATH="$PWD/build/fmtvenv/bin:$PATH" make format
```

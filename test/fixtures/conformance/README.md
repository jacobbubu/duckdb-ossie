# Conformance fixtures

Ossie models written by **other implementers**, copied verbatim from
[apache/ossie](https://github.com/apache/ossie). They are Apache-2.0 licensed; see that repository
for terms and authorship.

They are here because every other fixture in `test/fixtures/` was written in this repository, by the
same person who wrote the compiler. Those fixtures answer "does the compiler agree with itself?"
They cannot answer "does it agree with the Ossie format?", because the fixtures and the compiler
share the same assumptions. These files are the only ones that can.

They are **copied in, not fetched at test time**: CI must not depend on the network, and upstream
moves.

All files are taken at apache/ossie commit `e4f545a4fd6aef1a98a8438c22e550d67a992e06` and
keep upstream's own paths under this directory, so where each came from is visible in its path. The
per-converter fixtures the previous snapshot carried (databricks, gooddata, nvidia, omni, orionbelt)
no longer exist upstream; the compliance models replace them.

| file | source in apache/ossie | loads today |
|---|---|---|
| `examples/tpcds_semantic_model.yaml` | `examples/` | yes (its three window-function metrics are refused when queried) |
| `examples/renamed_fields.yaml` | `examples/` | yes (field names differ from source columns) |
| `examples/flights.semantic_model.yaml` | `examples/` | yes |
| `compliance/wide_table/retail/retail_model.ossie.yaml` | `compliance/wide_table/retail/` | yes (dataset-scoped metrics; `row_count`, `order_less_return`, `amount_less_order` and `running_revenue` are refused when queried) |
| `compliance/wide_table/stores/stores_model.ossie.yaml` | `compliance/wide_table/stores/` | yes |
| `compliance/wide_table/stores/stores_ambiguous_model.ossie.yaml` | `compliance/wide_table/stores/` | yes (its two paths to `date_dim` are refused as ambiguous when queried) |
| `compliance/query/scott/scott_model.ossie.yaml` | `compliance/query/scott/` | yes |
| `compliance/portability/m0/m0_ansi_qualified.ossie.yaml` | `compliance/portability/m0/` | yes |
| `compliance/portability/m0/m0_ossiesql_qualified.ossie.yaml` | `compliance/portability/m0/` | yes |
| `compliance/portability/m0/m0_sum_bare.ossie.yaml` | `compliance/portability/m0/` | no — its metric names a column without a dataset, which the spec forbids |
| `compliance/portability/m1/m1_scalar_field.ossie.yaml` | `compliance/portability/m1/` | yes |
| `compliance/portability/m2/m2_aggregates_qualified.ossie.yaml` | `compliance/portability/m2/` | yes |
| `compliance/portability/m2/m2_aggregates_bare.ossie.yaml` | `compliance/portability/m2/` | no — same bare column reference |
| `compliance/portability/m3/m3_filtered_agg_qualified.ossie.yaml` | `compliance/portability/m3/` | yes |
| `compliance/portability/m3/m3_filtered_agg_bare.ossie.yaml` | `compliance/portability/m3/` | no — same bare column reference |

`ossie-schema.json` is the official schema from `core-spec/`, kept so the models here (and our own)
can be validated against the format's own definition rather than against our reading of it.

All fifteen are Ossie 0.3.0.dev0 documents: one model at the root, `expression.sql` shorthand,
dataset-scoped `metrics`, `nonportable_extensions`. When upstream changes them again, re-vendor all
of them from one commit and update that commit here and in `NOTICE`.

Twelve of fifteen models load. The three refusals are correct: the `*_bare` portability models
reference a column by bare name inside a model-scoped metric, which the spec requires to be written
`dataset.field`; upstream keeps them as pending variants of the `*_qualified` siblings. Keep this
table and `test/sql/ossie_conformance.test` in step when the number changes.

## Wide-table compliance cases

`compliance/wide_table/` also carries upstream's 46 compliance cases for the Wide Table Query
Interface (Layer 3), with the data they run on and upstream's own READMEs: `cases/<case>/query.json`
and `expected.txt`, `cases/README.md`, `retail/retail.sql`, `stores/stores.sql`. Each case is a query
plus the rows or the Section 8 error code it must produce; `expected.txt` names the fixture to load
and holds the result as upstream's `ost` runner prints it, and `cases/README.md` says what each case
shows.

`scripts/wide_table_compliance.py` runs them: it loads the fixture's SQL and model in a duckdb CLI,
submits `query.json` as it stands through `ossie_query(query)`, and compares as upstream's README prescribes (rows as a
multiset, values by position, exact numerics equal, the one approximate column within 1e-9; an empty
cell is NULL). Models are loaded
with `left_joins => true`, the join the specification requires. Every case gets one status:

| status | cases | meaning |
|---|---|---|
| `pass` | 46 | rows and output names match, or the refusal carries the expected error code |
| `names` | 0 | rows match under different output names |
| `wrong` | 0 | rows differ |
| `false-refusal` | 0 | refused where rows were expected |
| `false-acceptance` | 0 | answered where an error was expected |
| `refused` | 0 | refused as required, but the message does not carry the `E_*` code |

`wide_table_status.tsv` pins the status of every case, and `scripts/full_functionality_check.sh`
fails when a run disagrees with it. A case that improves is a failure too: update the pin with
`scripts/wide_table_compliance.py --update` and this table in the same change, so the file always
says where the implementation stands against the specification. Run with `-v` to see what each case
returned.

## Query compliance cases

`compliance/query/` carries upstream's 32 compliance cases for the Relational Query Interface
(Layer 2), with the SCOTT data and upstream's README: `cases/<case>/query.ossie.sql`,
`expected.sql` and `reference.duckdb.sql`, and `scott/scott.sql`.

A case is SQL over the model's relations, with `AGG()` evaluating a measure:
`SELECT deptno, AGG(sum_sal) FROM scott_model.emp GROUP BY deptno`. `scripts/query_compliance.py`
measures every case two ways against the SCOTT data, model loaded with `left_joins => true`, and
compares as upstream's README prescribes (columns by position, rows as a multiset or in order when
the case has `ORDER BY`, exact numerics equal, approximate ones within 1e-9).

**As written.** The statement of `query.ossie.sql` is handed to `ossie_sql` as it stands, which is
what the specification asks of an implementation. Every case gets one status:

| status | cases | meaning |
|---|---|---|
| `pass` | 32 | the query runs, and its rows and column names match |
| `names` | 0 | its rows match under different column names |
| `wrong` | 0 | its rows differ |
| `refused` | 0 | `ossie_sql` refuses the query |

**As a request.** `ossie_query`, the Wide Table interface, is what an agent is given, so the
narrower question is kept: can the question of a case be asked as **one** `ossie_query` request, and
does that request return the rows of `expected.sql`?

`query_requests.sql`, written here, holds that request for each case that has one, and for each
that has none a line saying what stops it. A request is one `SELECT` over one `ossie_query` call.
Around the call it may order, limit, select and rename columns and compute scalars from them, which
is what the cases do to the values of `AGG()`; it reads no table itself. Every case gets one status:

| status | cases | meaning |
|---|---|---|
| `pass` | 12 | a request asks the question, and its rows and column names match |
| `names` | 0 | its rows match under different column names |
| `wrong` | 0 | its rows differ |
| `refused` | 0 | the request is refused |
| `no-request` | 20 | no single `ossie_query` request asks the question |

What stops the 20:

| cases | what the query does that a request cannot |
|---|---|
| `b05`, `b08`, `b09`–`b13` | joins `emp` to itself and reads measures of both roles, under a join type the query chooses |
| `b06`, `b07` | joins on a range (`sal BETWEEN losal AND hisal`); the model has no relationship there |
| `b04`, `b18`, `b20` | groups a measure of the "one" side by a field of the "many" side; the Wide Table specification refuses this (`E_PARTIAL_DIMENSION_REACHABILITY`), and `ossie_query` does |
| `b14`, `b16` | lets the join, or a condition in `ON`, restrict one side's measure and not the other's |
| `a11`, `b03`, `b19` | filters by a subquery or joins a derived relation |
| `a08` | groups by an expression rather than a declared field |
| `a09`, `a12` | combines grouping sets (`ROLLUP`) or aggregates measure values again; two requests and the caller's SQL can |

The script also runs each case's `reference.duckdb.sql` and compares it with `expected.sql`, with
no part of the extension involved; all 32 agree, so the vendored data and the comparison are sound.
If one stops agreeing the script exits 2.

`query_status.tsv` pins both statuses of every case, as written and then as a request, and
`scripts/full_functionality_check.sh` fails when a run disagrees with it. Update the pin with
`scripts/query_compliance.py --update` and these tables in the same change. Run with `-v` to see
what a query or a request returned, why one was refused, or why a case has no request.

The small `*.json` files beside the upstream copies are written here, not vendored: each exercises
one malformed document shape against the loader.

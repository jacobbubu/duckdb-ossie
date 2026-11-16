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

All files are taken at apache/ossie commit `da73e0e1b5c5b1d17aa7ca763fe1fc5db9bc943c` and
keep upstream's own paths under this directory, so where each came from is visible in its path. The
per-converter fixtures the previous snapshot carried (databricks, gooddata, nvidia, omni, orionbelt)
no longer exist upstream; the compliance models replace them.

| file | source in apache/ossie | loads today |
|---|---|---|
| `examples/tpcds_semantic_model.yaml` | `examples/` | yes (its three window-function metrics are refused when queried) |
| `examples/renamed_fields.yaml` | `examples/` | yes (field names differ from source columns) |
| `examples/flights.semantic_model.yaml` | `examples/` | yes |
| `compliance/wide_table/retail/retail_model.ossie.yaml` | `compliance/wide_table/retail/` | yes (dataset-scoped metrics; `row_count`, `order_less_return` and `running_revenue` are refused when queried) |
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
Interface (Layer 3), with the data they run on and upstream's own README: `cases/<case>/query.json`
and `expected.json`, `retail/retail.sql`, `stores/stores.sql`. Each case is a query plus the rows or
the Section 8 error code it must produce.

`scripts/wide_table_compliance.py` runs them: it loads the fixture's SQL and model in a duckdb CLI,
submits the query through `ossie_query`, and compares as upstream's README prescribes (rows as a
multiset, values by position, exact numerics equal, approximate ones within 1e-9). Models are loaded
with `left_joins => true`, the join the specification requires. Every case gets one status:

| status | cases | meaning |
|---|---|---|
| `pass` | 1 | rows and output names match, or the refusal carries the expected error code |
| `names` | 17 | rows match; output columns are named `dataset.name` where the specification wants the unqualified name |
| `wrong` | 0 | rows differ |
| `false-refusal` | 2 | refused where rows were expected: queries without a metric (`a07`, `a12`) |
| `false-acceptance` | 2 | answered where an error was expected: an aggregate in the filter (`e07`), a filter reading two datasets (`e09`) |
| `refused` | 19 | refused as required, but the message does not carry the `E_*` code; six of them (`e05`, `e08`, `e17`, `e19`, `e21`, `e23`) are refused only because the query has no metric |
| `inexpressible` | 5 | the query uses a part of the JSON binding `ossie_query` has no argument for: an alias (`a09`, `e10`), an unknown key (`e01`, `e20`), a null filter (`e02`) |

`wide_table_status.tsv` pins the status of every case, and `scripts/full_functionality_check.sh`
fails when a run disagrees with it. A case that improves is a failure too: update the pin with
`scripts/wide_table_compliance.py --update` and this table in the same change, so the file always
says where the implementation stands against the specification. Run with `-v` to see what each case
returned.

The small `*.json` files beside the upstream copies are written here, not vendored: each exercises
one malformed document shape against the loader.

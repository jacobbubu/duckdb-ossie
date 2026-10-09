# Population compile explanation

`ossie_concept_compile` now returns `plan_json` with the project-specific schema
version `ossie-population-plan-v1`. It describes the generated semantic SELECT AST
and the immutable definition/data bundle used to build it. This is our extension
contract, independent of Ossie document versions and the MCP protocol version.

```sql
SELECT sql, plan_json, snapshot_id, executed
FROM ossie_concept_compile(
  'ValidUser', ['gaap_sum'], ['dws_gaap_di.ds'],
  ['User.product = ''X'''], include_trace => true);
```

This is not an optimizer or physical plan. The compile endpoint does not execute
or perform the final query `Prepare`/table-output binding. `executed=false`,
`physical_plan=false` and `sql_replayable=false` make that scope explicit.
For example, duplicate metric aliases can appear as separate ordered projections
in a diagnostic plan, while the table query binder rejects duplicate output names.
The execution result and its trace remain the evidence that a query actually ran.

## Fields and evidence source

| Field | Meaning and source |
|---|---|
| `schema_version`, `phase`, `request_kind`, `profile` | Project schema and actual compilation path |
| `engine` | DuckDB dialect, library version and source ID |
| `definitions` | Core/ontology/mapping names, validated versions, byte hashes and manifest hash from the loaded bundle |
| `snapshot` | Bound snapshot/content hash, full captured population row count and row-order sensitivity |
| `concept.formal_rule` | Rule owner, original definition SQL, resolved definition SQL and whether this request applied it |
| `identity` | Original logical dataset/field and the bounded profile's entity/record identity relationship |
| `grain` | Validated population input dataset, original declared primary key and aggregation stage |
| `inputs` | Structured source captures: dataset, actual source, complete captured rows and input hash |
| `capture_join` | Validated relationship endpoints/key columns and INNER/reject policy; null for single-table |
| `metrics` | Ordered canonical metric names, selected/available dialects, declared SQL and parsed/lowered definition expressions |
| `declared_fields` | Original declared fields, datatype, expressions and effective/explicit time roles; includes unused declarations |
| `filters` | Clearly labeled requested strings and final generated WHERE SQL |
| `time` | `range_inferred=false`: time scope remains expressed by WHERE and declared time roles |
| `generated_ast` | Actual FROM, ordered projection ordinal/expression/alias, and GROUP BY expressions from the same final statement |
| `result_contract`, `limitations` | Query generation behavior and limits of the explanation |

Definition provenance is captured during load before changing source names or
flattening the joined model. `declared_sql` is the selected text saved by the
parser; `parsed_lowered_sql` is its parsed/lowered representation, not a raw
original-document AST. Available dialect names do not mean every variant was
selected or executed. Ontology rules/request filters use the profile's scalar
SQL subset and do not inherit the selected core expression dialect.

The final WHERE, FROM, projections and groups come from the production statement
after function references are pinned to engine functions. They are not generated
by a second compiler. Resolved formal-rule SQL is definition provenance; the
final effective WHERE shows whether and how it participated in this request.
Root requests set `applied=false`; no predicate is JSON null. Duplicate projections
retain individual one-based ordinals and aliases rather than collapsing into a map.
With `include_trace=true`, the three actual trace projections appear in this list.

Full capture row counts are not filtered membership counts, group counts or AVG
denominators. `filtered_rows_computed=false` avoids attributing an unexecuted
measurement to compile. A rule's threshold applies before aggregation; under the
current profiles each captured identity has one observation. That does not define
multi-period entity identity generally.

A fact-dimension bundle records both source inputs even when only fact metrics are
requested. Its input grain was validated against the original model before
flattening; join keys and complete matching were checked over the whole captures.
The join occurred at load-time into frozen data. The generated query scans that
collection and has no live dimension JOIN. Group expressions describe requested
output grouping separately from the original fact input grain.

Time roles include `is_time_explicit` so an explicit false differs from a default.
No single interval is inferred from complex conditions. In the boundary fixture,
`date < March 2 OR date >= March 31` includes April 1; the plan retains that entire
predicate rather than silently reporting a March-only range or calendar.

## Output compatibility and lifecycle

The compile output gains `plan_json` immediately before `executed`. The original
first nine columns retain their positions, but the output now has **eleven columns**
and the position of `executed` changes. Fixed ten-column unpacking is affected.
Read by column name. Object/query result schemas are unchanged; compile newly
accepts `include_trace` using the same trace projection code as those endpoints.

Plan text is stored with the bound statement. Prepared compile requests retain
old definition and snapshot metadata across reload; failed loads retain the current
bundle. Later file modifications cannot change a loaded explanation. There is no
bind-time reread of model assets and no additional history cache.

Diagnostic SQL still needs the frozen AST dependency and cannot be replayed as
ordinary SQL. Separate compile/query requests can bind different snapshots after
reload. JSON numeric transport and external renderer size/truncation guarantees
are outside this parser-plan contract; result generation itself adds no sorting,
paging, truncation or calendar completion.

[Native stock-artifact controls](research/ossie-req-001/r7-plan-artifact-results.json)
parse the entire JSON and check normal execution, duplicate-binding refusal,
disjoint DATE groups, empty captures, dialect selection/time opt-out, prepared
reloads, failed loads and modified definition files. Additional
[joined controls](research/ossie-req-001/r7-plan-joined-population-artifact-results.json)
check quoted names, computed CASE fields and protected ratios. The actual
[MCP transport](research/ossie-req-001/r7-plan-mcp-results.json) returns and verifies
this plan with trace projections enabled. These support the bounded explanation
package, not complete R7/P0 or physical-plan conformance.

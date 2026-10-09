# R6 first safe-join contract: research result

The current artifact confirms a safe numeric baseline for six synthetic facts and
four dimension rows, including one unreferenced dimension. It also demonstrates
that declaration-based core joins accept unsafe data. This document specifies the
next bounded population implementation; it does not claim that implementation exists.
The existing `gaap-scalar-population-v1` still refuses multiple datasets and joins.

## What the artifact establishes

An independent hand-defined association/cents oracle gives six facts, five non-NULL
amounts, SUM 55.01 and AVG 11.002. Joined groups are North: four facts/SUM 24.98;
South: two facts/SUM 30.03. The explicit active-and-eligible record condition gives
f1, f2, f5, f6 and SUM 24.98. Fact-ID drilldown is obtained through a core grouped
query, with a native SQL comparison; it is not a multi-table ontology object query.

The fixed-fixture research SQL preflight checks the whole fact/dimension batches.
It is an oracle, not an exported extension validation function.

| Data change | Actual core behavior | Research safety decision |
| --- | --- | --- |
| Duplicate referenced dimension id 1, including identical row | Eight joined rows, SUM 85.04; fact-only query stays six/SUM 55.01 | Reject duplicate dimension key |
| Duplicate unreferenced dimension id 4 | Joined result unchanged | Reject the entire invalid dimension batch |
| NULL dimension key | Joined result unchanged | Reject NULL key |
| Missing dimension id 3 | Four joined rows, SUM 60.06 | Reject unmatched fact foreign key |
| NULL fact foreign key for f1 | Five joined rows, SUM 45.00 | Reject NULL foreign key |
| Unmatched f3 outside active scope | Active query could avoid this row | Reject before request/population filtering |
| Duplicate or NULL fact identity | Duplicate rows or COUNT(id) hides the NULL identity | Reject identity violation |
| INTEGER fact key versus VARCHAR dimension key containing both '1' and '01' | Implicit coercion causes duplicate matches | Reject unequal physical key types |
| Empty facts with duplicate dimension key | No joined rows | Still reject the invalid dimension batch |
| Nonempty facts and empty dimension | No joined rows | Reject unmatched foreign keys |
| Both tables empty, valid schemas | Scalar COUNT=0, SUM=NULL; no groups | Accept empty batch |
| NULL nonkey dimension label | A NULL dimension group | Accept missing scalar attribute |

Actual join-row counts and per-fact multiplicities are recorded separately from
COUNT(fact.id), which excludes NULL. Counts or total amounts alone cannot establish
an identity-preserving join. Unreferenced valid dimension rows create no facts.

Declared many-to-many, reverse fan-out, ambiguous paths and mixed metric grains
remain refused by the actual compiler. A declared unique_keys entry can provide
the same structural cardinality as primary_key, but neither declaration proves
actual uniqueness. A dimension-grain metric can compile independently: COUNT of
the four dimension rows is 4, whereas the same reference over flattened fact rows
counts 6. Compile success alone cannot certify fact-grain execution.

## Proposed bounded implementation

Use a separate, explicit two-table profile, leaving v1's contract unchanged. The
first shape has one designated fact dataset, one dimension dataset, one plain
single-column equality relationship, and an explicit INNER/complete-match policy.
The profile selects the join policy; relationship cardinality does not select it.
Start with equal native INTEGER foreign/target keys as exercised here. Any future
integer-width or uncollated VARCHAR key expansion needs its own controls; floating
keys, collations, coercion, composite keys, multiple facts, role-playing dimensions,
LEFT joins and temporal effective-dated relationships remain outside this slice.

The fact's non-NULL unique String identity remains both entity and record identity.
Many facts may share the same dimension key. That key does not replace fact identity
or implicitly introduce a dimension ontology entity. Dimension keys are non-NULL
and unique over the entire dimension table, even for unused rows. Every fact foreign
key is non-NULL and matches exactly one dimension row; unused valid dimension rows
are allowed. Missing nonkey values remain SQL NULL. All checks precede formal
population rules and request filters.

Both sources must be committed native tables in the same underlying database.
Capture both complete bounded tables in one transaction, then validate and join only
those captured collections. The host experiment holds a transaction open after a
fact read while a second connection changes a dimension label: subsequent dimension
and joined reads still see the old label. After commit, the live core query sees the
new label. This proves the tested native transaction behavior, not production bundle
capture, cross-database consistency, or post-publication immutability.

Reuse the current collection lifetime and atomic bundle publication. Type/identity/
matching failure or cancellation must preserve the old bundle. Verify that the joined
fact-ID multiset equals the captured fact-ID set with multiplicity one. Enforce bounded
row/column/byte budgets on both inputs and the result; don't silently truncate unused
dimension rows. Input evidence must include the complete schemas/data of both tables,
join policy, mapping version and definitions. Changes to unused dimensions must also
change their input hash; document whether row order affects hashes.

Before flattening, validate each metric against the original model and the designated
fact grain. Extract a small read-only RequireMetricGrain-style interface from existing
compiler analysis, sharing CollectGrains/AggregateGrain/SafelyReachable with Compile.
Do not duplicate a second AST grain implementation. Every aggregate recognized by this analysis must infer the designated fact grain.
The recognized function-name set is not exhaustive, and analysis precedes field
expansion. This is a prerequisite, not a complete expression safety proof. Fact SUM/AVG/COUNT and a dimension condition in a
fact-grain CASE may pass; dimension-grain aggregates, mixed grains, windows and
independent grainless COUNT(*) keep explicit refusals. The new checker also does
not infer a grain for COUNT(1) or a grainless aggregate alongside SUM(fact.amount),
while the existing core's anchored COUNT(*) behavior is retained. These are bounded
profile restrictions, not claims that such SQL formulas are universally unsafe.
Nested aggregates and hidden field expressions can pass the grain prerequisite
and fail physical binding later. In a measured counterexample, an unrecognized
approx_count_distinct(dimension.id) alongside SUM(fact.amount) passes both grain
analysis and binding. Therefore the future profile must apply its strict AST
allowlist to original and expanded expressions before using this prerequisite;
physical binding cannot substitute for that allowlist. This check supplements normal
Compile reference/path checks and physical binding; it does not prove data safety.
Never perform this check after merging both datasets into a single synthetic dataset.

Preserve explicit `(dataset, physical column)` to frozen-column correspondence and
rewrite AST references structurally. Check case/quoting/shared column names/reserved
names, rather than relying on concatenated names. Retain original field provenance,
metric grain and actual connection explanation in compile/execution metadata. The
current compiler's population_source replaces only the root: dimension BindTable
calls would still read live tables. A correct implementation must ensure its final
AST references only frozen collections and carries their dependencies. Source updates
should become visible only after explicit reload, for both tables.

## Evidence and remaining work

The [artifact report](research/ossie-req-001/r6-join-research-artifact-results.json)
records actual core results, separate research safety reasons, source/fixture hashes,
planner refusals and the native transaction experiment. The
[control summary](research/ossie-req-001/r6-join-control-results.json) records local gates.
The [existing issue](ossie-execution-research.md) and
[existing plan](../ossie-duckdb-plan.md) own remaining work and authorization.
The designated-grain prerequisite is now implemented as internal C++
RequireMetricGrain, sharing AnalyzeMetric with Compile. It has no public SQL
entry and is not yet wired into a joined population profile. Its
[control evidence](research/ossie-req-001/r6-grain-control-results.json) covers 42
C++ checks plus actual-artifact regressions. The next implementation package is
the explicit frozen two-table profile and object/metric, reload/cancellation/lifetime
controls. Those production behaviors are not yet implemented or verified.

The fixed [Ossie relationships schema](research/ossie-req-001/sources/apache-ossie/core-spec/spec.md)
defines foreign/target columns without an INNER/LEFT selection. DuckDB's official
[join documentation](https://duckdb.org/docs/current/sql/query_syntax/from) describes
matching and outer-join behavior; its
[transaction documentation](https://duckdb.org/docs/current/sql/statements/transactions)
describes snapshot isolation. Runtime claims above use pinned 1.5.6 actual results.

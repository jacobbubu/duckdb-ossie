# R6 safe-join contract and production checkpoint

The original research artifact confirmed a safe numeric baseline for six synthetic facts and
four dimension rows, including one unreferenced dimension. It also demonstrates
that declaration-based core joins accept unsafe data. The historical evidence below
is separate from the new production implementation checkpoint.
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

## Historical research and prerequisite evidence

The [artifact report](research/ossie-req-001/r6-join-research-artifact-results.json)
records actual core results, separate research safety reasons, source/fixture hashes,
planner refusals and the native transaction experiment. The
[control summary](research/ossie-req-001/r6-join-control-results.json) records local gates.
The [existing issue](ossie-execution-research.md) and
[existing plan](../ossie-duckdb-plan.md) own remaining work and authorization.
At the prerequisite checkpoint, the designated-grain API was implemented as internal C++
RequireMetricGrain, sharing AnalyzeMetric with Compile. It has no public SQL
entry; integration had not yet begun at that checkpoint. Its
[control evidence](research/ossie-req-001/r6-grain-control-results.json) covers 42
C++ checks plus actual-artifact regressions. The subsequent implementation checkpoint below records the explicit frozen two-table
profile and object/metric, reload/cancellation/lifetime controls.

The fixed [Ossie relationships schema](research/ossie-req-001/sources/apache-ossie/core-spec/spec.md)
defines foreign/target columns without an INNER/LEFT selection. DuckDB's official
[join documentation](https://duckdb.org/docs/current/sql/query_syntax/from) describes
matching and outer-join behavior; its
[transaction documentation](https://duckdb.org/docs/current/sql/statements/transactions)
describes snapshot isolation. Runtime claims above use pinned 1.5.6 actual results.


## Production checkpoint: fact-dimension-population-v1

The separate profile now implements this bounded contract. A manifest adds an
explicit `join` object to the same hash-bound `core`, `ontology` and `mapping`
references:

```json
{
  "profile": "fact-dimension-population-v1",
  "join": {
    "fact": "fact",
    "dimension": "dimension",
    "relationship": "fact_dimension",
    "type": "INNER",
    "missing": "reject"
  }
}
```

The complete executable [fixture manifest](../test/fixtures/joined-population/bundle.json)
includes the required definition references. Dataset order does not select the fact.
The loader captures two distinct committed native tables within one source transaction,
compares their AttachedDatabase identities, and validates every captured key before
filtering. It constructs one output row per fact through exact native INTEGER lookup,
then rescans the actual frozen collection to compare fact identity sets and multiplicity.
No source SQL JOIN remains in the execution model. Both definitions' fields are
structurally mapped to distinct generated logical/physical namespaces, with original
names used for mappings, filters, grouping and object output aliases.

`ossie_concept_compile.execution_target` is JSON for this new profile. It reports the
actual frozen target, fact metric grain, relationship and physical keys, INNER/reject
policy, full input table row counts/hashes, mapping version and logical/physical
provenance. The v1 target remains its original text. Diagnostic SQL still requires the
AST dependency and cannot independently recreate the snapshot. `ossie_concepts.source`
names both original sources; profile/snapshot/hash discovery describes the joined batch.

Full ordered input schemas/data (including unused dimension rows), joined data,
definition/manifest hashes and the provenance mapping contribute to the content hash.
Hashing is row-order-sensitive; native scans are not promised to produce a stable order.
Each input and the joined result has the existing row/column/canonical-byte bounds,
and the joined schema is limited to 256 physical columns. These are acceptance budgets,
not strict bounds on peak materialization memory. The shared diagnostic capability
label now describes the bounded population profiles instead of hardcoding v1.

Independent production controls cover exact fact-ID association, money/count/average,
shared object/metric population, 24 whole-batch/definition refusals, empty tables,
NULL nonkey attributes, generated-name collisions, quoted identifiers, computed fields,
guarded ratios, successful and failed reload, source updates/deletion, running load
cancellation and parent-alive database close. All failure/cancellation controls retain
the prior snapshot. A prepared request keeps its previously bound snapshot across reload.
The new verifier is separate from the earlier research verifier and wired into the same
real-artifact CI job. At the first production milestone, deterministic writer injection between the two
production captures had not been tested. The controlled-native checkpoint below
now supplies that source-path evidence; the earlier host transaction experiment
remains separate supporting evidence. Remote CI and cross-platform behavior remain unverified.
Current validation reports and remaining necessities stay in the existing issue/plan.

Production evidence: [joined controls](research/ossie-req-001/r6-joined-population-artifact-results.json) and [local gate summary](research/ossie-req-001/r6-joined-control-results.json). The first production milestone recorded an unexplained native mutex error under parallel verification. The old verifier cleanup defect was corrected without claiming it caused every native failure. The subsequent checkpoint below retains and investigates that incident with a reproduced product lifetime defect.


## Controlled production capture and cancellation checkpoint

A new native `ossie_population_capture_test` links the same production extension
sources, without a test-specific FreezeJoin variant. A caller-local internal C++
observer pauses after fact capture and before dimension capture. An independent
writer connection commits distinguishable changes to both tables in one transaction.
The published batch still contains old fact values, old dimension attributes/rules,
and identical full input hashes/metadata. Removing the observer and reloading exposes
both new versions. Prepared readers retain their original snapshot.

The runner also cancels at two acknowledged boundaries: after fact capture while
the source transaction remains open, and after all checks but before the publication
interrupt check. In threads=4 and threads=1, both paths preserve prior snapshot/hash,
actual objects and metrics. Callback failure does not publish, and reload after
observer removal succeeds. Observers have no SQL or environment switch, receive
no mutable bundle/source connection, and execute outside the registry lock.
Gate waits have five-second timeouts; worker join is not a hard process timeout.
The test proves these controlled boundaries, not that every interrupt arriving
between the final check and publication can prevent publication.

The added capture cancellation exposed a real lifetime defect in the existing Run
helper. A nested PendingQuery had already scheduled work, but caller cancellation
threw before task cleanup; its destructor does not drain tasks, and ClientContext
skips Destroy during exception unwinding. A native crash report showed background
TaskNotifier destruction accessing expired context state. Run now interrupts and
calls CancelTransaction on the nested connection while it and the pending result
remain alive, draining tasks before throwing caller interruption. This API cleans
query execution; it does not by itself roll back an explicit source transaction.
The short-lived source context's transaction destructor handles that rollback.

The earlier stock mutex error was reproduced immediately after load cancellation.
The confirmed cleanup defect matches that symptom; no first-throw mutex native
backtrace was obtained because macOS denied debugserver attach. The actual native
crash frames are archived in a sanitized hash-bound record. The controlled native
crash no longer occurs, and all six stock verifiers pass against the fixed artifact.
This identifies and repairs a verified unsafe lifetime path, without asserting that
all possible native mutex failures have this one cause.

Evidence: [138-check native capture and local gates](research/ossie-req-001/r6-capture-control-results.json),
[pre-fix failure evidence](research/ossie-req-001/r6-capture-failure-evidence.json) and
[fixed stock joined profile](research/ossie-req-001/r6-capture-joined-population-artifact-results.json).
The native controlled runner and the stock artifact regression are complementary;
no claim is made that the stock Node verifier itself injected the writer barrier.
Remote CI, Linux tidy and the wider requirement acceptance remain outstanding.

# Fact-dimension population

`fact-dimension-population-v1` is the bounded two-table profile next to the
single-table [`gaap-scalar-population-v1`](population-profile.md). It freezes one
designated fact dataset and one dimension dataset, associates them through one
single-column equality relationship, and then serves the same `ossie_concepts()`,
`ossie_objects(...)`, `ossie_concept_query(...)` and `ossie_concept_compile(...)`
functions over the joined, frozen population. v1 keeps refusing multiple datasets
and core joins; this profile is selected explicitly in the manifest.

## Manifest

The manifest adds a `join` object to the hash-bound `core`, `ontology` and
`mapping` references:

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

The [fixture manifest](../test/fixtures/joined-population/bundle.json) is a
complete example; its two sources are created by the statements in
[rows.sql](../test/fixtures/safe-join-research/rows.sql). `join.fact` names the
dataset whose rows become objects; dataset order in the core does not select it.
`join.relationship` must be a core relationship from the fact to the dimension
with one `from_columns` and one `to_columns` entry. The profile selects the join
policy; relationship cardinality does not. Only `INNER` with `missing: "reject"`
is supported: LEFT joins, composite keys, multiple facts, role-playing dimensions
and effective-dated relationships are refused.

By default both key columns must be native INTEGER with equal physical types.
[`native-string-join-v1`](string-join.md) opts a batch into byte-exact VARCHAR
keys; implicit coercion between key types is never applied, because a VARCHAR
dimension holding both `'1'` and `'01'` would otherwise match one INTEGER fact
key twice.

```sql
CALL ossie_bundle_load('test/fixtures/joined-population/bundle.json');
SELECT * FROM ossie_concept_query('User',['record_count','amount_sum'],['dimension.label'],[]);
SELECT entity_id FROM ossie_objects('User',['dimension.label = ''South''']);
```

Fields of both datasets keep their original `dataset.field` names in mappings,
filters, grouping and object output. Internally they are rewritten to distinct
generated namespaces; the generated names are not addressable from requests.

## Capture and validation

Both sources must be committed native tables in the same underlying database.
The loader captures both complete tables within one source transaction, so a
writer committing to both tables between the two reads cannot produce a batch
that mixes old fact rows with new dimension rows. Every check below runs over the
captured collections, before any membership rule or request filter is applied,
and any failure rejects the whole batch:

| Condition | Decision |
| --- | --- |
| Duplicate dimension key, including an identical row and keys no fact references | Reject the dimension batch |
| NULL dimension key | Reject |
| Fact foreign key with no matching dimension row | Reject |
| NULL fact foreign key | Reject |
| Duplicate or NULL fact identity | Reject |
| Unequal physical key types | Reject |
| Empty facts with an invalid dimension batch | Still reject |
| Nonempty facts and an empty dimension | Reject (unmatched keys) |
| Both tables empty with valid schemas | Accept: COUNT is 0, SUM is NULL |
| NULL non-key dimension attribute | Accept: the attribute is SQL NULL |

The fact's non-NULL unique String identity remains the entity and record
identity. Many facts may share a dimension key; the key never replaces fact
identity or introduces a dimension entity. Unreferenced valid dimension rows are
kept in the capture but create no objects. After building one output row per
fact through exact key lookup, the loader rescans the frozen collection and
checks that the joined fact-ID multiset equals the captured fact-ID set with
multiplicity one.

Type, identity or matching failure, resource refusal and cancellation all keep
the previously published batch. Prepared requests keep the snapshot they were
bound to across reloads and after the source tables are dropped. Source changes
become visible only through an explicit reload.

## Metric grain

Before fields are expanded, every metric is checked against the designated fact
grain using the same grain analysis as the core compiler. Fact `SUM`/`AVG`/`COUNT`
and a dimension condition inside a fact-grain `CASE` pass; dimension-grain
aggregates, mixed grains, windows, grainless `COUNT(*)` or `COUNT(1)` and
unrecognized aggregate functions are refused. Nested aggregates and hidden field
expressions can pass the grain check and still fail at binding. This check
supplements the normal reference/path checks and physical binding; it does not
prove that an arbitrary expression is safe.

## Compile metadata and hashing

`ossie_concept_compile(...).execution_target` is a JSON object for this profile.
It reports the frozen target, fact metric grain, relationship and physical keys,
INNER/reject policy, both input tables' row counts and hashes, mapping version and
logical/physical provenance. The v1 target remains plain text. The diagnostic SQL
still depends on the AST and cannot recreate the snapshot on its own.
`ossie_concepts().source` names both original sources.

The content hash covers the complete ordered schemas and data of both inputs,
including unused dimension rows, the joined data, the definition and manifest
hashes and the provenance mapping. It is row-order-sensitive, and native scans do
not promise a stable order. Each input and the joined result is subject to the
existing row/column/canonical-byte limits, and the joined schema is limited to 256
physical columns. These limits are refusal thresholds, not bounds on peak memory.

## Lifetime notes

`ossie_population_capture_test` links the production loader with an internal
observer that pauses between the fact and dimension captures, so a concurrent
writer and cancellation at the capture boundaries can be exercised in the C++
tests. Observers have no SQL or environment switch. Cancelling a running load,
whether during capture or after validation, preserves the prior snapshot; the
nested query helper interrupts and cancels its connection before propagating the
caller's interruption so that scheduled tasks are drained first.

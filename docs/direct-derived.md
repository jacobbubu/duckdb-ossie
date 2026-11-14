# Bounded multiple direct derived concepts

`direct-derived-concepts-v1` is an explicit implementation capability for the
existing `gaap-scalar-population-v1` and `fact-dimension-population-v1` profiles.
It removes their one-child limit within the bounds below; it adds no business
profile and does not claim full Ossie Ontology conformance. The pinned document
format remains Ossie `0.2.0.dev0`.

Add `direct-derived-concepts-v1` to the bundle manifest `capabilities` list. It
can appear alone or with `concept-scalar-requires-v1`, and on the joined profile
with `native-string-join-v1`, in any order without duplicates. Unknown or illegal
combinations are refused. Other four profiles retain their existing contracts.

## Population and binding

There is exactly one root and 1–8 direct children. Each child has exactly one
parent, the root, and exactly one existing bounded scalar BOOLEAN `derived_by`
expression. Identity, attributes and the single root Mapping are inherited.
Independent child Mapping, identity or attributes, multiple parents, multilevel
inheritance, cycles and sibling inference are refused.

Each child is an independent subset of the root: only TRUE creates membership;
FALSE or NULL does not. Subsets may overlap and need not cover the root. An object
present in two children remains one root object. There is no implicit union,
exclusive classification or new cross-concept aggregate operator.

In the new path a rule may reference the root or its own child qualifier. A
request filter may also use declared dataset fields, as before. Another child's
qualifier is refused, rather than treated as that sibling's membership or
silently rewritten. Executable concept names remain exact; duplicate concept
names differing only in case are refused. Attribute resolution keeps existing
qualified-field behavior. Dataset and concept namespaces must be distinct.

Optional concept requires use the existing finite Integer/DATE grammar and
actual mapped source types. The root is checked over all root objects; each
child is checked over its own TRUE members. Every declaration is preflighted,
including empty children. Any violation fails the whole load before publication;
query filters cannot shrink that validation scope. One child's constraint must
not inspect another child's population. Existing whole-source identity and join
integrity checks still apply before these constraints.

## Discovery and versioning

Opt-in batches report API `2.5.0` and diagnostic plan schema
`ossie-population-plan-v3`. Existing SQL columns stay the same. The root plan has
`parent=null` and `formal_rule=null`. A child plan names its root parent and its
own applied rule. It lists selected root/child constraint owners and separately
identifies previously completed load validation; it does not revalidate constraints
on the query population. It never selects the first sibling as an explanation for the
root or another child. Compile remains diagnostic, `executed=false`, with no
replayable SQL guarantee.

Discovery lists the root and all direct children in definition order, with root
first. `capabilities_json.direct_derived` has schema
`ossie-direct-derived-concepts-v1`, complete child names/parents/rules, bounds,
shared identity/Mapping and overlap/refusal flags. Optional requires and typed
join metadata remain independently identified. Typed object JSON retains its
existing result schema and binds to the selected concept and immutable snapshot.

Without the new capability, defaults remain API2.2/plan-v1, sole requires remains
API2.3/plan-v1, String opt-in remains API2.4/plan-v1, and observation plan-v2 is
unchanged. Old single-child acceptance/rejection is preserved. Old prepared
queries retain all their bound concepts, rules, metadata and data across reload,
child addition/removal and source deletion; failed/cancelled loads retain the
previous snapshot.

## Resources and verification

Each rule is limited to 16 KiB, 1,024 AST nodes and depth 32; total direct-rule SQL
is capped at 128 KiB. Requires remain at most eight per concept, **16 total** and
128 KiB total, not eight times the expanded concept count. Capture still allows
at most 10,000 rows per input, 256 columns and 16 MiB canonical bytes. These are
acceptance limits, not measured peak memory or throughput guarantees.

The two new fixtures differ structurally: one classifies Integer values from a
single fact table, the other classifies a dimension's DATE via String keys while
retaining a conflicting fact DATE, repeated FK values and unused dimensions.
Each has overlapping, exclusive, unclassified and NULL records. Independent
full identity/value/group answers and hand-written SQL are used; a total count
or renamed business example is insufficient.

Offline tests open no DB. The separate controlled entry point imports without
starting any process:

```sh
node --test --test-concurrency=1 test/tooling/direct_derived.test.ts test/tooling/direct_derived_native.test.ts
node scripts/run_direct_derived_native.ts prepare --output .local-agent-evidence/direct-derived-prepared-001
node scripts/run_direct_derived_native.ts build --prepared .local-agent-evidence/direct-derived-prepared-001/prepared.json --sha256 '<prepared SHA-256>'
node scripts/run_direct_derived_native.ts run --prepared .local-agent-evidence/direct-derived-prepared-001/prepared.json --sha256 '<prepared SHA-256>' --build-receipt .local-agent-evidence/direct-derived-native-build-001/summary.json --build-sha256 '<build summary SHA-256>' --artifact-sha256 '<extension SHA-256>'
```

The fixed suite runs direct-derived checks, the full String suite, then all six
original stages including the 20-stage regression. It keeps existing assertions,
probes, postflight byte guards, failure sealing and unknown-cleanup rejection.
The default entry point, quality commands and CI are unchanged. New paths must
be fresh; build is fixed to `build/direct-derived-001`. Source, dependency,
tool and 14 output byte pins are reviewed before execution; no installs or
automatic query/data corrections occur.

The new worker permits 10,000 explicit SQL calls and 120 seconds of work, with
150 seconds outer supervision and 16 MiB each for raw event stream/report.
There are zero retries. Shared build allows 1,800 seconds; whole run 1,500 seconds,
cleanup 10 seconds and seal 60 seconds. Raw process output is capped at 64 MiB
and evidence at 512 MiB. These are separate deadlines, not an end-to-end SLO.

## Limited local acceptance

Frozen candidate-001 passed one controlled Mac ARM64 build and one eight-stage
native run, without retries, on DuckDB 1.5.6 and Bun 1.3.11. The new family passed
142 empty/nonempty scopes and 7,713 explicit SQL calls: 48 successful loads,
94 expected load refusals, and 88 expected sibling-reference request refusals.
Independent full identities, values and groups matched hand-written SQL. Plans,
constraint owners, complete discovery, typed JSON and prepared preservation after
reload/removal/source deletion were checked from the raw events.

The complete prior String family (128 scopes/3,428 SQL), requires boundaries
(408 scopes/9,017 SQL), original A/B, capture (874 checks, threads=4/1), all 31 SQL
files (1,206 assertions) and the 20-stage regression passed. Targeted offline
checks passed 166 new and 386 existing Bun tests. These are fixed local proofs,
not exhaustive ontology, capacity, cross-platform, CLI/MCP or Agent acceptance.

Accepted extension SHA-256: `558fb0d03a8c7a7060fc6b549348dbcfe0c3a44d2a7924beac4bb1c698cad3c8`.
The 24-file frozen candidate and original prepared/build/run receipts remain
immutable. This subsequent five-document status update has separate before/after
pins and preserved copies; it does not re-sign old receipts or make the old
strict prepared byte check accept changed documentation. Repeating execution
requires fresh reviewed binding and directories.
 Existing
requires/String/Agent/CLI/MCP results apply only to their own bound versions.
Old snapshots and public anchors are preserved, not re-signed. AMD64, new-model
calls, public target-tree delivery and cross-platform CI of unpushed work are
outside this local feature slice; the whole Ontology/P1 is not complete.

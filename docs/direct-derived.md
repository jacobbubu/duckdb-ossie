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

## Resource limits

Each rule is limited to 16 KiB, 1,024 AST nodes and depth 32; total direct-rule SQL
is capped at 128 KiB. Requires remain at most eight per concept, **16 total** and
128 KiB total, not eight times the expanded concept count. Capture still allows
at most 10,000 rows per input, 256 columns and 16 MiB canonical bytes. These are
acceptance limits, not measured peak memory or throughput guarantees.

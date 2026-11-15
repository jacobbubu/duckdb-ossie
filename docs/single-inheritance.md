# Bounded single inheritance

`bounded-single-inheritance-v1` is an explicit opt-in for the ordinary scalar and
fact-dimension population profiles. It permits exactly one root and 1–8 derived
concepts, with a single direct parent each and at most three parent edges from
the root. All concepts share the root identity, scalar attributes and sole root
Mapping. This is a bounded subset of the fixed Ossie ontology, not a general
ontology reasoner or a new business profile.

A derived object's membership requires every rule along its ancestor path to be
TRUE. FALSE or NULL at any ancestor excludes the object, even if its own rule is
TRUE. Branches may overlap and need not cover the root. Root counts remain root
identity counts. Rules and request filters can use attributes qualified by self
or an ancestor; these are aliases for the same mapped root attributes, not
membership tests or joins. Sibling and descendant references are refused.
Requests can still use declared dataset fields; rules cannot. Requires retain
their owner-only finite Integer/DATE grammar.

Each owner's requires validates that owner's complete effective population at
load, including all ancestor rules. Root constraints inspect the entire root.
Intermediate constraints cannot be narrowed to leaf members or query filters.
All declarations are preflighted even on empty populations. Any violation
rejects the whole load without publishing a partial tree. There is no guarantee
of short-circuit evaluation masking invalid casts or arithmetic errors.

Discovery reports direct parents and own rules, root first and then increasing
depth with definition order breaking ties. API `2.6.0` and diagnostic
`ossie-population-plan-v4` apply only to this opt-in. Plans retain the own
`formal_rule`, a root-to-self `lineage`, all nonroot `applied_rules`, and the full
path of `selected_constraint_owners`. Constraint metadata uses
`ossie-concept-constraints-v2` with grammar `concept-scalar-requires-v1` and each
owner's lineage. These describe completed load validation, not query-time
revalidation. Compile is diagnostic, not a replayable or executed physical plan.

The new mode is mutually exclusive with `direct-derived-concepts-v1`; legal
requires and typed-join capabilities can be combined without order dependence.
Old API2.2/2.3/2.4/2.5, plans v1/v2/v3 and specialized profiles retain their
contracts. Multiple parents, cycles, unknown parents, excessive depth, child
Mapping/identity/attribute declarations, explicit ValueType and general
relationship inference are refused. Forward parent declarations are allowed.

Per-rule limits remain 16 KiB/1,024 AST nodes/depth32, with 128 KiB total rule
text. The generated ancestor predicate has a separate 4,096-node/depth40 bound;
that is not a claim that the complete request has depth32. Up to 64 request
filters retain their individual limits. Requires remain eight per owner and
16 total, with 128 KiB total. Existing capture limits remain unchanged. None of
these limits proves peak memory or throughput.

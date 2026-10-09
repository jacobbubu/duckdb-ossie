# Project quality controls

These controls supplement the existing pinned DuckDB format/tidy, build/SQL,
stock CLI artifact loading and MCP checks. They preserve the current compiler
boundary and address one model/policy consistency defect. They do not complete
the OSSIE-REQ-001 ontology goal or establish remote branch protection.

## Model and policy capture

`OssieState::GetSnapshot()` copies the shared model and `allow_filter_functions`
under one lock. A concurrent successful load publishes both under that same lock.
Readers hold `shared_ptr<const Model>`; publishing code must not subsequently
mutate the model. This is a read contract, not a deep copy of every AST node.
Failed loads leave the previous pair intact. The independent policy getter has
been removed; discovery may read only the model through `GetModel()`.

`ossie_query` captures the pair in `bind_replace`. Its generated AST contains the
chosen model expressions and policy decisions, so an already bound prepared query
retains that plan across model reload. The native table data remains live.
`ossie_compile` captures one pair per execution chunk and is explicitly VOLATILE
to prevent constant folding at PREPARE. A prepared scalar call reads current state
on execution. One chunk cannot mix model A and policy B; a multi-chunk statement
can observe a later publication in a later chunk. This model/policy contract is
separate from the population API's frozen observation data collection.

The dedicated `ossie_state_test` executable uses a condition-variable barrier to
hold A while another thread publishes B, then verifies actual compiler acceptance,
source/expression choice and old-model lifetime. It also exercises two connections,
failed loads and the distinct prepared scalar/table-query timing. Checks throw on
failure in Release builds; they do not disappear with NDEBUG. Waits have timeouts
and thread cleanup joins on every path.

The target is built explicitly by `state-test-release`/`debug`/`reldebug`, which are
prerequisites of the corresponding non-skipped SQL test targets. It does not depend
on `ENABLE_UNITTEST_CPP_TESTS`; ci-tools normally disables that setting. The pinned
distribution workflow calls these Make test targets where tests are enabled. Builds
that deliberately skip tests do not provide this evidence.

## Commands and formatter scope

```sh
make quality-local       # format, graph rules, isolated negative controls
make release
make test                # dedicated state runner, then existing SQL tests
make state-test-release  # run only the dedicated state contract
```

Project-maintained tooling runs on **Bun 1.3.11 + TypeScript**. Install with
`bun install --frozen-lockfile`; `package.json` and `bun.lock` pin the client and
development tools. TypeScript 5.9.3 runs strict `tsc --noEmit`, independently of
Bun execution. Prettier 3.6.2 checks project `scripts/*.ts`, `test/tooling/*.ts`
and toolchain configuration; `bun test test/tooling` runs isolated controls.

`make format-check` retains the pinned DuckDB formatter for `src test`.
`make script-format-check` checks TypeScript; `make quality-local` requires both,
as well as type checking, the include graph and Bun tests. The upstream reusable
format job has no Bun setup, so it gates C++ formatting while `project-controls`
installs the fixed Bun runtime and gates TypeScript. Neither check silently skips.
`make format` / `format-fix` also rewrite the project TypeScript files.
No vendor, submodule or immutable research-source tree is formatted.

The C++ extension, CMake/Make build, shell CLI smoke adapter and pinned upstream
Python formatter/tidy scripts remain engine tooling. There are no active
project-maintained Python gates or artifact verifiers. Historical Python research
sources and reports remain immutable. Upstream format tools retain Black 24.x,
clang-format 11.0.1 and cmake-format 0.6.13; the root `.clang-format` remains
identical to the pinned DuckDB file. Local clang-tidy is unavailable; its authoritative
Ubuntu job has not been run locally. No new global naming/coverage threshold is imposed.

## Bounded dependency rules

`scripts/check_architecture.ts` inventories all project C++ source/headers under
`src`, compares source files with `EXTENSION_SOURCES`, and resolves quoted/angle
project includes and relative paths. It rejects unresolved project includes,
nonliteral includes it cannot analyze, empty source inventories and zero inputs.
Commented-out includes are ignored; preprocessor conditions are not evaluated.

Compiler and model/parser structure modules cannot reach state/functions/extension
registration nodes through the project include graph. Header-only paths through
helpers are checked. Project header cycles are refused. Direct DuckDB runtime
includes through those project paths are also checked. The only adapter exceptions
are precise edges in `src/model/parser.cpp` (ClientContext/FileSystem) and
`src/model/catalog.cpp` (catalog entry lookup/ClientContext), because those files
contain loading/catalog adapters alongside pure helpers. These exceptions permit
no state/functions edge. DuckDB AST includes remain allowed; `catalog.hpp` declares
both a pure source-name helper and a runtime adapter, so including it does not imply
catalog I/O. Model authors' permitted physical-column references remain supported.

This is a bounded static rule, not a complete preprocessed dependency graph or a
proof of no I/O/injection. DuckDB/vendor transitive includes and function-call/data
flows are not expanded. It does not treat every SQL string template as unsafe.
Violations print a dependency path. The isolated tests cover direct/indirect edges,
runtime headers, header cycles, resolution, comments, missing inventories and zero
inputs, and format failure followed by recovery.

## CI and independent evidence

The stable `CI gate` still runs with `always()` and requires all existing jobs to
succeed; it additionally requires `project-controls`. A negative control executes
the actual gate shell body with failure/skipped/cancelled outcomes for every
required job, and confirms nonzero status. Another checks Make's state runner is
scheduled before the SQL runner. An actual missing-fixture state invocation was
also confirmed to propagate failure through Make.

The existing Linux/macOS `artifact-load` matrix now runs the loader, population, expression and DATE
verifiers against the same downloaded extension in stock DuckDB Node bindings 1.5.6 (client 1.5.6-r.1),
checking source ID `069cc9f9b5` and unsigned file loading without static fallback.
Evidence is uploaded per architecture. The loader checks its required eight entry
names and query overloads, tolerating unrelated additional functions; its metadata
checks remain intact. The population verifier retains its independent numeric and
object oracle and description/AI/unit-extension invariance. CSV values are parsed
independently as bigint cents; SUM and counts compare exactly, AVG uses the existing
1e-10 DOUBLE tolerance. DATE and DECIMAL use native typed values, without JSON or
floating-point conversion. Empty list parameters explicitly bind LIST(VARCHAR).
Native interrupts run during the load, and close releases both connection and
instance before another process reopens the file while the parent stays alive.

The TypeScript MCP probe retains the exact server.sql install replacement, exclusive
semantic tool listing, query/export refusal, rows, grain refusal and positive control
without disabling flags. Reads have a real deadline; every exit path terminates the
child and removes temporary files. Transport controls exercise silent and failed servers.

JSON metadata serialization now owns yyjson's write buffer with a local custom
RAII deleter, including exceptional string construction. Normal metadata controls
passed; allocator failure was not injected. The single existing yyjson NOLINT is
explained as a local C API adapter, with no wider exemption.

Historical cleanup results are in
[cleanup-results.json](research/ossie-req-001/cleanup-results.json), with same-artifact
[loader](research/ossie-req-001/cleanup-loader-artifact-results.json) and
[population](research/ossie-req-001/cleanup-population-artifact-results.json) controls.
The CLI smoke script now requires a stock CLI and real artifact, rejects load failures,
and reuses the existing nested-field fixture as a normal gating assertion.
Missing CLI/artifact, loading failure and relative-path normalization have isolated controls.
There is no project Python fixture generator or static build-tree fallback.

Historical expression-slice results are in
[r3-control-results.json](research/ossie-req-001/r3-control-results.json), with same-artifact
[expression](research/ossie-req-001/r3-expression-artifact-results.json),
[loader](research/ossie-req-001/r3-loader-artifact-results.json) and
[population](research/ossie-req-001/r3-population-artifact-results.json) reports.
The expression verifier uses hand-derived ratios and native type/NULL controls,
27 rejection cases, macro isolation and guarded-overflow evidence. Local checks pass
30 Bun controls, 19 C++ checks and 566 SQL assertions in 16 cases; this does not
establish full R3 or safe-join support.

Historical DATE-boundary controls are in
[r3-date-control-results.json](research/ossie-req-001/r3-date-control-results.json),
with same-artifact [DATE](research/ossie-req-001/r3-date-artifact-results.json),
[loader](research/ossie-req-001/r3-date-loader-artifact-results.json),
[population](research/ossie-req-001/r3-date-population-artifact-results.json), and
[expression](research/ossie-req-001/r3-date-expression-artifact-results.json) reports.
Native DATE sentinel handling preserves infinity rather than formatting it as a
finite date. Local verification passes 31 Bun controls, 19 C++ checks and 610 SQL
assertions in 17 cases; CI wiring schedules all four verifiers against one download.
No C++ execution change or new artifact build was necessary in this slice.

Historical R6 research controls are in
[r6-join-control-results.json](research/ossie-req-001/r6-join-control-results.json) and
[r6-join-research-artifact-results.json](research/ossie-req-001/r6-join-research-artifact-results.json).
The fifth same-download verifier distinguishes actual live core behavior from a
fixed-fixture research SQL safety oracle; it does not add joined population support.
Local gates pass 31 Bun controls, 19 C++ checks and 661 SQL assertions in 18 cases.
The previous four actual-artifact reports remain applicable to unchanged sources,
helper and artifact; they were not rerun in this research package.

Current designated-grain prerequisite evidence is in
[r6-grain-control-results.json](research/ossie-req-001/r6-grain-control-results.json).
The dedicated ossie_metric_grain_test runner runs explicitly before SQL alongside
the state runner, even with ENABLE_UNITTEST_CPP_TESTS=FALSE. Local results are
42 grain controls plus 19 state checks, 661 SQL assertions in 18 cases, and 31 Bun
controls. All five actual-artifact verifiers were rerun on the rebuilt artifact.
This adds an internal original-model analysis prerequisite, not joined population.

Previous Bun migration results and source hashes are in
[bun-toolchain-results.json](research/ossie-req-001/bun-toolchain-results.json),
[bun-loader-artifact-results.json](research/ossie-req-001/bun-loader-artifact-results.json)
and [bun-population-artifact-results.json](research/ossie-req-001/bun-population-artifact-results.json).
The previous quality/Python and earlier research reports remain historical evidence.
Remote CI execution and branch protection are unverified; no protection or upstream
setting was changed. See the [existing work record](ossie-execution-research.md)
for authorization and remaining ontology work. Sanitizer/coverage expansion remains
outside this bounded package.

## Population MCP checkpoint

The existing `mcp-round-trip` job now also runs `verify_population_mcp.ts` against
the same downloaded extension and uploads its result. It uses the stock CLI and
real JSON-RPC, reads concept resource content, validates three exclusive tools,
independent original GAAP threshold-10/20 and joined fixture results, list failures,
formal rule protection, recovery and fail-fast setup. The original core MCP probe
now reads its metric/field resources as well; reversed PRAGMA query/URI arguments
in the old example and CLI smoke registration are corrected. See
[the calling contract](population-mcp.md) and
[local evidence](research/ossie-req-001/r7-population-mcp-results.json).
This is a local transport package and CI wiring, not remote CI or full R7/P0.

## Population plan checkpoint

The same-download `artifact-load` matrix now runs `verify_plan_artifact.ts` in
addition to the six existing stock verifiers. It parses versioned `plan_json`,
checks original-vs-generated expression provenance, actual output ordinals and
trace, disjoint DATE population, empty capture, dialect choice, immutable prepared
reload/failed loads and changed definition files. Joined controls also inspect
quoted-name and computed CASE/ratio plans. The MCP probe checks the plan returned
by the actual server; its compile template enables trace projections. New SQL
envelope/prepared controls avoid a JSON-extension dependency; full JSON parsing
is performed in the stock Bun verifier. See [the plan contract](population-plan.md).
This is generated-AST evidence, not a physical plan or remote CI result.

## Population discovery checkpoint

The same-download matrix also runs `verify_discovery_artifact.ts`. It checks
actual load-preflight types, original field and metric vocabulary, project API
version/capabilities, separate core state, prepared discovery across reload,
failed loads and immutable definition files. GAAP is the primary requirement
fixture; non-GAAP joined controls cover discovered requests, reversed datasets
and computed/quoted names. The existing joined verifier additionally checks
quoted dataset provenance. Actual MCP probes read all four discovery resources
and construct queries from returned names; there remain exactly three tools.
See [the discovery contract](population-discovery.md). Validation flags describe
successful load binding, not execution or arbitrary combined-request success.

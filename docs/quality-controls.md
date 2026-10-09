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

`make format-check`, `format` and `format-fix` retain the pinned DuckDB formatter
for `src test` and add a prerequisite for project `scripts`. The same formatter
uses Black with `--skip-string-normalization --line-length 120`; C++ uses the
unchanged root `.clang-format` (identical to the pinned DuckDB file). No vendor,
submodule or immutable research-source tree is formatted. Script reformatting is
style-only except the explicit verifier/architecture changes; executable modes
are retained. Python source files under `scripts` are now covered; shell lint is
not part of this package.

The formatter accepts Black >=24 and clang-format 11.x, while existing CI installs
Black 24.x and clang-format 11.0.1. Local acceptance used Black 24.10.0,
clang-format 11.0.1 and cmake-format 0.6.13 in a task venv. CI's new control job uses
Python 3.12; the local venv used Python 3.14.7. Both use the same pinned formatter
commands; actual versions are printed. Existing clang-tidy uses the Ubuntu system
package and its current pinned configuration, including disabled identifier naming.
No new global naming, coverage or complexity threshold is imposed. Local clang-tidy
was unavailable; this package does not claim its authoritative CI job passed.

## Bounded dependency rules

`scripts/check_architecture.py` inventories all project C++ source/headers under
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

The existing Linux/macOS `artifact-load` matrix now runs the loader and population
verifiers against the same downloaded extension in stock DuckDB wheel 1.5.6,
checking source ID `069cc9f9b5` and unsigned file loading without static fallback.
Evidence is uploaded per architecture. The loader checks its required eight entry
names and query overloads, tolerating unrelated additional functions; its metadata
checks remain intact. The population verifier retains its independent numeric and
object oracle and adds description/AI/unit-extension invariance.

JSON metadata serialization now owns yyjson's write buffer with a local custom
RAII deleter, including exceptional string construction. Normal metadata controls
passed; allocator failure was not injected. The single existing yyjson NOLINT is
explained as a local C API adapter, with no wider exemption.

Current local results and hashes are in
[quality-control-results.json](research/ossie-req-001/quality-control-results.json),
[quality-loader-artifact-results.json](research/ossie-req-001/quality-loader-artifact-results.json)
and [quality-population-artifact-results.json](research/ossie-req-001/quality-population-artifact-results.json).
Historical reports remain intact. On 2026-10-09, read-only GitHub API returned
Actions enabled but zero registered workflows/runs for this fork. The new quality
changes were not pushed during local acceptance. Remote CI execution and branch
protection are unverified; no protection or upstream setting was changed. See the
[existing work record](ossie-execution-research.md) for authorization and remaining
ontology work. Sanitizer/coverage expansion remains outside this bounded package.

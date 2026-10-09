# Testing this extension

This directory contains all the tests for duckdb_ossie.

- `sql/` — [SQLLogicTests](https://duckdb.org/dev/sqllogictest/intro.html). This is the primary
  format; most behavior should be asserted here as SQL. Negative tests belong here too, and they
  assert both *that* a query is refused and *what the error message says* — the messages are part
  of the interface, since an agent reads them and retries.
- `golden/` — expected `ossie_compile` output for a fixed set of semantic queries. These need no
  database and are the fastest signal, so they should run on every commit.
- `fixtures/` — the TPC-DS semantic model plus a deliberately nasty model (multiple fact tables,
  an ambiguous join path, a role-playing dimension, a nullable FK). Which of its queries a phase
  can answer is largely what defines that phase.

Differential tests run generated SQL and the hand-written TPC-DS equivalent against the same data
and diff the result sets. That is the only layer that catches grain bugs.

The root makefile contains targets to build and run all of these tests. To run the SQLLogicTests:

```bash
make test
```

or

```bash
make test_debug
```
Project-specific quality checks:

```sh
bun install --frozen-lockfile
make quality-local
make state-test-release
```

The state runner is a dedicated C++ executable with controlled publication and
cross-connection compiler checks. `make test` builds/runs it before SQLLogicTests,
even with `ENABLE_UNITTEST_CPP_TESTS=FALSE`. Bun/TypeScript controls use isolated temporary
trees to prove the format/architecture gates reject violations and that the actual
CI gate rejects failed, skipped or cancelled dependencies. See
[quality-controls.md](../docs/quality-controls.md) for scope and timing contracts.

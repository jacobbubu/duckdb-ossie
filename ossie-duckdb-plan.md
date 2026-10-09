# duckdb_ossie — Implementation Plan

## Current research checkpoint — 2026-10-09

Current local milestone: the separate fact-dimension-population-v1 implementation now
captures and validates two full native tables in one transaction, freezes exact INTEGER
associations, preserves original provenance and rejects non-fact metric grains before
flattening. Actual artifact controls cover shared object/metric membership, reload,
source deletion, cancellation, prepared snapshot retention and database close. Final
same-artifact gates pass 19 state and 42 grain C++ controls, 713 SQL assertions in
19 cases, 31 Bun controls, all six stock artifact verifiers, CLI and MCP. Evidence
is reconciled in OSSIE-REQ-001; an earlier intermittent native mutex incident is
retained without an established common root cause. Remote CI and
deterministic concurrent writer injection into production capture remain unverified.
P0 and the wider R6 goal remain incomplete. This milestone is retained in local Git.
Next authorized work strengthens production capture/concurrency evidence; no new push is authorized.
The earlier package counts in this introduction remain historical evidence.

The first research package is complete: pinned source evidence, an independent
475-row GAAP oracle, hand-derived boundary controls, schema-checked definition
drafts and an independently reviewed minimal execution contract. This does not
complete P0; the initial research did not prove ontology execution. The subsequent
bounded single-table implementation is now independently tested, as described below.
The user subsequently authorized local implementation and tests in this repository,
starting with an isolated build of the pinned feature branch. Only the pinned new
document shape is required; legacy wrapper compatibility is not required. Push,
upstream PR changes, merge and publication remain outside the authorization.
The problem, evidence, remaining necessities and next authorized action are
maintained in [the OSSIE-REQ-001 research record](docs/ossie-execution-research.md).
The historical ontology non-goal below is superseded only for the reviewed bounded
scalar-population profile in OSSIE-REQ-001. General ontology/OWL/RDF execution is
still excluded. The user accepted the YTD-only initial population rule (active/paid
unverified), SQL grouping and NULL defaults, and explicit calendar completion as
a separate capability. The first local implementation package is complete: exact core version checks,
logical datatypes/time defaults and metadata discovery, verified by 437 assertions in
14 SQL cases plus a real artifact loaded in an independent DuckDB wheel. The second
package now implements the strict single-table User→ValidUser profile, whole-batch
identity/type/AST preflight, shared frozen object/metric population and execution
trace. Fifteen SQL cases/503 assertions, independent real-artifact Decimal and
boundary controls, cancellation and cross-process database-close checks passed.
This completes that package; P0 and full A1–A8 remain incomplete. The next authorized
step is independent controls/research for remaining NULLIF/division/time semantics
and safe multi-table planning, before widening execution. General ontology, calendar
completion and population MCP integration remain later necessities. Current evidence,
retained failures and action scope stay in the same linked research record.

A DuckDB extension that reads Apache Ossie semantic model files and answers
semantic queries against the tables they describe, exposed to AI agents over MCP.

This document is the working spec. It is written to be handed to an agentic coding
tool. Read the whole thing before writing code — the invariants and non-goals matter
more than the file layout.


## Quality checkpoint — 2026-10-09

The user inserted the architecture/quality handoff before further feature work.
The completed functional baseline 2b1e8b5 was pushed to the fork branch as explicitly
requested. The bounded local quality package now fixes paired core model/policy
capture and prepared scalar timing, extends the pinned formatter to scripts, checks
project include reachability/cycles, and wires the independent artifact verifiers
and isolated controls into the existing CI gate. `make test` executed the dedicated
19-check state runner with DuckDB C++ tests disabled, then passed 15 SQL cases/503
assertions. Nineteen gate controls passed; two stock-wheel verifiers tested the same
final artifact, including descriptive metadata invariance. Root formatting, vendor
source and gitlinks remain intact.

This completes local acceptance of this quality package, not P0 or remote CI.
The fork reports Actions enabled but no registered workflows/runs; quality changes
have not been pushed, and authoritative Linux/macOS CI/tidy and remote merge rules
remain unverified. No remote settings changed. Review the quality package/delivery
boundary before resuming the existing R3/R6 work; keep evidence and remaining needs
in [the same work record](docs/ossie-execution-research.md), with the implementation
contract in [quality-controls.md](docs/quality-controls.md).

## Current toolchain checkpoint — 2026-10-09

Project-maintained gates, probes and two stock artifact verifiers now use Bun 1.3.11
and strict TypeScript with a frozen lockfile. Existing C++/CMake/Make, shell CLI smoke
and pinned upstream formatter remain. Historical research sources and reports are
preserved. Native Bun binding load, exact-money oracle, running interrupt and
parent-alive cross-process close controls have passed. Make/CI wiring and final
source-hash reports are complete: 25 Bun controls, 19 C++ checks, 503 SQL assertions,
loader/population independent artifact controls, actual MCP and 40 FAITHFUL stock CLI
checks passed. See docs/research/ossie-req-001/bun-toolchain-results.json. No remote CI or new push
is claimed; remaining product R3/R6 work remains in the existing research record.

---

## 1. Context

### What Apache Ossie is

Apache Ossie (incubating, formerly Open Semantic Interchange) is a vendor-neutral
**file format** for semantic models. A model is a YAML or JSON document with four
building blocks:

- `datasets` — logical entities bound to physical tables via `source: db.schema.table`
- `fields` — columns, or SQL expressions over columns, tagged with `datatype` and
  `dimension.is_time`
- `relationships` — declared foreign-key edges: `from`/`to` datasets plus
  `from_columns`/`to_columns`
- `metrics` — aggregate expressions written against qualified names, e.g.
  `SUM(store_sales.ss_ext_sales_price)`

Plus `ai_context` blocks (synonyms, instructions) scattered throughout, aimed at LLM
consumers.

Reference model to develop against:
`https://github.com/apache/ossie/blob/main/examples/tpcds_semantic_model.yaml`

### The critical fact

**Ossie describes data in place. It never transforms data.** `source:` points at a
table that already exists. The `expression` fields exist so the user does not have to
reshape anything.

### The gap this extension fills

The spec defines leaf expressions but not the query they go into. Search the TPC-DS
example for `GROUP BY`, `WHERE`, or `JOIN` — zero hits. Those depend on the question
being asked, and the file does not know the question.

No engine reads Ossie natively today. The shipped reference implementations are
*converters* (Ossie→dbt MetricFlow, Ossie→Polaris, Ossie→Snowflake semantic views) that
translate definitions into a vendor format so that vendor's engine can run them. A
standardized semantic query language with a reference compiler is under discussion in
the Ossie community but not shipped.

This extension is that missing engine: embedded, single-binary, zero-infrastructure.

### Why a compiler and not a template

The same metric definition produces different SQL depending on what was requested:

```sql
-- total_sales, no dimensions
SELECT SUM(ss_ext_sales_price) FROM store_sales;

-- total_sales by item.i_brand
SELECT item.i_brand, SUM(store_sales.ss_ext_sales_price)
FROM store_sales JOIN item ON store_sales.ss_item_sk = item.i_item_sk
GROUP BY item.i_brand;
```

The fragment from the file is byte-identical in both. Everything else was derived at
request time from the relationships graph. The correct join set is a function of the
requested fields — which is why the decision cannot be made once at load time.

---

## 2. Invariants

These hold in every phase. Violating one is a bug regardless of what the tests say.

1. **Never return a wrong number.** Refusing with a clear error is always correct
   behavior. An agent querying this extension cannot check the answer, so a plausible
   wrong number is worse than a failure. This outranks feature coverage.

2. **Never build SQL by string concatenation.** Parse fragments into
   `ParsedExpression` trees at load time, rewrite the trees, emit from the tree.
   String splicing produces quoting bugs, precedence bugs, and injection.

3. **Fail at load time, not query time.** Malformed expressions, unresolvable
   `source:` names, and dangling relationship endpoints are `ossie_load` errors.

4. **`ossie_compile()` is a first-class output, not a debug aid.** It makes the
   extension useful where DuckDB is not the executor, and it makes the test suite cheap
   (golden files, no database needed).

5. **The model is the authorization boundary.** A name not in the model does not
   resolve. Never provide an escape hatch that lets a caller reference an unmodeled
   column.

---

## 3. Non-goals

Explicitly out of scope. Do not build these speculatively.

- Writing, mutating, or validating Ossie models beyond schema conformance
- Converting Ossie to other semantic formats (that space is well covered)
- General ontology / OWL / RDF reasoning. The bounded scalar-population profile
  authorized in the current research checkpoint is the sole exception.
- Query result caching
- Ossie `custom_extensions` vendor blocks — parse and ignore
- Natural-language → metric-name mapping. The agent does that; the extension exposes
  vocabulary and accepts exact names.

---

## 4. Architecture

```
ossie_load(path)
  └─ parse JSON/YAML → validate → build Model
       ├─ datasets, fields, relationships, metrics
       ├─ ParsedExpression tree per field and metric  (parsed once, here)
       ├─ relationship graph with per-edge cardinality flags
       └─ stored in ClientContextState

ossie_query(metrics, dimensions, filters)      ossie_compile(same args)
  └─ Compiler                                    └─ Compiler
       ├─ resolve names → required datasets           └─ stmt->ToString()
       ├─ plan joins over the graph
       ├─ grain check (refuse or split)
       ├─ rewrite expression trees to aliases
       └─ assemble SelectNode
            └─ bind_replace → SubqueryRef
```

### Compiler stages

**Stage 1 — Resolve.** Map each requested identifier to a metric or field, and record
the owning dataset. Unresolvable names error here with a message listing near matches.
Output: the set of required datasets.

**Stage 2 — Plan joins.** Find the minimal connected subgraph of `relationships`
covering the required datasets. For a star schema this is a walk outward from the fact
table. In general it is a Steiner tree. If more than one path connects the same pair of
datasets, **refuse** — do not pick.

**Stage 3 — Grain check.** For each edge in the join tree, determine cardinality: the
edge is many-to-one if `to_columns` matches the target dataset's `primary_key` **or any
entry in `unique_keys`**. Check both — in the TPC-DS example `store` declares
`primary_key: [s_store_sk]` but `unique_keys: [[s_store_id]]`, and the relationship
joins on `s_store_sk`.

If a requested metric aggregates a column from a dataset that fans out relative to the
query grain, either refuse (phase 3a/3b) or split into a per-grain subquery (phase 3c).

**Stage 4 — Rewrite.** Walk each metric/field `ParsedExpression` tree. For every
`ColumnRefExpression` whose qualifier names a dataset, rebind to the bound table alias.
If the referenced field is itself computed, inline a copy of its expression tree.
`ParsedExpressionIterator::EnumerateChildren` gives the traversal.

**Stage 5 — Emit.** Build `SelectNode` with `select_list`, nested `JoinRef` in
`from_table`, `where_clause`, `groups.group_expressions`, and
`aggregate_handling = AggregateHandling::STANDARD_HANDLING`.

- `ossie_compile` → `stmt->ToString()`
- `ossie_query` → return `make_uniq<SubqueryRef>(std::move(stmt))` from `bind_replace`

`bind_replace` runs before binding, so DuckDB plans and optimizes the generated query as
if the user typed it. Join reordering, filter pushdown, and projection pruning come free
— do not reimplement them.

### Worked example: fan-out

`store_productivity` is defined as:

```
SUM(store_sales.ss_ext_sales_price) / NULLIF(SUM(store.s_number_employees), 0)
```

Joining `store` to `store_sales` replicates `s_number_employees` once per sale line.
Naive evaluation returns a number ~1000× too large. The correct shape computes each
aggregate at its own grain:

```sql
WITH sales AS (
  SELECT store.s_state, SUM(store_sales.ss_ext_sales_price) AS num
  FROM store_sales JOIN store ON store_sales.ss_store_sk = store.s_store_sk
  GROUP BY store.s_state
),
staff AS (
  SELECT s_state, SUM(s_number_employees) AS den
  FROM store
  GROUP BY s_state
)
SELECT sales.s_state, sales.num / NULLIF(staff.den, 0) AS store_productivity
FROM sales JOIN staff USING (s_state)
```

Phases 3a/3b refuse this query. Phase 3c produces the above.

---

## 5. Public interface

### Loading

```sql
CALL ossie_load('model.json');
CALL ossie_load('model.json', rebind => MAP{'tpcds.public': 'tpcds.main'});
```

`rebind` remaps warehouse-qualified `source:` prefixes onto the local catalog. Without
it, TPC-DS models referencing `tpcds.public.*` will not resolve against a local DuckDB
catalog. Required in practice — implement it in phase 0.

### Introspection

```sql
SELECT * FROM ossie_datasets();       -- name, source, primary_key, description, synonyms
SELECT * FROM ossie_fields();         -- dataset, name, datatype, is_time, expression, synonyms
SELECT * FROM ossie_metrics();        -- name, datatype, expression, description, synonyms
SELECT * FROM ossie_relationships();  -- name, from, to, from_columns, to_columns, cardinality
```

These are how an agent discovers vocabulary. They are not scaffolding.

### Querying

```sql
SELECT * FROM ossie_query(['total_sales'], ['item.i_brand'], ['date_dim.d_year = 2001']);
SELECT ossie_compile(['total_sales'], ['item.i_brand'], ['date_dim.d_year = 2001']);
```

Dimensions are `dataset.field`. Filters are SQL predicate strings over qualified names;
parse and validate them, never splice them raw.

### MCP publication

```sql
INSTALL duckdb_mcp FROM community; LOAD duckdb_mcp;

PRAGMA mcp_publish_tool(
  'semantic_query',
  'Query the semantic model by metric and dimension names. Call list_metrics first.',
  'SELECT * FROM ossie_query($metrics, $dimensions, $filters)',
  '{"metrics":{"type":"string"},"dimensions":{"type":"string"},"filters":{"type":"string"}}',
  '["metrics"]'
);
PRAGMA mcp_publish_query('vocabulary', 'SELECT * FROM ossie_metrics()');
PRAGMA mcp_server_start('stdio');
```

Verify these signatures against the current `duckdb_mcp` docs before relying on them;
the extension is young and the pragma arguments may have shifted.

---

## 6. Phases

Each phase ends with something demoable. Do not start the next phase until the exit
criteria pass in CI.

### Phase 0 — Parse, validate, introspect

Build the extension skeleton from the DuckDB community extension template, pinned to a
specific DuckDB version. JSON models only.

- `ossie_load` with `rebind`
- Full model struct; every field and metric expression parsed via
  `Parser::ParseExpressionList` **at load time** and retained as a tree
- Validation: `source:` resolvable, relationship endpoints exist, referenced columns
  exist in their dataset, expressions parse
- Model stored in `ClientContextState`
- Four introspection table functions

**Exit:** `SELECT * FROM ossie_metrics()` on the TPC-DS model returns 5 rows with
synonyms populated. A model with a malformed expression fails `ossie_load` with a
message naming the offending metric. CI builds and tests on Linux and macOS.

*Note: DuckDB vendors yyjson but no YAML parser. JSON-only is not a shortcut — dbt's
Ossie implementation also accepts only `.json`.*

### Phase 3a — Star-join compiler + MCP

Real per-request planning, easy case only.

- Stages 1, 2, 4, 5 of the compiler
- Single fact table, star topology
- Cardinality flags computed at load time; **refuse** any query where a single aggregate's
  arguments span datasets, or where more than one fact table is required
- A metric *may* reference a second dataset as a scalar — `item.i_category` inside a `CASE`,
  say — provided that dataset is reached only across many-to-one edges. That is an ordinary
  join with no fan-out, and it needs none of phase 3c's machinery. Do not lump it in with
  genuinely multi-grain metrics and defer it
- `ossie_compile` and `ossie_query`
- MCP publication wired up at the end of this phase

**Exit:** `total_sales` by `item.i_brand` filtered on `date_dim.d_year` returns correct
rows against loaded TPC-DS data. `store_productivity` errors with a message naming the
metric and the fanned-out dataset. Claude Desktop, pointed at
`duckdb -init server.sql`, answers "top brands by revenue in 2001" correctly.

*The MCP step is a day of work and belongs here, not later. It is where you learn
whether JSON-string parameters are the right shape, whether the agent reliably picks
names from synonyms, and what the error messages need to say. Discovering the tool
surface is wrong after phase 3c is expensive.*

### Phase 3b — Join correctness

Each of these is a separate decision with a separate failure mode:

- **Ambiguous paths.** More than one route between two datasets. Refuse with both paths
  named. Add an optional explicit `path` argument only if a real model demands it.
- **Snowflaked models.** Connecting two requested datasets pulls in a third nobody
  asked for. Legal, but it changes row counts — make sure the grain check sees it.
- **Join type.** Ossie declares the edge but not `INNER` vs `LEFT`. If the FK column is
  nullable, `INNER` silently drops fact rows. Pick a default (`LEFT` from fact to
  dimension is the safer choice), document it, allow override.
- **Role-playing dimensions.** `date_dim` joined once as sold-date and once as
  returned-date. Requires per-edge aliasing and qualified dimension references that
  disambiguate which role is meant.

**Exit:** golden-file SQL for a dozen real TPC-DS queries expressed semantically;
differential tests confirm result sets match the hand-written originals row for row.

### Phase 3c — Multi-grain and multi-fact

Turn 3b's refusals into answers.

- Determine each *aggregate's* grain from the datasets its own arguments reference. Grain is
  a property of each aggregate node, not of the metric as a whole: a metric may reference
  three datasets and still contain a single aggregate at a single grain
- When grains differ, emit one CTE per grain aggregating at that grain, then join the
  CTEs on shared dimensions
- Multiple fact tables fall out of the same machinery — they are metrics that cannot
  share a grain

Do **not** implement symmetric aggregates (the `SUM(DISTINCT hash(pk) * 2^n + value)`
trick). It is clever and one-pass, but brittle with floats and hash collisions. The CTE
approach is correct and debuggable.

**Exit:** `store_productivity` returns a correct number when grouped by a dimension every
grain can reach, such as `store.s_state`. Grouping it by `item.i_brand` stays refused, and
correctly so: the per-store CTE reaches `item` only back through `store_sales`, so there is
no shared dimension to join the CTEs on. Multi-grain support means requests with a shared
groupable dimension work — not that every multi-grain request does. Record that residual
refusal in `docs/limitations.md`. A query mixing `store_sales` and `web_sales` metrics
grouped by a shared dimension works.

### Phase 5 — Hardening and release

- YAML support (vendor rapidyaml)
- Swappable dialect emitter so `ossie_compile` can target Snowflake / BigQuery /
  Postgres. Keep the AST dialect-neutral and the printer pluggable so this stays an
  addition rather than a rewrite.
- Ossie version handling. The example model is `version: "0.2.0.dev0"` while dbt
  accepts only 0.1.0/0.1.1. Pin a supported range, error clearly outside it, expect
  churn.
- Submission to the DuckDB community extensions repo

---

## 7. Testing

**Golden-file SQL.** `ossie_compile` output for a fixed set of semantic queries,
checked into the repo. No database needed. This is the fastest signal and should run on
every commit.

**Differential.** Run the generated SQL and the hand-written TPC-DS equivalent against
the same data; diff result sets. This is the only test that catches grain bugs. Build
the harness in phase 3a even though it only has a few cases then.

**Negative.** Every refusal path needs a test asserting both that it errors *and* what
the message says. The error messages are part of the interface — an agent reads them
and retries.

**Fixtures.** Generate TPC-DS at SF=1 via DuckDB's `tpcds` extension. Keep a second,
deliberately nasty model in the fixtures: multiple fact tables, an ambiguous join path,
a role-playing dimension, a nullable FK. Most phases are defined by which of its
queries they can answer.

---

## 8. Known pitfalls

**DuckDB internal APIs are not stable.** `ParsedExpression`, `SelectNode`, `JoinRef`,
and `bind_replace` signatures shift between releases. Pin the version in the template
and expect churn on upgrade. This is the cost every community extension pays; the
alternative (bringing your own AST layer) means reimplementing expression printing and
precedence.

**`unique_keys` vs `primary_key`.** Checking only one will misclassify edge cardinality
and silently disable the fan-out guard. Check both.

**Computed fields must be inlined, not referenced.** `customer_full_name` is
`c_first_name || ' ' || c_last_name` — emitting `customer.customer_full_name` produces a
column-not-found error against the physical table.

**Dialect selection.** Fields carry `dialects: [{dialect: ANSI_SQL, expression: ...}]`.
Prefer a `DUCKDB` dialect if present, fall back to ANSI, and validate the fragment
parses before using it.

**`ToString()` emits DuckDB dialect.** If `ossie_compile` is meant to target other
engines, that needs a separate emitter (phase 5), not a post-processing regex.

**Data locality.** The extension executes SQL, so DuckDB must be able to reach the
tables. Natural for Parquet, Iceberg, local files, or attached Postgres. If the data
lives in Snowflake, `ossie_compile` is the useful surface and something else executes.

---

## 9. Suggested repo layout

```
duckdb_ossie/
  src/
    ossie_extension.cpp          registration
    model/
      parser.cpp                 JSON → Model
      model.hpp                  Model, Dataset, Field, Metric, Relationship
      validate.cpp               load-time checks
      graph.cpp                  relationship graph, cardinality flags
    compile/
      resolve.cpp                stage 1
      join_planner.cpp           stage 2
      grain.cpp                  stage 3
      rewrite.cpp                stage 4 — ParsedExpression rewriting
      emit.cpp                   stage 5 — SelectNode assembly
      dialect/                   phase 5
    functions/
      load.cpp
      introspect.cpp
      query.cpp                  ossie_query (bind_replace)
      compile.cpp                ossie_compile
  test/
    sql/                         sqllogictest
    golden/                      expected SQL
    fixtures/
      tpcds_semantic_model.json
      nasty_model.json
  docs/
    limitations.md               what refuses to answer, and why
```

`docs/limitations.md` is not filler. Phases 3a and 3b ship with real refusals, and
documenting them precisely is what makes the extension trustworthy rather than
half-finished — it is also the most interesting thing to bring to an Ossie spec
discussion, since it identifies exactly where the format underdetermines execution.

---

## 10. Scope guidance

Phases 0 and 3a are the credible artifact — enough to link in a PR or a working-group
discussion. Phase 3b is where it becomes broadly useful. Phase 3c is where it becomes
complete.

If time runs short, a narrow extension that refuses clearly beats a general one that
guesses. Resist letting 3b's cases leak into 3a; the ambiguity work is open-ended and
will absorb whatever time it is given.

### Bun + TypeScript checkpoint（2026-10-09）

用户新增工具链范围，详见现有 local issue `docs/ossie-execution-research.md` 的迁移节。迁移项目自维护的 Python gates/probes/artifact verifiers 到 TypeScript；固定 Bun、依赖与 lockfile，保持 C++/SQL、独立数值及取消/生命周期证明。引擎 C++/CMake/Make、固定上游 formatter 和历史来源保留。本包本地完成：四脚本及控制已迁移、Make/CI已接线；25个Bun控制、19项C++、503条SQL、两个真实产物验证器、MCP及40项stock CLI检查通过。远程CI/Ubuntu tidy未运行，不能称跨平台验收。功能 R3/R6 研究继续等待质量工作完成，不扩产品范围。

### 推送前清理 checkpoint（2026-10-09）

用户要求清理后交付当前分支，范围与验证沿用 local issue 的清理节。必要剩余：shell 重复模型/Python、静态fallback、过期XFAIL及无用 instructions 成员/旧链接/cache清理；新增失败控制并复核 C++/SQL、同产物独立 verifier、CLI/MCP。原研究来源、旧格式拒绝及活跃core接口保留；不扩main合并/远程规则授权。

清理checkpoint结果：实际删除shell重复Python、静态fallback、非门禁XFAIL、CI旧复制包装及旧链接/cache。instructions经真实编译证明仍用于metadata，已完整保留，C++最终无diff。30个Bun控制、19项C++、503条SQL、同产物loader/population、40项stock CLI及MCP通过，新证据见docs/research/ossie-req-001/cleanup-results.json。下一动作：提交推送当前fork分支；不扩main合并/远程规则，产品R3/R6仍待研究。

### R3 checkpoint（2026-10-09，继续功能开发）

清理08c06e6已推送当前fork分支；用户确认继续开发。当前切片为NULLIF及显式NULLIF分母保护的除法，语义/证据/限制维护于现local issue。下一动作：固定host边界/pre-fix证据、Advisor限定取舍、strict AST实现与独立比值/NULL/空集/拒绝控制；验证后本地交付。R6安全连接、日期时间更广边界、calendar及完整验收仍必要但本轮不扩；新功能不自动推送。


### R3本地交付 checkpoint（2026-10-09）

当前结果：本轮NULLIF/直接数值零保护除法切片完成，并修正根函数包裹聚合的存在性遍历。8行手算控制/27拒绝、30 Bun、19 C++、16 SQL用例566断言，原loader/population与真实stock CLI/MCP同产物通过。证据和失败保留于现local issue与r3-control-results.json；/产生DOUBLE且overflow仍可Infinity，不称精确Decimal除法。

交付边界：此前清理08c06e6已推送；本轮只本地提交，远程CI/Ubuntu tidy未验证。上面历史checkpoint的计数/待推送状态按各包当时记录保留，本段为当前状态。下一已授权研究沿原需求推进R3日期时间/其余表达式边界及R6安全连接；calendar、更广ontology/身份、population MCP和完整P0仍未完成。不在此切片扩大实现或交付权限。

### R3 DATE checkpoint（2026-10-09，恢复）

f1e523b本地交付完成；下一已授权动作是DATE边界独立控制与类型转换研究。复用现local issue，先固定1.5.6实测月界/闰日/NULL/半开区间/CAST与TRY_CAST/非有限日期及函数拒绝，不将loader时间metadata扩大为timestamp执行。发现语义缺口后再确定修复范围；后续R3其他表达式、R6安全连接/calendar/更广ontology/MCP仍必要，远程CI/Ubuntu tidy与新push未做。


### R3 DATE本地交付 checkpoint（2026-10-09）

结果：现有DATE执行契约已补齐11行手算、两时区、21具名拒绝及失败快照保持；修正artifact helper非有限DATE表示。31 Bun、19 C++、17 SQL用例610断言与四个同产物verifier通过。C++未改，使用f1e523b产物；历史报告保留，现local issue记录CAST宽松前缀/interval先AST拒绝及harness失败。

本包本地完成，下一动作本地提交，不推送。下一已授权研究R6安全事实/维度连接的前提/正负控制；剩余R3时间类型/函数与一般表达式、calendar、多期身份/ontology、population MCP完整验收继续在现计划/issue维护。远程CI/Ubuntu tidy未验证，全P0未完成。

### R6研究 checkpoint（2026-10-09，恢复）

DATE包fccce8d本地完成。下一已授权动作：人工事实/唯一维度连接oracle及实际core正负控制；核实整批键/匹配/类型与单事务双表观察，确定共享冻结连接人口的最小集成方式。先保持现单表v1拒绝多表，不把研究SQL预检或core live查询当生产安全连接。结果/证据/必要剩余沿现local issue维护；后续R3/calendar/多期身份/ontology/MCP与远端验证仍待完成。

### R6研究本地交付 checkpoint（2026-10-09）

结果：6fact/4dimension独立oracle与core真实产物正例、11数据反例、4规划拒绝和双连接同事务实验通过。坏声明键确实重复/丢弃事实，维度COUNT4不能在事实展平上变成6。v1多表拒绝保留；生产双表人口未实现。31 Bun/100断言、19 C++、18 SQL用例661断言通过，新证据与集成约束在现issue及docs/safe-join-research.md，失败也保留。

本包本地研究完成，下一动作本地提交，不推送。下一已授权实施从共享原模型指定fact grain校验和独立冻结双表profile开始；实际键/完整匹配检查、来源AST重写与同冻结数据对象/指标、失败保持/取消/重载/lifetime都仍须落地证明。其余R3/calendar/多期身份/ontology/MCP和远端CI/tidy仍未完成。

### R6 grain validator checkpoint（2026-10-09，恢复）

研究0f05afb已交付；当前指定fact grain前置检查包先实施。共用原模型Compile grain分析，每指标指定grain控制，默认core不改，独立C++runner接Make/CI与真实artifact回归。结果/失败/剩余维护现local issue；完整双表冻结profile是下一必要阶段，本轮不先宣称连接人口可执行，不推送。


### R6指定grain本地交付 checkpoint（2026-10-09）

结果：内部RequireMetricGrain与Compile共享原模型识别分析，42独立grain控制+19 state、661 SQL/18用例、31 Bun/103断言与五个重建同产物verifier、stock CLI/MCP通过。已识别无来源聚合在新API保守拒绝，旧core行为不改；隐藏/嵌套与未识别聚合分层证据明确，不能把此API称完整安全证明。失败和哈希归现local issue/r6-grain-control-results.json。

本包本地完成，下一动作本地提交，不推送。下一已授权实施是独立双表冻结profile及严格AST+指定grain+数据预检/来源重写/对象指标共同人口；完整R6/P0和前述R3/calendar/身份/ontology/MCP仍未完成，远程CI/Ubuntu tidy未验证。

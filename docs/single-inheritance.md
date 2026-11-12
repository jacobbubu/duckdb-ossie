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

The dedicated entry is `scripts/run_single_inheritance_native.ts`, with explicit
`prepare`, `build`, and `run` phases. The new family allows 16,000 SQL calls,
120 seconds work, 150 seconds outer supervision, 32 MiB each for raw operation
stream/report and zero retries. It precedes the complete eight prior stages;
the default quality commands and CI are unchanged. Build/run/cleanup/seal budgets
remain those of the existing controlled supervisor. Frozen source, dependencies,
tools, fresh paths and all 14 outputs are checked. New fixture answers use a
single-table Integer tree and a String-key join whose DATE comes from the
dimension while a conflicting fact DATE is preserved. Every level and branch
has independent complete member/value/group and hand-written SQL checks.

Implementation and offline validation are in progress; no native acceptance is
claimed yet. Old archives and source anchors remain immutable. New prepared
objects must retain their old tree, rules, plans and data across reload/reparent,
source removal, failed loads and cancellation. AMD64 and CLI/MCP/model acceptance
are outside this package; public delivery and CI closure remain separate.

### 构建复用来源门与共享校验

`scripts/native_build_reuse.ts` 提供显式的来源准入 API，导入模块不会启动编译器、数据库或验收进程。它区分旧构建身份与当前运行源码身份：原 prepared、构建回执及独审、失败运行和终结清单按原哈希核验；当前声明的完整源码和依赖清单、13 项工具字节及 SDK 选择信息另行绑定；这不代表整个系统或 SDK 目录的字节闭包。旧失败仍为失败，旧 Core 未运行仍为未运行，不重签旧回执。

来源差异只允许固定六路径：SQL 错误消息分隔符的精确修复、原生测试文件和本文的追加、共用监督脚本的阶段校验体提取，以及新来源门和它的测试。C++、CMake、依赖和工具不能以这些例外绕过核验。共享提取须有冻结合同明确绑定的 AST 对应证明；证明核对原阶段校验体、普通入口的唯一调用替换及其他函数不变。这是受审来源证明，不是接受任意外部自报结果。`inspectReuseSnapshot` 仅作纯数据策略检查，其返回值不是准入回执。

`admitNativeBuildReuse` 使用原绑定读取、截止时间、取消与结束确认、完整构建回执校验和封存实现。准入工作预算仍为 120 秒，清理 10 秒，封存 60 秒；共享阶段接口不增加原生运行预算。旧 14 项构建输出、归档成员与字节、终结清单、旧 helper 和保全文件都必须一致；别名、符号链接、未知清理或未结束任务均不能得到通过回执。新目录必须独立且不存在。候选文件只有在同目录最终 summary 通过且封存完整时才有准入意义。

`validateNativeStage` 与普通原生入口调用同一完整校验体，包括语义报告、捕获、31 项 SQL 枚举、SLT 结果和旧回归报告；该公开接口不接受测试 hooks。普通入口仍执行原来的严格 prepared 和构建来源检查，不会把复用视图当作普通 prepared。来源门本身不执行九阶段或 Core，也未提供运行命令桥接；将来执行仍须冻结独立合同、逐阶段核对绑定并独立审计。本包的合成元数据、临时普通文件和模拟流测试只证明离线校验行为，不代表真实产物准入或单继承 Goal 完成。

### 显式复用执行接线 v2（离线验证范围）

上一节保留 v1 来源门包的交付边界。v2 新增独立薄入口
`scripts/run_native_build_reuse.ts` 和只读审计入口
`scripts/verify_native_build_reuse.ts`，共用原九阶段执行生命周期及完整阶段校验。
普通入口的严格 prepared 校验保持；复用包装不接受 Prepared、Start、guard 或预算替代品。
旧回执和 v1 六路径策略不扩权。新 v2 合同必须显式绑定八个固定差异路径、完整审计输入、
新的生命周期 AST 对应证明及其引用的原 v1 证明。

新证据分开记录 `origin_prepared_sha256`、`origin_build_receipt_sha256`、
`current_contract_sha256` 和 `admission_summary_sha256`。封存准入不等于原生验收，
候选文件、未封存或清理未知的回执不能用于运行。首次检查目录不存在；运行后继续检查
来源和字节，不能因目录已存在而跳过绑定检查。全部九阶段及 21 个 guard 均保留，
273 个原生探针与准入、外层和独审探针分别记录。更改 v1 为 v2 的字段不是重签旧构建。

以下是接口形状，必须先提供另行冻结、审阅并获执行授权的真实文件与哈希；
本实现包没有调用这些真实命令。每次只执行所选操作，无自动串联、安装、构建或重试：

```sh
node scripts/run_native_build_reuse.ts admit CONTRACT_JSON CONTRACT_SHA256 NEW_ADMISSION_DIRECTORY
node scripts/run_native_build_reuse.ts run ADMISSION_SUMMARY ADMISSION_SHA256
node scripts/verify_native_build_reuse.ts native ADMISSION_SUMMARY ADMISSION_SHA256
node scripts/run_native_build_reuse.ts core ADMISSION_SUMMARY ADMISSION_SHA256 NATIVE_AUDIT_SUMMARY NATIVE_AUDIT_SHA256
node scripts/verify_native_build_reuse.ts core ADMISSION_SUMMARY ADMISSION_SHA256
```

运行目录由固定 runId 派生。原生和 Core 分别使用新的 `-recorder` 保存父层实际退出、双流与
组回收，审计使用新的 `-audit` 目录。Core 只有在本轮原生、外层和独审全部通过且 artifact
一致时才启动，并重新核对完整原生证据；它不接受旧 007 的失败结果。
旧 `core-run-contract-007.json` 从未生成，不能视为旧 Core 曾运行。

原生仍使用 1,500,000 ms 工作窗口、1,800,000 ms 外层窗口和原阶段上限；绑定工作
120,000 ms、清理 10,000 ms、封存 60,000 ms 均保持并受父截止约束。Core 仍为
30 行、24 语义调用、100 SQL、单 worker、零重试；worker 工作 60,000 ms、关闭等待
1,000 ms，外层 180,000 ms，清理和封存沿用原上限。worker 捕获和结果各 1 MiB，
外层双流合计 2 MiB；原生原流 64 MiB、归档 512 MiB 及各专项更小上限继续适用。
独审工作也使用已有 120,000 ms 绑定窗口，不重置其父截止。失败或未知终态不进入下一阶段。

独审保留独立完整成员/值/分组、NULL、错误对象、原始操作流重放、输入及生成文件绑定、
31 SQL 实际 SLT 和旧 20 阶段保护。Core 仍使用原固定输入、手写 SQL 和独立答案，
核对 12 计算、2 编译诊断、3 预期拒绝；旧独审迁移到 TypeScript，Python 原件留存。
合成元数据和假后端案例只证明离线接线与拒绝行为，不构成实际原生/Core 通过证据。
本节不宣称多层单继承 Goal 完成，亦不扩大 Ontology、平台、CLI/MCP 或模型支持范围。

独审中的旧同步读取在受控审计子进程执行，外层仍复用同一监督器。新增的
`-audit-recorder` 记录实际退出、合并输出、关闭和所属进程组；Core 同时要求独审结果与这份
父层封存一致。审计外层 120,000 ms 工作窗口覆盖子进程的加载、核验和结果封存，
子进程自己的 60,000 ms 封存上限不能延长父窗口；之后父层清理最多 10,000 ms、
父层记录封存最多 60,000 ms，合计截止不超过 190,000 ms。超时留下未知状态，
不能拿子进程提前写出的 passed 文件绕过父层失败。没有新增通用监督框架或任意进程信号权限。

## Build origin and the current acceptance environment

The explicit v3 build-reuse contract keeps the original `toolchain`, prepared
record, build receipt and all fourteen output hashes as the build origin. Its
separate `runtime.toolchain` describes the current acceptance environment. The
original receipt is always verified with the original prepared record and its
configure/build commands. Current SDK or compiler bytes never rewrite that
history. Existing v1/v2 contracts retain their strict original tool checks.

This bounded v3 path permits different bytes only for `clang`, `clang++`, `ld`
and `sdkSettings`, at the same declared alias and canonical paths. The developer
and SDK selection paths remain fixed. Node and the other executors retain their
original pins. Every current tool, including the four changed entries, remains
subject to all thirteen byte checks at every source guard. A change after
admission fails the next check. This does not add compilation or installation to
the reuse operation, or claim a different Node version is supported.

The current contract also pins the Git file selected by a controlled
`xcrun --find git` command. Its invocation, environment declaration, actual
exit, both output streams, cleanup and exact five-file archive are checked.
This is a tool-selection declaration; it does not prove the process image
ultimately dispatched by `/usr/bin/git`, all loaded libraries or all SDK bytes.
Version strings are metadata and do not replace byte checks.

Each native `binding-N` directory retains its exact thirteen original source
probes. A sibling `tool-selection-N` holds the additional selection evidence;
the independent audit requires exactly twenty-one such archives. Admission,
parent and audit rechecks use their own `tool-selection` child directories.
These operations use the existing supervisor and the same guard deadline,
cleanup and archive allowance. A selection command has at most ten seconds,
clipped to its parent's remaining work window. No deadline is extended, no
additional top-level result directory is introduced, and failed or unknown
cleanup cannot grant admission or permit Core to start.

The source contract and admission receipts distinguish v3 from v2, while the
existing process-envelope schemas keep their unchanged meanings. The contract
hash binds both origin and runtime declarations; old receipts cannot be
relabeled as new acceptance. Full native computation, independent answers,
NULL/group/member checks, rejection and replay protections remain required.
Offline tests of this path use synthetic files and receipts. They do not count
as a real tool-selection probe, native acceptance, Core acceptance or a new
build.

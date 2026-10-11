# duckdb_ossie — Implementation Plan

## Active P1 goal — 2026-10-11

用户已启动完整P1本地实现/验收goal，取代“暂不实现”。启动基线baeb866，
沿本计划与[OSSIE-REQ-001](docs/ossie-execution-research.md)推进，不新建backlog。
授权研究/设计/实现/修复/验证/本地提交；真实模型、推送、合并、发布/部署不授权。
上一goal前设计包为实际进展（baeb866）；本goal首轮重新核对源码、原需求第5节和冻结证据。

原第5节五项全部保留为必需，验收范围按以下有限用例研究并落实，不能静默删除或
用拒绝所有新形状替代支持路径；新增业务语义未确定的子项先研究、集中请求决策。

| 原需求/必需工作 | 有限实现目标 | 完成所需实际证据 | 当前状态 |
| --- | --- | --- | --- |
| P1.1 混合粒度、多事实、复杂人群 | 至少两个独立事实以真实不同声明粒度各自聚合后组合（如User/date与User/date/product，禁止无依据分摊产品），明确金额/人数/比率分母；交/并/排除型人群筛选 | 人工双事实fan-out反例、独立逐成员及精确金额oracle、组合前后粒度证明；缺键/NULL/重复与不支持组合拒绝 | native typed程序与双事实加载通过；专用人群/解释/发现/MCP和八场景无模型完整oracle通过；最终统一审计保留 |
| P1.2 跨日期、状态、历史关系、窗口 | 已设计每天合格集合；扩两个月同User数据、明确TRUE/FALSE/UNKNOWN状态变化、日期窗口和历史关系有效期 | 每日集合12研究请求实际产品复现；跨月/闰日/缺失/状态变化/关系边界和重叠控制；当前规则与历史状态分开 | 每日全接口/五场景模拟通过；稀疏观察行窗口native/SQL/MCP/无模型已实施；状态/历史口径待答，完整时间能力仍必需 |
| P1.3 非递归派生关系、约束 | 声明引用的有向无环派生关系及明确约束检查，不能以任意SQL或忽略requires冒充支持 | 固定规范承载核对、独立关系边集合、满足/违反/NULL约束、循环/坏引用/全批范围外失败控制 | 关系profile原生/完整links/解释/发现/MCP/六场景模拟已通过；最终统一兼容/交付审计保留 |
| P1.4 函数及窗口 | 上述实际用例所需的函数/窗口AST、明确窗口粒度/排序/边界/NULL | 正负AST控制与独立计算，公布固定SQL2026与项目实现兼容矩阵；不透传任意语法 | LAG/有界ROWS SUM原生与双冻结执行通过；旧profile拒绝保持；专用窗口SQL/解释/发现/MCP/无模型完整oracle通过，兼容矩阵/本机质量及Linux15TU tidy已通过；状态/历史新增后仍需最终质量 |
| P1.5 对象/解释/发现/MCP | 对应新增能力完整对象与同请求解释/版本/限制/错误声明、MCP工具和无模型Agent演练 | 同SHA产物native/SQL/stock/MCP及无模型重放；旧三个profile回归；解释阶段/执行标记与实际绑定一致 | 每日/关系完整接口与MCP/无模型通过；多事实/窗口专用SQL/MCP/无模型已实施；最终同产物验收仍必需 |

实施顺序：每日SQL/资源及成员证据 → 每日MCP/无模型验收；同时研究新时间/
派生关系契约 → 时间/关系/约束切片 → 多事实与组合/复杂人群 → 必需函数窗口与
全接口收口（依赖可调整，但五项不可减少）。每个切片更新本表及原issue，保留失败原件。
未来P1验收不是仅此首片通过；最终需逐项审计本表与原第5节、同产物回归/质量门禁、
文档/能力真实对应、本地提交及待推历史私有信息预检。远端CI不在未授权推送下运行。

每日SQL首片现已本地实施/验证，见[契约](docs/population-daily-users.md)及
[证据](docs/research/ossie-req-001/p1-daily-sql-controls/verification.json)：12请求成员/独立
关系除法一致，24检查/28拒绝含精确1MiB与+1、UTF-8/转义、366日、scope/规则回显。
原生238项只证明daily执行前/编码前取消及恢复，不扩中途阶段；25SQL/959断言、
98Bun/403expects、格式/类型/架构及同产物旧A1–A8/旧MCP330/106、观察MCP43/38
和四场景模拟/离线重放通过。当前API2.2.0，daily SQL及专用逻辑compile/plan已本地实现；
独立二工具每日MCP及支持式无模型演练已验证；原三个工具/每天拒绝保护保留。
两项新业务问题已集中提出：每日User观察三值状态、有限半开DATE单值历史关系。
回答未到之前不实施其依赖语义；其他工作继续。原所有五项必需范围不变。
每日完整解释、MCP/decoder、无模型验收与CI命令已完成，见下方各包证据。
当前关系profile及MCP/无模型已实施；多事实/窗口SQL和独立MCP已完成本地工作包，
无模型演练/重放/CI本地工作包已完成；函数兼容矩阵及macOS/Linux当前实现功能复验已通过。
当前Linux15TU tidy与有界历史预检通过；[时间候选研究](docs/research/ossie-req-001/p1-time-carrier-research/research.md)
已明确待答业务口径与预算，下一为决策后的固定schema核对及状态/历史实现。
新增后最终全P1验收和尾部历史预检仍必需，
此首片不完成P1。保留测试工具失败及修复；无真实模型、推送/合并/发布。

本轮恢复checkpoint（846a5cd）：前轮progress，当前干净；开始daily专用compile/plan，
明确资格SQL投影之外的日期/产品/跨日集合归约。共享bind与scope/trace，但不运行
执行器/fetch、不算成员；旧compile保留。来源表删除、规则/范围/trace、prepared旧
快照及计划预算是必需控制，原首片证据不覆盖。后续MCP/decoder/无模型/CI与全部
五类P1仍必需；两项业务决策等待答复，继续推进独立工作。

每日解释工作包本地完成：专用plan-v1与共享DailyBind，实际输入类型与完整逻辑
步骤，executed/members_computed=false；Prepared/旧规则/trace、来源删除、执行
超结果预算时compile仍成功均有控制。新[证据](docs/research/ossie-req-001/p1-daily-plan-controls/verification.json)
为30检查/34预期拒绝、12请求解释/执行与独立成员对照，native254项threads4/1、
25SQL/959断言、98Bun/403expects、质量门禁和同产物旧A1–A8/MCP及观察四场景
模拟/重放通过；真实模型0。规则注入试图触发1MiBplan失败在既有16KiB限制，
raw保留；改验最大合法规则完整解释，不降低规则上限或宣称精确plan上限实测。
下一已授权动作为单独每日MCP工具、严格decoder、实际tools/list/RPC及无模型
演练/CI；继续规范承载研究，两个业务决策待答，五项P1及tidy/全验收/历史审计
均保留。没有推送，远端旧CI不覆盖新本地代码。

MCP恢复checkpoint（83af415，干净）：独立每日二工具示例与专用decoder/真实
stdio控制；先查固定上游6f9c4ba的参数执行行为，明确SQL能力和服务器工具暴露。
旧观察服务/Agent拒绝不改，不默认新增业务语义；无模型演练/CI及其余P1保留。

每日MCP工作包完成：新daily_user_server.sql只暴露daily_users/daily_compile，
五资源包括实际服务surface声明；原观察服务/Agent文件不改。固定上游6f9c4ba源码与
实际RPC核对外层未知参数忽略、对象request转String；范围仅在request_json内由
native校验，不声称全JSON Schema强制。专用result/plan decoder保护完整性、数值
文本、日期、原始UTF-8身份、逻辑步骤和追溯，六组损坏控制通过。
[新证据](docs/research/ossie-req-001/p1-daily-mcp-controls/verification.json)为12请求真实
stock stdio、16检查/28拒绝及恢复；104Bun/454expects与格式/类型/架构通过。
CI加入daily SQL/plan和二工具MCP实际命令，尚未运行远端；C++及产物未改变，
复用83af415同SHA native/SQL/旧MCP证据。本包错误carrier测试失败原件保留。
下一授权步骤：每日支持式无模型Agent演练、私有原始证据/离线重放与CI；并行
推进不依赖待答业务决策的规范承载研究。五项P1完整范围仍未完成，无模型/推送。

无模型演练恢复checkpoint（982d803，干净）：独立daily run schema与已有private
hash容器，shared loop/Responses注入与顺序重放，手列oracle只用于结果审计。公开
选择receipt，不导出raw；compile-only/调用越权/失败封存控制，保留原观察四场景。
复用只读Advisor，不委派实现。完整P1业务决策/其他四类与最终验收继续保留。

每日支持式无模型工作包完成：[final receipt](docs/research/ossie-req-001/p1-daily-offline-controls/final-receipt.json)
与[证据](docs/research/ossie-req-001/p1-daily-offline-controls/verification.json)记录五个实际
MCP上下文、14模拟completion/9实际工具调用、严格顺序离线重放；18重新封存伪
记录与incomplete失败不能完整重放。compile-only为not_executed，不推断人数/
不冒充能力拒绝。完整成员独立手列oracle，六组unit控制保护错成员/相同空集
错scope/错rule/旧trace/无执行引用；原始证据ignored，公开仅选择性receipt。
110Bun/468expects、最终typecheck/格式/架构和每日MCP16/28回归通过；首轮
TS2339测试类型错误原件保留，未改oracle。CI新增五场景模拟命令尚未远端执行。
原C++/artifact/旧观察服务与协议不改，复用同SHA原生/SQL/旧MCP证据；真实模型0。
完整P1仍active，下一授权动作转向固定规范中非递归derived_by/requires/时间及
混合粒度承载研究，再进入依赖明确的实现；状态/历史关系两项业务决策待答，
不因待答停止独立研究。全部五类/窗口兼容矩阵/当前产物全验收/tidy/历史审计保留。

剩余承载研究checkpoint（bc330d5，干净）：核对固定关系规则/约束/窗口规范及
单Bundle限制；关系调用按typed角色/DAG处理，不用实体等号强行当标量。旧profile
不放宽，优先限定RelationProgram，并用双事实fan-out和observed-row窗口独立
计算约束后续设计。schema/普通SQL不是实现验收，两项业务决策等待；全部P1保留。

剩余能力承载研究完成：[候选契约](docs/research/ossie-req-001/p1-program-research/research.md)
和[实际报告](docs/research/ossie-req-001/p1-program-research/oracle.json)为八检查、三隔离
拒绝及旧snapshot保留。阈值10/20完整link18/7，全人口约束能看见资格过滤隐藏的
负值违反；显式NULLguard使谓词总有真假，不默认UNKNOWN业务策略。双事实分开
聚合A收入375、直接连接525，整数分oracle与集合交/并/排除一致；跨月LAG/两观察
行SUM与独立计算一致，仅普通SQL研究。固定schema/parser接受不表示插件支持，
四份探索失败日志和首版报告均保留。下一实现选择独立有限RelationProgram，typed
根/目标身份、DAG和完整人口约束后原子发布；旧profile不放宽。全部P1未完成，
状态/历史两项业务决策待答；没有模型调用/推送，新原生全回归待代码改变后执行。

## Current delivery checkpoint — 2026-10-11

当前完整P1 goal的最新本地提交为805df8c，当前实现五个profile（原单表、安全
双表、每日观察、声明关系、两事实）。每日集合、有限非递归关系/约束、两事实
指标/人群和LAG/有界ROWS窗口的SQL/compile/发现、MCP及无模型演练均已本地
验证。完整P1仍缺三值每日状态、历史有效期及其全接口和最后统一验收，不称完成。

[macOS功能/Linux tidy](docs/research/ossie-req-001/p1-tidy-controls/review.md)
与[精确ae0d0f4的Linux功能](docs/research/ossie-req-001/p1-linux-functional/review.md)
分别绑定产物、源码和失败/恢复原件。macOS与Ubuntu ARM64均通过六native、
27SQL/1026、138Bun/721、P0、11接口及四无模型；Linux15TU tidy通过。
当前P1未推提交没有远端CI结论。远端最后已验证的观察增量与下方历史阶段分开。
本goal只授权本地工作/提交，不授权模型、推送、main合并、部署或发布。

### 剩余工作与建议顺序

下一必需路径：答复[两项时间业务口径](docs/research/ossie-req-001/p1-time-carrier-research/research.md)
后核对固定schema、实现三值每日状态/相邻日变化及有限历史有效期；再完成
对应原生/SQL/对象/compile/发现/MCP/无模型独立对照、旧行为回归及质量检查，
最后按原第5节全部五条审计与本地交付/待推历史检查。没有删减原范围。
分页仍为用户要求先调研的独立未来项；一般递归推理、真实模型和远端交付
不混入本goal。等待决策时先完成已有实现的证据/当前文档一致性审计。

## Historical pre-P1 delivery checkpoint — 2026-10-11

以下保留goal启动前的当时交付与建议。这里的“当前”“未来”“尚未启动”
均属于原阶段，不覆盖上方完整P1状态；历史证据原件不改。

用户授权的分支推送与新tip验证已完成：origin/codex/ossie-execution为
收口文档及本轮剩余梳理的文档tip；最后完整核验的代码head是e3a6f5f，
[run38122315959](https://github.com/jacobbubu/duckdb-ossie/actions/runs/38122315959)
17项全部成功。首轮干净检出的归档测试初始化失败已最小修复；
[首次失败、隔离复现及最终产物证据](docs/research/ossie-req-001/ci-e9359d2-controls/final-state.json)
分别保留。收口文档936f7dd已按用户指示推送，本轮计划补记也沿同分支
文档授权推送；后续纯文档CI尚未完整核验，不继承旧运行结论。无main合并/发布授权。

三个有界profile的本地实现与限定验收已有证据，两个真实Agent工作包的
结论均获用户接受。最新观察/Agent增量已完成分支交付验证；更广能力
仍须独立研究和授权。范围仍固定Ossie `0.2.0.dev0` 快照与
DuckDB1.5.6，不宣称完整Ossie、一般推理或真实业务数据正确。

| 工作包 | 当前结论与证据 | 交付边界 |
| --- | --- | --- |
| 原单表与安全双表profile | 调整后的A1–A8通过，[P0验收](docs/p0-acceptance.md)保存来源/规则/NULL/粒度/连接/追溯/typed与拒绝证据 | 旧profile跨环境与旧远端CI通过，不扩多事实/任意join或全部表达式 |
| 每日观察profile | User稳定身份与User/date/product tuple、全批重复拒绝、观察/用户/非空金额分母；本地及新tip Linux AMD64/macOS ARM64各24 SQL/931断言通过 | 新tip跨平台构建与tidy通过；Linux ARM64等matrix关闭测试的目标只证明构建，不是一般多期语义 |
| 新观察MCP/发现/plan | 新tip两个产物各43检查/38拒绝，旧MCP各330检查/106拒绝；来源/同产物哈希核对通过 | 两个平台四场景模拟/重放/失败控制通过，真实模型请求0；模拟不是额外真实Agent验收 |
| Agent入口/私有档案/离线重放 | [提示协议及98 Bun/403 expects验证](docs/research/ossie-req-001/observation-prompt-controls/verification.json)通过，失败封存与篡改拒绝有证据 | 用量/字节/时间限制不等于金额或服务端存储保证；完整transcript保持私有 |
| 两次真实Agent限定验收 | [旧三场景用户裁定](docs/research/ossie-req-001/agent-live-corrected-controls/acceptance-decision.json)及[新四场景用户裁定](docs/research/ossie-req-001/observation-live-first-controls/acceptance-decision.json)均接受 | 新四场景8请求/5工具，仅一次合成数据集成实验；原pending/失败保留，不估计可靠率 |
| 远端分支与CI | e3a6f5f，run38122315959共17项成功，实际下载两平台报告/衍生哈希核对通过 | 覆盖新观察增量；保留上游部分目标跳过测试的边界，旧本地Ubuntu证据另记 |

[状态核对快照](docs/research/ossie-req-001/observation-live-first-controls/delivery-state.json)
以400b2cf为先前基线，当时尚未推送；现开发分支已交付至e3a6f5f，未合并/
发布；后续收口与本轮计划文档已推送。历史段落保留各阶段计数和未完成描述，
不替代本当前状态。所有问题继续沿[OSSIE-REQ-001](docs/ossie-execution-research.md)，
这里是现计划的交付收口，不创建第二backlog。

### Historical remaining-work recommendations

1. **已完成的交付准备**：观察MCP预检与四场景脚本模拟/实际MCP/
   重放/失败封存已接入两种产物任务。本地新增命令及98 Bun/403 expects
   通过；[待推历史扫描](docs/research/ossie-req-001/observation-ci-controls/history-audit.json)
   未发现已知私有配置，私有原件保留。生产代码未改，上游构建/native与
   format/tidy入口保留，最新跨平台/tidy结果现已远端通过。
2. **已完成的分支交付验证**：已推送并核对e3a6f5f的17项CI和两平台产物
   报告，首轮失败与最小修复保留；收口文档已推送，其新CI与代码CI分开核对。
   main合并/发布单独决定，不因本地验收或继续整理而自动进行。
3. **未来能力，先研究再定范围**：MCP大结果分页（用户要求先调研）、
   calendar补齐、窗口/as-of/期末状态、多事件/修订、每天用户交集、混合
   粒度/多事实/更广连接及一般ontology推理仍未实现。观察tuple和存在式
   User投影只完成多期研究的一个最小切片；这些不阻塞已接受的限定用例。
4. **已记录的接口改进**：compile最终输出绑定保证、ontology属性catalogue
   仍是增强项，现semantic plan/概念发现不能冒充它们已实现；后续独立
   定范围，不重新标记调整后的P0为失败。

下一包建议先研究第3项中的**每天合格User集合**：当前区间User指标是
至少一次合格，逐日人数不够证明用户交集。它直接承接已接受观察用例，
比同时引入窗口/修订/多事实更容易限定范围；分页保持先调研的独立需求。
研究先定义必需日期、缺失观察、产品范围与每日资格，再以独立小数据见证
确定接口支持/拒绝和最小切片。此为原建议，研究与后续设计现已完成、实现尚未启动；main审阅/
合并/发布与接口增强可分别决定，不因建议而自动执行。

此研究包已完成（2026-10-11，bd8c0ee基线）：
[日期/缺失/产品契约与最小验收](docs/research/ossie-req-001/multi-period-research/every-day/research.md)
建议显式非空自然日集合内每日存在范围内合格观察，可跨日换产品；缺失仅
是没有见证，不推断业务不合格。原八行不变，新23行合成fixture的12请求
手列/普通SQL/集合交集一致，三个替代量词及四个SQL反例核对；9项未来
拒绝用例只作设计，未新增validator/profile。生产路径及旧Agent拒绝不改，
无Ossie LOAD、MCP启动、模型调用或推送。下一步先审阅并接受候选业务
契约，再单独授权表达承载/发现/最小接口与资源门禁设计，暂不实现。

用户已接受该集合口径，独立[接受裁定](docs/research/ossie-req-001/multi-period-research/every-day/interface-design/acceptance-decision.json)
绑定55531fe研究与原验证哈希；原候选文件保留历史未批准描述。
[最小接口/能力设计](docs/research/ossie-req-001/multi-period-research/every-day/interface-design/design.md)
本地完成：独立SQL JSON入口/MCP工具/结果schema，唯一QualifiedObservation，
原user_count不变；API2.2.0与资源错误码只作未来实施方案，当前2.1.0仍不支持。
候选366日/64产品/256字节产品/16KiB请求/1MiB内部完整结果；无峰值内存、
外层MCP报文或硬deadline承诺。[设计核验](docs/research/ossie-req-001/multi-period-research/every-day/interface-design/verification.json)
记录结构正反例、独立门槛组合与旧10文件哈希不变；没有生产语义/资源执行证据。
本轮只设计，未改生产/注册/配置、未启动MCP/模型、未推送。设计工作包完成，
下一步先审阅接口/预算并另行授权最小SQL及资源守卫实施，验证后再接MCP/Agent；
分页/通用P1及main合并/发布仍是后续，不因本次口径接受而启动。

真实Agent暂不重复调用。本轮分支交付验证完成；剩余第3/4项能力与
main合并/发布仍须独立决定，本轮不扩大分页或模型实验。

## Historical error/discovery milestones — 2026-10-09

Current local milestone: population API 1.1.0 adds error schema v1 for recoverable
callback and JSON-list failures. Errors retain object/stage/reason, expose coarse
codes and nullable missing capabilities, and preserve pre-existing DuckDB exception
classification and transaction behavior. ExtraInfo prevents nested envelopes;
unknown native failures, cancellation/serious errors and external SQL/MCP boundaries
remain native. A new strict JSON-list helper replaces the example-only MCP macro.
Final local gates pass 788 SQL assertions in 22 cases without skips, 19 state/42
grain/158 capture controls, 33 Bun tests/112 expects, nine same-artifact stock
verifiers (42 new error checks), 301 actual MCP checks/106 refusals, core MCP and
40 FAITHFUL stock CLI checks. Native and stock missing-file behavior, stage retention,
escaped/marker inputs, explicit transactions and recovery are verified. A bare IO
escape from the original class catch was repaired using serialized error types;
cross-DSO RTTI remains an unconfirmed explanation. Sources and failures are retained
in OSSIE-REQ-001. This error package is locally complete, not full R7/P0/A1–A8.
Next local work should audit remaining requirement evidence before selecting the
numeric MCP transport/size or other acceptance package. Compile final output binding,
ontology attribute catalogue, broader time/calendar/multiperiod semantics and remote
CI/Ubuntu tidy remain visible. No push, merge or service publication is authorized.

The preceding milestone: frozen population bundles expose original metric and
field vocabulary, declared and actual load-preflight types, time/AI metadata,
snapshot/definition hashes, and project API 1.0.0/discovery-v1 capabilities.
The MCP example publishes four resources and retains three tools; names used by
the generic query template are discovered rather than hardcoded GAAP vocabulary.
GAAP remains the primary requirement fixture; non-GAAP fact/dimension, reversed
datasets, computed/quoted names, separate core state, prepared discovery/reload
and failed load controls pass. Successful individual load preflight does not
promise arbitrary combined-request binding or execution.
Final local gates pass 780 SQL assertions in 21 cases without skips, 19 state/42
grain/138 capture native checks, 31 Bun controls, eight same-artifact stock
verifiers, 195 actual population MCP checks (106 refusals), core MCP and 40
FAITHFUL stock CLI checks. Source-bound reports and retained harness failures
stay in OSSIE-REQ-001. That discovery package was locally complete; its next
uniform-error package is now complete as described above. R7/P0 remain incomplete.

The preceding milestone: compile exposes project-versioned plan_json from the
actual generated semantic SELECT AST, with immutable definition/dialect and input
capture provenance, applied formal rule, final WHERE, ordered projections/groups,
validated input grain/join policy, snapshot hashes and explicit stage limits.
It is not a physical/binding plan; captured rows are not filtered counts and no
time interval is inferred from arbitrary predicates. Compile supports trace
projections and now returns eleven columns; consumers must read columns by name.
Prepared and failed-load/file-change controls retain their old explanation.
Final local gates pass 741 SQL assertions in 20 cases without skips, 19 state/42
grain/138 capture native checks, 31 Bun controls, seven same-artifact stock
verifiers, 189 population MCP checks and CLI/core MCP. Retained failures and
source-bound evidence stay in OSSIE-REQ-001; remote CI/Ubuntu tidy are unverified.
That explanation package was locally complete; its next metric/field discovery
and interface-versioning package is now complete as described above. R7/P0 and
the wider goal remain incomplete.
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

### P0 证据核对 checkpoint（2026-10-09，当前）

基于本地提交 4a9c01d，现有 [OSSIE-REQ-001](docs/ossie-execution-research.md) 已逐条对齐 38 个 R 子项及 A1–A8，覆盖此前双表 profile、生命周期修复、真实 population MCP、机器 plan、词汇/API 版本与统一错误的后续交付。早期“未实现”记录是历史状态，不能代替当前矩阵。R1.3/A8 沿用户决定仅支持新格式，R5.6 沿已接受 YTD 分类、active/paid 未验证；未扩至自主 Agent 或完整语言符合性。

本次审计包仅核对来源/同产物报告哈希、引用及范围，修正 limitations 的过期双表描述，没有重跑通过的功能门禁。整体 P0 尚须收口：MCP 精确整数/Decimal 与有界大结果及空结果关联证据，R2 字符串数组 scalar 容错/逐字段偏离结论，最后串起一次规则驱动的完整验收。下一已授权本地研究优先 MCP transport，先真实观测/独立 oracle，再决定编码修复或支持范围；本轮不先实现。compile 最终输出绑定、ontology 属性 catalogue 是改进；calendar、多期身份、更广 ontology/窗口/混合粒度为后续范围。remote CI/Ubuntu tidy 未运行，仍无新 push/merge/发布授权。本包本地提交保留，证据矩阵与审计记录维护在现 issue，未创建第二 backlog。

### MCP transport 研究 checkpoint（2026-10-09，当前）

06b88c5 后研究包完成：实际原生/原始 MCP numeric token 保留 synthetic 大整数/DECIMAL/HUGEINT SUM，但普通 JSON Number 解码有 18 处差异。10,000 全字段对象与 10,000 分组 oracle 完整，最大已测 line 为 33,367,965 bytes；不是最大输出保证。固定启动空结果 compile/hash 关联、10,001 行和超 canonical byte limit 失败保持通过。证据和限定 Advisor 取舍归现 [issue](docs/ossie-execution-research.md) 与 r7-transport-* 报告，quality-local 33 Bun/112 expects 通过，生产代码/templates 未改，未重建/重跑无关门禁。

当前本地提交保留、不推送。下一已授权本地包：MCP adapter 稳定字符串数值/实际类型的版本化契约与兼容验证，先确定类型来源，原生 SQL 类型/业务公式不变；非有限 DOUBLE 仍需明确处理。R2 scalar 字符串容错/字段对应结论及最终规则驱动验收随后收口，整体 P0 未完成。更广 P1/非目标与 remote CI 继续可见，未扩发布权限。

### 后续 MCP 分页需求（2026-10-09）

用户要求只记录今后需求，先调研再设计/实施。问题、目标、研究方向及未决定事项归现 [OSSIE-REQ-001 的分页需求](docs/ossie-execution-research.md#后续需求mcp-大结果分页2026-10-09用户新增)。目前不启动研究或实现，不加入当前数值传输修复包/P0 必需完成条件；当前 10,000 行上限仍是输入批次限额，接口没有分页。将来先核对协议/实际实现及参考方案，再确定对象/分组分页、快照一致性、输出预算和独立完整性验收。

### MCP typed numeric 本地交付 checkpoint（2026-10-09，当前）

e2468ca 后数值修复包完成，细节/失败/兼容与报告归现 [issue](docs/ossie-execution-research.md)。新增可选 typed JSON SQL adapter，实际 bound types/values 编码；普通 JSON 客户端保留 integer/Decimal 字符串，Float 明确近似及非有限值，空结果带同一 bound batch 的 metadata。原生 SQL 类型/公式不变；MCP 成功 schema 有不兼容变化，按项目政策 API2.0.0，独立 result-v1；保持三个工具/四资源及 compile/discovery 形状。ASCII duplicate 拒绝/Unicode 不同列、prepared/reload/运行时错误及取消恢复已验证。

最终同 artifact e95dd360…：19 state/42 grain/158 capture、814 SQL/23 cases/0 skips、38 Bun/124 expects、10 stock verifiers、新 typed24、真实 MCP330/106 refusals、CLI40 PASS FAITHFUL/core resource 通过。10,000 对象/17 columns 与 10,000 groups 完整，新 object wire63,181,123 bytes；不是 output/峰值内存上界。CI已接 typed probe，远程未跑；分页仍只为后续需求。当前本地提交保留、不推送。下一已授权 R2 字符串数组 scalar 容错收紧及字段偏离结论，再归档完整规则驱动验收；整体 P0 尚未完成，原更广范围及 remote CI 未自动扩权。

### R2 字符串数组 checkpoint（2026-10-09，恢复）

45afa83 后本轮修复固定 schema 字符串数组成员类型，范围/验证见现 OSSIE-REQ-001 的 R2 checkpoint。先保留 pre-fix 接受证据，明确拒绝数字/Boolean/NULL/容器，合法字符串和失败加载旧快照保持；unique_keys 已严格、单值字符串容错不在本轮修改。重建同产物门禁及 stock/MCP 回归后本地提交。当前必要剩余为逐字段支持/偏离结论及最后规则驱动验收，不扩大分页或发布授权。

### R2 数组本地交付 checkpoint（2026-10-09）

StringArray 不再转换数字/Boolean，合法文本按长度保留。35 具名严格拒绝、19 静态 bad models 与每次完整旧 metadata 保持通过；hash-valid bad core 实际 LOAD_FAILED/load 保留 bundle。最终同产物 19/42/158 原生控制、817 SQL/23 cases/0 skips、38 Bun/124 expects、十个 stock verifiers、population MCP 330/106、core MCP 与 stock CLI 40/0/0 通过，证据/失败见 OSSIE-REQ-001 的 R2 数组结果及 r2-strings-control-results.json。

当前工作包完成，本地提交、不推送。必要剩余：R2 逐字段偏离结论（单值 scalar 容错未改，非 full schema）、最终规则驱动 A1–A8 验收归档；整体 P0 未完成。下一已授权动作是逐字段结论收口，分页仍未来调研，remote CI/Ubuntu tidy 未跑，既有 P1/非目标及发布边界保留。

### R2 逐字段核对 checkpoint（2026-10-09，恢复）

当前包完成固定 core schema 全字段到内部结构/发现接口的对应审阅，实际探测剩余单值转换、未知字段/扩展和字符串长度边界。结果与必要修复归现 OSSIE-REQ-001，接受行为不等于 schema 合法。仅审阅/characterization 与本地提交，复用已验证 artifact；最终 A1–A8 归档及新发现必要修复按证据收口，不添加第二 backlog 或发布权限。

### R2 逐字段审阅本地交付 checkpoint（2026-10-09）

50 个固定 schema properties 已逐行映射到内部/发现/执行边界，14 个真实 characterization 保存 13 接受/1 拒绝（不等于符合 schema）。发现合法 NUL 在 AI instructions/unique_keys 展开出口截短，raw AI 完整；单值 scalar、minLength、未知属性/扩展和方言容错也已单列，未自动接受为例外。生产 C++/artifact 未改，复用 c25d118 同产物，新增工具 quality-local/38 Bun/124 expects 通过，证据与失败归现 issue/对应表/r2-correspondence 报告。

审阅包本地完成，提交保留、不推送；整体 R2/P0 未完成。下一已授权必要修复为 length-aware string fidelity，再收紧或明确处理剩余校验偏离并归档 A1–A8。分页仅未来调研、P1/非目标、remote CI/Ubuntu tidy 和发布权限保持既有范围。

### R2 字符串保真 checkpoint（2026-10-09，恢复）

当前必要动作：修复 ScalarText、AI string 和 unique_keys 的非 length-aware 读取，增加 raw/flattened/list 与执行边界、失败保持控制，沿 OSSIE-REQ-001 记录证据。其他 scalar/schema 容错不在本轮修改；重建同产物门禁后本地提交。剩余为其他校验偏离的明确收口、最终规则驱动验收；分页/P1/远端验证与交付边界保持原范围。

### R2 保真本地交付 checkpoint（2026-10-09）

实际长度读取修复 scalar string/AI instructions/unique_keys，26 字段控制验证 raw/flattened/list 与五个 scope/说明/label/复合键。新增 model expression NUL 明确拒绝，防 DuckDB prefix parse 改公式；失败旧 metadata 保持，population NoNUL 边界保持。最终同产物 19/42/158 native、829 SQL/23 cases/0 skips、38 Bun/124 expects、11 stock verifiers、population MCP330/106、core MCP 与 CLI 40/0/0 通过，证据/中间 prefix parse 发现归现 issue/r2-fidelity 报告。CI接新增 verifier，remote CI/Ubuntu tidy 未跑。

本包完成，本地提交、不推送。整体 R2/P0 仍须单值 scalar/minLength/unknown properties/extension/dialect 校验偏离收口、最终 A1–A8 归档；下一已授权动作优先单值字符串类型校验。分页/P1/非目标及发布权限保持既有边界。

### R2 单值字符串 checkpoint（2026-10-09，恢复）

当前必要动作：仅接受实际 JSON/YAML string 的 RequiredString/OptionalString，optional 缺省保留空值，显式错误类型拒绝；保真修复保持。沿现 issue 覆盖实际调用 scope、失败旧 metadata/bundle 保持及正常合法字符串，重建同产物门禁后本地提交。minLength/unknown-property/extension/dialect 与最终规则驱动验收仍待收口，不扩分页/P1/发布权限。

### R2 单值字符串本地交付 checkpoint（2026-10-09）

仅接受 actual string 的 RequiredString/OptionalString 已落实；optional 缺省保留空值，显式 NULL/数字/Boolean/容器拒绝，合法长度保真/quoted text 保持。154 字段控制中120具名拒绝逐次完整旧 metadata 保持，hash-valid bad core 旧 bundle 保持与恢复；旧 YAML 裸1.5原样负例保留，正例quoted与JSON一致，首轮 SQL失败及纠正归现 issue。最终同产物19/42/158 native、834 SQL/23 cases/0 skips、38 Bun/124 expects、11 stock verifiers、MCP330/106、core MCP与CLI 40/0/0通过，证据 r2-scalars-* 绑定最终文件。

本包完成，本地提交、不推送。整体 R2/P0 仍需 minLength/unknown properties/extension/dialect 校验边界收口、最终A1–A8归档；下一已授权动作优先名称非空/未知core属性检查。分页/P1/非目标、remote CI/Ubuntu tidy 与发布权限维持原范围。

### R2 名称/未知属性 checkpoint（2026-10-09，恢复）

本轮实施 name/source/from/to 的 schema 非空约束及八种 core object 的已知属性检查，AI/vendor 自定义内容按原 schema 保留，extension 成员/dialect enum 不提前扩入。沿 OSSIE-REQ-001 记录 JSON/YAML、合法空描述与 AI 扩展、失败旧状态和同产物门禁；只本地提交。整体剩余 extension/dialect 偏离和最终规则驱动验收，分页/P1/远端发布边界不变。

### R2 名称/未知属性本地交付 checkpoint（2026-10-09）

八个 required nonempty 位置与八种 closed core objects 已按固定 schema 校验；unknown/case/NUL 属性键拒绝且旧 metadata 保持，五种 AI context 自定义内容/empty optional/whitespace name 合法保留。193 字段控制、50 错误控制、最终同产物19/42/158 native、837 SQL/23 cases/0 skips、38 Bun/124 expects、11 stock verifiers、MCP330/106、core MCP/CLI 40/0/0通过。一次 quoted C++ context 编辑构建失败及修复、全部 hashes 归现 issue/r2-fields-*；API2.0.0不变。

本包完成，本地提交、不推送。必要剩余 extension 成员结构/字段和 dialect enum/大小写，再最终 A1–A8 归档；整体R2/P0未完成。下一已授权优先这两项schema检查，不额外限制vendor自由字符串/AI追加属性或extension.data内容格式；分页/P1/远端验证及发布边界不变。

### R2 扩展/方言 checkpoint（2026-10-10，恢复）

本轮沿现 issue 实施 custom_extensions array/object/required strings/已知成员字段，vendor 自由字符串和 data 字符串内容不额外限制；全部 dialect variants 严格固定 enum/case，可执行范围保持两种方言。增加合法/拒绝/旧状态/bundle 回归，重建同产物门禁后本地提交。最终规则驱动验收尚须归档，分页/P1/remote CI/发布权限不变。

### R2 扩展/方言本地交付 checkpoint（2026-10-10）

五种custom_extensions严格成员校验、全部dialect精确enum/case已实现；任意vendor/data字符串、合法AI追加属性和方言执行子集保持。365字段控制35接受/330拒绝、54错误控制、population47拒绝/discovery18控制及旧非法扩展负例通过；中间fixture/转义匹配失败与修复归现issue。最终同产物19/42/158 native、840 SQL/23 cases/0 skips、38 Bun/124 expects、11 stock verifiers、MCP330/106、core MCP/CLI 40/0/0通过，证据r2-vocabulary-*。

50字段对应与已发现校验偏离结论完成，声明执行限制保留，不称全schema/语言符合性。当前包本地提交保留、不推送；整体P0仍需最后A1–A8规则驱动验收归档。下一已授权动作是该归档；分页仅未来调研、P1/非目标、remote CI/Ubuntu tidy未跑与发布权限维持原范围。

### 用户授权五轮自动执行（2026-10-10）

沿现计划完成最终验收，五轮依次为38项证据更新、A1–A4完整GAAP证据、A5–A6边界/安全连接、A7–A8追溯/typed/MCP/格式、最终结论与复现入口。第1轮复用同产物报告并检查hash，不声称功能重跑；每轮本地提交，不扩P1/分页/远程发布。

### 自动第1轮完成

当前38项矩阵已更新，20来源与12同产物报告绑定、50字段、8验收索引核对通过（文档审计，不是功能重跑）。本地提交后进入第2轮完整GAAP查询证据；第3/4轮边界追溯及第5轮最终验收尚待完成。

### 自动第2轮 checkpoint：A1–A4完整查询证据

沿现issue扩展既有独立bigint-cents verifier的结果记录，保存完整对象/逐日/三维分组/子组对象与实际compile plan、trace。校验10/20只有derived_by变化、core/mapping/请求模板一致；原CSV/正式资产不改。实际重跑该stock verifier，声明均值容差、无序和单位来源。边界/追溯最后结论仍在后续轮次，不扩大生产范围。

### 自动第2轮结果：A1–A4完整查询证据

既有独立stock verifier已实际重跑，保存阈值10/20完整166/63对象、15/15逐日、101/45三维分组和全部下钻对象、实际SQL/plan/trace；同core/mapping/请求模板且ontology仅derived_by变化，完整结果对照独立bigint cents及AVG容差通过。3月10日37/1697.80/45.88648648648649与21/1277.54/60.8352380952381可直接复核。新记录[完整报告](docs/research/ossie-req-001/p0-final-population-artifact-results.json)与[验收入口](docs/p0-acceptance.md)在research record中链接；plan内链接以docs为准。本轮只增强证据输出，不改生产C++/原始输入；format/typecheck通过。本地提交后进入第3轮A5–A6；第4轮追溯及第5轮整体结论仍必要。

### 自动第3轮 checkpoint：A5–A6边界与连接

沿现issue保存已有人工边界fixture的完整对象/每日/空集结果，实际重跑单表、表达式、DATE、core连接研究与生产双表控制，以及原生state/grain/capture和SQL。研究join证据与生产拒绝分开，schema/规则/源资产不改；明确全批预检、INNER/missing=reject及无日历补齐。后续追溯/MCP与最终验收仍必要。

### 自动第3轮结果：A5–A6边界与连接

单表完整报告增加11行边界fixture实际对象/每日与独立期望，空对象/分组=[]，scalar COUNT=0/SUM=AVG=NULL；阈值相等/NULL/半开日期/sparse groups按声明执行。单表/表达式/DATE/core连接研究/生产双表stock verifiers实际重跑通过，生产双表6facts/55.01/11.002、4派生对象和24类拒绝，研究fan-out控制独立保留。原生19state/42grain/158capture与840 SQL/23 cases/0 skips实际通过，无生产C++修改。第2轮报告因新增边界记录重跑更新为本轮完整输出，旧r2证据不覆盖。本轮本地提交后进入第4轮A7–A8；最终结论仍必要。

### 自动第4轮 checkpoint：A7–A8追溯/格式/传输

沿现issue重跑loader/字段对应/plan/discovery/error/typed结果与真实population MCP、core MCP、stock CLI。独立进程重复固定定义/同CSV，对比完整对象与全部组/下钻及SQL/定义/content hashes，不以snapshot ID充当业务相等证明。合法新格式与两种执行方言、其他合法描述性variant、非法版本/类型/AST拒绝保持；10,000规模实测不是分页或任意大小承诺。第5轮最终结论仍必要，无远端行动。

### 自动第4轮结果：A7–A8追溯/格式/传输

固定CSV/定义在独立进程复现完整两阈值对象、aggregate、每日/分组/下钻及SQL/content/ontology hashes一致，记录p0-round4-replay-results.json；并非普通SQL-text replay/通用determinism。loader、365字段、36plan、18discovery、54error、24typed、实际MCP330/106拒绝、core MCP资源与stock CLI 40/0/0 FAITHFUL实际重跑通过。10,000对象/分组完整传输与Float近似/输出内存限制保持，未实现分页或发布。新replay检查脚本format/typecheck通过。第4轮本地提交后仅余第5轮最终归档及复现入口；remote CI/Ubuntu tidy、P1/非目标均不扩大。

### 自动第5轮 checkpoint：最终验收与复现

沿现issue汇总A1–A8最终支持/拒绝证据、声明偏离与范围，提供Bun单命令复现及机器归档检查；复现写入调用方新目录，保留仓库历史结果。核对最终源码/输入/报告hash并执行quality-local。范围裁定使用既有只读Advisor一次反证，requested Astra/high，有效backend/effort/usage/billing未知；Lead自行核实。结论仅覆盖已调整要求的两个有界profile，remote CI/Agent/P1/分页不得以本地通过冒充。

### 自动第5轮完成：最终交付

调整范围A1–A8验收归档及实际单命令复现完成，source/fixture/report绑定和最终quality-local通过；前四轮支持/拒绝路径证据已共同收口。Bun runner的stderr类型/import错误和修复保留，Advisor限定反证与Lead实测分开。五轮均本地提交，无推送/合并/发布。当前已授权验收目标完成；P1/分页/remote CI/自主Agent等独立后续范围不自动启动。

### 跨环境复现/Linux质量 checkpoint（2026-10-10，用户授权）

用户选择先补跨环境复现与Linux质量验证。沿现OSSIE-REQ-001解决来源审计的Mac原始xlsx/docx绝对路径、补原需求raw核对；保持raw和原manifest字节不变，研究hash-bound相对副本locator与严格missing/tamper拒绝，不降低fidelity。复现入口与CI局部source hash核对需覆盖新增依赖；Ubuntu24.04/Linux ARM64 Docker实际构建、clang-tidy、TS/原生/SQL/stock验收按可用环境验证。与远端Linux AMD64 CI分开，不推送/修改远端规则/发布，不扩P1。Advisor一轮只读审阅来源契约，Lead负责实现和证据。

### Linux clang-tidy 发现与修复 checkpoint（2026-10-10）

Ubuntu18.1.3 clang-tidy已在population.cpp发现137处大括号、4处range-copy、2处乘法widening、2处BIGINT narrowing、using namespace和非const全局atomic诊断（当前147项，完整门禁尚未结束）。本包按原质量范围修复这些实际诊断：仅让clang对大括号生成fix，其他显式using/宽类型常量/局部static原子/const引用/受10000行预算约束的转换由Lead审核；不关闭检查或扩大业务能力。C++变更使旧二进制/报告不能代表最终源码，须重建Mac/Linux并重跑原生/SQL/完整stock/MCP及最终hash核对。初次tidy失败原日志保留，formatter安装主动终止与AMD64格式检查单独记录。

### Linux运行验证发现 checkpoint（2026-10-10）

修复后Mac原生/SQL/43 Bun和完整stock/MCP通过；Linux原生capture因standalone Git自动缩写8位source ID拒绝，stock双表verifier一次2ms interrupt未产生预期拒绝。前者完整DuckDB commit已核对，按上游CMake接口显式传入真实commit再重建，保留严格10位身份检查。后者调查Node API参数化执行先异步prepare的时序，拟明确prepare/bind后反复interrupt并要求timer实际运行、严格拒绝/旧bundle恢复，不隐藏重试/放宽失败；复用只读Advisor限定反证该控制，Lead实现和实测。确定性取消阶段证明仍来自native屏障，stock timer控制不作阶段确定性声明。失败日志/failure.json及当时产物hash保留，最终Linux原生/完整验收仍必要。

### 跨环境复现/Linux质量本地交付 checkpoint（2026-10-10）

两端同最终源码独立产物通过19/42/158 native、840 SQL/23 cases/0 skips、43 Bun/136 expects、11 stock verifiers、真实MCP330/106、独立进程replay、core资源与CLI40/0/0。LinuxARM64全量clang-tidy18.1.3通过，13独立TU/26compdb项含root注册；成功调用在上游runner中静默，inventory并非运行trace。固定11.0.1格式门禁在Mac及独立UbuntuAMD64通过，ARM64不称完整quality-local。20项跨环境完整GAAP结果对照通过；20+1原始来源哈希核对和严格locator负例通过，历史20文件审计不追溯补称原需求已查。修复实际tidy诊断与prepare/cancel测试竞态，保留各次失败，取消具体阶段仍由native屏障证明。证据与复现指向上方current checkpoint。本包完成、本地提交，不推送；remote AMD64/MacCI及Agent仍未跑，P1/分页不启动，下一交付核验只核对最终文档/报告hash和本地提交。

### 恢复与远端CI交付预检（2026-10-10）

用户在跨环境包0d79c73完成后要求继续。本地已授权验收/跨环境工作包完成，不自行启动P1或分页；比较剩余后，下一必要交付证据是远端Linux AMD64/macOS CI。只读预检确认push目标为jacobbubu/duckdb-ossie的codex/ossie-execution；远端tip为08c06e6668b7c033ea774ce2170526f22ea5db23，预检时本地0d79c73领先26提交。GitHub Actions enabled=true、allowed_actions=all，分支run list为空、workflow list为空；不把启用状态当成跑过。仓库Issues已关闭，使用现OSSIE-REQ-001记录，不声称读取远端issue。

本次没有重跑功能、推送、dispatch或修改远端设置；上一包源码/报告hash保留0d79c73时点，不重写历史控制。用户AGENTS.md规定“A pilot does not authorize push, main merge or deployment”及“Continue continues authorized work; proposals are not approval”；此前本轮只本地提交，因此推送这个具体fork/分支须明确授权。已完成reviewable工作并核实目标，下一待授权动作是普通fast-forward push（禁止force）触发现有CI，再检查实际jobs/失败并按本地范围修复；不合并main、不发布、不改远端规则。此预检说明本地提交后，push范围会再包含这一文档提交；实际推送前须重新核对remote tip与ahead/divergence，不能依赖本节旧快照。

### 推送交付与 Actions 阻塞诊断（2026-10-10）

用户明确授权推送后，27提交已普通fast-forward推送至jacobbubu/duckdb-ossie的codex/ossie-execution，tip ee78f5fcf7ec16c48d36f583f2fb548d78067ab8。随后用户要求判断Actions原因与处理方式，沿OSSIE-REQ-001核查。默认main与开发分支均有MainDistributionPipeline.yml和push/pull_request/workflow_dispatch入口；fork Actions enabled=true/allowed_actions=all，但workflow/run总数均0；手动指定开发分支dispatch返回workflow-not-found 404，未创建run。上游iqea-ai/duckdb-ossie同名工作流state=active。默认token权限read、不允许批准PR保持。

结论：当前阻塞发生在工作流注册/启用层，而非已有job构建失败。结合GitHub官方fork默认不运行工作流说明，最可能缺fork首次启用，但尚未在已登录Actions页面确认，不能把推断称确证。推荐先在fork Actions页核对并完成首次启用，再手动dispatch现开发分支并追踪实际CI gate；无需先合并main、重写YAML或放宽token权限。当前浏览器未登录，CLI无法看到可启用的workflow对象；未修改远端设置、合并或发布。远端Linux AMD64/macOS CI证据仍缺，本地验收包结论及历史hash不变。下一动作须取得首次启用这一具体远端操作授权并可访问对应页面；本次仅保存诊断记录，不额外提交推送。

### Actions 首次启用后远端验证 checkpoint（2026-10-10）

用户确认首次启用已完成。GitHub已注册主工作流380187158，state=active；此前fork首次启用解释获得前后状态证据。Lead已成功dispatch codex/ossie-execution，run 38015505660，验证tip ee78f5fcf7ec16c48d36f583f2fb548d78067ab8，事件workflow_dispatch。运行链接：https://github.com/jacobbubu/duckdb-ossie/actions/runs/38015505660 。初查queued/jobs=[]，不称CI通过；下一必要动作跟进实际job和CI gate，保存失败/最终结果。未更改权限、合并或发布；诊断文档仍本地未提交。

### OSSIE-REQ-001：远端 AMD64 tidy 字节窄化修复 checkpoint（2026-10-10）

实际run38015505660的tidy job114104763139失败；原始日志保存/tmp/ossie-ci-tidy-38015505660.log。population.cpp JSONString的两处unsigned char追加string触发bugprone-narrowing-conversions，重复TU输出仍只有两处源位置。沿已有issue修复char原始输出/unsigned字节分类，不降低tidy规则、不改变JSON约定。验证覆盖所有字节在signed/unsigned char编译选项下的输出对应及UTF-8/quote/backslash/control转义，再重建和原生/SQL及stock typed/error验证；必要剩余为新提交远端tidy和CI gate。前一LinuxARM64tidy通过不能代表AMD64字符类型环境。用户push/跟进CI授权沿该fork开发分支复用，不合并或发布。

### AMD64 tidy 字节窄化本地修复结果（2026-10-10）

JSONString以原始char追加字符串、显式unsigned char用于分类；不改JSON编码或降低规则。实际重建后Mac产物hash仍为13d5317fb860f9ad81437a0a86cb8c996d7742a026f927f7b503c0fadf340c95（优化后的产物与前包字节相同，不声称源码未变）。signed/unsigned-char各256字节（含控制/quote/backslash/高位字节）独立输出期望通过，-Werror/-Wconversion编译通过；原生19/42/158和SQL840/23通过；stock typed24含UTF-8/转义/真实MCP、error54通过；clang-format11.0.1和diff whitespace检查通过。首次辅助harness自身int转换/输出循环-Wconversion失败，显式转换修复后通过；typed首次因stock CLI未在PATH失败，使用原独立CLI后通过，未放宽检查。

原CI tidy失败日志及byte脚本/结果、typed/error同产物报告保存在docs/research/ossie-req-001/ci-amd64-tidy/。当前工作包仅本地验证完成，远端tidy/构建/完整验收仍必要；沿用户该fork分支push和CI跟进授权提交推送修复，新push触发现有工作流，不改设置、不合并或发布。历史证据及失败日志不覆盖。

### 修复推送后的远端验证 checkpoint（2026-10-10）

修复提交96ac9682bb2a5750f6b8a3e7b8f98fb151d25d0f已正常推送；push自动创建run38016307549：https://github.com/jacobbubu/duckdb-ossie/actions/runs/38016307549 ，初查queued。旧run38015505660的tidy已确定失败，其日志已归档；取消旧运行以避免继续构建已被替代的提交。当前源码本地检查通过，远端新tidy/构建及CI gate仍待结果，不称远端通过。本段状态记录留本地，避免为运行状态重复推送触发新一轮CI。

### OSSIE-REQ-001：远端 MSVC 原生测试构建环境 checkpoint（2026-10-10）

新run38016307549已完成：tidy/format/project通过，LinuxAMD64/ARM64及两种Mac、三种Wasm构建job成功；WindowsMSVC在Test extension阶段首次编译ossie_state_test，找不到标准库memory（C1083），MinGW因矩阵fail-fast取消；整个build failure使stock artifact/MCP被跳过，CI gate失败。上游v1.5.6 workflow的Build extension cmd步骤调用vcvars；后续Test extension bash没有该环境。现原生目标EXCLUDE_FROM_ALL导致后续才编译，故按MSVC条件纳入主构建，保留所有测试和严格门禁，不修改上游或远端权限。验证本地CMake/原生检查及下一轮实际MSVC编译运行；无本地Windows，不能声称本地MSVC通过。失败原日志/tmp/ossie-ci-38016307549-failed.log保留。

同时更正Linux覆盖说明：上游workflow对linux_arm64明确跳过inside/outside tests，因此该远端job成功目前只证明ARM64构建；本地UbuntuARM64原生/SQL完整验收已有独立证据，远端独立P0仍配置LinuxAMD64和MacARM64。剩余必要为Windows修复后的CI gate及这些实际产物验收，不扩大Linux版本/P1/分页。

### MSVC 原生目标构建阶段修复本地交付（2026-10-10）

CMake仅在MSVC时将三个既有原生目标纳入ALL，编译发生于上游已初始化vcvars的主构建；其他工具链仍显式构建，测试调用/门禁保持。本地实际重新configure/build及19state/42grain/158capture通过，固定format-check通过；没有本地Windows，MSVC分支与运行仍待远端。失败日志、上轮各job状态、本地CMake/format/native日志保存docs/research/ossie-req-001/ci-msvc-environment/。沿已授权fork开发分支提交推送触发下一轮CI；整体远端验收尚缺Windows通过后的artifact-load/MCP与CI gate。本包不改业务源码、不扩测试发行版、不合并或发布。

### 远端 CI 验收完成（2026-10-10）

实际run38019831758验证65b9828fe1d85dd2985113bfed8dc779f65149f9，17个job均success，CI gate成功；包含tidy/format/project、Linux两架构构建、Mac两架构、WindowsMSVC/MinGW、三种Wasm、LinuxAMD64/MacARM64独立产物验收和MCP round trip。ARM64 Linux上游跳过tests的限制保留，不把构建success说成该平台测试跑过。Windows修复已获实际远端编译和测试步骤成功证据。

下载LinuxAMD64/MacARM64产物与独立报告，重新运行现verifyAcceptance：两端A1–A8/来源与verifier绑定及各16份日志hash通过；产物SHA256分别e24605ad2a00a53b6372d483aa7b268891739c3367a14f2b87860e62565c4ca7、0a928dcfe8baa5a679bbc0817c180aa665fd265484c115b1041c039a780f0088。原始报告ZIP与GitHub artifact digest逐字节核对通过，原样保存在docs/research/ossie-req-001/ci-final/，并附run/artifact元数据与下载复核结果。初次复核因CLI不在PATH、进程内PATH更新不可用和本地MacCLI与runnerCLI哈希不同失败；最终分别使用官方1.5.6 LinuxAMD64/MacARM64 CLI启动独立Bun进程后严格通过，未放宽hash检查。

报告中remote CI未运行文字来自独立验收器旧模板，本次CI结论由run及全部job状态单独证明；原始报告不重写。该模板文字改进作为现issue后续维护项，不阻塞已经实际完成的远端验证；自主Agent、分页/P1等仍不在此包内。当前跨环境/remote CI工作包完成，不启动新能力、不合并main或发布。此后仅本地提交原始证据和状态文档，不为了归档再触发整轮CI；远端已验证tip继续为65b9828。

### OSSIE-REQ-001：验收报告模板收尾（2026-10-10，恢复）

用户要求继续。远端CI工作包已完成，比较剩余后本轮只处理已记录的模板维护及归档推送，不启动P1/分页/Agent。独立验收/来源审计不应硬编码remote CI未运行，改为说明其不提供CI/native质量结论，状态由独立运行证据给出；replay说明仅约束该检查的证明范围。原始历史报告/ZIP不改。验证现绑定远端两端产物与原报告重算验收（文档性复核，不重跑功能）、TS格式/类型/既有工具测试，提交并普通推送现fork开发分支；新tip的CI与65b9828已通过运行分开记录。不合并或发布。

### 验收模板维护交付（2026-10-10）

三个报告模板只改证明范围措辞，去除硬编码remote CI未运行；CI结果仍由独立run/job证据确认，验证逻辑/业务源码/历史报告ZIP不改。对65b9828远端LinuxAMD64与MacARM64同产物及既有报告实际重算验收，A1–A8行为/status/evidence hashes逐项一致；这是文档性复核，不是功能重跑。新模板源码hash/复核报告及43Bun/136expects、format/typecheck通过日志归docs/research/ossie-req-001/report-template-maintenance/。旧ZIP的原始文字保留，原通过CI链接仍有效。

本轮模板维护完成；将ce6fae5归档和本轮提交普通推送已授权fork分支，新push的CI另行核实，不能把65b9828通过等同新tip已通过。已调整的P0/跨环境/远端CI工作包完成，当前剩余交付仅本次push及新CI状态；P1、分页调研与自主Agent等后续范围保持未启动，不合并main或发布。

### OSSIE-REQ-001：产物上传超时检修（2026-10-10）

用户要求检修。最新b2d486a的run38027392941 attempt1失败在MacARM64 upload-artifact；该job已通过原生42grain/158capture和840SQL/23cases，唯一失败步骤为CreateArtifact接口五次request timeout。其余构建/format/tidy/project已成功，后续artifact/MCP被上游依赖跳过，CI gate严格失败。没有代码断言或编译失败证据，不用改代码或放宽门禁。原始Mac日志保存docs/research/ossie-req-001/ci-artifact-timeout/attempt-1-macos.log，已使用gh run rerun --failed重跑失败任务与依赖。记录retry-state；下一必要动作检查attempt2上传、后续验收及gate。之前65b9828全绿证据仍是该提交结论，不代称b2d486a已全绿；本轮状态/日志先本地保存，避免推送触发另一轮，不改设置、不合并或发布。

### 产物上传检修完成（2026-10-10）

run38027392941 attempt2已completed/success，验证远端b2d486a4bec4941531e8a8daf5aa82ac3bc7c2af；17个job全部success，MacARM64上传、两端Load built artifact、MCP round trip及CI gate均成功，独立验收和MCP证据artifacts存在未过期。状态、逐job结果及artifact元数据保存ci-artifact-timeout/final-state.json、attempt-2-result.json、attempt-2-artifacts.json；attempt1原始超时日志和排队状态仍保留。重跑成功支持此次为瞬时上传故障的判断，不推断整个GitHub服务故障；无需生产代码、workflow或规则修改。

本轮检修及最新tip远端验证完成，不自动启动P1/分页/Agent，不合并或发布。检修记录仅本地提交，避免纯状态归档再次触发整轮CI；远端已验证tip保持b2d486a。当前已授权工作包无剩余必需修复，后续能力工作需要确定范围。

### Agent discovery preflight — 2026-10-10

Research-only continuation verified both trusted startup bundles against the local
Mac artifact: initialization, three tools and four readable resources pass. Rule,
metric/unit and field metadata are available; candidate fixed prompts are archived.
Separate processes produced equal snapshot IDs but different ontology hashes;
verification must compare the full source/definition hash tuple, not snapshot ID
alone. See [the research evidence](docs/research/ossie-req-001/agent-discovery-preflight/summary.json).
No model or population tool was called and no Agent harness was implemented.
Provider/model/API/budget selection remains pending; real Agent acceptance is still
unverified. This preflight is complete, while the subsequent Agent experiment is not.

### Minimal Agent harness — 2026-10-10

The user's gateway/model/key-variable selection enables implementation of the
reviewed Bun/TypeScript experiment. The explicit loop and three-scene runner are
implemented without dependencies or production changes. All 50 tooling tests/167
assertions, typecheck, format and architecture checks pass; actual MCP preparation
passes in three isolated processes. Mock controls do not establish model behavior.
The initial injectable fetch type error and correction are retained. The next
necessary evidence is a bounded live provider run and human review of scope,
rule explanation and response citations. Pricing and monetary budget remain
unknown; no model request has been made. See [the run guide](docs/agent-minimal-acceptance.md).

The bounded implementation review found configuration, deadline and completion
status gaps; these are repaired with local controls. Final evidence is 54 tests/184
assertions plus successful three-process preparation. Provider/model mismatches
are refused before requests, credential-bearing URLs before logs, and a shared
120-second scene deadline includes discovery. Live execution additionally requires
an explicit token-only budget choice because proxy pricing is unknown. No live
request or Agent pass is claimed. The local implementation package is complete;
the actual three-scene experiment and human semantic verdict remain necessary.

### User-selected Responses endpoint — 2026-10-10

The user corrected the gateway API to `/v1/responses`. The chat adapter is removed;
actual requests now use Responses input/function-call outputs and per-scene server
continuation. Instructions/configuration repeat each round; fresh scenes have no
previous response ID. Public response projections exclude local private reasoning,
while store:true explicitly relies on provider-side persistence. Transport-byte
limits do not bound retained context or monetary cost. All 59 tests/238 assertions,
typecheck/format and three-scene actual MCP preparation pass. Migration failure,
read-only review and final source-bound evidence are retained in
[Responses controls](docs/research/ossie-req-001/agent-responses-controls/prepare/summary.json).
This protocol correction package is complete locally. No provider request was made;
live gateway compatibility and Agent behavior remain unverified. The pending budget
choice is unchanged; endpoint correction does not approve it.

### Public OpenAI configuration — 2026-10-10

The user requires generic open-source configuration: OPENAI_BASE_URL,
OPENAI_MODEL and OPENAI_API_KEY. Private service/model/key values live only in
ignored local .env_test; .env_test.example supplies public placeholders. Fixed
internal destination/model/key-variable bindings are removed. Current tracked
configuration references are redacted derivatives with immutable originals kept
locally outside Git; source business inputs are unchanged. All 59 tooling tests/239
assertions, typecheck/format and explicit local-env three-scene MCP preparation
pass. No model call occurred. The unpublished history cleanup is now complete;
see the following checkpoint. Current-tree cleanup alone was not history cleanup.

### Unpublished history cleanup — 2026-10-10

The six local milestones above remote b2d486a are retained; only the two historical
configuration-bearing trees and their descendants required new commit IDs. The
rewritten tip tree exactly matches the previous tip, including source bytes and
file modes. A private ignored bundle was actually restored and passed strict Git
object checks. All six complete snapshots, commit metadata and newly reachable
blobs pass internal configuration/key fingerprint scanning, including escaped
variants. Six existing private files retain their SHA256 values. No other local
ref contains a replaced old commit; old objects intentionally remain in the local
backup/reflogs. Historical executed-source hashes remain original provenance.
See [the verification record](docs/research/ossie-req-001/agent-history-cleanup-controls/verification.json).
The final documentation commit is scanned separately before delivery. No push or
live model request occurred. The next necessary Agent evidence still requires the
budget decision, bounded live run and human semantic review; P1/pagination remain
later work.

### First real Agent run authorized — 2026-10-10

The user explicitly accepted the existing token/call/time budget and unknown
monetary ceiling. Next authorized action is one three-scene live run using the
previously selected private configuration, preserving the first raw result in
ignored local evidence. Evaluate numerical gates and independently review scope,
rules and actual tool-response references. No automatic retry, push or scope
expansion is authorized.

### First live attempt blocked — 2026-10-10

Baseline MCP discovery completed; the first model request returned HTTP 401. No
model-selected tool call or scene completed. Original clean-head evidence is
retained privately with [public hash-bound findings](docs/research/ossie-req-001/agent-live-first-controls/summary.json).
Inherited OPENAI_API_KEY overrode the selected .env_test key. Clearing inherited
OpenAI variables loads all three selected values correctly, verified without a
network request. Provider authentication with the selected key remains untested.
No retry occurred; usage/billing are unknown. The next action requires a user
decision for one corrected-launch attempt at the same limits.

### Corrected launch authorized — 2026-10-10

After the failed attempt and explicit same-budget retry question, the user said
continue. One fresh experiment is authorized with inherited standard OpenAI
variables cleared and the selected private file loaded. Preserve the 401 run and
all new first results; no further automatic retry or push.

### Corrected live run completed; human acceptance pending — 2026-10-10

All three isolated scenes completed from clean 40390db with five model requests
and two model-selected population_query calls. Both exact numerical gates pass;
unsupported active/paid scope returns null numerical claims without substitution.
Offline replay and original request serialization/continuation/budget checks pass.
Lead checked date/product/concept scope, changed formal rule, units, record-count
semantics, actual executed columns and resource references: consistent. Human
verdict remains pending in immutable originals and public derivatives. Provider
usage is 34287 input/1288 output/35575 total tokens; billing is unknown. Private
configuration and both original experiments remain local; public records redact
provider/model with hash-bound provenance. Production implementation is unchanged.
Next necessity is user semantic acceptance of [the three answers and scope](docs/research/ossie-req-001/agent-live-corrected-controls/review.md),
not another model run. No push, P1 or pagination work occurred.

### Scoped Agent acceptance recorded; multi-period research completed — 2026-10-10

The user accepted the delegated Lead judgement and requested recording the
conclusion/data limits. [Later acceptance decision](docs/research/ossie-req-001/agent-live-corrected-controls/acceptance-decision.json)
is hash-bound to the original run and public review; raw pending review fields
remain unchanged, not retroactively relabelled. Independent original CSV Decimal
recalculation, archived generator value correspondence, source hashes and original
workbook USD cell pass. Both numeric cases and unsupported active/paid case are
accepted within the defined minimal scope. The synthetic fixture cannot validate
cumulative YTD consistency: 95 rows have gaap_val>ytd_gaap, generated independently.
This is a stated limitation, not an unconditional financial inconsistency finding.
Unique fixture IDs do not establish real cross-period user identity.

Compared remaining work and selected existing P1 repeated-entity/observation grain
for research only. [Research and independent SQL witness](docs/research/ossie-req-001/multi-period-research/research.md)
show 6 qualifying observations, 3 users and 5 non-null amounts; count/AVG/period
entity projections require distinct contracts. Fixed spec supports investigating
compound observation identity, while current production profiles reject this
shape. Bounded Advisor follow-up and Lead source/code/SQL verification completed.
Next necessary design decision is the minimal daily-observation contract before
any production implementation. No push, model rerun, paging or production change.

### Daily-observation contract and minimal design — 2026-10-10

User authorized contract clarification and implementation design, not production
edits. [The design](docs/research/ossie-req-001/multi-period-research/minimal-design.md)
fixes User identity by raw non-null/nonempty user_id and finite DATE/product daily
observation tuples; whole-batch duplicates, including identical rows, refuse
before filters. Qualifying observations project an existential user set; AVG uses
non-null observation amounts and distinct users have a separate count. Only
registered five metric AST forms are allowed, not arbitrary formulas under names.

Proposed new opt-in profile, three concepts and observation-only SQL execution
keep both existing profiles intact. Exact standard-document sketches, role-aware
metadata example and compatibility matrix are saved. New concepts-v2/plan-v2 are
profile-specific; additive API2.1 and result/metric/field-v1 retain compatible
shapes. User remains referent-only. MCP and new Agent runs are later scope.
Advisor findings on mapping depth, COUNT(*) restrictions, binary keys/finite dates
and versioning were verified by Lead. Independent SQL null/empty/duplicates and
identity-collision witnesses pass; no production shape was executed. Full fixed
JSON Schema validation of drafts is still required before loader acceptance; no
local validator was available and no dependencies installed.

Design package complete. Next authorized action is review of this concrete design;
local implementation requires a further user instruction. Old source/experiment
bytes preserved; no push, new model call or production change.

### Fixed schema gate and local observation implementation authorized — 2026-10-10

User explicitly authorized fixed schema checks followed by the minimal local
observation profile, retaining both existing profiles. Offline Ajv Draft2020
validation binds three schemas to the fixed spec commit and byte hashes. Original
drafts failed six required `verbalizes` checks; immutable failure report remains.
New threshold10/20 fixtures pass all three schemas. Three tooling controls pass
(offline construction/validation, malformed documents, tampered schema rejection),
and TypeScript checking passes. The malformed IRI control uses root prefixes:
concept iri is only a string in the pinned schema, so its format was not presumed.
Schema acceptance establishes document shape only. Next necessary work is separate
production profile binding, whole-batch typed identity validation, query/metadata
implementation and same-artifact old/new regression checks. No push or live call.

### Local daily observation profile verified — 2026-10-10

[Local SQL implementation and contract](docs/population-observation.md) completed
within the authorized slice. Separate profile binding validates exact mapping
correspondence, raw typed User/date/product tuples and all five aggregate shapes.
Whole-batch duplicate/empty/NULL/nonfinite identity refuses before publication;
repeated Users are projected distinctly without removing observations. User is
referent-only, date/product grouping is bounded, objects expose tuple columns.
Concepts-v2/plan-v2 separate observation and referenced User identity; API2.1 is
additive, old profiles preserve object columns/legacy discovery and plan-v1.
Fixed offline schemas use hash-verified bytes for registration and validation;
both threshold bundles have recurring hash/schema controls. Original drafts and
failed schema report remain immutable.

[Hash-bound local verification](docs/research/ossie-req-001/observation-schema-controls/verification.json):
21 observation checks/47 normalized refusals pass against stock Node DuckDB1.5.6;
independent CSV cents/tuple/User oracles confirm 6/3/5, USD31.00 and mean6.2.
Typed identity collisions, NULL/empty, threshold20, source mutation, prepared
queries, and failed load retention pass. Native controls pass 19 state/42 grain/
206 capture assertions, including new-profile concurrency and cancellation at
threads4/1. All24 SQL cases/931 assertions and63 Bun controls pass, with format,
typecheck and architecture checks. Same artifact hash13addd19a8d1ab8ac3139e94c38fcfcc36a14ad9e7370c26192c8da828521ac3
passes [archived intermediate legacy A1–A8 acceptance](docs/research/ossie-req-001/observation-schema-controls/intermediate-evidence.json)
including stock CLI/MCP and typed result regressions. Failures/corrections remain
recorded with local originals and public hashes.

This work package is complete locally; it does not complete all P1 requirements.
The existing CI matrix now includes the new observation verifier, but no remote
CI/tidy run is claimed. Next necessity is review of this local slice before
separately authorizing new-profile MCP/Agent integration or remote delivery.
Pagination, general ontology/entity navigation and user-level derived measures
remain outside the slice. No push, release or additional live model call occurred.


### Final observation identity closure — 2026-10-11

Final review reproduced a global default-collation defect after the earlier
scoped passes: uncollated source columns still inherited nocase in private
connections. New-profile Engines now pin binary in their own SESSION before
any binding/execution, including initial load after profile selection. Caller
and global defaults stay unchanged; explicit source collations still refuse.
Source capture SELECT has no comparisons and retains raw values. Bounded Advisor
clarification covered connection/binding scope; Lead verified SESSION isolation
(the pinned engine rejects SET LOCAL). Outer caller SQL retains caller semantics.
Concepts/plan metadata now include ordered identity relationship roles and nested
User.id referents, plus internal comparison semantics. Legacy v2 adaptation
retains original dataset/field provenance instead of generated flattened names.

[Final hash-bound verification](docs/research/ossie-req-001/observation-schema-controls/verification-final.json)
uses artifact b012058ed30639a899ad20efdff18a5cd637077408c4b65d570b40d87dbf8db3:
25 positive observation checks,48 normalized refusals; global nocase/noaccent,
true typed tuple duplicates, binary filter/group/count and prepared controls pass.
19 state/42 grain/210 capture native checks,24 SQL cases/931 assertions and63 Bun
controls plus format/typecheck/architecture pass. [Final legacy A1–A8 and MCP/CLI
regression](docs/research/ossie-req-001/observation-final-controls/acceptance.json)
passes on that same artifact. Intermediate legacy originals are preserved locally
with [byte hashes and archive locations](docs/research/ossie-req-001/observation-schema-controls/intermediate-evidence.json),
not overwritten or presented as final-source evidence.

Authorized local work package is complete. Remaining project needs are review,
then separately authorized new-profile MCP/Agent integration and eventual remote
CI/delivery; these are not required to claim this local SQL slice complete. No
push, release, new model request, paging or broader P1 capability is included.

## Observation MCP/Agent research closure — 2026-10-11

The authorized research package is complete in
[the integration and minimum acceptance design](docs/research/ossie-req-001/multi-period-research/mcp-agent-design.md).
Use an isolated observation example with concepts-v2 and the existing three tools;
keep the legacy example and acceptance behavior. Four isolated Agent scenes test
registered observation/User denominators, rule changes, nonadditive daily User
counts and the unsupported every-day User condition. Oracle values are not sent
to the model. The stronger proposed claim gate associates actual requests, rows,
columns and binding hashes, permits separate same-scope queries, and keeps
semantic review explicit. A bounded Advisor review was reconciled.

No new MCP/harness implementation or model request occurred in this package.
The next proposed implementation package is the independent example, real MCP
checks without a model, and a prepare-only observation driver with offline claim
controls. Live acceptance needs a separately authorized four-scene budget after
those gates pass. Remote CI/tidy, delivery, pagination and broader P1 remain
separate; this research completion does not complete those items.

## Observation MCP and no-model preparation closure — 2026-10-11

The authorized local package is complete. The independent observation server
uses concepts-v2 with the existing three tools/four resources. Stock stdio
checks pass 43 assertions and 38 refusals, including actual typed carriers
checked by the per-claim audit. The four prepare-only scenes have isolated
servers and model-visible contexts containing no oracle or scripted business
calls. The driver has no live mode and reads no provider configuration.
[Verification](docs/research/ossie-req-001/observation-mcp-controls/verification.json)
binds source/test/report hashes and the unchanged b012058e extension. Same-artifact
legacy MCP passes 330 checks/106 refusals; all 78 Bun tests and quality gates pass.
Two verification assumptions failed and were corrected without production changes;
failures and bounded Advisor reconciliation are preserved in OSSIE-REQ-001.

Literal date boundaries must match the question, not merely produce the same
fixture members. Unknown valid forms stay pending; determined metric/type/value
errors still fail. Complete objects may derive registered daily counts, and
separate same-scope queries remain valid evidence. Final semantic judgement is
explicitly pending even after mechanical gates pass.

Next proposed work is a live observation driver, offline replay and private
evidence handling. Running the four real scenes requires its own explicit
budget/authorization after reviewable setup. No model call, push, merge, release
or remote CI/tidy result is claimed by this package. Wider P1 and pagination
remain separate necessities.

## Observation live-entry/replay/archive implementation checkpoint — 2026-10-11

The user authorizes implementing the live entry, offline replay and private
evidence archival, explicitly without model calls. Reuse the existing provider
and no-model discovery/claim gates. Implement fresh ignored private records,
failures and integrity seals, exact request/tool/continuation replay, and
hash-linked sanitized public derivatives. Exercise simulated completions plus
actual MCP locally; never present them as real Agent acceptance. Existing raw
evidence remains immutable. A bounded privacy/evidence architecture review is
requested. Broader delivery/CI/P1 and pagination remain outside this package.

## Observation entry/replay/private archival closure — 2026-10-11

The authorized package is locally complete, with zero real model requests.
The separate live entry requires explicit run/token-budget choice and uses
standard OPENAI_* configuration. Fresh ignored private directories/files use
0700/0600, sync projected-event records and an integrity seal, including failures.
Ordered replay rebuilds discovery and Responses/tool/continuation correspondence
without fetch/MCP startup, rejects source drift, and keeps semantic judgement
pending. Public export is an explicit scanned, hash-linked derivative; neither
hashes nor replay authenticate external execution. Raw captures omit credentials
and private reasoning; any credential echo redaction is marked.

[Final verification](docs/research/ossie-req-001/observation-agent-entry-controls/verification.json)
binds the four simulated completions/real-MCP scenes and an incomplete-response
refusal/control archive. There are seven scripted provider completions and four
actual MCP tool calls in the complete rehearsal, not seven real model requests.
All 94 Bun tests/391 expects and quality gates pass; unchanged-artifact observation
MCP43/38 refusals and legacy MCP330/106 refusals pass. Original/intermediate
records remain hash-indexed privately. Production C++ and legacy interfaces
remain unchanged; no new native, remote CI/tidy, push or release is claimed.

Next: review the fixed four-scene prompts, token-only budget and evidence scope,
then obtain explicit direction for any real model run. The current no-model
instruction remains in force. General P1, pagination and delivery remain separate.

## Observation prompt/budget/evidence review closure — 2026-10-11

The authorized read-only review is complete against local 2485a7a. The four-scene
business coverage remains useful, but concepts citations, unit/null conventions,
precision and exact JSON requirements must be aligned with the prompt before
live acceptance; two question-scope clarifications are proposed. No prompt or
runtime code was changed. The current token-only budget remains a candidate,
not proof of completion capacity or a monetary ceiling. Local private capture
does not imply absent provider storage, and public export is a rewritten
derivative requiring content review. Sources and simulated byte/count observations
are linked in [the review](docs/research/ossie-req-001/multi-period-research/agent-run-review.md).
Zero real model requests and no new MCP process/test run occurred. Next proposed
package: minimal wording alignment and no-model regression, preserving old raw
archives; any real four-scene run still requires explicit user direction.

## Observation prompt alignment/no-model verification closure — 2026-10-11

The user continued the reviewed package. The observation system now states the
exact JSON/citation/unit/precision contract; two questions clarify existential
qualifying observations and every-day qualification with possibly different
products. Baseline/changed-rule still share the identical question. The gate,
budget, shared loop, legacy profiles and production code are unchanged.
[Verification](docs/research/ossie-req-001/observation-prompt-controls/verification.json)
binds fresh contexts and prompt/source hashes, four simulated scenes with actual
MCP, ordered replay, and a sealed incomplete refusal. Seven scripted completions
and four MCP calls in the complete rehearsal plus one incomplete completion are
not live model requests: the live count is zero. All 98 Bun tests/403 expects and
quality gates pass; four new protocol controls cover JSON/citation/unit/precision.
Old raw integrity remains valid; current-source replay refuses source drift.
Two receipt-checker assumptions failed and were corrected against actual error
text/summary structure, with failures retained separately from experiment results.
The local work package is complete; next is explicit user direction for any live
four-scene run. Token-only budget limitations and pending semantic review remain.
No new native/legacy-MCP/remote CI/tidy/push/release claim is made.

## Observation first real four-scene run closure — 2026-10-11

The user explicitly confirmed the fixed prompt/token-only budget. The run used
adc626b runtime sources and the same b012058e artifact, with only its authorization
checkpoint document dirty. Four fresh MCP/model contexts completed: 8 actual
model requests, 5 model-selected tool calls, no retry or expansion. Numeric and
execution-support gates pass for three scenes; the every-day scene probes daily
counts then declines the unregistered intersection target without substituting
a number. Lead checked formal thresholds, identities, literal date/product scope,
NULL denominators, unit/precision and actual response columns/citations; no
discrepancy found. Original pending semantic fields remain unchanged and final
user acceptance is pending, separately from completed execution/Lead review.
[Selected public evidence and limitations](docs/research/ossie-req-001/observation-live-first-controls/review.md)
bind source/prompt/runtime/raw seal/file hashes and reported usage. Offline replay
passes with zero new live requests and unchanged raw seal. Full transcript is
private; only a provider-redacted selected summary is recorded publicly. This
single guided synthetic-data run is not a reliability estimate or full compliance.
No further live requests, source changes, remote CI, push/merge/release occur.

关系绑定实施恢复checkpoint（fa72a6c，干净）：先实现独立C++ binder和原生语义
控制，为后续新profile提供typed角色/DAG/约束结果，旧profile不改。关系必须正向
锚定同一映射User目标，NULL谓词不自动成为link。编译不等于源类型绑定、捕获/
约束执行/原子发布；完整接口及所有P1仍必需，状态/历史业务问题待答。Lead负责
实现，Advisor只读反证；禁止模型/推送保持。

独立关系binder工作包完成：[契约](docs/research/ossie-req-001/p1-relation-bind-controls/contract.md)
及原生55检查记录typed目标、正向角色绑定、拓扑/多规则/菱形/循环及复制前预算；
总NULL约束保留两种模式、常量在声明类型范围内。仅编译逻辑，不代表新profile已
加载或数据约束已验证。新SHA扩展的旧native19/42/254、SQL25/959、Bun110/468、
A1–A8/MCP与观察/每日无模型回归通过，测试接入native CI入口但远端未执行。
下一实施为加载器接入、实际类型预检、完整根/link约束与原子发布，再做完整对象/
解释发现/MCP和独立link oracle；混合粒度/窗口/业务决策和全P1/tidy/历史审计仍必需。

关系profile加载恢复checkpoint（615974a）：将binder接入冻结类型预检及完整根/
link约束，原子发布前检查；先独立program发现，旧查询对新profile明确拒绝。
完整关系对象/解释/MCP在后续切片，不能缩减五类P1。旧profile不改，Lead实施。

关系profile加载工作包完成：[契约](docs/research/ossie-req-001/p1-relation-load-controls/contract.md)
及最终7检查/14拒绝为实际类型、全根/link约束、失败旧snapshot、prepared跨reload。
program发现明确未查询对象；旧接口对新profile拒绝。修复门禁/绑定两次Current
竞态，native280含确定双连接双向切换和关系发布前取消/恢复；26SQL/975断言、
旧native/Bun/质量/同最终SHA旧A1–A8/MCP/观察和每日模拟通过。CI命令增加但没有
推送或远端结果。下一必要实施为完整关系对象的源tuple/typed目标、解释与发现、
18/7全link独立oracle及MCP；原其他P1/状态历史决策/tidy/历史审计继续保留。

完整关系对象恢复checkpoint（22a8c94）：按同一冻结bundle建立四列完整link查询及
专用compile，保留目标User身份/类型和六hash追溯；独立成员oracle、重载/删除源/
预算/原生取消及旧回归验证。compile不算成员，实际集合不能只distinct User。

完整关系对象工作包验证：[契约](docs/research/ossie-req-001/p1-relation-object-controls/contract.md)
和实际12检查/5拒绝证明完整源tuple/目标、18/7成员、规则并集/重命名、重载隔离、
宽DATE、10000links及精确/转义JSON边界。native308含compile零执行与三点取消恢复；
26SQL/982、Bun110/468及同SHA旧A1–A8/观察/每日MCP/两组离线模拟通过。
失败原件保留；CI新增命令未推送或执行远端。下一必要步骤为新关系MCP严格decoder/
独立无模型场景，然后多事实混合粒度/窗口等所有原P1，状态历史业务决策继续等待。
本包不代表P1完成，授权仍不包含真实模型、推送或发布。

关系MCP恢复checkpoint（128c198，干净）：新独立server注册关系program发现资源和
两工具，不改旧三个server；严格四列link/完整预算/宽DATE/规则身份与六hash解码，
同实际MCP独立oracle与误参数拒绝验证。日期按DuckDB有限DATE，不复用每日十位
日期检查。旧profile回归保留，后续新关系Agent演练和所有P1工作继续，禁止模型/推送。

关系MCP本地工作包完成：[用户契约](docs/population-relations.md)与
[验收/局限](docs/research/ossie-req-001/p1-relation-mcp-controls/review.md)记录两工具/
两资源、实际14检查13拒绝、10000完整links及raw/最小最大有限DATE，六新增测试
及全部Bun116/522、类型/格式/架构和旧观察/每日MCP/每日无模型回归通过。Advisor
找到负无穷下界及同成员掩盖关系错配，已依固定源码修正并补反例。C++/扩展SHA
未变，沿上一包native/SQL/A1–A8证据，无本轮重跑声明。下一必要行动为新关系
Agent无模型协议/私有证据/重放，再继续多事实/粒度/人群/窗口及全部原P1；状态/
历史业务待答。新增CI未推送/远端验证，不包含模型/合并/发布。

关系Agent无模型恢复checkpoint（1995c5d，干净）：沿既有agentLoop/私有hash容器，
建立独立六场景关系协议、提示词/预算、完整link独立oracle、逐事件重放与重封伪造
拒绝；复用Responses字段解析但始终注入simulation，不读本地配置/不调用真实模型。
compile-only不算成员且不能称不支持，不能把links条数改成distinct User人数。然后
转向原多事实/不同粒度/窗口，实现缺口保留，所有原P1和业务待决事项不缩减。

关系Agent无模型工作包完成：[六场景receipt/证据界限](docs/research/ossie-req-001/p1-relation-agent-controls/review.md)
绑定17模拟completion/11实际MCP工具、完整link oracle、逐事件重放/20重封伪造及
incomplete拒绝。两个协调反例分别到达成员oracle/执行引用门禁，不把其他对应失败
称作语义证明。121Bun/544、质量/当前关系MCP/旧每日离线通过，扩展SHA未变沿旧
native/SQL/A1–A8证据。源码变化前首版原件保留，最终receipt单独绑定新私有档案。
下一原授权行动为多事实/不同粒度独立聚合、复杂人群与观察行窗口的typed程序/
捕获/实际查询，不以研究SQL替代；状态/历史待决及全P1/final tidy/历史审计继续。
没有模型请求、推送或远端CI。

多事实/窗口下一实施决策：[原研究的实施契约](docs/research/ossie-req-001/p1-program-research/implementation-contract.md)
冻结独立FactProgram/新profile路线，明确common按事实行存在性、先单事实归约、
金额同单位才组合、集合差TRUE成员语义、窗口先全捕获计算后输出日期筛选。Advisor
只读反证后采纳逐叶grain/AVG拒绝/child依赖/合计预算/同事务要求。还没有实现新
profile，下一已授权动作是native typed binder/阶段AST再实际捕获加载/查询；所有
原P1、日历/状态/历史待决与最终质量/接口验收保持，不把观察行窗口代替整体时间能力。

多事实native实施checkpoint（4742e96，干净）：按已冻结实施契约建立独立typed
FactProgram，先证明逐叶SUM/COUNT来源、金额单位组合、common行存在性、TRUE
集合差与窗口分区/排序/帧/筛选阶段。native生成AST接收两个明确source AST，
后续替换为两不可变捕获依赖；本轮binder执行对照不是已加载新profile。旧Compile
与既有profile不改，Lead实现/验证，原P1剩余能力继续，禁止模型/推送。

多事实native绑定工作包完成：[审查及receipt](docs/research/ossie-req-001/p1-fact-program-controls/review.md)
记录78实际DuckDB检查，375对525、NULL共同域/TRUE差集、窗口先算后筛选、0/366
帧、默认降序及复制前预算/缓存深度反例。新产物全部旧native/SQL/A1–A8/观察每日
及关系接口/无模型回归、121Bun/544及质量通过。失败原件与hash派生件保留。
这只是内部typed binder/阶段AST，尚无新profile；下一已授权动作是同事务双冻结
事实加载、完整键/类型/角色及合计预算、原子发布/旧prepared依赖，再完整API/MCP/
无模型。所有原P1/状态历史待答/最终跨平台tidy和历史审计继续，未推送或调用模型。

双事实冻结加载恢复checkpoint（d79e8b6，干净）：前轮native绑定与证据提交为progress。
沿OSSIE-REQ-001/实施契约接新multi-fact-user-program-v1，manifest program仅facts
(dataset/concept/currency)及windows(core指标名)，不重复公式/不推断币种。固定core/
ontology/mapping校验，根完整身份与同一User referent；显式新解析策略只允许此profile
已声明指标引用，旧解析/旧Compile保持。两独立child冻结同事务源、总canonical预算/
完整类型键预检，parent原子发布/hash及prepared保留双集合。先native-only AST seam
验证真实冻结来源，不加临时公开查询；后续完整发现/查询/MCP/无模型仍必需，整个
P1与业务状态历史待答/最终质量历史审计保持，禁止模型/推送。

双事实加载回归checkpoint：原生238检查及新加载4检查/28拒绝通过；同产物旧
A1–A8在joined_population超限拒绝文案处失败。实际拒绝发生，新的统一预算守卫
改写了旧错误原因，故保留默认单捕获的“batch exceeds 16 MiB”前缀，仅剩余
合计预算使用双捕获原因。失败日志保留，修复后需重建并重新绑定产物验收；
未完成整体P1，公开多事实/窗口API及MCP、业务待答和最终质量仍必需。

双事实加载兼容修复结果：重建产物后完整A1–A8 runner退出0，新加载4检查/28
拒绝、238原生双事实检查、旧capture308/state19/grain42/relation55/fact78及
26SQL/982断言通过；类型、原生/TypeScript格式和diff检查通过。最初joined
超限文案失败以及误用native可执行文件路径的退出127保留。工作包仍未本地
提交：需归档原件/hash派生证据，更新验收receipt并完成当前产物接口/离线回归
复核。下一是公开多事实/窗口接口，整个P1及业务待答、最终tidy/跨平台仍未完成。
没有真实模型请求、推送、合并或发布。

双事实加载本地工作包完成：[验收与局限](docs/research/ossie-req-001/p1-fact-load-controls/review.md)及receipt绑定当前扩展
25a7e3c、238原生检查/加载4检查28拒绝、完整旧回归与无模型证据。默认超限
文案兼容修复及测试/构建失败原件保留，16MiB只限canonical、hash非集合归一、
探针固定binary及Prepare非溢出执行证据的限制明确。下一授权动作是公开多事实/
窗口查询/编译解释/发现、私有Engine与JSON完整输出，再MCP/无模型；整个原P1
及状态历史待答、最终tidy/跨平台/历史审计不缩减。未推送或调用模型。

双事实公开接口恢复checkpoint（e39637e，干净）：[实施契约新增接口段](docs/research/ossie-req-001/p1-program-research/implementation-contract.md)
冻结五专用函数、真实32/16/128上限、独立common分母AST、空结果保留分母、
BC/全有限DATE规范往返、原始身份排序和完整1MiB输出、单RPC冻结及未执行
计划/发现边界。Advisor只读反证建议采纳，尚未注册或实现新公开接口；下一
原授权动作是按此契约实现分母/严格请求/私有Engine查询与解释发现，再实际
插件/原生控制/MCP/无模型验收。原完整P1和业务待答保持，禁止模型/推送。

双事实公开接口实施checkpoint：查询/解释/发现与实际边界验证已接入，尚未
提交。Advisor发现交换facts顺序的source元数据错配，改按dataset查找；同名
count_star及减号宏风险已实际复现（共同1误报42、90误算99），内部计数/
金额运算将显式绑定system.main，保留用户输入限定函数拒绝。修正后需
重新构建并跑宏/顺序反例、258原生取消/prepared控制及完整旧回归，不能
沿修复前产物证据。整个P1/MCP/无模型/业务待答和最终质量继续。

同名SUM宏反例补充发现固定DuckDB 1.5.6的WindowExpression渲染省略catalog；
内部窗口编译原先渲染后再解析，丢失system绑定。改为保留已验证窗口AST副本
直接嵌入阶段AST，发现另列真实catalog/schema；诊断SQL仍无普通重放保证。
失败原件保留，需再次重建宏/窗口及全部必要回归，不以降级拒绝普通宏替代支持。

新增五函数后旧SQL metadata预期注册数27/25失败，实际32/30是新增接口的
正常结果，已更新基线并补五函数真实arity检查。补独立SQL profile查询/解释/
分母/窗口/宏/源删除用例，注册示例使用可执行的完整显式请求而非占位符。
最终描述变更后重新构建同产物回归，不把之前26用例数当当前新SQL证据。

双事实公开SQL工作包本地完成：[审查/receipt](docs/research/ossie-req-001/p1-fact-api-controls/review.md)绑定最终产物9ddb20db，
30实际接口检查/55拒绝、260原生取消/prepared控制、27SQL/1026断言，五函数
标准客户端实际oracle及全部旧native/A1–A8/观察每日关系/MCP/无模型回归通过。
121Bun/544、类型/格式/架构通过；宏错误分母/加减、窗口渲染丢catalog、facts
来源次序及旧注册计数失败均保留。最大两源10000行/18,2/512组件叶实际精度
对照与已有预算下非溢出界限记录；宽源/越界展开拒绝，不声称触发运行时溢出。
下一授权工作为新profile MCP四工具/资源/严格decoder及实际RPC，再无模型/
重放/CI。原完整P1、状态历史待答、函数兼容矩阵和最终跨平台tidy/历史私有
审计不缩减。无模型请求、推送、合并或发布。

双事实MCP恢复checkpoint（dd609c9，干净）：沿用OSSIE-REQ-001，实施独立
fact_server.sql，四个users/windows查询与compile工具、program/surface资源。
唯一必填外层request_json字符串；内层仍由native严格校验，外层上游忽略未知
参数的现状须实际验证且明示。decoder核对完整键集合、精确金额/整数文本、
全有限DATE、排序唯一身份、请求/声明/六绑定一致及计划未计算标志；不能将
结构正确视为成员正确。实际stdio以独立BigInt/完整元组oracle验证NULL、空集
保留分母、先窗口后展示、原始身份和10000行，并保留失败证据。下一为本地
实现和实际RPC/旧MCP回归；无模型/推送，完整P1与业务待答保持。

双事实MCP中间证据：首轮26完整oracle/registry、42拒绝和129Bun/663断言
通过。补边界探针发现verifier只读tool content且猜错资源错误code，上游实际
返回JSON-RPC error.message中的RESOURCE_LIMIT_EXCEEDED；这是验收脚本错误，
实际16385字节已拒绝。失败report/RPC原件保留，修正解析两种载体并按既有
错误契约复测。Advisor只读发现请求、COUNT、币种/粒度及预解析重复键门禁
缺口已补，仍须最终实际MCP和旧服务回归，不将结构验证当成员真值。

双事实MCP本地工作包完成：[审查/receipt](docs/research/ossie-req-001/p1-fact-mcp-controls/review.md)
绑定原产物9ddb20db和38份私有只读原件；实际32完整oracle/registry检查、48
拒绝，四工具16KiB边界、完整10000行、转义JSON超限compile成功/执行拒绝、
BC/极值/NUL身份及集合/窗口/独立分母对照通过。严格decoder请求/币种/粒度/
计数/重复键控制和129Bun/663断言、类型/格式/架构及旧观察43/38、每日16/28、
关系14/13MCP通过。资源错误载体/code和旧观察CLI参数的脚本失败原件保留。
下一继续新profile无模型演练/离线重放/私有伪造控制与CI；完整P1、状态历史待答、
兼容矩阵及最终跨平台tidy/历史私有审计不变，无模型请求或推送。

双事实无模型恢复checkpoint（7903dc3，干净）：前轮progress，继续现有
OSSIE-REQ-001。冻结八个合成场景：分别归约/金额与COUNT、差集右UNKNOWN、
空筛选保留分母、窗口后展示、空来源、BC/极值窗口、raw身份、compile-only。
使用实际MCP及注入Responses完成模拟，答案必须引用实际执行结果，独立
BigInt/全身份oracle留在审计端；compile-only不执行/不声称unsupported。
沿共享loop和私有hash容器，严格顺序离线重放不启动进程或模型；重封伪造需
覆盖同步金额/同数成员/分母/先筛日期/引用计划冒充执行，而非仅hash失败。
本轮只实施无模型入口/审计/重放/控制与必要CI；全P1及业务待答保持。

无模型中间checkpoint：八场景首轮actual MCP/Responses注入和23重封控制通过，
原件保留但仍是早期源码版本。Advisor发现definition binding只内部一致而未
对应当前fixture，新增四定义hash交叉核验；模拟model/zero usage、runtime
形状/调用者产物hash、固定模拟compile-only文字/局限也补门禁，不扩展共享
旧profile。新增协调伪造须到达指定新门禁。CI加入双事实加载、对象/窗口、
stock、MCP和无模型实际命令，远端尚未运行；本地需最终源码再完整演练/重放。

双事实无模型工作包本地完成：[审查/receipt](docs/research/ossie-req-001/p1-fact-agent-controls/review.md)
绑定最终八场景、23模拟completion/15工具调用、30重新封存伪造拒绝；同步
金额/同数成员/共同分母/先筛日期窗口/compile引用控制均到达独立审计门禁。
定义hash对应真实fixture、模拟身份/usage/runtime及元数据重复键门禁已补。
纯重放封锁网络/Node与Bun进程入口仍通过；138Bun/721、类型/格式/架构及本地
五个新CI命令、旧每日18/关系20控制通过。58旁证及195阶段私有容器保持
原件与hash；早期通过版本不当当前源码最终证据。远端CI未运行，无模型调用/
推送。下一原授权动作为固定SQL2026/各profile函数窗口矩阵实际对照，再
全P1剩余时间/历史与最终跨平台tidy/历史私有审计，业务待答不缩减。

函数兼容矩阵恢复checkpoint（0aee104，干净）：上一goal问答轮为no progress，
本轮核实最终三个检查均exit0，完成无模型包本地提交。沿OSSIE-REQ-001核对
固定SQL2026原件、旧表达式与profile AST门禁、实际独立oracle。矩阵分规范
定义、解析/绑定、profile允许位置、执行语义与证据；DuckDB支持不等于本项目
接受。当前不扩大函数语言，不以LAG行偏移代替每日状态或历史关系。复跑固定
产物SQL2026/各profile SQL与native程序，记录缺口/旧引擎偏离；两项业务待答
和完整P1/最终质量/私有历史审计保持。无模型或推送。

P1函数/窗口矩阵本地工作包完成：新增[兼容矩阵](docs/ossie-sql-compatibility.md)
区分固定REQUIRED语法、通用core lowering、各人口profile允许位置、实际执行
和未逐项验证的拒绝。新增12个fact原生边界，90检查通过；同SHA9ddb产物
27SQL/1026断言和关系55检查通过，20原始source及原需求hash完整核验。
七份raw独立只读归档，public日志保留hash；clang-format/文档链接通过，
无需重复不变TS代码测试。LAG行偏移不当每日状态，ROWS不当历史有效期。
矩阵见新receipt，不声称完整SQL2026/全P1或远端CI。下一授权为最终质量/
同产物范围审计与待推历史隐私预检；两项业务决策待答，状态/历史实现及
相应接口/回归仍必需，不能因矩阵工作包完成而收掉整个goal。无模型/推送。

最终质量恢复checkpoint（70f8ae5）：矩阵工作包完成但goal active。cached diff
收口发现public SQL log尾部空行，原件不动，派生文本修正并更新hash/receipt，
后续diff通过。下一先本机quality-local和全部六个native runner，按当前artifact
复核，不混用历史Ubuntu证据。Docker无运行/停止容器，仅既有Ubuntu24.04镜像；
旧/tmp Linux checkout需重新绑定当前源码才可作为新证据。全P1状态/历史待答
及其实现/接口仍必需；待推历史审计和跨环境质量继续保留，无模型或推送。

本机质量阶段复验完成（70f8ae5后）：quality-local的固定C++/TS格式、类型、
架构及138Bun/721通过；六native实际state19/grain42/capture308/relation55/
fact capture260/fact program90均exit0。启动协调首次缺grain ROOT、其次fact
ROOT未带尾斜杠的两份失败原件保留，正确参数复跑，不冒充实现回归。证据见
[本机质量receipt](docs/research/ossie-req-001/p1-local-quality-controls/receipt.json)，
九raw只读归档。矩阵27SQL/1026同产物证据不变；未跑当前Linux tidy/远端CI。
下一需当前源码隔离Linux质量、待推历史私有预检；原状态/历史口径及实现/
完整接口仍待完成，不标整个P1完成，无模型或推送。

Linux质量/历史预检恢复checkpoint（7481dff，干净）：上一goal轮progress，
当前23个未推送提交。Docker只有既有Ubuntu24.04 ARM64镜像，无live容器；
旧/tmp Linux源码/日志仅作历史参考，不当最新P1证明。新隔离checkout绑定
7481dff及三个固定submodule，只挂载新checkout/证据目录、不含.env_test或
私有原始Agent材料。先跑当前源码所有src翻译单元tidy，再按结果选择必要
Linux功能回归；同时审计未推提交blob/消息中内部endpoint/model/凭据线索，
不读.env_test、不打印疑似秘密值。两项业务决策待答，完整P1保持。

历史预检阶段完成（范围bd8c0ee..7481dff）：远端read-only确认base一致，
23消息/1158变化路径/672unique blob无已知内部服务/模型/凭据常量/私有路径
发现，无merge遗漏；.env_test仍ignored且未读。实际检查36份新receipt源码
绑定全部匹配。三私有原件只读，public选择receipt；不冒充通用秘密认证。
当前Linux容器仍live安装依赖；两业务问题重新集中列出等待，不推进其语义。
下一继续同容器tidy，尾部新提交另补检，完整五类P1未完成。

Linux tidy中间发现：当前15TU检查已报告population/fact新代码的控制语句
缺花括号，以及一处weak回调按值复制、一处单参构造缺explicit。检查仍live，
未将历史tidy当通过。先保留完整首轮raw/候选源码与固定编译命令，再在隔离
副本使用限定clang-tidy修复，Lead审查diff并移回主工作树。预期机械修复仍
需实际重建/六native/SQL/接口回归及二轮tidy；不修改业务口径或放宽配置。

修复后回归中间发现：新产物66ec8a7c的六native/27SQL/完整P0及quality通过。
独立回归driver的观察/每日通过，关系loader因/tmp在macOS指向/private/tmp而
生成错误相对fixture路径，首轮失败保存。此为验收器输出目录canonical问题，
非模型loader放宽；两个关系artifact verifier统一在mkdir后realpath目录，再
于新目录复测全部语义/拒绝。不改固定fixture或已封存证据，Linux tidy二轮live。

当前P1 tidy/回归本地工作包完成：首轮248unique诊断原件保留，246花括号/
Binder explicit/weak const引用修复经token对应审查，无关源码不动；二轮
Ubuntu24.04 ARM64 clang18对15TU全部exit0。新mac产物66ec8a7c六native
19/42/308/55/260/90、27SQL/1026、完整P0、11profile/stock/MCP和四无模型
全exit0；138Bun/721及type/format/architecture通过。关系验收器/tmp真实
目录修复与观察public导出symlink拒绝的失败保留，未放宽原件保护。
[证据包](docs/research/ossie-req-001/p1-tidy-controls/review.md)绑定315旁证及
77原私有容器，首轮/伪造不当成功执行。Linux只静态检查，mac功能证据；
未运行当前Linux功能构建/远端CI。下一继续两业务决策后的状态/历史设计
实现、相应完整接口与旧行为回归，再最终全五项审计；若答案仍未到，先
补本修复提交尾部隐私预检及必要独立研究。原scope不缩减，无模型/推送。

Linux功能验收恢复checkpoint（ae0d0f4，干净）：上一goal轮progress，当前
mac66ec全回归与Linux15TU tidy已通过，本修复提交尾部秘密审计无发现；
远端read-only tip仍bd8c0ee。剩余独立必要工作先补当前Linux功能产物：
复用已有Ubuntu24.04 ARM64工具链镜像，隔离checkout从7481dff+已审查修复
更新为精确ae0d0f4，并固定三个submodule；不带主树私有材料/.env_test。
构建、六native/27SQL、Bun及新增SQL/stock/MCP/无模型均用Linux本地产物。
不要求跨平台二进制hash相同，不以Linux tidy代替Linux执行，也不宣称远端
AMD64 CI。两业务问题仍未答，不实施状态/历史依赖语义，完整五类scope保持。

时间承载独立研究checkpoint（等待Linux构建）：复用单一只读Advisor反证，
确认现daily的IS TRUE投影已丢FALSE/NULL，现关系布尔Binder只绑定同一User，
不能冒充新状态/跨源历史目标。上游set/noNULL不决定源记录重复政策。
本轮仅设计/普通SQL与独立枚举研究，不实现候选接口或冻结未答业务口径。
补齐User域/相邻日边界/状态与转移总量/历史记录身份/全批冲突及中间bag
预算，明确上游要求、项目限制、待答政策。研究证据不得算插件验收。

Linux验收恢复checkpoint（ae0d0f4）：完整release构建614步骤通过，但独立原生测试
因EXCLUDE_FROM_ALL未由release默认构建，测试启动exit127；这是本地驱动遗漏，
不是原生用例失败。保留首轮日志/容器，继续同源码同构建目录显式构建六目标，
再运行六native、SQL、P0和接口/离线验证。Linux本地ARM64不等于远端AMD64 CI。

Linux功能工作包本地完成：[复验和receipt](docs/research/ossie-req-001/p1-linux-functional/review.md)
绑定精确ae0d0f4/Linux a5bef339产物，六native19/42/308/55/260/90、27SQL/1026、
138Bun/721、类型/格式/架构、完整P0、11接口/stock/MCP及四无模型全通过。
首轮遗漏EXCLUDE_FROM_ALL目标的exit127保存，恢复驱动显式构建后通过。
2480私有文件字节清单绑定旁证和原容器副本；不声称远端AMD64 CI或复制路径重放。

时间承载研究工作包：[候选契约及枚举](docs/research/ossie-req-001/p1-time-carrier-research/research.md)
明确每日八种值集合、TRUE-witness信息丢失、bool_or(FALSE,NULL)反例、全捕获/范围内
User域差异、请求内相邻变化及U×(2D−1)总对象预算；历史身份与link集合分开、有限
端点、全批无事实冲突、同目标重复的中间bag预算。普通SQL与独立TS对照通过，
没有native新时间能力声明；首轮参数辅助接口误用失败原件保存。复用单一只读
Advisor反证，实际后端/effort/usage/billing未知。已将待答两问题补齐User域/变化
边界/来源ID及同目标重叠选择，仍无答案。完整P1五项不缩减，下一必要行动为
收到业务决策后固定schema承载核对/时间native实现和完整接口验收；等待时可继续
现交付证据与未推历史尾部检查，不擅自采用预选项。无真实模型或推送。

本轮收口复核：2480私有副本逐文件hash、原容器mode、262报告绑定及全部公开
日志hash通过；时间研究重复输出字节相同、三个源hash与10新文档链接核对通过。
新增研究脚本format/typecheck/architecture通过，C++/Linux产物源码未再改变。
当前必要下一步仍为两业务决策后的schema/时间实施，未将研究完成当goal完成。
本轮只做本地提交和尾部有界隐私预检，不推送。

完整scope审计恢复checkpoint（805df8c，干净）：上一轮Linux/时间研究/本地提交
是progress。本轮按不可变原需求第5节五条逐项核对当前声明、实际实现与最新
mac/Linux原件绑定；不以历史阶段页或本地通过替代全P1完成。发现当前交付章节
仍停留goal前，relation使用文档把已完成多事实/窗口写成未来项目工作，先修正
当前入口/历史标识，保留原阶段证据。待答两项业务口径仍是同一依赖阻碍，
其候选SQL不能当native验收；本轮不实施依赖语义，无模型或推送。

完整scope当前状态审计完成：[五条逐项报告](docs/research/ossie-req-001/p1-scope-audit/report.json)
核对20固定来源/原需求、32当前实现源hash、两实际产物和262Linux报告绑定。
mac receipt还绑定一个当时兼容文档：其ae0d0f4历史blob校验通过，当前追加
新证据后hash不同，原receipt不改；首轮误将历史文档要求与当前字节相同的
审计失败保存，实施源码没有变化。原五条中跨期状态/历史缺失，函数与全接口
也因该缺失未完成；其他有限工作包有本地证据，但不取代最终全goal审计。
当前交付入口已重写，goal前章节明确标为历史，README/关系/P0入口范围一致。

阻碍复核：每日状态与有限历史关系的关键业务口径持续多轮待答，前述原生/
接口、质量、历史隐私、两平台回归及本轮独立证据审计已全部推进。自动goal
续行不是人类业务答复，预选项不能采用。下一实施及最终验收都依赖该答复；
剩余分页/一般图/发布等是独立未来或未授权项，不能用作替代进展。本轮本地
提交并补其尾部有界隐私检查后，没有剩余必要且不依赖答复的动作；需将goal
如实标为blocked等待决策，完整目标和所有未完成项保持，回答后继续。

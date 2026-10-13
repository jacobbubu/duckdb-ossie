# facts 最小发现声明与无模型验收

2026-10-13，基线15b8ab2。最小发现声明已实现：查询与编译工具都带完整集合构造说明，新增共享 `ossie://facts/request-contract`；实际 MCP 返回4工具/3资源，6组发现请求与回复、1次初始化通知。唯一必填参数仍为String request_json，工具名、原生请求/结果语义、native program、原提示词及模型预算保持。

共享[契约载荷](../../../../examples/fact_users_request_contract.json)独立标识为ossie-fact-users-request-contract-v1，含解码后结构schema、谓词/数值/集合/资源限制、6个通用例子。例子假定另一个program声明order_count与refund_count；不加入pilot，不含实际验收成员/金额答案，也不能直接在当前快照执行。结构schema不是原生指标/数值绑定；contentSchema不作自动内层验证。窗口工具沿原行为，users与users_compile共享同一合同。

运营者启动时用read_text把静态文件读入服务数据库的一张普通表，由资源查询返回一行contract_json String。此表不是固有不可变对象，也不属于native六快照绑定。客户端对实际carrier做重复字段/大小/一行一字段String检查，再核对内容和原始字节与本地已审文件一致；两个users工具描述都必须包含同一construction_notice。新JSON、辅助模块及其重复键解析依赖分别加入来源清单，实际resource/context/body与新seal共同对应。版本/hash标识静态语法，program六绑定标识冻结数据，两者没有混为一项。

## 验证结果

[核对记录](receipt.json)绑定64份P1来源、27份fact来源和同macOS原生产物98b0aeba。实际发现载荷12,148 UTF-8字节，两工具说明与它对应，resource进入本地冻结context及序列化首请求。facts模拟请求最大32,208字节，低于原128 KiB限额；同次模拟的states/history分别17,327/8,964字节，预算未扩大。发现RPC由facts5变6，states7/history5保持；该发现增加不占用模型工具调用预算。这个宿主明确读取资源，不能推断其他宿主自动包含它或模型理解它。

| 检查 | 当前源码结果 | 边界 |
| --- | --- | --- |
| 实际facts MCP | 37项完整oracle/registry核对，48拒绝 | 既有真实native工具执行；模型请求0 |
| 原facts Agent演练 | 8场景、30重封篡改拒绝 | 模拟响应＋实际MCP；重放无模型/子进程 |
| P1离线入口 | 33控制 | 新载荷/context/body与封存/重放一致 |
| P1传输入口 | 20控制 | 虚拟配置/受控传输，含mock live fetch2；真实模型/网络0 |
| Bun回归 | 170测试、854断言 | 新carrier/版本/适用工具/描述漂移/结构分支/来源绑定检查 |
| 质量 | 格式、类型、架构通过 | 未改C++，没有新native构建/tidy或远端CI |

[白名单P1摘要](p1-summary.json)是模拟正路径的派生物，semantic_acceptance仍为not_proven；它不冒充真实模型结果。出站只由发现信息、原系统说明、原问题和实际工具回复构成，没有加入隐藏答案、预期请求或新任务模板。指标/成员真值仍由原独立oracle在执行后核对；只解释不充当执行结果。当前通用示例通过结构schema/封装检查，未建假想native数据或声称其原生执行成功。

## 回归修复与原件保留

首轮TEMP表版本的资源查询返回错误；改用普通服务数据库表后实际读取通过。原SQL、错误RPC及首次脚本保留。原合成unit fixture只含两个资源，新内存衍生物添加当前说明、资源回复、移位的RPC ID及当前server hash；原公开fixture没有改写，衍生物明确不是新实际执行证据。

协调篡改测试原先会把所有JSON字符串顺便重编码，导致未修改的合同载荷先触发字节校验、遮蔽原目标拒绝。现在仅在被变换对象确实改变时重编码，未改变的字符串保持原字节；生产接受逻辑没有放宽。P1重放发现计数及unit总回复数也同步到新资源，仍严格核对顺序/事件/参数/字节与来源。

一轮只读Advisor发现新p1_agent_json依赖缺少fact来源绑定，Lead加入并补测试；另外补了已发布schema的错误结构拒绝用例。此前失败、修复前中间通过和最终新源结果分别保留，没有重封旧记录。2332份原入口/真实失败/审阅文件校验保持；旧记录仍归属原source/seal，不能以当前源码不匹配为由改写旧hash。真实facts首次失败仍是未接受，本次没有再次真实调用，也没有修正原模型提议。

Advisor请求Astra/high，实际backend/effort/usage/billing未报告，记录为unknown。Lead核实其source-binding、测试范围、静态表与native绑定区分的建议并完成最终复验。相关hash、日志/私有清单及文档检查见[verification](verification.json)。

## 交付与下一步

沿[原issue](../../../ossie-execution-research.md)和[现计划](../../../../ossie-duckdb-plan.md)，本工作包完成。开发继续原codex/ossie-execution工作树；迁移后的主目录是另一个旧checkout，未混用。父仓检查忽略已记录的DuckDB子模块Git路径问题，本轮没有处理子模块或构建原生工程。

没有读取实际.env_test/模型配置、调用真实模型、推送、合并或发布。现有CI调用实际facts/P1验证入口，因此后续新CI将覆盖发现资源，但未推送/运行，不能声称远端跨平台通过。下一建议先审新版实际facts模型上下文、预算和命令/证据范围，再另行决定单题真实验收；不自动重试或继续其他场景。整体三题真实验收仍未完成。

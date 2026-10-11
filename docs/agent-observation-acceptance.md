# 观察 profile 的 MCP 预检与 Agent 准备

独立的 [observation_server.sql](../examples/observation_server.sql) 已接入
`daily-observation-population-v1`：concepts 资源使用 v2，其余三工具/四资源
名称及 typed result-v1 保持。旧 GAAP 示例和旧 Agent 验收行为保持。
本工作包只验证新 MCP 接入和无模型准备，尚未运行真实观察 Agent。

## 本地使用

从仓库根目录运行示例，需要具有观察 API 的 Ossie 产物及 duckdb_mcp：

```sh
duckdb -bail -init examples/observation_server.sql
```

示例的操作者启动块导入八行合成观察，加载固定 threshold-10 bundle。
使用未发布本地产物时，用绝对产物 LOAD 替换 INSTALL/LOAD 两行，并以
`-unsigned -bail` 启动。User 是 referent-only；一个独立进程只服务一个
冻结 bundle。更换正式规则需由操作者重启，不向 Agent 开放 import/reload。
资源 URI 复用不意味着可跨服务器缓存；以服务器会话及绑定哈希隔离。

真实 MCP 预检不需要模型配置：

```sh
bun scripts/verify_observation_mcp.ts \
  --cli /absolute/path/to/stock/duckdb \
  --extension /absolute/path/to/ossie.duckdb_extension \
  --out /tmp/new-observation-mcp-results.json
```

输出路径必须不存在。stock CLI 固定 DuckDB1.5.6/source069cc9f9b5，记录
实际 duckdb_mcp 版本/产物哈希，而非声称最新协议兼容。检查真实 stdio
JSON-RPC、独占工具、v2 组件/引用 User 身份、五指标表达式和五字段类型、
四资源/执行绑定、完整对象、分组/空集/NULL、plan-v2及合法请求恢复。

四个隔离场景的准备模式：

```sh
bun scripts/agent_observation_acceptance.ts --prepare \
  --cli /absolute/path/to/stock/duckdb \
  --extension /absolute/path/to/ossie.duckdb_extension \
  --out /tmp/new-observation-agent-prepare
```

每个场景使用新 MCP 进程，保存 `setup.json`、实际发现 `events.json` 和
`model-context.json`。后者只含 system、用户问题、运行时工具及资源；
不含宿主 oracle、预检结果、期望答案或预制工具参数。当前程序没有
`--run`，不读取模型环境配置或 `.env_test`。发现准备不能称为模型验收。
四场景是基准、正式规则变化、每日/区间 distinct 对比、每天合格能力缺口。

## 离线验收门禁

`scripts/observation_evidence.ts` 提供实际事件到成功执行证据的构造器、
逐主张审计与观察答案检查。事件必须关联同场景的唯一实际请求/响应；
JSON-RPC error、isError或compile不能成为数值支持。指标先按本 fixture
的发现表达式和 bound type 校验，随后检查概念、六哈希、请求范围、指定
分组行和独立 CSV oracle；不按相同数字跨行/错范围拼出支持。

只对日期 literal 的有限 AND 条件证明边界，支持含首尾与半开区间等价；
不实现通用 SQL 解析器。无法识别的合法表达式、缺失或宽于题目但当前
成员碰巧相同的日期范围，都保持待语义审阅。已确定的错指标、错分组、
错结果或错答案仍失败，pending 不能掩盖这些错误。范围成员匹配也必须
通过；本 fixture 的五指标与分母不会授权其他客户端业务计算。

允许同范围的多次指标查询，完整 typed objects 也可推导已注册五指标及
每日人数。对象先逐项验证身份/金额/分类和完整性，再选择目标日期；
按日结果用日期键对应。整数/Decimal精确字符串、AVG DOUBLE绝对误差
1e-10；当前机械检查不接受展示舍入替代完整数值，未来展示舍入需增加
显式审阅规则。金额单位与文字口径仍需 Lead/人工审阅。

`checkObservationAnswer` 即使机械检查通过也保留 `overall=pending semantic
review`；它不会冒充模型推理质量或人工裁定。每天合格场景必须无目标
口径数值，但允许先探查再拒绝。该拒绝说明注册能力缺失，不声称完整
原始数据无法计算用户集合交集。

## 已验证范围与下一步

[本地记录](research/ossie-req-001/observation-mcp-controls/verification.json)
绑定实现、测试、扩展、实际新/旧 MCP 报告及四场景准备报告。观察 MCP
43 项检查/38 项拒绝通过，新增15项离线控制通过，全部78项 Bun 测试及
格式/类型/架构检查通过；同一扩展产物的旧两个 profile MCP 回归通过。
生产 C++/fixture 未改，不重建或冒称本轮新增原生/远端 CI/tidy 结果。

下一步先准备真实观察 Agent 的运行入口与证据归档，再明确四场景调用
授权和预算。研究上限为每场景8模型请求/12工具调用/120秒/每请求2048
输出 tokens，总计最多32模型请求、48工具调用、65536请求输出 tokens；
不是输入 token 或金额上限。服务配置仍使用标准 `OPENAI_*`，内部值只
留本地 ignored `.env_test`，不进入提交或公开证据。分页/跨日实体指标/
发布和交付继续是独立范围。

# 历史独立 MCP 与严格完整返回值

历史 profile 现有两个独立只读工具及 history-program/surface 两项资源。
[MCP 契约与示例](../../../population-history.md)说明身份、半开有效期、观察筛选
范围、来源计数与字节限制。没有旧人口工具、任意 SQL 或加载/重载工具。
原生 program 的 mcp_registered=false 不探测运行中 registry；实际 tools/list
证明两工具已暴露。该区别不影响 SQL query_supported/compile_supported=true。

[选择性验证报告](verification.json)与[receipt](receipt.json)绑定真实 stock
DuckDB 1.5.6 stdio MCP、固定上游 duckdb_mcp `6f9c4ba` 及 macOS ARM64
产物 `d51854132706e00d5bd150d79ea55d25d22fe6cca0d2f2e9ce8cadb4421845bf`。
15 项检查、24 项拒绝及 28 种损坏 carrier 控制通过。实际 MCP 返回的 10,000
条四角色身份逐条比较；完整内层 JSON 超过 1 MiB 拒绝后，小范围请求恢复。
逻辑 compile 可成功绑定不能保证执行响应大小，测试保留这个区别。

先用实际 SQL 读取来源，核对 14 个观察及 12 个 record 与独立数组完全对应；
再逐个观察枚举原始历史见证，不使用索引合并/二分算法作为 oracle。包括同目标
重叠只输出一条、异目标相接、缺口、无历史观察、没有观察的历史、两产品、
大小写/Unicode/NUL、BC/宽年份/有限 INT32_MIN。范围/all/subset/空来源的完整
links 与请求、program/plan 六 hash 一致。合法 carrier 本身不证明成员真值。

校验器拒绝重复解码字段、额外字段、错误 API/类型/hash、数字计数、不完整、
截断/重复/乱序/同观察多目标、非法有限日期、孤立 surrogate、越界链接、错误
计划算子/类型/限制与伪造 program 来源/角色/计数/MCP 标志。固定上游实测会
忽略未知外层参数，也会把 object 转 String；文档明确，调用者仍发送 String，
不伪称上游传输拥有本项目未实现的外层严格门禁。

Cpp 和共享产物未改，沿用[同 SHA 原生、SQL 与全部旧 MCP/无模型回归](../p1-history-links-controls/receipt.json)。
本轮 138 Bun/723 expects、TypeScript 类型/格式、架构及 diff 检查通过。
原始 MCP 会话、源码及失败日志由私有清单绑定。初次类型声明问题已修正；
SDK 的日期字符串把公元前一年写作 year0，来源对应检查改用 DuckDB 原生
DATE::VARCHAR，保留初次失败，不以不同表示形式冒充数据差异。

CI 增加历史 MCP 检查，但未推送或远端执行。历史 Agent 无模型循环/封存重放、
新代码 Linux/tidy 与全五项 P1 统一验收仍需完成；本工作包不是完整 goal。
无真实模型、私有配置读取、推送、合并或发布。

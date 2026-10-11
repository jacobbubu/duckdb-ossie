# 每日三值状态与相邻日比较

本接口复用 `daily-observation-population-v1` 的冻结观察与当前正式资格规则，
专用 `state_api_version=1.0.0`。旧每日合格集合入口保持原行为。
用户已接受[业务口径](research/ossie-req-001/p1-time-accepted/acceptance.json)；
本工作包仅完成原生 SQL、逻辑解释与专用发现，新状态 MCP、严格调用方解码器、
无模型演练及历史关系仍需实施，完整 P1 尚未完成。

```sql
SELECT result_json FROM ossie_daily_states_json(
  'QualifiedObservation',
  '{"start_date":"2024-02-28","end_date":"2024-02-29","products":{"mode":"all"},"user_domain":"captured_users","transitions":"within_request_days"}'
);
```

`concept` 选择资格规则。用户域取整个冻结观察批次中的去重 User；日期和产品
只筛当天观察。没有范围内观察的 User 仍输出 UNKNOWN。这不是登记用户全集。
每个 User/自然日有任一 TRUE 即 TRUE；观察非空且全部 FALSE 才是 FALSE；
缺失、全 NULL、FALSE 与 NULL 混合均为 UNKNOWN。不同产品的 TRUE 可满足当天资格，
不要求每个产品分别合格。不延续前日状态；全部日期应用同一冻结规则，不推断历史规则。

成功结果 `ossie-daily-states-result-v1` 的 `states` 是完整
`[User,DATE,status]` 数组；`comparisons` 是完整
`[User,from_DATE,to_DATE,from_status,to_status,changed]` 数组。
比较包含未变化，只比较请求内相邻自然日；单日请求没有比较。
User 按原始 UTF-8 字节排序，日期升序；不修改大小写、Unicode 或控制字符身份。
`state_totals` 的单位为 User/day，三个状态计数和应等于 `state_count`，
不是人数或观察行数。`changed_comparison_count` 计算状态标签发生变化的相邻日对数。
全部数量是十进制整数字符串。规范化请求、资格规则及六项快照追溯随结果返回。

`ossie_daily_states_compile(concept,request_json)` 返回 `plan_json`，
schema 为 `ossie-daily-states-plan-v1`，绑定完整 User/DATE/product/BOOLEAN 投影，
保留 FALSE/NULL。`executed=false`、`states_computed=false`、
`comparisons_computed=false`；仅绑定，不扫描计算状态。投影 SQL 未包含 C++ 的
日历归约，且依赖冻结 AST，不能独立重放。Prepared 持有旧规则和旧快照；
源表删除或重新载入新批次不会改变已绑定请求。

`ossie_daily_states_program()` 返回 `ossie-daily-states-program-v1`，声明三入口、
实际冻结域计数、身份、口径、限制和拒绝项，`query_executed=false`。
当前 `mcp_registered=false`，不能把 SQL 可用理解为服务已注册工具。

| 限制 | 定义 |
| --- | --- |
| 源数据 | 沿用观察 profile 全批 10,000 行、256 物理列、16 MiB canonical 输入 |
| 请求 | 原始完整 JSON 最多 16,384 UTF-8 字节，解析前检查 |
| 日期 | 有限真实公历 DATE，0001..9999，首尾含，最多 366 日 |
| 产品 | 显式 all 或非空 subset；subset 最多 64 个唯一非空原始字符串，每个最多 256 UTF-8 字节 |
| 总对象 | 状态与全部相邻比较合计最多 10,000；绑定时检查 `U × (2D−1)`，扩展前拒绝超限 |
| JSON | 包括元数据、转义和结构的完整响应最多 1,048,576 UTF-8 字节 |

空域可返回空数组；未知产品合法并产生 UNKNOWN。总对象预算通过不保证 JSON 能装下。
两项预算同时生效，不截断、不分页、不转只返回计数。内存峰值、并发总量和完成时间
不受这些数字保证。错误使用既有 error-v1，原生 interrupt/internal/fatal/OOM 保留。

缺字段、额外/重复键、坏日期、NULL 或错误产品类型拒绝；其他用户域、请求外前驱、
carry-forward、历史有效期推断均不支持。旧 `ossie_daily_users_json` 拒绝新状态请求字段。
本地证据覆盖全部八种当天值集合、九种状态对、跨月/闰日、范围外用户、冻结重载、
确定性阶段取消和恢复、10,000/9,999 对象与超限，以及精确 1 MiB 与多一字节拒绝。
证据只覆盖当前 macOS ARM64 产物，新增代码的 Linux/tidy 和远端 CI 尚未复验。

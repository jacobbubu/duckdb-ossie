# 每日三值状态与相邻日比较

本接口复用 `daily-observation-population-v1` 的冻结观察与当前正式资格规则，
专用 `state_api_version=1.0.0`。旧每日合格集合入口保持原行为。
用户已接受[业务口径](research/ossie-req-001/p1-time-accepted/acceptance.json)；
原生 SQL、逻辑解释、专用发现及独立状态 MCP、严格调用方解码、六场景无模型
演练已完成本地工作包。历史关系全接口及新增代码 Linux/tidy 已有本地双平台证据；完整 P1 的统一审计仍需完成。

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
`mcp_registered=false` 表示原生不会探测运行中的 MCP 注册表。下面独立服务的
实际工具和资源由真实 tools/list 与 resources/list 验证。

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
原状态报告保留当时 macOS ARM64 产物；后续[当前双平台功能与 tidy](research/ossie-req-001/p1-final-linux-functional/review.md)已复验新增代码，远端 CI 未运行。

独立[状态服务](../examples/daily_state_server.sql)只注册 `population_daily_states`
与 `population_daily_states_compile`，有 concepts、capabilities、state-program、
state-surface 四个资源。它与旧每日集合服务分开，没有导入、重载、任意 SQL 或
旧每日集合工具。操作员载入冻结数据，Agent 只有查询与解释能力。state-surface
说明实际工具暴露；state-program 的元数据计数来自载入，不代表一次查询执行。

固定上游 MCP 可能把对象参数转成字符串，并忽略未知外层参数，本地真实验证
确认了这两种行为。因此调用方必须发送 concept/request_json 两个 String，
全部范围放在 request_json 内；不能靠外层 products 实现筛选。共享 Agent 调用
门禁拒绝未知或非字符串参数，内部 JSON 仍由原生严格校验。

[专用解码器](../scripts/daily_state_result.ts)检查独立 schema/version、唯一解码键、
完整字段、六项追溯、范围、完整矩形 User/day 日期网格、原始 UTF-8 排序、全部
相邻比较、变化标记、计数及 User/day 统计单位。解释检查实际输入类型和明确算子，
发现检查限制与拒绝项。外层 carrier 有本地 8 MiB 校验门禁，不是上游传输限制；
内部完整 JSON 继续 1 MiB。结构通过不能证明状态计算正确，完整行另与独立观察
fixture 计算对照。结果与解释要比较同一范围、规则和追溯；分次调用可能跨重载。

[无模型入口](../scripts/verify_daily_state_agent_offline.ts)通过注入 Responses completion
执行全部产品、产品 X、缺失日、阈值 20、空批次和只解释六个场景；共 17 次模拟
completion、11 次真实 MCP 工具调用。每场最多 3 轮、2 次工具、120 秒、4096 输出
token、131072 输入 UTF-8 字节，这是本地演练预算，不是模型实测需求。入口不读取
provider 配置，没有真实调用分支。该演练证明工具链与支持边界，不能证明真实模型
自主选工具或正确理解自然语言。

原始证据只写入调用者提供的 Git 忽略私有归档，严格顺序重放核对当前源 hash、
资源、请求、实际响应、公开 completion 投影、最终答案和独立完整行 oracle。
解释场景不能输出计算结果；只引用匹配的执行证据才可回答。哈希证明本地字节
对应，不证明提供商真实性或业务数据真实性。16 个重新封存的合成损坏记录拒绝，
同时改错且结构合法的工具/答案仍被独立 oracle 拒绝；incomplete completion
封存为失败，不能完整重放。[MCP 与演练证据](research/ossie-req-001/p1-daily-state-mcp-controls/review.md)
保留失败及修复范围。CI 已加入两条命令，尚未运行远端。

# 观察 profile 的 MCP 接入与最小 Agent 验收设计

2026-10-11；OSSIE-REQ-001 现有多期工作项。状态：当时的研究方案；后续无模型接入已本地实施，见
[实施与准备说明](../../../agent-observation-acceptance.md)。真实模型验收仍未运行。
本轮不修改生产代码、不启动新 MCP 示例、不调用真实模型、不推送。

## 结论与前置证据

建议增加独立观察 MCP 示例和观察 Agent 验收驱动，复用现有三工具、
四资源及 Responses 循环。先完成无模型的真实 MCP 预检，再考虑四个
隔离场景的真实 Agent 实验。旧 GAAP 示例与旧验收驱动保留其现有行为。

本地 SQL 实现位于 `5c7f795`，支持 `daily-observation-population-v1`；
[使用契约](../../../population-observation.md) 和
[最终本地证据](../observation-schema-controls/verification-final.json)
覆盖复合身份、五指标、冻结批次、v2 发现/计划及两个旧 profile 回归。
它们不证明新观察 MCP 传输或 Agent 使用已经成功。远端 CI/tidy 仍是
独立交付事项；既有真实 GAAP Agent 验收不能替代观察验收。

## 当前接口的实际缺口

`examples/population_server.sql` 固定导入旧 CSV/bundle，concepts 资源调用
`ossie_concepts()`。新观察 profile 明确拒绝该单身份入口，必须调用
`ossie_concepts_v2()`。其余 metrics/fields/capabilities 函数、typed
objects/query 和 compile 工具入口已经存在；生产 C++ 暂无必须新增的
执行接口，实际 MCP 传输需要另行验证。

`capabilities_json.concept_discovery` 给出有效概念发现函数与 schema。
顶层 `discovery_schema` 仍为通用 discovery-v1，不能据此猜测 concepts
资源的结构。观察计划是 plan-v2，执行结果仍是 result-v1；这些是项目
接口版本，不是 MCP 协议版本或上游 Ossie 新标准。

`scripts/agent_acceptance.ts` 固定旧日期、GAAP 字段及单一 count/total/average。
其 `checkAnswer` 按值搜索被引用的 query 行，保留人工范围评审；它没有
自动证明同一行/同一范围支撑全部主张。不能把它更换 fixture 后就称为
观察验收。通用 `agent_loop.ts` 与 `population_result.ts` 可继续复用。

## 最小 MCP 接入边界

未来增加 `examples/observation_server.sql`，操作者先导入研究八行观察，
加载固定 threshold-10 bundle，成功后才启动 stdio；失败必须在启动前
退出。一个进程只服务一个已冻结 bundle，不增加 Agent 导入/改规则工具。
threshold-20 实验独立重启进程，禁止跨场景复用模型会话或旧资源。
本轮不增加通用 profile 自动切换或迁移现有 GAAP 示例。

| 表面 | 建议保持/调整 |
| --- | --- |
| 四资源 URI | 仍是 `ossie://population/{concepts,metrics,fields,capabilities}`，仅在该独立服务器内部使用 |
| concepts 内容 | 固定发布 `ossie_concepts_v2()`；验证能力声明、profile、API 2.1.0 与实际资源吻合 |
| 三工具 | 仍是 `population_objects`、`population_query`、`population_compile` |
| 输入与输出 | 沿用 JSON 编码字符串数组、typed result-v1 与现有错误契约；compile 解析 plan-v2 |
| 能力限制 | User 是 referent-only；仅日期/产品可分组；用户数是请求范围的存在式 distinct，AVG 分母是非空观察金额 |

URI 复用依赖单服务器隔离，客户端缓存键必须包含服务器会话及绑定哈希，
不能只按 URI 跨进程缓存。新示例启动时已知道 profile，固定选择 v2；
客户端仍核对 capabilities，而不是运行任意资源声明中的 SQL。
工具描述说明上述角色/分母/输出编码，但不写研究正确数字，不给示范
业务调用参数或固定调用顺序。编译不能冒充执行证据。

无模型 MCP 预检必须经过 stock CLI 和真实 stdio JSON-RPC，不用 SQL 单测
代替；记录 CLI、duckdb_mcp、扩展、示例、CSV、bundle 与定义哈希。依次核对：

- 独占三工具/四资源、禁用内置任意查询/执行/导出/描述等入口；失败启动不会服务。
- v2 的有序三组件、嵌套 User.id、User 不可查询、raw capture 8 观察/4 用户；
  四资源与成功执行响应绑定同一 snapshot/content/definition/manifest。
- objects 逐项 typed tuple 与五逻辑列完整对应独立 CSV；query 五指标、
  日期/产品分组、阈值 10/20；不得以相同数字代替身份对应。
- NULL-only C：1 观察/1 用户/0 金额，SUM/AVG null；空标量 counts 0、
  SUM/AVG null，空分组及 objects 无行；空响应仍有绑定哈希。
- compile plan-v2 与未执行状态；User 直接请求、非法分组、未注册指标、
  错误列表拒绝，后续合法请求恢复。编译/执行是独立绑定，逐次比较。

现有传输存在 native 数组可能被序列化后接受、额外参数可能忽略的已记录
行为；本方案不宣称 MCP 服务器严格执行 JSON Schema。模型循环仍只转发
发现白名单内的字符串参数。分页按原要求仅为今后研究，不纳入此次接入。

## 四个最小 Agent 场景

每场景使用独立 MCP 进程及新 Responses 会话；研究 CSV 不改变。提问只
规定用户需要的对象、范围和计量，不告诉答案、阈值、工具名或参数。
正式规则从发现资源获取；oracle 只供宿主验收，绝不发送模型。

| 场景 | 用户提问草案 | 宿主独立期望 |
| --- | --- | --- |
| 基准 | 按当前正式定义，2026-01-31 至 2026-02-01（含首尾，所有产品）的合格观察有多少条、涉及多少不同用户？金额非空有多少条、总额和每条非空观察平均金额是多少？说明单位和分母并引用依据。 | threshold10：6 观察、3 用户、5 非空；USD31.00、6.2 |
| 改规则 | 与基准完全相同的提问，启动 threshold20 的独立服务器 | 3 观察、1 用户、3 非空；USD17.00、17/3 近似值 |
| 区间投影 | 按当前正式定义，分别给出这两天每一天的不同用户数，以及整个区间的不同用户数；解释每日人数相加是否代表区间人数，并引用依据。 | threshold10：Jan31=1、Feb1=3、区间=3；每日相加4不是区间distinct |
| 能力边界 | 这个区间内每天都合格的用户有多少？请按“每天都合格”的口径回答并给出依据。 | unsupported；本 profile 未提供该跨日期实体条件，不以“至少一次合格”替代，不产生该口径人数；这是当前注册能力边界，不声称原始数据无法计算 |

第四项只测一个明确缺口，避免同时混合“期末”“活跃付费”“用户平均”
多个拒绝原因。用户均值/期末/日历补全保留在 SQL/MCP 拒绝与能力说明中。
八行数据不足以推断真实业务日历，不能自行将无记录日解释为不合格。

期望来自原始 [observations.csv](observations.csv)：SHA256
`bfd38a0130028fe5829aa45e40dce8ac12e228cc2cc61e2f08f293493764f441`。
本轮 Bun 临时只读重算按 bigint cents 加总、按原始 User 字符串集合计数，
得到 3100/500=6.2 和 1700/300=17/3，与既有独立 SQL/观察产物报告对应。
这不是 MCP 或真实模型执行证据。该金额是合成可加 USD，不是 GAAP/YTD。

## 答案与证据门禁

新驱动采用独立观察答案结构，避免旧 `count` 含义不清：
`status`、`scope`、`definition`、`unit`、`observation_count`、`user_count`、
`amount_value_count`、`amount_sum`、`mean_observation_amount`、`daily_user_counts`、
`evidence_ids`、`limitations`。数字为字符串，未请求/不支持的项为 null；
按日数据有明确日期键，顺序不构成语义。此结构用于可核对的记录，不规定
模型思考或工具调用顺序，也不要求调用 compile。

宿主分开记录四个结论，最终判定不能仅由 JSON/数值校验通过产生。System 一般性约束
只回答当前已注册的指标/口径，不用客户端重新计算能力外指标替代；
可从完整 objects 推导的五个已注册指标仍有效。此约束不会告诉模型
哪个场景应该拒绝，也不预先解释问题答案。

四项结论如下：

1. 来源完整：发现、工具实际请求/响应、最终回答与所有字节哈希可追溯，
   引用确实存在，执行与发现的六个绑定哈希相等。
2. 语义对应：选择的概念、正式规则、日期首尾、所有产品、group keys、
   五指标表达式和单位与提问一致；raw capture 8/4 不能当合格结果。
3. 计算正确：三类 count 独立比较；Decimal 保留准确小数，AVG DOUBLE
   对独立分数按明确 1e-10 绝对误差核对，显示舍入则记录位数与证据；
   null 不转 0，区间 User 不从每日/每产品人数直接相加。
4. 解释及边界：人工/Lead 语义审阅对象、范围、分母、单位、引用与拒绝，
   记录裁定者/日期/证据哈希。一次成功只证明这组限定实验。

新门禁必须把每个主张绑定到实际成功执行响应、实际请求和列/行；不可从
不同范围的响应各摘一个相同值拼成“支持”。同一范围的指标可以由多次
成功查询分别支持，第三场景也允许每日与区间两种范围的分别查询；逐项
关联证据，不要求整份答案出自单次 RPC。分组结果按明确日期/产品键
对应，不按行顺序。实际 filters 允许等价的范围表达式；无法由有界解析
确认时标为待语义评审，而不是猜测其等价或静默判通过。

模型可以通过直接聚合，也可以通过完整 objects 计算；后者只有全部 typed
身份/行与独立原始 CSV 对应、请求范围可核对、最终计算可复算才成立。
不从截断响应推断总数，不要求预制工具序列。unsupported 场景允许合理
探查后拒绝；仅要求无目标口径数值或替代解释，不能以零调用数作通过条件。

未来离线验收控制至少包括：错概念/同值但错误范围、跨行拼接、跨场景引用、过期哈希、raw counts 冒充
合格 counts、错分母、日人数相加、null 转零、伪引用/哈希变化，以及正常
直接聚合/同范围分次查询/完整对象推导/无序分组路径。旧验收记录和原始证据不改写。

## 分步实施与停止条件

下一实施工作包应只含独立示例、Bun MCP 预检、新观察驱动的 `--prepare`
及离线证据门禁控制，并跑现有两旧 profile 的回归。当前研究不实施它们。
`--prepare` 不应要求或保存模型服务配置，也不读取 `.env_test`。
无模型预检和离线门禁通过后再确定真实调用预算并取得该次运行授权；
不自动新增 CI matrix、推送或发布。

真实运行继续使用 `OPENAI_BASE_URL`、`OPENAI_MODEL`、`OPENAI_API_KEY`；
内部配置仅在 ignored `.env_test`。私有原始响应不可覆写，公开衍生记录
脱敏后绑定两者哈希，公开内容不冒充原始精确请求字节。保留失败。
复用当前每场景最多8轮、12工具调用、120秒、每次输出2048 tokens、
出站131072 bytes 限制。四场景最高32模型请求、48工具调用、65536请求输出 tokens，
这不是输入 tokens 或金额上限；服务费用与重复稳定性仍未知。
额外运行不得因一次失败自行扩预算。

本方案依赖已观察的 MCP 协议/构建，不主张当前最新 MCP 或全协议符合性。
如预检发现 v2 内容编码、绑定或描述与实际行为不一致，先归档失败并修复
本工作包，不能靠增加模型提示掩盖接口错误。

## 有界反证与 Lead 决定

复用已有 `review_agent_acceptance` 一轮只读 Advisor，无写入或嵌套委派；
requested Astra/high，有效 backend/effort、usage、billing 均 unknown。
Advisor 支持隔离示例/driver，指出旧按值搜索的跨行/响应风险，强调
“期末”可能只是合法末日过滤、每日交集/用户均值也可能从完整 objects
数学计算，不能把注册能力缺失说成数据不可计算；并要求允许同范围分次
查询、明确四场景预算和预检隔离。Lead 已纳入这些边界。

Advisor 建议第四场景可换成用户期间总额的平均；Lead 选择固定“每天都
合格”，与本次存在式期间投影的能力界限直接对应，且只测这一缺口。
判定限定为遵守当前已注册契约，不断言数据本身不可用于交集计算。
本轮未验证实际新 MCP 传输、模型能否遵守约束或新门禁实现，均须留待
下一实施/实验；不将 Advisor 建议等同验收。

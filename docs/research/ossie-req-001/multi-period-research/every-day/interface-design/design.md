# 每天合格 User 集合：最小接口与能力设计

2026-10-11，OSSIE-REQ-001，基线55531fe。用户已接受[集合口径](../research.md)，
接受裁定独立保存在[acceptance-decision.json](acceptance-decision.json)，不改原研究或原始输出。
**本文件是待评审设计，未实现、未注册、未获实施授权。** 当前API仍2.1.0，
真实Agent第四场景仍应拒绝；普通SQL研究oracle不能代替产品能力。

## 接口与结果

建议新增专用入口，不改现有区间 `user_count`（期间至少一次合格）、
通用metrics/objects/compile、User referent-only属性或旧两个profile：

```sql
-- 未来接口草图，当前不可执行；concept取本批次能力声明的已注册派生观察概念。
SELECT result_json FROM ossie_daily_users_json(
  'QualifiedObservation',
  '{"start_date":"2026-01-31","end_date":"2026-02-01","products":{"mode":"all"}}'
);
```

签名为 `(concept VARCHAR, request_json VARCHAR)`，返回一行一列VARCHAR
`result_json`；一次调用只查询一个concept。SQL NULL拒绝。采用JSON字符串保留
参数原类型，避免DATE形参在回调前隐式截断TIMESTAMP。第一版只允许观察profile当前正式规则对应的已注册 `QualifiedObservation`，不接受根观察/User、任意派生表达式或阈值覆盖。
能力声明从本bundle注册记录列出这个唯一支持concept；不扩展为任意派生概念。

MCP新增单一工具 `population_daily_users`，两项必填字符串参数
`concept`、`request_json`，复用专用SQL入口。工具描述必须同时写明：首尾含自然日、
每天至少一条范围内合格见证、可跨日换产品、缺失不入集合、当前同一规则、
名单完整但不证明现实业务数据完整。外层多余参数应由工具边界明确拒绝，
不能依赖现SQL模板忽略参数；内层请求始终由入口验证，不能声称MCP已全面校验schema。
不增加通用SQL、导入、reload、export或用户自定查询权限。

[request.schema.json](request.schema.json)是结构草图，必填start_date/end_date/products，
不允许额外键。日期严格四位年ISO日期，支持0001..9999且必须是真实Gregorian日期；
此为最小接口范围，BC/五位年份等即使是有限DuckDB DATE也明确拒绝。首尾含、
start≤end，不允许默认日期/时区/观察日期推断。`products`二选一：
`{"mode":"all"}`，或 `{"mode":"subset","values":["X"]}`。
subset非空、成员为非空原始字符串、重复值拒绝；不trim/casefold，按现binary身份比较。
未知字符串Z合法并可能返回空；NULL/空列表不能转all。原研究cases.json中null表示all
仅是研究文件约定，不能照搬成接口默认。JSON对象重复键、尾随值、非JSON数值明确拒绝；
解析前须能检出重复键，不能等JSON对象已丢弃重复键后才检查。

成功采用独立 `ossie-daily-user-set-result-v1`，不能向现result-v1塞新request_kind。
固定必填字段如下；本轮不新增生产decoder或返回已执行示例：

| 字段 | 约定 |
| --- | --- |
| schema_version / request_kind / executed | 新schema / daily_users / true，仅实际成功时产生 |
| concept / user_concept | 资格派生观察概念及其实际引用User名称 |
| request | 原日期；all或按原始UTF-8字节排序的subset；不得偷偷缩小范围 |
| required_day_count | 日期跨度十进制整数字符串，1..366；不是金额分母 |
| members | 完整raw User字符串数组，原始UTF-8字节升序，无重复，不输出观察身份 |
| member_count | 十进制整数字符串，必须等于members长度；空集为"0"和[] |
| eligibility_semantics | 固定 `every_calendar_day_exists_qualifying_observation` |
| qualification_rule | 已绑定的正式规则标识与表达式，从实际ontology读取；非请求阈值 |
| profile / snapshot_id / content_sha256 / core_sha256 / ontology_sha256 / mapping_sha256 / manifest_sha256 | 与此次执行同一冻结bundle的既有追溯值；不借用前一次发现响应 |
| paging / truncated / complete | false / false / true，complete仅指请求集合完整返回 |

整数沿现typed carrier习惯用文本；request_kind/schema独立使旧decoder继续拒绝陌生结果。
完整名单可独立验证交集；人数不是每天人数之和，也不是观察数。排序只约束新入口，
不改变原结果无序约定。结果不含逐日见证明细、比例、金额、分页或count-only开关。

## 能力与版本

候选成功实施后API升级到2.2.0，现discovery-v1、concepts-v2、plan-v2及原
result-v1/error-v1保持。这里是本项目接口，不是Ossie0.2或SQL2026新增标准。
扩展 `ossie_population_capabilities` 的capabilities_json，新增独立
`daily_user_set`对象；[capability.draft.json](capability.draft.json)明确标记设计未启用。
未来运行时声明需要包含enabled、SQL函数/签名、MCP工具、请求/结果schema、
**本bundle实际支持的唯一concept清单**、固定量词/范围、排序和各项数值预算。
不把全局time改成支持一般calendar，也不把User改成queryable。

SQL能力与MCP工具可用性分开：SQL资源声明本身不能证明stdio示例已注册/运行。
未来观察示例只有对应SQL确实支持时才注册工具；旧两profile声明enabled=false、
concept清单为空且不注册该工具。错误profile直接调用SQL拒绝。客户端应检查
具体operation/concept/result版本，不能仅凭API≥2.2或名称存在推断可用。

一次调用在bind时获取同一bundle并持有至完成；prepared保留旧bundle，reload失败
保留旧bundle。多个RPC间不保证快照原子性；客户端必须核对实际结果六项追溯，
不能用旧发现证明新结果。规则对全部日期相同，不解释为历史日期有效规则。

现有能力 `mcp_example.result_schema` 对旧三个工具的result-v1声明继续成立，
新增operation→schema映射单独列出daily_users，不能让旧decoder接收新格式。
现工具protocol/missing参数等外部错误仍不自动转成population错误。

### 错误兼容设计

沿用 `ossie-population-error-v1` 的字段、framing和两种MCP错误carrier。
新增 `stage=resource` → `RESOURCE_LIMIT_EXCEEDED`，只用于本操作已声明预算
超限（请求、跨度、产品数量/字节或完整结果）；object标识具体预算，例如
`daily_user_set.result_json_utf8_bytes`，reason给出上限与可用的实际计数，
不把整个超大请求/成员串回显到错误。结构/日期/重复键等非法请求为
`INVALID_REQUEST`；不支持的profile/concept/语义为 `UNSUPPORTED_CAPABILITY`。
额外的limit等未声明参数属于非法结构，不能用一个键表达未支持语义然后忽略它。
执行失败沿原 `EXECUTION_FAILED`；身份/加载失败仍沿原阶段；原生取消/OOM/
internal/fatal不改标成资源预算或统一错误。无成功envelope可供继续使用。

新增码是API2.2.0的可恢复错误类别增量，不更改旧调用错误映射。
本仓库 `scripts/population_error.ts` 检查code为字符串，没有封闭枚举，
可保留此新码；这不保证外部客户端兼容。实施时须补参考控制并写明未知码
仍是失败、不可当空成功；若外部协议要求封闭枚举则需重新评估版本，不能静默发布。
1MiB成功预算不约束错误消息、SQL位置或MCP外层，不能因此承诺全链路总字节。

## 资源预算及执行边界

以下数字是**建议固定的第一版保守门槛**，不是性能实测结论或上游标准。
实施前必须用边界fixture证明守卫生效，不能先在运行时声明已保证。

| 资源 | 候选限制与拒绝点 |
| --- | --- |
| 冻结输入 | 沿已有全批10,000行/256物理列/16MiB canonical输入；不能先按请求过滤再规避载入或全批tuple重复校验 |
| 请求文本 | 16,384 UTF-8字节，入口收到的完整request_json文本（含空白/转义）；解析前检查，不包括SQL字面量外层或MCP再次转义 |
| 日期 | 首尾含最多366日；非法日历/倒序先拒绝，超过预算整体拒绝；不生成User×日期笛卡尔积 |
| subset产品 | 最多64个不同值，每值解码后最多256 UTF-8字节；字节不是字符数，保留原字符串包括转义字符 |
| 执行中间集合 | unique(user,date)≤全批观察10,000；最终成员≤全批User≤10,000；逻辑去重，不重复计日 |
| 成功结果文本 | 完整内部result_json最多1,048,576 UTF-8字节，包含所有metadata、标点和JSON转义；增量编码前检查剩余容量，禁止先生成无界字符串再测量 |
| 时间/取消 | 不宣称固定完成时限；执行与编码设置协作取消检查，沿原生interrupt/drain语义；当前客户端60秒等待不是服务端执行deadline或自动取消保证 |
| 内存/传输 | 输入/结果字节限额不是进程RSS、分配开销、峰值或外层MCP JSON-RPC线长的上限；MCP嵌套转义另增字节，当前无已验证全链路wire上限 |

366日容纳含闰日的年跨度；64/256/16KiB/1MiB用于限制第一切片的枚举和完整返回，
没有吞吐/延迟基准支撑“足够快”。三个输入门槛独立同时满足：64个256字节产品
连同结构/转义可能超过16KiB，仍须拒绝；64不是保证任何最大长度组合可接受。
不限制现载入User ID为短字符串；长ID或长规则/trace导致结果超预算时整个请求拒绝，
不能截断ID、删metadata、只报人数或暗中分多次RPC。全批空、没有合格用户为正常空成功。
没有服务端deadline与MCP总报文守卫的保证应在能力资源中显式false/unknown，
不能把单请求内部文本上限说成网络、Agent总预算或任意并发的保证。

最小算法设计：在全批已验证身份和资格规则上按日期/产品选出TRUE观察，
去重(raw User, DATE)，仅当用户不同日期数等于required_day_count才入集合。
连续且有界的DATE使此条件等价于已接受的每日交集。禁止把COUNT观察行替代COUNT日期，
或从观测到的日期数反推分母。此算法草图尚未编译/执行，普通SQLoracle仍独立保留。

## 支持、拒绝及验收

| 请求 | 第一版设计行为 |
| --- | --- |
| 单日、连续多日、all/非空subset、跨日换产品、未知Z、缺失日、全NULL分类或空捕获 | 接受范围；无见证返回完整空集，NULL金额不影响资格 |
| 时间戳/时区转换、非连续或工作日日历、隐式日期、BC/宽年份、非正式阈值 | 明确拒绝，无默认缩范围 |
| 同一产品每天、每个产品每天、全部观察都合格、历史/as-of/修订、多个批次混用 | 不支持，不能改用现user_count或只比较每日人数 |
| 任意ontology/concept、根User查询、多个事实/join、金额/比例/分组/自定SQL、limit/offset/分页 | 不支持；沿原issue后续范围，不在本包增加其他查询模式 |
| 全批重复/身份失败、请求或结果超预算、取消/执行失败 | 没有部分成功、没有success+truncated；失败后验证原bundle仍可读 |

最小实施验收仍沿OSSIE-REQ-001，不另建backlog：复用12个研究请求，逐成员与
独立SQL/集合oracle核对；分别覆盖单日、missing Feb2、switch E、NULL G/H、Z、空捕获、
正式规则20、原八行及三个替代量词。新接口必须拒绝9个原未来负例及上表模式，
旧user_count继续期间存在语义，两个旧profile全部既有回归保护。

资源控制须分别在上限/上限+1验证：366/367日（含闰年与跨月）；64/65产品；
256/257解码UTF-8字节（含汉字/转义）；16KiB完整请求；1MiB完整编码结果
（长ID、控制字符/引号、metadata膨胀、无成员但长规则）。不要用仅字符数替代字节数。
再检验重复JSON键、重复产品、NUL等raw字符串按现身份长度处理、全批范围外重复、
prepare/reload/取消/drain恢复、错误两carrier、工具与能力实际注册一致；取消仍原生。
需要同产物native/SQL/stdio回归后才修改Agent第四场景与新result decoder，
先无模型演练；任何真实模型运行另行确定范围。分页、硬deadline/进程内存/外层报文
研究及main合并/发布仍为现计划后续，不随本设计启动。

本轮验证仅限文档/结构例子/既有研究文件完整性；schema接受不能证明日期语义、
字节守卫、实际执行、MCP可用性或业务正确。下一步先审阅这一设计与预算，
若用户再授权实施，先落独立SQL请求/结果及资源守卫，验收后再接MCP和Agent。

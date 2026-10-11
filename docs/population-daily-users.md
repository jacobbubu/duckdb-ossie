# 每个自然日有合格见证的 User 集合

`ossie_daily_users_json(concept, request_json)` 是项目API2.2.0的专用SQL入口，
仅支持当前 `daily-observation-population-v1` 的正式派生观察概念（示例
`QualifiedObservation`）。它返回一行VARCHAR `result_json`，完整成员集合
与人数均指每个指定自然日都有至少一条范围内合格观察的User。产品可跨日更换；
同日其他FALSE/NULL观察不抵消TRUE；NULL金额不影响资格。未入集合只表示
缺少见证，不证明现实用户不合格或数据完整。原 `user_count` 仍指期间至少一次合格。

```sql
SELECT result_json FROM ossie_daily_users_json(
  'QualifiedObservation',
  '{"start_date":"2026-01-31","end_date":"2026-02-01","products":{"mode":"all"}}'
);
```

必填日期严格为真实公历 `YYYY-MM-DD`，年0001..9999，首尾包含且start≤end。
全部自然日均为必需日期，不根据观察行删日期/周末/假期，不截断TIMESTAMP或猜时区。
产品必须显式 `{"mode":"all"}` 或 `{"mode":"subset","values":["X"]}`。
subset非空、不重复、只含非空原始String；不trim/casefold。未知Z合法且可返回空。
SQL/JSON NULL、额外字段、对象重复键（包括解码后同名）、非法日期、尾随JSON均拒绝。

成功schema为独立 `ossie-daily-user-set-result-v1`，包含：`request_kind=daily_users`、
`executed=true`、concept/user_concept、规范化request、required_day_count和member_count
十进制整数字符串、完整members、固定eligibility_semantics、qualification_rule（concept及
实际declared_sql）、profile及六项既有快照/定义追溯。members与subset按原始UTF-8
字节升序；保留Unicode、大小写、NUL及控制字符身份，并正确JSON转义。
空集合 `members=[]`、`member_count="0"`；paging/truncated=false、complete=true
仅表示请求集合完整返回。不能将此结果送入旧 `ossie-population-result-v1` decoder。

请求绑定并持有同一冻结bundle；同一当前正式规则应用于全部日期，prepared保留旧规则/
快照，失败加载不替换旧批次。全批typed(user,date,product)重复校验仍在加载前执行，
不能通过请求过滤隐藏范围外重复。源表更新需显式reload，多次调用间无快照原子保证。

| 限制 | 精确定义 |
| --- | --- |
| 输入 | 沿原全批10,000行/256物理列/16MiB canonical输入；不是峰值内存限制 |
| 请求 | 收到的完整request_json最多16,384 UTF-8字节，含结构/空白/转义，解析前检查 |
| 日期 | 首尾含最多366个连续自然日 |
| subset | 最多64个不同产品，每个解码后最多256 UTF-8字节；all不限制源产品域为64 |
| 中间/成员 | unique(user,date)及最终User不超过10,000，无User×日期扩展 |
| 成功结果 | 完整内部JSON最多1,048,576 UTF-8字节，包含metadata/标点/字符串转义；增量编码追加前检查 |

各限制同时满足，64个256字节值连同结构可能超过16KiB请求。合法载入的长User ID
仍可能使结果超预算；整个请求以error-v1 `stage=resource` /
`RESOURCE_LIMIT_EXCEEDED`失败，不截断、不转count-only或暗中分页。此上限不约束
错误消息、MCP外层转义报文、进程RSS/峰值、并发总量或完成时间。原生interrupt/
internal/fatal/OOM继续原生传播，不冒充预算失败。代码有执行逐行、编码每1024输入字节
及完成检查；本轮确定性取消证据只覆盖执行前/编码前与恢复，不证明所有中途阶段。

能力发现 `daily_user_set` 单独声明SQL启用、实际唯一concept、结果schema、限制及
未保证项。旧两个profileenabled=false，直接调用此SQL拒绝；User仍referent-only。
`ossie_daily_users_compile(concept, request_json)` 返回一行VARCHAR `plan_json`，
schema为独立 `ossie-daily-user-set-plan-v1`。它共用相同请求校验和冻结bundle绑定，
给出实际绑定的user/date/product类型、资格投影AST文本，以及有序逻辑步骤：TRUE资格、
包含首尾的日期过滤、产品范围、去重(user,date)、日期数等于必需自然日数、原始字节排序、
完整JSON预算。日期、规则、范围及追溯字段与执行出口一致。

```sql
SELECT plan_json FROM ossie_daily_users_compile(
  'QualifiedObservation',
  '{"start_date":"2026-01-31","end_date":"2026-02-01","products":{"mode":"all"}}'
);
```

这是绑定后的逻辑解释，不是物理计划。`executed=false`、`members_computed=false`；
不运行资格投影或Fetch成员，但内部连接配置及Prepare绑定仍发生。
`input_projection_binding=true`不等于最终输出绑定、资源充足或执行成功；成员过大时
compile可成功而execute失败。投影SQL不包含C++归约，也依赖内部冻结AST，不能直接重放。
plan完整JSON同样按增量编码限制在1,048,576 UTF-8字节内，超限以
`daily_user_set.plan_json_utf8_bytes`资源错误拒绝。当前实测最大16KiB合法规则的完整解释；
尝试用更长规则触发plan上限会先被既有loader拒绝，未宣称精确1MiB plan边界实测。

能力声明 `compile_supported=true` 与独立plan_schema。旧compile继续解释对象/指标。
专用[每日MCP示例](../examples/daily_user_server.sql)新增两个工具：
`population_daily_users`与`population_daily_compile`。它独立于原观察服务，
默认使用23行研究观察，当前阈值10；operator可选择已审查的阈值20。
五个资源包含原概念/指标/字段/SQL能力及 `ossie://population/daily-surface`。
最后一个明确此服务只暴露两个每日工具；SQL指标/对象可用不等于MCP工具存在。
Agent无导入/reload/任意SQL工具；原观察服务三个工具和每天场景拒绝保持。

参数为concept与request_json两个String，范围全在request_json。固定上游
[6f9c4ba的参数绑定](https://github.com/teaguesterling/duckdb_mcp/blob/6f9c4ba1d2f1040c2a4638dabcd66da630a763ad/src/server/tool_handlers.cpp#L675)
只按schema声明参数绑定，未知外层参数忽略，String参数可能从对象转为JSON文本；
已实际验证对象request与字符串的scope一致。不能声称外层完整JSON Schema严格
执行或additionalProperties=false生效，调用方必须发送String，禁止把filters或products
单独放到外层期待筛选。内部request_json依然由native拒绝额外字段/重复键/坏日期/
数字产品，missing/null/numeric request亦拒绝；MCP protocol error与native envelope
分开解码。SQL/资源声明中的 `mcp_example_registered=false`表示原生不检查活动服务，
本服务实际注册以tools/list/daily-surface为准。

专用[Bun decoder](../scripts/daily_result.ts)验证carrier、版本、完整性、成员数整数字符串、
原始UTF-8排序/唯一身份、正常日期/产品范围、类型/七步plan及追溯字段；不复用旧
population-result decoder。它验证结构并比较compile/execute绑定，不证明数据真实或
业务规则适当。使用普通JSON.parse，不声称检测任意外部carrier的重复对象键；native
请求的重复键拒绝已有实测。输出1MiB限制指内部JSON，MCP外层转义可能更大。
新工具已有12请求真实stock stdio与手列成员对照、解释/执行追溯及28拒绝/恢复，
[证据](research/ossie-req-001/p1-daily-mcp-controls/verification.json)另记；新每日支持式无模型演练已有[独立receipt](research/ossie-req-001/p1-daily-offline-controls/final-receipt.json)：
五个新MCP进程分别检查all/X/缺日/规则变化和compile-only；每个loop独立上下文。
scripted参数固定，答案从实际工具输出构造，完整成员另与手列oracle比较；预期答案
不进上下文。compile-only记为not_executed，不称能力不支持；不允许据计划猜人数。
共享Agent dispatcher比固定上游更严格，外层未知参数和非String在RPC前拒绝。

模拟请求14次、实际工具调用9次、真实模型0次；原始事件/上下文/出站body留在
ignored私有0700/0600目录，使用既有observation-archive-v1哈希容器及独立daily run
契约，不能当原观察四场景验收。离线重放重新解析公共Responses投影、消费完整
provider/tool/RPC顺序及引用、规则、范围、trace；不启动进程或调用模型。18种重新
封存的伪记录仍拒绝，incomplete独立封存且不能完全重放；哈希容器不证明真实性。
公开只给选择性receipt，不是原始transcript或wire。模拟证明接线和审计，不证明模型
理解/自主选参或真实业务。每loop使用既有round/call/output/input-byte/时间限制，
discovery仍有独立RPC读超时，不声称整个进程硬deadline/内存或费用上限。

CI已接每日SQL/plan、MCP及五场景模拟/重放命令，尚无远端运行证据。本入口不是一般calendar、窗口、历史规则/as-of、多事实、非递归关系
或完整P1。

[已接受语义研究](research/ossie-req-001/multi-period-research/every-day/research.md)、
[原设计](research/ossie-req-001/multi-period-research/every-day/interface-design/design.md)与
[本地SQL增量证据](research/ossie-req-001/p1-daily-sql-controls/verification.json)及
[逻辑解释验证](research/ossie-req-001/p1-daily-plan-controls/verification.json)分别保留。
本能力为项目接口，不宣称Ossie0.2/SQL2026规范新增了一种专用函数。

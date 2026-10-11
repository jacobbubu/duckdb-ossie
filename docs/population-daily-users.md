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
**当前未接新MCP工具、专用compile/plan或Agent场景**，能力分别声明
`mcp_example_registered=false`、`compile_supported=false`、`plan_schema=null`。
旧compile解释对象/指标，不解释C++日期/产品/跨日归约，不能借其SQL冒充此操作。
现观察MCP/Agent每天场景仍拒绝，后续接入必须单独实际验收；本入口不是一般calendar、
窗口、历史规则/as-of、多事实、非递归关系或完整P1。

[已接受语义研究](research/ossie-req-001/multi-period-research/every-day/research.md)、
[原设计](research/ossie-req-001/multi-period-research/every-day/interface-design/design.md)与
[本地SQL增量证据](research/ossie-req-001/p1-daily-sql-controls/verification.json)分别保留。
本能力为项目接口，不宣称Ossie0.2/SQL2026规范新增了一种专用函数。

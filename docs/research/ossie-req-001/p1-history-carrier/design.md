# 有限历史关系实施承载

已接受的有限有效期、全批异目标冲突和来源身份政策落实为独立
`historical-observation-relation-v1` 的固定承载设计。三份文件通过固定离线 schema，
见 [fixed-schema.json](fixed-schema.json)；这仅证明文件结构接受。随后
[typed 双源加载/索引/program](../../../population-history.md)已本地实施与验证，
历史 link/compile/MCP 仍需实现。沿 OSSIE-REQ-001 和现计划推进，不另建 backlog。

## 类型、来源与算子

[fixture manifest](../../../../test/fixtures/population-history-program/bundle.json)
声明两份 core dataset：`observations` 与 `history`。前者的实体是
`DailyObservation`，身份为 `(User,date,product)`；后者是 `MembershipRecord`，
身份为显式 `record_id`，单独映射 User、Team、starts、ends。User 与 Team
分别声明 EntityType、一个 String 身份；恰好相等的字符串不表示两个概念相同。

`program.operator=history_at_observation` 是本项目执行算子，不是上游标准。
program 选择两个 dataset/concept 与唯一 `team_at_observation` 关系。该关系
在观察实体下声明为 `ManyToOne`、末角色 Team；无额外直接 link mapping，也无
另一个 ontology derived_by。其他对象与关系必须完整按名称映射。非空而未执行的
derived_by/requires 一律拒绝，不能忽略；不使用任意 SQL 或编造上游隐式变量语义。
既有关系 profile 继续执行其已支持的 ontology 非递归规则，本 profile 不替代它。

固定角色名为观察 `user/date/product`、历史 `record_id/user/team/starts/ends`、
User/Team `id`。日期双角色须按名称解析，不能按 Date 类型或字段顺序猜。
物理 primary_key 与对象映射对应；绑定确认实际 VARCHAR/DATE 类型、字段/角色
及精确可直接映射列。来源是两个不同的已提交本地 native 表，同数据库同源事务。

## 完整捕获与执行

两来源各最多 10,000 行/256 物理列，共同 canonical 最多 16 MiB；合计最多
20,000 行。包含未被查询选择的行/列，不能靠日期/产品过滤隐藏源冲突。观察身份、
历史 record_id、User、Team 均非 NULL/非空且保留原始 UTF-8，重复完整观察身份或
全局 record_id 拒绝，包含相同值重复。历史两端均为有限 DATE，starts<ends。

有限 DATE 指 DuckDB 的有限日期域，包括 BC 和超过四位年份；不得套用每日状态
的 0001..9999/366 日限制。首点包含、末点不含，末点相同的不同目标交接合法。
最大有限日期没有更大的有限 exclusive end，不可擅加无限端点覆盖。观察日期
同样必须有限，查询不会截断 TIMESTAMP 或猜时区。

原始双源 collection、record_id、行数与 canonical hash 保留。派生索引先按
User/Team 合并重叠或相接区间，再按 User 排序；任何剩余异目标覆盖重叠拒绝全批，
包括没有观察的 User。不能只比较未合并记录的相邻行：同目标长区间会掩盖冲突。
不得跨缺口合并。索引最多历史行数个段、端点最多 20,000，索引不是原始来源替代品。

每个已捕获观察用原始 User/date 在有界不可变索引中查目标，避免构造
观察×历史中间 bag。缺口不产生 link；同目标不同 ID 的见证保留来源，最终按完整
`[user,date,product,target_id]` 输出关系集合，不能把 record_id 加进 link 键。
同一天两个产品保留两条观察根链接。每观察至多一个目标，完整输出最多 10,000 条，
该边界可由 10,000 观察和一条历史记录实际达到。

## 后续接口与验证必要项

实施采用独立 history JSON/compile/program 接口，具体参数/成功 schema 随 native
切片冻结。查询日期/产品仅筛观察，载入冲突政策不受它们影响；全日期请求须能
处理全部有限 DATE，不能只有四位年份过滤路径。完整 link 的 Date 使用实际
DuckDB canonical date-text，排序按日期值而非字典序；既有 relationDate helper
支持有限域，可复用但须用新真实产物控制核对。

承载核对发现并修复既有 decoder 的孤立下界错误，见
[有限日期边界证据](finite-date-controls/verification.json)：DuckDB 的 ±INT32_MAX
是 infinity，而 INT32_MIN 对应 `5877642-06-23 (BC)` 且有限。旧原生关系完整返回
该日期，旧 decoder 拒绝；修正后五个有限日期实际输出与 epoch 日值一致，两个
infinity 哨兵和越界前一日仍拒绝。138 Bun/723 expects、既有关系/多事实 MCP、
类型/格式通过，原失败/修复证据保存。这里只验证边界/样本，不是枚举所有日期，
也不证明尚未实施的历史算子。

请求完整原始 JSON 最多 16 KiB，响应/解释/发现完整内层 JSON 最多 1 MiB，
metadata、转义、结构计入；subset 产品沿既有 64 个/每项 256 UTF-8 字节。
无分页、截断或 count-only 回退；数字不是 RSS/并发/完成时间承诺。compile 只绑定
实际投影和载入索引元数据，不执行匹配或算链接，不冒充物理/可重放 SQL 计划。
父 hash 绑定两份原件、声明与实际政策，prepared 持有同一父/双源；失败、第二源
捕获失败、冲突、超限或取消均不替换旧批次。新 profile 必须加入私有 binary Engine。

必要验证包括完整独立枚举 link oracle；起点/终点、相接/嵌套冲突、同目标重复和
缺口、无观察冲突、重复 ID、角色交换/类型错配、两个产品、大小写/NUL身份、
有限 DATE 极值；两源顺序/hash、同事务并发、第二源失败、prepared 跨 reload/drop、
各阶段取消恢复、10,000 完整链接与 JSON/请求字节边界。随后补真实 MCP、严格
解码/同绑定核对、无模型/封存重放、旧五 profile 回归、Linux/tidy 和全 P1 审计。

本轮复用一个只读 advisor-escalation，一次反证及一次澄清。采纳独立 profile、
观察复合身份、明确项目算子、原件/索引分离、角色名、全有限 DATE、私有 binary
及可达 10,000 输出边界；Lead 核对固定 schema 和现有 capture/binder 代码。
请求 Astra/high；实际 backend/effort/usage/billing 未知，Advisor 没有运行测试。
没有新增业务待答问题或收缩原 P1，下一必要行动是原生实施，不推送或调用模型。

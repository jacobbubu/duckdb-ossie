# 历史关系双源加载与索引

`historical-observation-relation-v1` 已实现 typed 双源加载、完整历史校验、冻结索引与
`ossie_history_program()`。当前尚无历史 link 查询、compile 或 MCP 接口；program
明确 `query_supported=false`、`compile_supported=false`、`mcp_registered=false`，
加载成功不等于历史查询验收完成。完整 P1 仍需后续实施与跨环境验收。

[fixture](../test/fixtures/population-history-program/bundle.json) 有两份数据集：
观察实体按 User/DATE/product 标识，历史实体按全局 record_id 标识。User 与 Team
是不同的 EntityType，各有 String id。历史记录的 user/team/starts/ends 按明确关系
名及完整 object/link mapping 绑定，两个 DATE 不按位置或类型猜；实际字段必须为
VARCHAR/DATE，与声明类型对应。每个角色映射独立物理列，观察与 record_id 的
physical primary_key 顺序须与实体身份一致。

Manifest 的 `history_at_observation` 是项目算子；关系声明为 Observation→Team
ManyToOne。此 profile 不执行上游通用 derived_by：非空而未执行的规则/约束与另一个
直接生成关系映射拒绝。固定三份文件通过上游离线 schema，program 属本项目扩展，
不能把 schema 接受理解为执行了所有 OSSIE 表达式。

```sql
CREATE TABLE observations(user_id VARCHAR, ds DATE, product VARCHAR);
CREATE TABLE history(record_id VARCHAR, user_id VARCHAR, team_id VARCHAR,
                     starts DATE, ends DATE);
INSERT INTO observations VALUES ('u', DATE '2024-02-29', 'X');
INSERT INTO history VALUES ('a', 'u', 'T', DATE '2024-02-28', DATE '2024-03-01');
CALL ossie_bundle_load('test/fixtures/population-history-program/bundle.json');
SELECT program_json FROM ossie_history_program();
```

同一源事务捕获两个不同的已提交 native 表，TEMP/view/external/generated/collated
字段等沿现捕获限制拒绝。来源各最多 10,000 行/256 物理列，共同 canonical 最多
16 MiB，含全部行列与元数据；超过上限整体拒绝，不截断。加载输出 batch_rows 是
两来源行数合计，不是用户人数或链接数。program 分别给出源行数/hash、原始历史
record 数、派生 index 段数和历史 User 域计数；这些是载入元数据，未执行查询。
program 内部完整 JSON 仍限 1 MiB，包含转义/元数据；限制不承诺 RSS、并发或时间。

原始观察身份、record_id、User 和 target 必须非 NULL/非空，保留大小写、Unicode、
NUL 和原始字节；重复完整观察元组或全局 record_id 均拒绝，包含完全相同的记录。
历史两端和观察日期必须有限 DATE，start<end。有限域包括 BC、多位年份和合法
INT32_MIN 日，不缩为四位年份。有效期 `[start,end)`，起点包含、末点不含。
最大有限日期没有更大的有限 exclusive end；不补无限端点或默默延长覆盖。

完整历史中同 User 的不同目标区间重叠拒绝整批，包含没有观察的 User；查询不能
过滤隐藏冲突。不同 ID 的同目标重叠保留原始捕获，派生索引合并连续覆盖并保留
缺口。原始 collection/hash 不被索引替换。父 hash 绑定双源及定义，索引段数最多
历史行数，查找不构造观察×历史 bag。完整 link 去重及查询执行仍待下一切片。

候选全部成功后才原子替换父快照。第二源失败、冲突、超限或两处确定性取消均
保留旧状态；并发修改两表在一个事务提交不会令本次双捕获混合新旧。prepared 的
program 保留旧父，重载及删两表后仍可读取旧元数据；该证明不能外推为尚未实现的
历史 link 执行正确或所有引用释放后的无泄漏证明。私有 binary Engine 不改调用者
设置。旧五 profile 保持原行为；新 profile 的旧通用人口入口明确拒绝，不给错误声明。

[加载与生命周期证据](research/ossie-req-001/p1-history-load-controls/review.md)包含两源
各 10,000 行完整载入/各自多一行拒绝、失败保留、原生取消、同事务及 prepared 控制。
当前仅 macOS ARM64 本地通过；新代码 Linux/tidy、远端 CI 和完整 P1 尚未完成。

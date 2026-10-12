# 历史关系双源加载、查询与解释

`historical-observation-relation-v1` 已实现 typed 双源加载、完整历史校验、冻结索引、
`ossie_history_program()`、`ossie_history_links_json()` 与 `ossie_history_links_compile()`。
program 明确 `query_supported=true`、`compile_supported=true`、`mcp_registered=false`。
历史独立 MCP 与七场景无模型演练/严格重放已本地验证，完整 P1 仍需跨环境及统一验收。

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
SELECT result_json FROM ossie_history_links_json(
  'team_at_observation', '{"dates":{"mode":"all"},"products":{"mode":"all"}}');
SELECT plan_json FROM ossie_history_links_compile(
  'team_at_observation', '{"dates":{"mode":"all"},"products":{"mode":"all"}}');
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
历史行数，查找不构造观察×历史 bag。查询返回完整 `[User,DATE,product,target]`
关系集合；不同 record_id 的同目标重叠只产生一条完整 link，缺口不产生 link。

查询日期支持 `{"mode":"all"}` 或 `{"mode":"range","start_date":"2024-02-28",
"end_date":"2024-03-01"}`，范围两端包含，日期须为有限 DATE 的规范文本。产品支持
all 或 `{"mode":"subset","values":["X"]}`，最多 64 个不同非空值，每值最多 256
UTF-8 字节。日期/产品只筛观察，不筛掉已载入的历史冲突；不根据前日观察补行。
原始 request_json 最多 16 KiB，重复/未知字段与 SQL NULL 拒绝。完整结果最多
10,000 links、1 MiB UTF-8，字节含转义、元数据与请求范围；超限整体报错，不分页、
不截断。按原始 User 字节、DATE 值、产品与目标字节排序。

compile 只绑定冻结观察的 VARCHAR/DATE/VARCHAR 投影，不扫描来源、查找目标或
计算 link 数；输出逻辑算子、限制及与查询一致的范围/六项追踪值，不是物理计划
或可重放 SQL。计划 JSON 同样限 1 MiB。program 的源行数、历史 record 数及 index
段数不能替代查询的 link 数。

候选全部成功后才原子替换父快照。第二源失败、冲突、超限或两处确定性取消均
保留旧状态；并发修改两表在一个事务提交不会令本次双捕获混合新旧。prepared 的
program、links 与 compile 保留旧父，重载及删两表后仍可读取原先完整结果/计划；
取消查询后可恢复。这些证明不代表所有引用释放后的无泄漏证明。私有 binary Engine 不改调用者
设置。旧五 profile 保持原行为；新 profile 的旧通用人口入口明确拒绝，不给错误声明。

[加载与生命周期证据](research/ossie-req-001/p1-history-load-controls/review.md)包含两源
各 10,000 行完整载入/各自多一行拒绝、失败保留、原生取消、同事务及 prepared 控制。
当前仅 macOS ARM64 本地通过；新代码 Linux/tidy、远端 CI 和完整 P1 尚未完成。

[查询与解释证据](research/ossie-req-001/p1-history-links-controls/review.md)另以新产物
核对逐记录枚举的完整关系、有限日期极值、10,000 完整 links、精确字节预算及
prepared/原生取消。旧加载报告保留当时的 SHA 和接口状态，不改写历史证据。

独立 [MCP 示例](../examples/history_server.sql) 由操作者加载两来源，再公布两个只读
工具 `population_history_links` / `population_history_links_compile`，外层参数为 String
`relationship` 与 String `request_json`，范围只放在 request_json 中。只有
`ossie://population/history-program` 和 `ossie://population/history-surface` 两项资源；
前者公布原生角色、身份、来源计数与限制，后者说明实际工具范围。原生 program
的 mcp_registered=false 不是运行中 registry 探测；实际 tools/list 才证明工具暴露。
不公布旧通用概念/人口工具或任意 SQL、加载/重载工具。

固定上游 MCP 实测忽略未知外层参数，并可把 object 参数转 String；辅助调用者
发送 String，不把这两个行为伪称原生严格参数契约。内部 request 与完整返回值仍
按本 profile 严格校验。[MCP 证据](research/ossie-req-001/p1-history-mcp-controls/review.md)
包含完整 10,000 link 身份、超限拒绝恢复、28 项损坏 carrier，以及与原始观察/历史
记录对应的独立逐记录匹配。结构合法不证明关系成员正确。

[历史无模型演练](research/ossie-req-001/p1-history-agent-controls/review.md)覆盖全部观察、
日期范围、产品 Y、缺口、最小有限日期、空来源及只解释。执行场景最多三轮/两
工具调用，只解释最多两轮/一次 compile，实际没有查询。模拟 Responses 来自注入
回调，工具使用真实 stock MCP；旁路逐记录 oracle 不放进模型可见上下文。
验证最终回答与实际完整查询、请求、来源计数、定义及六 hash 对应；不代表模型
自主规划、真实业务结论或来源完整性。

运行与校验入口为 `scripts/verify_history_agent_offline.ts CLI ARTIFACT PRIVATE_OUT OUT_JSON`，
私人原件必须在未跟踪的 `.local-agent-evidence` 内；默认不读模型配置、不调用模型。
封存后 `replayHistory(path, expectedArtifactSha)` 严格消费全部有序记录，重算来源/定义
对应和最终成员；可绑定调用者指定产物 SHA。离线不启动 MCP/子进程或 fetch，不打开
归档中的安装路径，也不执行归档代码。缺记录、改范围、错误证据 ID、重封伪造、
只解释伪装查询、结构合法但语义错误等拒绝；完整源 seal 保持不变。新 C++ Linux/
tidy、整体 P1 及待推历史私有检查仍需完成。

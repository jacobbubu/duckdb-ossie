# 声明关系的完整查询

显式加载 `observation-relation-program-v1` 后，可以查询有限非递归关系的完整 links。根身份是 `(User,date,product)`，目标是同一映射 User。这里的关系集合由声明规则决定，不额外套用旧合格观察条件，也没有请求日期/产品筛选。此 profile 不提供多事实、窗口或任意实体图；项目另有[两事实与观察行窗口 profile](population-facts.md)；每日三值状态与历史有效期见[状态](population-daily-states.md)与[历史](population-history.md)。

```sql
CALL ossie_bundle_load('test/fixtures/population-relation-program/bundle.json');
SELECT * FROM ossie_relation_program();
SELECT * FROM ossie_relation_compile('audited_user');
SELECT * FROM ossie_relation_objects_json('audited_user');
```

加载前需要本体对应的 observations 源表；可参考 [独立服务示例](../examples/relation_server.sql) 的 operator setup。该示例的 community 安装行是部署占位；使用本地产物时把 LOAD 改为构建出的扩展路径。

SQL 返回一列完整 JSON 文本。link 为 `[sourceUser,date,product,targetUser]`，关系数量以 `link_count` 十进制整数文本表示；它统计完整关系条数，不能当作 distinct User 人数。排序使用原始 UTF-8 字节和 DATE 日数。DATE 文本支持 `(BC)` 和五位以上年份，有限范围为 `5877642-06-25 (BC)` 至 `5881580-07-10`。结果不分页，至多10,000条、完整内部 JSON 至多1MiB，超限明确失败。这个预算不约束 MCP 外层 JSON、临时分配或峰值内存。

解释预检四列投影实际类型，不计算成员；它可成功而结果因输出预算失败。prepared 查询保留绑定批次，不同新调用间重载可能改变批次。比较规则、范围、身份和六项追溯信息；不能仅比较人数或只比较 plan/result 是否互相一致。

独立 MCP 服务提供两个工具与两个资源：

| 类型 | 名称 | 用途 |
|---|---|---|
| 资源 | `ossie://relations/program` | 本体关系、依赖、约束、身份及批次发现 |
| 资源 | `ossie://relations/surface` | 本服务工具列表及参数边界 |
| 工具 | `population_relation_compile` | 同一声明关系的绑定逻辑解释 |
| 工具 | `population_relation_objects` | 完整根/目标关系集合 |

工具只有 `relationship` 字符串参数。先从 program 发现准确关系名，再调用。固定 MCP 上游会忽略多余参数，也可能把非字符串值转成文本；客户端必须传字符串，不能用 `date`、`products` 或 `limit` 偷加范围。示例关闭内置任意 SQL、导入、重载与通用数据库工具。原生 program 中 `mcp_registered=false` 是不观察外部注册的静态声明，不能用于判断正在运行的 MCP 服务；实际暴露以 `tools/list` 和 surface 为准。

客户端应校验唯一完整 carrier、字段、实际日期范围/顺序、同映射目标、完整 link/count、预算及绑定，并对照 program 发现中的规范关系名、身份、规则和哈希。结构合法不能证明成员真值；compile 的 operator 文本是逻辑描述，不是优化器或执行计划。

兼容边界保持明确：旧 gaap、join、observation profile 的接口行为保留；旧三个 server 不注册新工具。新 relation profile 使用专用 program/compile/objects 接口，旧通用对象/指标入口拒绝。

# facts MCP 集合 JSON 发现声明研究

2026-10-13，基线46d2b20。建议保留现有 String `request_json`，在查询和编译工具描述中都声明完整集合构造语法，并提供一份共享、独立版本的请求契约资源。两处描述负责构造请求必需的信息，共享资源负责完整结构 schema、语义、资源限制和通用示例。当前仅为研究候选，没有注册资源、修改代码/模型上下文/预算、读取私有配置或再次调用模型。

沿[OSSIE-REQ-001 原 issue](../../../ossie-execution-research.md)与[现计划](../../../../ossie-duckdb-plan.md)，复用[单次失败](../p1-agent-live-facts-once/review.md)。冻结上下文只说明集合语义，未披露内层字段结构；原生解析器要求 operation/predicate/children。失败在客户端派发前发生，不能把发现缺口等同于唯一原因，也不能预测补充说明后模型必然成功。现有仓库[人工使用文档](../../../population-facts.md)已有结构示例，但它没有进入实际模型上下文；其中与验收题相近的示例不适合直接用作这次发现补充。

## 放置方式与协议依据

| 候选 | 收益 | 限制与选择 |
| --- | --- | --- |
| 仅工具描述 | 最小修改，tools/list即可获得语法 | 查询/编译各一份长说明易漂移，完整契约不便单独审阅；可作较小备选 |
| 工具描述＋共享契约资源 | 构造要点直接可见，完整结构与语义可共同绑定 | 推荐；资源必须被客户端读取并确实放入上下文，两份摘要也需一致性控制 |
| 改成结构化对象参数 | 外层schema可以描述对象结构 | 改变现有String接口、后端转换和调用者，且仍不能替代原生语义绑定；另行设计 |
| 只添加contentSchema | 可注释字符串内容 | 不能依赖它自动解析或验证内层JSON；不作为最小方案 |

现握手协议为2024-11-05，其[工具规范](https://modelcontextprotocol.io/specification/2024-11-05/server/tools)允许工具description与inputSchema；这里schema描述的参数仍是String。该版[资源规范](https://modelcontextprotocol.io/specification/2024-11-05/server/resources)将资源定位为由应用决定如何纳入上下文；资源可读取不等于模型已得到内容。因此两个users工具均需可独立构造请求的摘要，不能只给URI。[JSON Schema内容词汇规范](https://json-schema.org/draft/2020-12/json-schema-validation)把contentMediaType/contentSchema作为注释而非断言，默认不自动解析字符串内容；结构schema应对显式解码后的对象另行验证。本研究也查看了2025-11-25规范作比较，不据此宣称当前后端支持新版能力或升级握手。

候选 `ossie://facts/request-contract` 只适用于 population_fact_users 与 population_fact_users_compile，当前尚未注册。原生请求解析/编译定义语法和语义，原生program继续提供实际指标、单位、定义与快照绑定。MCP层可以发布静态契约，不重建实际program事实，也不向已有program_json随意加字段。静态契约版本/hash与冻结program的六绑定分别标识语法与数据来源，未来客户端应明确检查适用工具/版本并封存实际载荷。

## 最小完整构造声明

以下是语法记法，C/P/OP等是占位符，不是可直接提交的JSON。真实示例见后文独立文件。

```text
外层参数：{"request_json": String(JSON对象)}
内层对象：{"domain":"common","input_scope":"entire_capture",
           "metrics":[已发现regular指标名,...],"cohort":C}
C = {"operation":"all"}
  | {"operation":"predicate","predicate":P}
  | {"operation":OP,"children":[C,C]}
OP = "intersection" | "union" | "difference"
```

两个工具摘要都应明确：内层对象的字段必须与分支完全对应；operation大小写敏感；二元运算恰好两个有序children；difference是左集合减右集合。根层计为1，深度最多16，根和叶均计入64节点；原始内层JSON最多16,384 UTF-8字节。metrics为1～32个已声明regular指标，大小写不敏感且不允许大小写重复；名称最多128 UTF-8字节。不能把JSON对象直接传给String参数，也不能将内层对象再次编码成JSON字符串值。

P必须说明为单个无别名比较：左侧是未限定、未加双引号的已声明regular指标，右侧是非NULL精确数值常量；比较类型为等于、不等于、大于/大于等于、小于/小于等于。示例采用=、<>、>、>=、<、<=常见拼写，不承诺穷尽DuckDB解析器所有词法形式。谓词可引用没有投影的已声明regular指标；禁止字段/窗口指标、指标对指标、算术或复合布尔条件、函数/cast表达式、DOUBLE和NULL常量。COUNT常量必须是可表示为BIGINT的整数；金额常量可为整数或DECIMAL，decimal scale≤2且精确可表示为DECIMAL(38,2)。引号、NUL、别名及多个表达式由原生Expression门禁拒绝；指数/一元符号等形式不在本轮作实测接受承诺。

每个谓词转换为 `(comparison) IS TRUE` 的成员集合；FALSE/UNKNOWN都不属于该原子集合。all表示共同域全部成员，交/并/有序差始终在共同域内；右侧UNKNOWN不被difference排除，不能用可空NOT替代。来源先按各自粒度归约再组合，共同域人数是筛选前分母，selected人数是筛选后成员数；全NULL金额仍保持NULL。嵌套二元对象树受预算约束而被支持；当前“recursive cohorts”拒绝描述应澄清为不支持命名/自引用递归，不能让人误以为禁止嵌套。

查询工具返回完整执行结果；compile使用同一请求合同，只返回绑定/解释，不能证明执行、成员或人数。外层未知参数由既有MCP后端忽略、Agent要求精确外层字段，这一边界需照实保留；内层未知/重复字段拒绝。补充发现信息不等于改变后端参数行为或放宽校验。

## 草案与不依赖验收答案的示例

[候选契约](contract-candidate.json)、[内层结构schema](inner-request.schema.json)和[通用示例](generic-examples.json)均为文档草案，不属于运行入口。例子假定另一个program已经声明Order来源的order_count与Refund来源的refund_count，都是计数指标；使用7、2、1等示意阈值，不含验收数据、实际业务指标、用户身份、成员答案或计算值。当前pilot未声明这些名字，不能直接运行这些例子；若将来声称原生可执行，必须建立独立的假想声明和fixture。

例如有序差：

```json
{
  "operation": "difference",
  "children": [
    {"operation": "predicate", "predicate": "order_count>=7"},
    {"operation": "predicate", "predicate": "refund_count>2"}
  ]
}
```

这只是语法：从满足左条件的共同用户集合中去掉满足右条件的集合，没有提供任何用户或人数。通用文件同时给出all、predicate、intersection、union、difference、嵌套union/intersection，以及真正String封装的完整外层参数。

结构schema使用oneOf分支、const/enum、required、additionalProperties=false、两个children与递归引用。它不验证原始重复JSON键、字节预算、全树节点/深度、名称大小写重复、指标声明存在性或数值/表达式语义。字符串maxLength不是UTF-8字节长度；不将其冒充字节门禁。contentSchema即使存在也不能替代客户端显式解码与原生绑定。

## 离线核对与差异

[验证记录](validation.json)绑定66份来源、原失败seal及17份原始/审阅文件。6个通用示例通过草案结构schema和当前Agent结构门禁，8个错误分支被双方拒绝；深度16/17、有效全二叉树63/65节点边界另核对。原生以根1、≤16计深度，p1门禁以根0、<16计，结果一致。完整二叉树节点数为奇数，不能构造合法64节点树来证明边界接受。

6个表达式反例说明结构schema和当前Agent门禁不做原生表达式绑定；空谓词和NUL谓词还显示当前p1门禁比草案/原生更宽。fact_result结果对应检查有更严格的空/NUL和节点检查，仍不证明原生谓词语义接受。研究记录这些差异，不在本轮修改任何门禁，也不把文档schema当作新生产校验器。

本轮没有原生compile、MCP会话或模型调用，不宣称假想program已执行或新资源实际可发现。[官方来源](official-sources.json)与[研究交付核对](verification.json)分别记录协议依据、文档/来源/私有证据边界。Advisor一轮只读支持组合方案并指出嵌套措辞、非投影谓词指标、quoted/alias门禁和63/65边界；Lead逐源码核实。请求Advisor Astra/high，实际backend/effort/usage/billing未报告，保持unknown。

## 后续最小实施候选与停点

若用户后续授权，先做共享契约载荷及两工具摘要，明确如何保持三份说明一致；客户端读取并核对适用工具/版本，将实际资源放入冻结上下文。当前FactSession严格断言只有两个资源、每资源一个row，factProgram也严格匹配字段；新增独立资源需显式更新发现/封存/重放，不应偷偷塞入native program。既有工具名和String参数、原生业务语义保持；本研究不要求新增原生函数、升级项目API或协议版本。

一个新资源将事实发现从5个RPC请求变为6个（若仍逐资源一次读取），不是增加模型工具调用预算。未来必须实测工具描述与载荷在实际tools/list、resources/list/read、context和serialized_body中的对应和大小，重新核对128 KiB出站、原预算及公开摘要边界；不能只检查文件存在。compile/execute同请求结构、分支/重复字段/未声明及非投影指标、大小写重复/数字类型、NULL/有序差、63/65和16/17边界都需分层验证。真实模型验收仍需独立授权，之前失败原件保留，不能把新描述下的离线成功当作旧pilot通过。

当前工作区普通git status因DuckDB子模块Git路径失效而失败；父仓检查使用ignore-submodules=all，只核父仓状态。研究不需子模块构建，本轮未修复；后续原生工作前仍需处理。当前工作包到研究交付为止，不实现、不重试、不运行其他场景、不推送。整体三题真实验收仍未完成，下一步候选是上述最小发现声明实施及无模型验证。

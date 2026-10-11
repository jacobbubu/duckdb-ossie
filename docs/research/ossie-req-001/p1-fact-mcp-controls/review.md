# 双事实 MCP 本地验收

独立四工具与两资源已实现，标准 DuckDB1.5.6/固定 duckdb_mcp 6f9c4ba 的实际
stdio 验收通过32项完整数据/注册检查及48个拒绝用例。此工作包连接已经实现的
双事实 User 聚合、人群集合和单来源观察行窗口；无模型 Agent、CI接入和完整P1
最终验收仍必需。没有调用真实模型、推送、合并或发布。

[服务示例](../../../../examples/fact_server.sql)仅注册 users/windows 查询与对应
compile 四工具；program 和独立 surface 两资源用于能力发现及实际工具暴露。
唯一必填外层参数是 request_json 字符串。实际 tools/list、resources/list/read
和未知工具拒绝证明本服务没有通用SQL、导入、reload或其他profile工具。
native discovery 的 mcp_registered=false 未被改成虚假的实际注册查询。
本地标准客户端使用新构建产物；这不是community发布证明。

[最终报告](final-mcp.json)包含五种成功数据设置和超限设置的摘要；私有原件保留
全部RPC、完整行和独立逐行计算。User oracle 独立按来源分别归组、用 BigInt
cents 计算，再取共同User和 TRUE 集合；实际A收入375/成本85/差额290、
B80/30/50、C全NULL金额以及非NULL COUNT(User)/COUNT(amount)1/0一致。
空筛选保持common=3、selected=0；未投影expense条件、交/并/差及右侧UNKNOWN
保留左侧成员都核对完整行。窗口oracle先按User/日期在全部收入观察行计算LAG1
和两行和，再做展示筛选；A2月1日前值200/和250，非共同User D保留。

实际raw设置覆盖é、A/a和NUL User身份，wide覆盖BC、10000年及两个有限DATE
极值，empty覆盖两空来源，full逐行比较10000个User/10000窗口身份和数值。
这些是固定合成输入的精确对照，不代表任意生产数据或GAAP解释已正确。

四工具内层UTF8请求恰好16384字节接受，多1字节以既有统一错误码
RESOURCE_LIMIT_EXCEEDED拒绝。上游外层limit/日期/范围未知参数实际忽略，不会
静默改变作用域；坏JSON、未知内层字段/范围、错种指标、重复键、缺参/数字/
对象参数及未注册工具拒绝。两源各10000行、带大量转义字符身份的捕获仍能加载，
compile通过，但User和窗口查询在完整内层JSON超过1MiB时拒绝，无部分结果。
没有逐一实测MCP窗口/计划/program恰好1MiB边界；SQL工作包的User精确边界
不外推到其他响应。canonical/inner JSON预算不是进程峰值或外层wire保证。

[严格decoder](../../../../scripts/fact_result.ts)检查外层和内层decoded重复键、
精确键集合/预算、六项binding、请求结构/显式范围/日期和声明对应、类型/单位、
身份顺序/唯一、完整计数、执行标志及非物理/不可重放计划。COUNT必须非NULL
且不超过对应来源行数，共同分母不超过任一源行数；all人群/展示必须保持完整
common/source行数。发现检查两来源同币种、不同完整身份粒度，金额单位及
COUNT单位与来源一致，窗口只能引用User/date事实。金额可为NULL并保留精确
DECIMAL文本；全有限DATE用独立历法计算。外层8MiB仅是本地decoder防护。

8项新增测试/119断言包括真实返回、损坏文本/金额精度、错误NULL COUNT和分母、
未知字段、币种/粒度、请求及空结果非法日期、重复键、错计划标志和错误绑定。
测试还明确演示格式合法的291替代实际290以及同数量错误成员仍可被结构门禁
接受；独立oracle必须另行拒绝。hash相等也不证明内容真实性或原SQL语义。

只读Advisor指出请求、COUNT/分母、单位/粒度和先解析丢重复键的门禁缺口，Lead
采纳并通过对应反例。本次请求Astra/high；有效backend/effort/usage/billing未知，
Advisor未运行测试。Lead负责所有实现和实际验证。原生代码和产物未变，仍为
9ddb20db；复用上一[SQL/native工作包](../p1-fact-api-controls/review.md)同SHA证据。
本次实际复验旧观察MCP43/38、每日16/28、关系14/13；完整129Bun/663断言、
类型/格式/架构/diff检查通过。当前跨平台/tidy不以此本地结果代替。

[receipt](receipt.json)绑定38份私有只读原件、最终源码和公开派生件。首次边界
探针的脚本错误保留：只读tool content且误写RESOURCE_LIMIT，实际上游RPC
error.message已带RESOURCE_LIMIT_EXCEEDED；修正后完整复测通过。旧观察脚本
首次用了错误位置参数，修正为其既有命名参数后通过，失败日志也保留。公开
材料替换本地路径，旧服务报告省略sessions，最终报告摘要省略完整RPC/10000行
但保留完整raw hash和实际/独立行hash；conformance保留四种实际成功设置。

下一授权动作是本profile无模型Agent场景、实际工具过程、离线重放及私有证据
伪造控制，再CI覆盖。状态/历史业务语义仍待答，完整P1兼容矩阵、最终同产物
质量/跨平台tidy及待推历史私有信息审计保持必需，不能以本工作包结束goal。

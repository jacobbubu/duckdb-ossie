# history 最小语法声明与无模型验收

2026-10-13，实施基线0361a36。history查询和编译工具均已补齐完整内层JSON
构造语法与紧凑通用示例，实际MCP、封存上下文和序列化请求对应一致。
保留两工具、两资源、5发现RPC及两个String参数，未新增共享资源、改变原生
API1.0.0/查询语义、提示词或调用预算。没有调用真实模型、读取私有服务配置或推送。

沿[OSSIE-REQ-001本地issue](../../../ossie-execution-research.md)、
[最小研究方案](../p1-history-discovery-research/proposal.md)与现计划实施。
原缺口是语义描述未说明dates/products内层分支的字段名与嵌套结构，并非已经
证明history模型失败；当前无模型通过也不证明未来模型能自主构造或稳定成功。

## 同一来源与发现一致性

[共享源](../../../../examples/history_request_notice.json)保存工具各自原说明、
共同构造声明和紧凑通用示例。[生成与检查](../../../../scripts/history_request_notice.ts)
由一个共同片段生成两份描述并写入现有SQL示例；手工执行此脚本的`--write`
可再生成，`--check`拒绝过期SQL。现有format:check已纳入该检查；本地类型、格式
和架构检查通过，尚没有新远端CI结果。

historySetup拒绝与共同源不同的SQL，实际tools/list发现和离线重放也要求两份
完整描述精确对应共同源，不能让compile仅引用query或漏掉语法。原专用query/
compile解释保留，执行与仅解释的区别不被共同片段覆盖。

声明包含dates all/range和products all/subset的精确分支、两个String外层参数、
单个内层对象、有限规范DATE/BC/多位年份/同日范围、首尾包含、原始产品身份、
1～64项/每项256 UTF-8字节/原始请求16KiB（含空白与转义）、重复或未知字段拒绝。
原生接受乱序subset并规范化，而当前Agent要求原始UTF-8字节预排序，分别说明。
观察筛选首尾包含与历史有效期[start,end)不同；缺口不补、历史全量冲突不能
通过请求过滤，源/索引计数不当link_count，compile不计算关系或数量。

紧凑例给出真实内层对象和外层String封装，假设另一program声明
membership_at_observation、产品Catalog-A；本pilot未声明该关系，描述明确禁止
把例子直接运行到pilot。没有本题日期/产品/用户/团队、数据行、结果或数量；
实际关系仍从history-program读取。八个研究通用例另在离线门禁检查中通过，
不把全部例子追加到每次模型上下文，也不新增fixture/原生关系。

## 实际证据和回归

[绑定记录](receipt.json)核对真实stock MCP的两描述/两资源/5发现RPC、context
与首次/续接body；两个工具都携带完整共同声明。首轮输入仅实际发现资源与
原中文问题，系统要求不变，没有加入独立答案或模拟器查询模板。虚拟首/续
body为12,911/11,902 UTF-8字节，均低于131,072；这些不是实际token或未来
响应/费用充分性证明。每次最多4模型请求/3工具意图/4096输出token、入站2MiB、
场景180秒及顺序清理合同保持；不自动修正错误查询，不自动继续其他题。

| 无模型验证 | 结果与证明范围 |
| --- | --- |
| history实际MCP | 15检查、24拒绝、28损坏carrier；完整独立关系oracle及发现，非模型自主选择 |
| 原history Agent | 七场景、29重放/语义/不完整负控；compile场景没有执行查询 |
| 共享P1离线 | 33控制，含合法错范围、失败/预算封存、原始字段及协调篡改 |
| 共享P1传输 | 20控制，mock live fetch2；实际网络/模型/配置读取0 |
| Bun | 173测试、873断言，含新增3项发现漂移/重复JSON/String边界/来源绑定控制 |
| 质量检查 | 类型、格式、描述生成一致性、架构通过 |

P1当前来源66（新增共同JSON和解析/生成helper），旧history显式来源25，均
绑定共同源与其严格p1_agent_json解析依赖。[公开固定v2摘要](p1-summary.json)
经纯离线重放后导出，保持semantic_acceptance=not_proven；原始history虚拟
单题仍pending，七场景脚本演练complete不当真实语义验收。

既有4份清单共4475个私有原件hash一致，原history旧seal和facts历史失败/成功
记录保留；没有将旧档案重封为新来源下验收通过。新私有封存留存所有无模型
运行、预算/篡改失败、日志与检查脚本，清单绑定。公开只汇总固定计数/来源与
边界，不导出私有provider标识、usage、原始RPC/答案；新共同声明及示例是静态
通用材料。未改变既有公开摘要/白名单响应合同。

无C++改动，无新native构建/tidy或远端CI；固定CLI/原生扩展继续使用已有产物。
实际模型验证和私有配置认证不在本轮范围。当前工作包已完成，下一候选先审阅
新版history实际上下文、单题命令、预算与证据范围，之后另行决定一次真实执行。
新版facts已有用户限定接受，history/states仍未真实运行，不把此实施包当整体
真实Agent验收完成。

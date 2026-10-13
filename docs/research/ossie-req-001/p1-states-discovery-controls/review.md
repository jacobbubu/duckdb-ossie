# states 两工具完整语法声明与无模型回归

2026-10-13，基线e5d74dd。本地实施完成：states查询与编译工具从同一来源生成
完整构造声明和紧凑通用示例，实际MCP、context与序列化body精确对应。
两工具/四资源/7发现RPC、concept/request_json两个String参数保持；未增加资源，
不改原生API/规则/请求门禁、提示词或预算。没有读取私有服务配置、真实模型
请求或推送。无模型通过不证明真实模型自主构造或语义验收。

沿[OSSIE-REQ-001本地issue](../../../ossie-execution-research.md)和
[states最小研究方案](../p1-states-discovery-research/proposal.md)。原缺口是products
嵌套mode/values未披露、compile仅引用query；本轮补说明，不改变计算方法。

## 同一来源与两工具区别

[共同JSON源](../../../../examples/state_request_notice.json)保存完整构造片段、
假想例及各工具精简专用后缀。[生成/检查入口](../../../../scripts/state_request_notice.ts)
以`--write`再生成现SQL两描述，`--check`拒绝漂移；已有format:check同时检查
history和state生成一致性。stateSetup拒绝过期SQL，实际发现与离线重放要求两
完整描述精确等于共同源。不是只让compile引用query，也没有机械复制旧重复说明。

五根字段start_date/end_date/products/user_domain/transitions明确全部必需，
后两项固定captured_users/within_request_days。products为mode=all仅该字段，
或mode=subset加非空values String数组；请求为一个对象的String编码，不用对象
外参或再编码的JSON String值。资格concept在外层，取实际state-program声明。
日期范围0001..9999规范YYYY-MM-DD、首尾含、1..366天，不复用history的日期域。
subset原始身份/UTF-8字节预算/重复字段与原始16KiB请求、原生排序和Agent
预排序分别说明。

日期定义输出日历及贡献观察，产品过滤贡献观察，两者不缩小全捕获User域。
任何TRUE胜出、非空全FALSE才FALSE，否则UNKNOWN；不carry、不推历史规则。
查询返回完整User/day和所有请求内相邻日对，含未变化；changed按标签差异，
含UNKNOWN变化。totals单位User/day，changed计比较对，不是变化用户数或源行。
query/compile都做捕获用户元数据容量门禁，compile不计算执行结果；专用后缀
保留各carrier/schema、state_api_version1.0.0、BOOLEAN投影/整数计数/当前规则
及六hash核对，计划非物理/独立可重放SQL，不保证执行结果大小。

通用例仅假设另一程序有EligibleVisit，使用2031日期及Catalog-A/catalog-a/商品，
提供真实内层对象和外层String。该concept未在pilot声明，明确不能直接运行到
pilot；没有本题资格概念/日期/产品/用户、数据行、状态结果或数量。八个研究例
仅做离线门禁核对，不全部进入模型请求，不新增fixture或规则。模型须读取当前
程序的真实资格规则，不能把假想示例当业务声明。

## 实际发现、封存与回归

[核对receipt](receipt.json)证明实际stock MCP的两描述/四资源/7发现、context及
每轮tools字段完整对应共同源；STATE_SCOPE_NOTICE、系统说明和原问题保持。
首输入仅实际发现资源与原题，没有oracle、正确请求模板或评估逻辑。虚拟首/
续body22,912/17,627 UTF-8字节，低于原131,072出站限；不是实际token、费用或
未来响应充分性证明。原4请求/3工具/4096输出token、入站2MiB、180秒与顺序
清理合同、公开v2白名单边界保持，真实服务配置不读取。

| 验证 | 结果与边界 |
| --- | --- |
| states实际MCP | 19检查/23拒绝/25 decoder损坏，完整状态oracle与错误后恢复，非模型自主行为 |
| 原states Agent | 六场景/18重放与语义负控，compile场景没有执行查询 |
| 共享P1离线 | 33控制，含错误scope不纠正、预算/失败封存与协调篡改 |
| 共享P1传输 | 20控制，mock live fetch2；实际网络/模型/配置读取0 |
| Bun | 176测试/892断言，新增3项描述漂移、重复JSON/String边界及来源parser绑定 |
| 质量 | 类型、格式、两个描述生成一致性、架构通过 |

首个新单测因复制时测试变更目标用了不存在的旧名而失败，生产SQL不曾变更；
修正测试目标后通过，初失败记录保留私有。没有因该失败重试模型或扩大实现。

P1来源68、旧states显式来源25，均包含共同JSON、新生成/解析helper与严格
p1_agent_json传递依赖。[固定v2摘要](p1-summary.json)由阻断fetch的纯离线重放
导出，仍semantic_acceptance=not_proven；单题虚拟raw pending不被模拟机械
通过改写，六场景脚本complete也不是实际模型验收。

上轮实施/审阅/研究/history真实证据清单1253私有文件hash一致，旧states seal
保持，facts/history原始接受/失败不覆盖，不重封为新来源下验收。新私有清单
绑定所有正负控、实际RPC/结果、模拟响应、失败封存与日志；公开只固定计数/
绑定/有界说明，不导出真实provider标识、usage、raw状态数组或模型自由文本。
共同声明/假想例属于静态通用材料。无C++改动，固定CLI/扩展沿用既有产物，
无新native构建/tidy/远端CI；本地检查不能冒充新远端结果。

本工作包完成。下一候选先审新版states实际模型上下文、单题命令、预算和
证据范围，再另行决定一次真实执行。facts/history已有fixture限定接受，states
仍未真实验收，不能自动调用模型或推导整体可靠率。

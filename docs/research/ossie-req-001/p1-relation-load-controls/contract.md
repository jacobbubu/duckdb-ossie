# 关系执行 profile 的加载与完整人口约束

本工作包把先前独立 binder 接入实际 `ossie_bundle_load`。新显式 profile 为 `observation-relation-program-v1`，在冻结源实际类型和完整根身份预检后绑定关系 DAG、执行根与实际 link 人口的约束，全部成功才原子发布。旧三个 profile 的派生关系拒绝保持。完整关系对象/解释/MCP 尚未接入，整个 P1 没有完成。

入口使用原本体/映射格式。根仍为观察实体，完整身份 `(User,date,product)`；映射的 User、Date、String、Decimal、Integer 五个物理角色保留原观察校验，额外关系用 `derived_by` 和单 User `ManyToOne` 声明。映射与派生关系共用名称检查，不能遮蔽非整数字段关系；额外映射 link 和同时映射/推导的冲突由映射校验拒绝。旧资格概念及五个指标的既有定义校验仍保留，**关系人口自身不附加该资格概念条件**，由每个关系的绑定谓词决定。

程序绑定支持和内部预算沿[绑定器契约](../p1-relation-bind-controls/contract.md)。本 profile 明确是五角色观察到同一映射 User 的有限形状，不是任意事实或任意目标实体图。实际整数类型来自冻结全字段投影的 Prepare 返回类型，再提供给 binder；不把声明 datatype 当作实际类型。主键及原始 User/date/product 身份、原始类型与 collation、10,000 行/256 物理列/16 MiB 捕获预算均走既有生产预检。

根 `requires` 对整个捕获的根对象人口执行，完全不受关系资格筛选影响。每个派生关系的 `requires` 对其谓词为 TRUE 的完整 link 人口执行。违反以 `IS DISTINCT FROM TRUE` 发现，抛出统一 error-v1 的 `CONSTRAINT_VIOLATION`、stage `constraint`；正常规则/角色/环拒绝仍是 `RULE_REJECTED`。由于 binder 只接受显式总 NULL 约束，未知业务策略没有被默认新增。空 link 集合满足关系约束是全称检查的空集行为，不证明存在 link。对象行唯一性预检在程序及约束前执行；任何失败都保留之前可用的 snapshot。

新增 `ossie_relation_program()` 返回一个完整 `program_json`，schema `ossie-relation-program-v1`。它列出 profile、根/目标概念、映射 User 关系、实际绑定的根身份字段与类型、拓扑依赖/深度、原规则、绑定谓词、根/link 约束、captured_rows 文本及六个 snapshot/内容/定义追溯字段。`source_types_bound=true` 与 `load_constraints_validated=true` 描述成功加载事实；`query_executed=false` 只表示没有运行对象查询，不能否认加载时已执行约束。谓词依赖内部冻结源，不可当普通 SQL 回放。

发现输出完整最多 1 MiB，超出抛出资源错误，不截断。当前是在完整字符串构造后检查成功输出大小，不声称工作内存或增量编码上限；精确 1 MiB 及超 1 字节边界尚未实测，仍待完整接口预算控制。新 profile 的旧 objects/metrics/compile/concepts/capabilities/daily 入口明确拒绝，防止沿旧接口返回错误身份或能力声明。完整关系对象接口是下个必要切片；当前 `objects_queryable=false`、`mcp_registered=false`。

一次并发审查发现 dispatch 检查 Current 后各 binder 再捕获 Current 的竞态。修正为各旧 binder 的 `LegacyCurrent` 一次捕获，同时检查并返回同一 bundle；每日 binder 本来在其捕获对象上检查。已有 prepared 查询按绑定时的旧 profile 继续执行，不按此刻 Current 再拦截。内部原生 observer 只在客户端测试状态注册，没有 SQL/环境配置开关或可修改 bundle 的能力。

最终[产物验证](gate-final/verification.json)为 7 检查、14 拒绝：23 行实际冻结类型/依赖/全人口约束、资格外负值违反、实际 link 范围违反、有效 link 约束和空集、环/坏角色/引用/名称遮蔽、旧 profile 拒绝、非总 NULL 谓词、实际物理 VARCHAR 类型错配、重复完整来源 tuple、prepared 旧观察与旧发现跨成功 reload、来源删除、未实现旧查询面明确拒绝。没有查询完整 link 集合，本轮不能宣称 18/7 个完整关系对象已经通过插件 oracle；下个切片仍必须落实。

最终生产 native capture 为 280 检查（threads4/1），新增确定性双连接控制：暂停在 legacy binder 捕获后，另一连接发布不同 profile；两个方向都证明门禁和实际绑定一致。另暂停在新关系 profile 完成约束后的发布前，取消保持原生 INTERRUPT、保留完整旧 snapshot，后续有效 reload 恢复。独立关系 binder55、state19、grain42通过；26 SQL/975断言、110 Bun/468expects、格式/类型/架构及同最终SHA的 A1–A8/旧MCP/观察43/38/每日16/28、原四场景和每日五场景模拟/离线重放通过。最后 native cancellation 测试只改测试文件，扩展SHA不变。

先前版本报告和 gate 修复前的同产物回归原件保留，不能混为最终SHA。两次 native SQL工具错误原件保留：缺少可选 json 扩展，改用固定字符串标记并由实际产物 verifier 做结构化 JSON 检查；sqllogic `statement error` 语法误用，改按现有仓库的 `----` 匹配错误码。一次 patch 上下文失配没有改文件，重新读取后修正。错误日志不改写。原始模拟档案留 ignored 私有目录，公开仅既有受控衍生物/选择性 receipt；真实模型0，未读本地服务配置。

新 verifier 已加入 Linux amd64/macOS ARM64 实际产物 CI 命令；新 SQL/native 测试走既有入口。本轮没有推送或远端 CI，Windows及Linux tidy不宣称通过。新增函数有注册描述和示例，函数清单更新为25注册/23名称；它不改变旧指标业务行为。

复用一轮只读 Advisor，Lead核实竞态并实现确定性反例/修正；所读最终源码不是完整 P1 审查。请求 Astra/high，实际 backend/effort/usage/billing 未知。下一已授权工作为完整关系对象的源 tuple + 带类型目标 User 身份、同请求解释/能力发现及完整 link oracle，再接 MCP/无模型验收；混合粒度、窗口、状态/历史决策和所有原五类 P1、最终 tidy/历史审计继续保留。无模型、推送/合并/发布。

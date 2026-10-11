# 双事实冻结加载与原生执行验收

已实现 `multi-fact-user-program-v1` 加载器。两个不同粒度的事实表在同一来源事务中分别捕获，完整身份、类型、共同 User 角色及合计预算校验后才发布父快照。238 项原生检查和实际扩展的 4 项检查、28 个定义拒绝用例通过。这是加载与内部执行工作包；没有专用公开对象查询、解释、能力发现或 MCP 工具，整个 P1 未完成。

沿 [OSSIE-REQ-001](../../../ossie-execution-research.md) 和[实施契约](../p1-program-research/implementation-contract.md)，manifest 的 program 只列两组 dataset/concept/显式 currency 和 core 中的窗口指标名。公式仅来自 core，固定 core/ontology/mapping schema 先校验；新 profile 显式允许已声明指标引用，旧默认解析仍拒绝裸指标依赖。只有 SUM/COUNT、同币种金额加减和既有有界窗口子集可以通过内部 FactProgram。原始 AST 的大小、深度、数量在复制/降低前检查。没有推断币种，也没有支持任意 SQL。

每个根概念的身份为 User/date 或 User/date/product；映射对象与完整关联树必须指向同一字段，嵌套 User id 必须符合 User 的唯一标识。物理主键顺序必须与映射身份一致。两个原生已提交表必须不同、属于同一底层数据库；类型限 VARCHAR、有限 DATE、金额 DECIMAL(18,2)。所有源行的身份必须非空且唯一，完全相同和冲突重复都拒绝，包括未进入共同 User 集合的单侧用户。每张源表最多10000行，最多256物理列；两份 canonical 捕获合计16MiB，空捕获的字段名和类型等元数据也计入预算。未使用物理列仍参与捕获及预算。

原生探针真正执行冻结来源 AST，得到 A 收入375、成本85、差额290，共同域保留全NULL金额的 C，COUNT(User)/COUNT(amount)对 C 为1/0；A 的2月1日前值200、两行窗口和250，证明先全捕获计算再输出日期。LAG 实际类型为 DECIMAL(18,2)，SUM 窗口为 DECIMAL(38,2)。默认降序不能改变显式 ASC 窗口。这个探针固定 reader 为 binary collation；还不能据此宣称任意调用者会话下的公开身份语义。后续公开执行必须使用已固定 binary 的私有 Engine。

并发写入在第一份捕获后同事务修改两张来源表，新加载仍捕获原完整版本。第二表不存在、捕获中断、发布前中断、错误实际金额类型、无限日期、单侧重复、10001行以及合计超限都不替换原有效快照。两边空源仍保留实际类型；增加未使用列会改变空捕获 hash。prepared 聚合和只访问一边的窗口均保留父快照及两个 child，跨 reload 和来源删除继续得到旧值；释放全部 prepared 后弱引用证明父与两个 child 均释放，没有 parent/AST/child 强引用环。去掉 AST 的集合依赖仅执行渲染 SQL会拒绝。

[receipt](receipt.json)绑定当前源码、扩展 SHA、私有原件 hash 和公开派生件。原件存于忽略的 `.local-agent-evidence/fact-load-native`；公开件替换本地工作区/用户路径；三个 MCP 摘要另省略体积较大的 sessions，完整 RPC 载荷保留于原件。公开件保留原报告对原件 hash 的引用，它们不是可重放的原件。

同一当前扩展下，旧 capture308/state19/grain42/relation binder55/fact binder78检查、26 SQL用例/982断言以及完整旧 A1–A8 runner通过。A1–A8仍限原来的两个 bounded profile和既有偏离口径，不能外推为 P1 验收。观察/每日/关系加载与对象回归通过；三个实际 MCP 分别43/38、16/28、14/13项检查/拒绝通过。每日、关系无模型演练及18/20项重封伪造拒绝通过。121 Bun测试/544断言、类型、原生及TypeScript格式、架构与diff检查通过。没有模型调用；没有推送、远端 CI、合并或发布。

失败记录保留：缺少 dialect include、有效构建环境为C++11导致 generic lambda 编译失败、错误 decoder 参数类型，以及测试探针把 streaming prepared结果误当 materialized 的断言与诊断失败。最后一项通过显式 materialization修正，不将测试读取问题归因于查询优化器。旧 joined 回归发现默认捕获超限文案被新合计预算文案替换，已保留旧“batch exceeds 16 MiB”前缀并重新构建、完整回归。一次探针执行路径写错导致退出127也保留。只读 Advisor 未发现加载工作包阻断项；建议保留以下限制，实际测试由 Lead执行。顾问请求 Astra/high，实际后端、effort、usage和billing未知。

16MiB约束对象是 canonical 字节，源查询先 materialize、ToSQLString临时值先产生，因此没有证明整个进程的峰值内存上限。hash绑定实际捕获行顺序和物理元数据，不是数学集合归一 hash。Prepare只证明绑定类型，不证明金额溢出的实际执行拒绝；本轮合计大输入拒绝也不能代替精确16MiB边界或公开JSON编码边界测试。当前通用旧查询/发现函数明确拒绝此 profile；现有加载函数描述仍只枚举已公开的旧profile，后续公开接口工作包必须同步描述与能力声明。

下一已授权行动是公开多事实人群与观察行窗口的查询、编译解释及 program发现接口，连接私有 Engine、完整输出和有界 JSON，再接 MCP与无模型验收。复杂集合、实际溢出、窗口兼容矩阵、状态/历史口径待答和最终tidy/跨平台/历史审计仍保留，不能以本包通过缩减整体 P1。

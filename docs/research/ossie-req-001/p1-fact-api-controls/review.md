# 双事实公开SQL接口验收

五个专用SQL函数已本地实现并通过实际扩展、原生与标准客户端验收：User指标/人群查询及编译解释、观察行窗口查询及编译解释、program能力发现。最终30项实际接口检查、55个拒绝用例、260项原生检查以及27 SQL用例/1026断言通过。新profile的MCP与无模型Agent尚未实现，整个P1仍未完成。

[用户契约](../../../population-facts.md)和[冻结实施契约](../p1-program-research/implementation-contract.md)沿OSSIE-REQ-001。两来源分别归约至User后才组合SUM金额；共同域按两边行存在，保留全NULL C、排除D/E。人群原子条件只选TRUE成员再交/并/有序差，右侧UNKNOWN不误删左成员。实际完整oracle为A收入375/成本85/差额290、B80/30/50、C金额NULL与COUNT(User)/COUNT(amount)1/0。额外N收入150/成本NULL，差集实际保留N。未投影的regular指标仍可作为条件，选择名大小写不敏感但输出规范拼写。

分母AST只按两个源User去重求共同域COUNT，不求任何金额；分母与成员绑定同一个父/两个child。空筛选实际common_user_count=3、selected_user_count=0，没有将空输出当分母零。独立窗口在全部单源观察行上计算，再筛显示日期；A2月1日前值200/两行和250，非common D仍在结果。DATE按实际日值排序，不按文本排序，BC、10000年与两个有限极值都能规范往返。私有binary Engine和显式ASC顺序保护调用者nocase/DESC下的A/a与窗口顺序。

compile只Prepare实际类型和阶段，不触发执行/计数观察点、不输出伪造的成员/计数，executed/members_computed/counts_computed均false。program说明完整身份、source/child hash/捕获行数、原始规则、单位、实际类型与函数catalog/schema、真实32/16/128等预算及拒绝边界；不计算common，mcp_registered=false不推断外部服务注册。来源captured_rows是加载事实，不是compile/发现本次计算结果。

原生控制覆盖计数后、排序前、编码前取消及恢复，保留native INTERRUPT且不返回部分JSON。public prepared跨reload与删除原表继续返回旧分母、金额和窗口，并在bind后创建同名宏仍保持旧正确值。父/child无环释放控制沿原加载探针继续通过。这个取消证据不覆盖排序/编码每一条指令、硬超时或峰值内存上限。

资源实测包含UTF8请求恰好16384字节接受、16385拒绝；User完整JSON恰好1048576字节接受、多1字节拒绝；转义身份超过1MiB时User和窗口都拒绝；完整10000个User/窗口保留首末身份与完整行数；最大32regular/16window、128字节标识符及所有16窗口实际执行，14个0前序窗口只取当前观察值。63人群节点、16深度接受，65节点和17深度拒绝。尚未逐一测窗口/计划/发现的精确1MiB编码边界，不能把User边界外推为所有响应边界。不存在分页/截断，canonical和内层JSON上限不是进程峰值或MCP外层wire上限。

金额精度使用两源各10000行、最大DECIMAL(18,2)输入及1023节点/512组件叶的合法派生式，实际结果完整匹配独立BigInt cents。按事先冻结的输入、行数和二元+/-树，所有非NULL中间/结果绝对cents≤512×10000×(10^18−1)<10^38−1；COUNT≤10000，窗口SUM≤367×(10^18−1)。所以当前范围不能构造合法的38精度金额溢出。更宽物理38,2源与下一2047节点展开拒绝，并保持最大值快照；这是精度和越界证据，不声称实际触发运行时溢出。若今后放宽范围，必须重新核验此界限。原防错误金额目标保留，没有通过临时收紧预算回避测试。

Advisor找到两个实际反例并已修正：交换manifest facts顺序时，发现误用core数组序号报告错误source，改按dataset名称取来源；同名count_star宏把1共同User报成42，同名减号宏把100−10报成99。内部计数及金额+/-改用system.main函数。同名SUM宏还揭示固定DuckDB1.5.6的窗口ToString省略catalog，先渲染再解析会失去系统绑定；编译改为直接嵌入已验证窗口AST副本。多个/单个/空共同域、SUM/COUNT/加减/window宏、交换来源逐项hash/字段/窗口来源及prepared后建宏都已通过。诊断SQL仍不能普通重放；它不是运行AST的完整目录/依赖表示。顾问只读、未运行测试；请求Astra/high，有效backend、effort、usage、billing未知，运行证据来自Lead。

[receipt](receipt.json)绑定最终扩展、源码、原件和公开派生件。所有19个同产物回归runner退出0：旧A1–A8仍只证明既有bounded口径/偏离；旧capture308/state19/grain42/relation55/fact78、观察/每日/关系加载与对象、三个旧MCP43/38、16/28、14/13，以及每日/关系18/20重封伪造拒绝通过。标准DuckDB1.5.6客户端单次加载实际扩展并验证五schema、完整User/窗口与非执行计划，Bun/TypeScript复验脚本已保存。121 Bun/544断言、类型、原生/TypeScript格式、架构、diff检查通过。没有模型调用、推送、合并或发布；当前跨平台/tidy仍需最终P1门禁。

原件保存在忽略的 `.local-agent-evidence/fact-api-native` 及两个各自私有离线档案；首次归档和补充manifest均只读保存。公开日志替换本地路径、规范末尾空行，三个MCP摘要另省略体积大的sessions，完整RPC载荷留于原件。早期COUNT(*)渲染预期、宏/窗口失败、旧注册数27/25与新增32/30不符、stock脚本变量遮蔽process的失败均保留；未以最终通过覆盖原始失败。

下一原授权工作为此profile独立MCP工具/资源与严格decoder、完整成员/类型/日期/分母对照，再无模型场景/重放/私有证据和CI。状态/历史业务口径仍待答；最终完整P1函数兼容矩阵、跨平台/tidy、历史私有信息审计与本地交付仍必需。

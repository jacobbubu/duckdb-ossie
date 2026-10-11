# 下一实施阶段：多事实、不同粒度和观察行窗口

本契约沿OSSIE-REQ-001与完整P1目标，是下一阶段的技术选择，不是当前已支持的能力。原研究SQL与整数分oracle保持不变。关系/MCP/无模型工作包闭合后，Lead实施独立有界FactProgram及新显式 `multi-fact-user-program-v1`，保留旧Compile单粒度/fan-out拒绝和四个现有profile行为，不伪造旧classification/qualification角色。

首个实际输入为User实体，以及RevenueObservation(User/date)和CostObservation(User/date/product)两种完整根身份。映射使用既有core/ontology/mapping格式，User跨事实通过明确的同一对象身份角色绑定，不能只凭两个字段标签相同认定实体相同。每事实字段和完整主键需全批预检，User/product非空原始VARCHAR、日期非NULL且有限、金额实际DECIMAL(18,2)。输入金额单位需明确声明为同一currency/scale，不能从列名或AST推断。单位元数据是该项目profile的明确政策，不能称为上游Ossie强制单位标准。

独立聚合支持每事实SUM和COUNT(声明字段)，明确COUNT金额为非NULL值数、COUNT非NULL身份为来源行数。AVG、COUNT(*)、聚合DISTINCT/filter/order、嵌套聚合、无来源或跨事实聚合叶节点先拒绝。派生指标仅允许同币种金额SUM组件间有界+/-，不能把金额与人数相减；实际结果类型、DECIMAL精度提升、溢出失败及NULL传播必须经绑定/执行验收，不能只凭语法形状断言。全NULL SUM保持NULL，不填零或替用户分配收入到产品。

请求必须显式选择 `domain=common` 和整个捕获范围；首片没有隐式日期/产品筛选。共同User域按两边事实行存在性定义，不按SUM非NULL，故C属于共同域，单侧D/E不属于。先分别按映射User归约至各一行，再组合金额组件；禁止连接原始明细后聚合。A的375/85/290对照及错误原始连接525为实际插件门禁，不以普通SQL探索代替。

复杂人群是明确共同域内的TRUE成员集合。每个金额/计数字段比较产生TRUE成员，交/并/集合差以集合代数组合；UNKNOWN不成为该原子集合成员。A差B为A的TRUE成员减去B的TRUE成员，不能写成普通SQL NOT(B)而在B为UNKNOWN时漏掉A。人口筛选在共同域定义之后，不能让并集带入D/E；结果保留完整User成员与声明分母/条件，不能只比较人数。首片不提供比率。

窗口使用独立Revenue行粒度入口，输入域为完整捕获Revenue行，分区为实际User、唯一有限DATE顺序。只开放用例所需LAG(value,1,NULL)和SUM ROWS n PRECEDING至CURRENT ROW（n<=366，最多n+1行）。全捕获范围上先计算窗口，再应用明确输出日期范围；首片不支持改变窗口基础行范围的筛选、跨事实先连接或默认日历补行。稀疏日期不等于前一天，LAG NULL可能来自没有前行或前值NULL，不能断言状态变化。观察行窗口不代替日历窗口、状态或历史有效期的剩余P1。

父级不可变包拥有两个独立冻结事实集合，两源来自同一数据库、同一源事务。复用native捕获与SnapshotTable的AST依赖，但每child必须owner=self、正确dataset别名/collection，不能指向父collection或形成反向强引用。private执行连接固定binary；parent的快照/hash绑定有序事实名称、完整身份、各内容和定义，两源任一失败/取消不得发布。prepared同时保留两个旧集合，源删除仍可执行；不能一旧一新。限制为两事实、每事实10000行、合计canonical捕获16MiB（含空表元数据）、最终完整JSON1MiB，不能声称峰值内存同限。规则/依赖/expanded AST需复制前预算，不能无界展开。

最小实施验收包括：实际525 fan-out反例、全NULL C及单侧D/E、UNKNOWN下集合差、同人数不同成员、范围外重复身份、交换映射角色、两捕获之间并发写仍一致源事务、第二捕获失败保持旧包、prepared跨reload/源删除、稀疏DATE/NULL前值/窗口边界/输出筛选阶段、类型/单位不兼容与DECIMAL溢出拒绝。每个支持路径还必须有对象/解释/发现/MCP/无模型和旧回归，最终规范/函数兼容矩阵与质量/tidy/交付历史审计仍必需。

复用只读Advisor检视方案，Lead按固定源码核实并采纳共同域行存在性、总集合差、逐叶聚合粒度、明确拒绝AVG、单位与精度、窗口输入/输出阶段、合计预算和child依赖/同事务建议。请求Astra/high，实际backend/effort/usage/billing未知。下一动作是原生typed binder/阶段化AST，再集成实际捕获/类型/执行，不把独立编译模块称作已发布profile。状态/历史业务问题仍待决；整个P1不缩减。没有真实模型、推送、合并或发布授权。

## 双事实加载后的公开接口契约（2026-10-11）

实际加载已由本地提交 e39637e 和[加载证据](../p1-fact-load-controls/review.md)证明；本节冻结下一工作包的接口，尚未声称已注册或通过验收。沿当前 Binder 的真实上限：两事实、regular指标1..32、窗口声明0..16、声明名为至多128字节的ASCII标识符。不得把core最多48个总声明误写成可选48个普通指标。

提供五个专用函数：`ossie_fact_users_json(request_json)`、`ossie_fact_users_compile(request_json)`、`ossie_fact_windows_json(request_json)`、`ossie_fact_windows_compile(request_json)` 和无参数 `ossie_fact_program()`。通用旧人口接口仍拒绝此profile。users/window 分别绑定实际类型，并分别编码结果、非执行计划；发现不计算common或请求结果。

用户请求必须完整指定 `{"domain":"common","input_scope":"entire_capture","metrics":["balance"],"cohort":{"operation":"all"}}`。metrics选择1..32个已声明regular名称，按声明规范拼写输出，解码后大小写重复拒绝。cohort结构严格：all仅含operation；predicate另有非空predicate字符串；intersection/union/difference另有恰好两个有序children，不能携带不适用字段。最大64节点/16深度，在递归构建前检查。predicate沿已有Binder只接受单个regular指标与一个可表示精确数值的比较；允许引用未投影的指标，计划/结果保留原条件，不能只按投影来判断依赖。条件文本不是任意SQL入口，额外语句、非比较、函数、列运算、NULL/浮点常量及不适用计数常量均拒绝。请求整体UTF8最多16KiB。

`common_user_count`是两捕获各自按User去重后的交集人数，位于cohort之前；`selected_user_count`是cohort之后的完整User人数。两者以整数文本编码，不是来源行数、非NULL金额数或比例。另建只投影User、分别去重后相交的分母AST，持有同一父及两个child；禁止借所有指标的all查询或优化器裁剪来间接计算分母。即使selected为空，仍返回实际common人数和零selected。若分母/成员任一步失败或取消，不返回部分JSON。

窗口请求为 `{"input_scope":"entire_capture","windows":["previous","rolling"],"display":{"mode":"all"}}`，或display明确mode=date_range、start_date、end_date。选择1..16个已声明窗口，必须来自同一可执行User/date事实粒度；不对共同User域筛选，单侧User也保留。日期范围只控制输出，不改变窗口基础行，不限制范围跨度、不补日历行。日期文本使用固定DuckDB DATE规范字符串：实际可解析、有限、往返ToString与请求完全一致，包含BC和宽年份；不复用daily的0001..9999解析器。start<=end；infinity、epoch快捷字、非规范别名、NULL、隐式all均拒绝。all不允许携带日期字段。

每个bind一次抓取不可变父shared_ptr，Prepare与执行均在固定binary的私有Engine和query_lock内进行；query AST显式持有正确双child及父依赖。prepared跨reload/删除原表继续使用旧父。不同RPC各自绑定快照，发现、编译、执行跨调用不保证原子相同；结果和计划携带snapshot/content/core/ontology/mapping/manifest六项身份以便核对。

结果schema为 `ossie-fact-users-result-v1` / `ossie-fact-windows-result-v1`。返回已验证请求、用户身份关系、单位和显式范围、实际绑定columns(name/ordinal/bound_type/encoding)、完整rows和row_count；用户结果另含上述两个User计数。窗口结果另指明dataset/概念及完整source identity字段、来源captured_rows、User/date输出身份和“先全输入计算再筛输出”的阶段。User和DATE不允许NULL/重复输出身份；Decimal及整数按实际类型精确文本编码，NULL保留null。按原始User UTF8字节、窗口再按DATE日值升序排序并核对唯一性。max10000结果行，完整内层JSON最多1MiB，包含所有元数据/请求/escaped值；没有分页或截断，超限拒绝。此上限不是峰值内存或MCP外层wire字节保证。

compile schema为 `ossie-fact-users-plan-v1` / `ossie-fact-windows-plan-v1`，提供同样绑定身份/请求/列类型/逻辑阶段，以及不带可重放保证的诊断SQL；executed=false、members_computed=false、counts_computed=false、physical_plan=false、sql_replayable=false，不提供伪造的common/selected/结果行计数。窗口源captured_rows是加载事实，不是结果计数。计划也有独立完整1MiB编码上限。

program发现schema为 `ossie-fact-program-v1`：两源声明dataset/concept/User/id、完整身份字段、映射角色及实际类型、captured_rows/child hash、显式currency；regular/window原始定义、规范名、单位、输出实际类型及窗口source；完整资源/支持/拒绝边界、common行存在语义、TRUE集合语义、窗口基础行范围/排序/帧和输出过滤阶段；六项父身份及项目API版本。声明五个专用函数，native仍mcp_registered=false，由后续MCP实际工具声明，不能让native推断外部注册。发现不执行对象查询，common/selected计数未计算。

只读Advisor反证后采纳真实32/16/128上限、独立分母AST、空结果保留分母、DATE全有限往返规范、未投影条件指标、严格递归前预算与RPC单次快照边界。请求Astra/high，有效backend/effort/usage/billing未知；顾问未执行测试。Lead后续必须以实际插件验收：common3/selected0、全NULL C、UNKNOWN差集、union不带D/E、未投影条件指标、窗口包含单侧User、稀疏日期先算后筛、BC/有限极值、case区分、请求和结果字节边界、计数后取消、prepared跨reload/drop，以及compile未执行。随后接MCP/无模型、全部旧回归；最终整个P1/状态历史待答/兼容矩阵/跨平台tidy和历史审计保持。

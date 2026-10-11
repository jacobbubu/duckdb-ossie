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

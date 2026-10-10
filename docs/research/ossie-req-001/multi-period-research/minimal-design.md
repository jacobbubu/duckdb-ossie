# OSSIE-REQ-001：用户与每日观察契约及最小实现设计

2026-10-10。用户授权明确契约并设计最小实现，本文件是可评审设计，不是实施完成或新增规范要求。沿原P1重复实体/多期状态项，固定Ossie 8dd6732、0.2.0.dev0与DuckDB1.5.6。原研究、475行数据、两个生产profile及Agent原始证据保持不变。

## 1. 本切片采用的明确契约

| 对象/事项 | 决定 |
| --- | --- |
| User身份 | user_id标识同一个人；与日期/产品无关。VARCHAR非NULL、非空字符串，原始值区分大小写、空格和Unicode编码，不trim/casefold/规范化。 |
| DailyObservation身份 | typed tuple(user_id,ds,product)，每个用户/日期/产品最多一个日快照；不是把三个值拼接成新用户ID。 |
| ds | 非NULL、有限DATE；拒绝正负infinity，不接受TIMESTAMP自动截日。以数据里已提供的日历日为准，不推断时区。 |
| product | 非NULL、非空VARCHAR，原始精确比较；同日同用户X/Y是两条观察、一个用户。 |
| observation_id | 8行研究表的o1等仅为诊断标签；新切片无需此列，不能用它掩盖重复业务tuple。 |
| 重复tuple | 完全相同与值冲突都在全批预检时拒绝；无DISTINCT吞记录、任取一行、latest-wins。即使位于请求范围外或规则不合格也拒绝。 |
| 重复User | 不同日期/产品重复user_id合法；User对象及User.id链接以声明的集合语义出现一次，所有合法观察保留。 |
| User属性 | 第一切片只绑定id，不在User上绑定日期、产品、分类值或金额，避免多条观察互相覆盖标量值。 |
| 分类状态 | QualifiedObservation是DailyObservation的记录级派生，classification_value >= 固定阈值；只有TRUE选中，NULL不选中。各日期独立，不carry-forward或回改历史。 |
| 期间用户集合 | 对当前规则与请求范围内合格观察做user_id存在式投影；至少一条合格即计入，不代表每天合格或期末合格。 |
| 数据源 | 单张committed native表，全批冻结；不能先查询筛选再验证身份。collated/coerced/computed身份键拒绝，实际比较与distinct必须一致。 |

上述raw字符串规则允许含空格的非空ID；A、a、A空格是不同ID。若业务要求清洗，应在导入前另行建立审核后的身份规则，执行器不猜测。空字符串和NULL错误与SQL NULL金额语义分开。产品和分类值是观察属性；本设计不主张某个用户跨产品分类值必定相同。

最多一条日快照是本profile选择，不是所有事实表的天然规律。多笔事件、多次修订必须另选业务粒度，不能塞入本切片。这里的amount_usd是声明为可按观察相加的合成金额；不代表期末余额或累计YTD，不把真实累计值跨日求和解释成收入。单位从已审核指标元信息取得，不推断或换汇。

## 2. 统计对象与分母

先得到S={全批中的观察，满足正式规则及请求过滤}，按请求分组G形成S_G。每组独立计算，不把日/产品用户数相加恢复期间人数。

| 注册指标的语义 | 最小允许表达式 | 分母/NULL行为 |
| --- | --- | --- |
| observation_count | COUNT(observations.user_id) | user_id已经全批非NULL，因此等价COUNT(*)；不新增通用COUNT(*)支持 |
| user_count | COUNT(DISTINCT observations.user_id) | S_G中不同User ID，NULL金额用户仍计入 |
| amount_value_count | COUNT(observations.amount_usd) | 非NULL金额观察数 |
| amount_sum | SUM(observations.amount_usd) | 非NULL观察金额，空集或全NULL返回NULL |
| mean_observation_amount | AVG(observations.amount_usd) | amount_sum/amount_value_count；分母0返回NULL，DOUBLE近似 |

名称是示例，不靠名字判断能否执行。新profile按展开后的AST及绑定字段只允许上表五种表达式形状；任何改名后的二阶段聚合、SUM/COUNT DISTINCT比率、算术组合AVG或窗口仍拒绝。first slice dimensions只允许date/product；filters只允许对已绑定观察字段的既有标量缩小谓词，不能改规则或触发用户级重新聚合。正式派生规则限绑定classification_value与整数阈值的>=比较；其他规则形状先明确capability错误。

按[8行独立见证](observations.csv)，阈值10全产品两日：6观察、3用户、5非空金额、总额31.00、观察金额均值6.2。只X：4观察、3用户、3非空金额、总额20.00、均值20/3。空集：三个count为0，SUM/AVG为NULL；只有C：观察/用户1，金额count0，SUM/AVG为NULL。

“每个用户均值再平均”(19/3)、“用户期间总额再平均”(15.5)、“总额除以所有用户”(31/3)都是不同问题，第一切片不提供；不能把6.2称作一般“每用户平均金额”。金额、用户人数、观察数必须在发现/结果说明中明确。

## 3. 最小模型、mapping及实现范围

新opt-in项目profile暂定daily-observation-population-v1。独立冻结一张观察表，三个EntityType：User（引用对象）、DailyObservation（可执行根）、QualifiedObservation（派生）。旧gaap-scalar与fact-dimension两profile保持各自验证和输出契约。

[core草图](design-drafts/core.json)、[ontology草图](design-drafts/ontology.json)、[mapping草图](design-drafts/mapping.json)全部使用固定规范已有字段，不增加上游字段。core.primary_key为[user_id,ds,product]；User.identify_by=[id]；DailyObservation.identify_by=[user,date,product]，user角色目标User，date目标Date，product目标String；amount/classification属于Observation。阈值20实验只改derived_by常量，不能修改指标或提示词。

参照[标识关系](../sources/apache-ossie/ontology/ontology.md:270)与[复合映射](../sources/apache-ossie/ontology/ontology.md:550)：Observation的user referent可以嵌套User.id，但这不增加关系元数。Observation根link tree的user链接必须是第二层，User.id另在User根link tree第二层，不能把它作为Observation.user下的第三层children制造三元关系。User重复object/id-link为集合构造，必须在观察tuple验证后派生；不消除观察记录。

第一切片只执行DailyObservation/QualifiedObservation对象与指标请求；直接查询User对象/指标明确拒绝并在发现中标记referent-only。user_count是观察范围内的实体投影指标，不表示已实现一般User粒度执行。对象输出保留user_id、ds、product作为三个普通typed列，不制造entity_id字符串；完整身份来自三列及绑定的profile/快照，User身份仅第一列。见[元数据与输出示例](design-drafts/metadata-example.json)。

## 4. 代码影响及兼容边界

主要实现仍在[src/population/population.cpp](../../../../src/population/population.cpp)，但需按profile隔离绑定与身份表示，不能只放宽duplicate检查：

1. 定义绑定：替代新profile单个Bundle.identity假设，以IdentityBinding保存有序typed组件、User referent和观察根；仅解析上述3概念、2个标准concept mappings及二元关系形状。旧profile沿旧分支。
2. 捕获验证：同一来源事务冻结后检查各键非空/有限DATE/类型与精确比较、全批tuple唯一，再生成唯一User集合。hash只作索引/追溯，碰撞后仍比较完整typed tuple，不能用hash代替身份。
3. 计划与执行：正式规则解析到观察字段，先人口范围再GROUP BY；五种指标AST单独检查。对象投影tuple各列，User请求在bind阶段拒绝。复用现有不可变collection、atomic发布、prepared bundle强引用、取消和失败保留旧状态。
4. 发现与计划：新ossie_concepts_v2输出identity_components_json、referenced_entity_identity_json、objects_queryable、metrics_queryable等明确角色，旧profile也可适配到v2。新profile plan-v2区分“Observation EntityType与记录双射”及“引用User与记录非双射”，不写全局entity_equals_record=false/true混淆两者。
5. typed结果：仍用result-v1各scalar列及既有快照/定义trace；plan/concept metadata解释身份列角色。旧输出字段/类型/规则不变，不把复合键塞进旧identity_field。

兼容选择依据[现有版本约定](../../../population-discovery.md:56)：新增函数/profile是兼容增量，拟API2.1.0，非整体API3。metric/field discovery-v1与旧profile plan-v1保持；新concepts专用schema-v2、new profile plan-v2，capabilities新增所选概念发现函数/schema。

| 入口 | 旧两个profile | 新观察profile |
| --- | --- | --- |
| ossie_concepts | 原输出照旧 | 明确拒绝，指出ossie_concepts_v2，不能伪造单字段身份 |
| ossie_concepts_v2（新增） | 适配原有单字段身份 | typed组件及referent-only User |
| 对象/指标SQL | 原契约照旧 | 仅两个Observation概念 |
| compile/plan | plan-v1原样 | plan-v2身份/人口元信息 |
| metric/field discovery、typed结果 | v1原样 | 可复用的v1形状加profile约束 |
| 原population MCP示例与Agent用例 | 原资产照旧 | 第一切片不接入，不宣称旧资源绑定兼容 |

新ossie_concepts_v2列契约拟定为：concept/parent/derived_by为VARCHAR（可空parent/derived_by）；identity_components_json与referenced_entity_identity_json为VARCHAR中的结构化JSON，referenced entity不适用时为SQL NULL；objects_queryable/metrics_queryable为BOOLEAN；captured_observation_rows/captured_user_count为BIGINT；再附与当前一致的profile/source/snapshot和五份定义/content哈希列。计数描述全批8观察/4个不同用户，不伪称QualifiedObservation的已过滤行数；发现不额外执行请求范围统计。组件顺序遵循identify_by，包含relationship/field/bound_type/referent说明。具体示例中的数组和值只描述新候选输出，未运行扩展。

## 5. 验证与实施顺序

独立普通SQL已验证空集、只有NULL金额用户、分类NULL、整tuple重复检测、raw ID大小写/空格/Unicode区分。拼接反例中typed tuple有2个，分隔符拼接只剩1个，证明不能拼接身份。有限DATE/isfinite反例也已核对。结果与hash见[契约核验](contract-verification.json)、[设计核验](design-verification.json)。这些不是新profile运行结果。

草图JSON语法、引用/身份顺序、二元role数量和User.id独立link树检查通过。当前检查的Python运行时无jsonschema，未安装依赖；完整固定schema校验尚未运行，不能称schema accepted，实施首门禁必须补齐离线固定schema校验及拒绝图形。

设计后的本地最小实施建议按依赖顺序：先固定草图/typed身份/全批预检与不可变载入（仍不执行查询）；再加入观察对象、五指标及v2发现/plan；最后以独立普通SQL/逐行精确计算验证新profile并回归旧profiles。同一实现切片内完成元数据才能称可交付，不把中间loader接受当执行完成。

验收必须包括：8行两阈值及用户集合；同tuple完全重复/冲突及范围外重复；NULL/空key、infinity DATE和collation/coercion拒绝；拼接碰撞/大小写/Unicode；NULL金额/空集/非可加用户数；两stage伪装指标拒绝；不同快照和prepared旧/新bundle存活；failed load rollback、取消、并发读；旧scalar/join全量行为与typed结果不变。实现过程不得让User重复处理影响旧profile或在过滤后校验。

## 6. 审阅结论、完成与剩余

复用既有Advisor一轮架构反证，requested Astra/high；effective backend/effort/usage/billing未知。Advisor认可语义与仅观察执行范围，指出mapping深度、观察也是EntityType、COUNT(*)现限制、collation/finite DATE、AST级限制和兼容minor版本问题；Lead对照固定spec/源码/版本文档落实。

契约与最小设计包完成，本轮生产代码、旧profile、原始数据/实验未改，没有模型调用/推送。剩余必要是用户授权本地最小实现后完成schema门禁、新profile功能和旧profile回归；不是自动授权MCP/Agent追加运行、分页、多事件、历史关系/as-of、calendar、窗口或混合粒度。

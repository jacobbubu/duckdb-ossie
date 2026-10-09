# OSSIE-REQ-001 本体执行基线研究

日期：2026-10-09。状态：首轮研究完成，本地实施已授权并开始固定 feature 的构建基线；P0 尚未完成。

本文件是 OSSIE-REQ-001 在本仓库的研究与验证记录，不另建实施 backlog。原始需求位于 `/tmp/duckdb-ossie-requirements.7kw5ic4l/需求.md`，保持不变；用户已决定仅支持固定快照的新格式，替代原 R1.3 的新旧共同支持要求，并相应调整 A8。

## 范围与授权

初始授权为基线研究、独立数据基准及最小执行设计。2026-10-09，用户对“本仓库本地实施、独立分支、先构建复核 feature 再补基础校验和最小单表执行”的第三项回答“没问题”，因此本地实现和测试已获授权；上游 PR 修改、推送、合并、发布或部署仍未授权。规范依据解释后，用户回复“好”，采用首阶段 YTD 阈值人口、active/paid 未验证、默认 SQL 分组与 NULL 行为、日历补齐另列显式能力。后续实现按此范围推进，不再重复询问。

研究的问题：现有 `feat/ossie-spec-0.2` 能复用什么；固定规范究竟定义了哪些执行语义；DataBridge 示例的对象、粒度、单位和结果是什么；最小概念执行链路如何验收。

验收：保存来源提交与文件哈希；逐项核对现有代码及规范；以独立 SQL 和独立逐行计算核对 475 行的逐日人数、金额与平均值；提出带拒绝边界的执行设计，并独立审阅。研究完成不等于 P0 或 A1—A8 完成。

## 结果与影响

现有 feature 分支可以复用新格式加载、表达式 AST 转换及粒度保护，但尚无 ontology/mapping 执行层。首个实现切片应证明：同一指标和数据批次下，只改变 `ValidUser.derived_by` 的阈值，实际对象范围、SQL 和聚合结果同步变化。以下是已审阅的最小设计；实施授权见上节。

原始数据的独立 SQL 与 Python Decimal 计算逐日一致，复核了原示例全部 15 个日均值。3 月 10 日：

| YTD 阈值 | 对象数 / 记录数 | GAAP 总额 USD | 未舍入均值 | 两位小数均值 USD |
|---|---:|---:|---:|---:|
| 10 | 37 / 37 | 1697.80 | 45.886486486486… | 45.89 |
| 20 | 21 / 21 | 1277.54 | 60.835238095238… | 60.84 |

这里的对象数等于记录数是本批数据实测事实，不是普遍规则。阈值 20 的结果是本次独立基准，不是原示例声明，也不是本体执行结果。两种阈值分别在完整期间选中 166 / 63 条产品 X 记录，总额为 2265.86 / 1472.73 USD。

## 基线与来源

- 本地 main 为 `9d432f4`，工作区开始时干净。
- 本地 remote feature 提交为 `c2947c7de8e583b70e3f8b859a94c0b9830a36a4`；上游 PR #12 同一提交。
- DuckDB、extension-ci-tools 与 fkYAML 子模块未初始化，本地无构建产物。本次不能声称扩展本地构建或测试通过。
- fork `jacobbubu/duckdb-ossie` 禁用 issues，因此在本文件记录问题、范围和验证；不创建远端 issue。
- [来源与哈希清单](research/ossie-req-001/sources/manifest.json)保存规范、原始需求、DataBridge 来源文件及扩展提交。DataBridge 本地提交为 `e0bae363fcabe0ebf66bad8345001d7e8d0c4c56`，开始时工作区干净；本次没有修改该项目。
- 研究固定 Apache Ossie `8dd6732da354f22ca71f16d82626a46039ecdcd9`，与 feature 的 conformance fixture 基线相同。core 和 ontology 标为 `0.2.0.dev0`，expression 文档状态为 Proposed Final；这是开发快照上的实现试验，不能称为已发布 0.2 的全量兼容。
- feature 的 DuckDB gitlink 保存在 manifest；独立计算实际使用 Python wheel DuckDB `1.5.6`，引擎 source_id `069cc9f9b5`。这不是从该 gitlink 构建扩展的证据。
- [原始定义提取](research/ossie-req-001/sources/business-source-extraction.json)绑定 xlsx/docx 原文件 SHA-256，记录单元格坐标和段落索引；仅提取内容，未验证文档视觉布局。原文件保持不变。[对应关系审阅](research/ossie-req-001/source-correspondence-review.json)同时绑定提取与定义草案哈希，区分已核实内容、设计提案与待裁定业务描述。
- [原始数据派生 fixture](research/ossie-req-001/gaap-original-475.csv)从固定哈希的 `demo_rows()` 生成，475 行、475 个不同 user_id、日期为 3 月 1—15 日。CSV SHA-256 为 `e01a940a3f83ed6f5f22beb598e64206144658ad3b9fef89124e2790e7fc3e18`；这是生成器数据的确定性序列化，不是获取了原有运行数据库的快照。

## 规范发现与兼容差距

| 范围 | 已审代码 / 固定规范证据 | 对需求的影响 |
|---|---|---|
| 根级单模型 | feature `parser.cpp: ParseModel` 拒绝 semantic_model；schema 要求 version/name/非空 datasets | 与用户的新格式决定一致；parser 仅保存 version，未执行固定版本检查，也未拒绝空 datasets |
| 元数据 | `ParseAIContext` 只读 synonyms/instructions；`Field.is_time` 默认 false | 字符串 ai_context 和 examples 被遗漏；规范要求 Date/Time/DateTime/DateTimeTz 默认 is_time=true，显式 false 优先 |
| 字段类型与键 | datatype 原样保存；ValidateKeys 只检查空 unique_keys；SourceResolves 检查表是否存在 | 需补类型枚举、真实列引用和键预检，不能把表存在解释为列或数据键有效 |
| 方言 | OSSIE_SQL_2026 优先于 ANSI_SQL，专门 lowering；过滤器总按 OSSIE_SQL_2026 lowering | 已有转换可复用；必须公开过滤器语言及选择。不能从模型方言推断过滤器语言 |
| 允许语法 | 未知函数未被 LowerFunction 拒绝；model AST 没有完整白名单；filter 有可放宽函数开关 | 需独立校验原始和字段展开后的 AST/函数，禁止语义未知的透传；正式规则不能复用可放宽的请求过滤策略 |
| 粒度与连接 | 多粒度、fan-out、歧义路径拒绝，全部 INNER；窗口指标在查询时拒绝 | 保留保护；安全多表切片须实测唯一键及完整匹配，不能靠字段声明保证人数不变 |
| 对象查询 | 当前 Compile 要求至少一个有聚合粒度的指标 | 不能把空指标列表当对象查询；对象路径需明确入口并与指标路径共享人口规划 |
| 追溯 | compile/query 共用 Compile；描述接口未输出完整方言、定义哈希和计划 | 保留同源编译，新增真实编译产物；不将请求回显当执行证据 |

逐字段内部表示还应区分“缺省”“显式值”和“未知”，保留 name/description/label/ai_context/custom_extensions 的合法内容。单位不是 core 标准字段：本用例 USD 来自原 workbook `metric!E2`，不做单位推断或换算。标准 `custom_extensions` 的 data 是 JSON 字符串；研究样例未发明新的标准键。

### Ontology 不是 OSSIE_SQL_2026 的另一层包装

固定 expression 文档明确只针对逻辑层，本体语言另行提案。ontology.json 的 Expression 描述为 ANSI SQL string，ontology.md 却给出关系导航、EXISTS 和递归示例；converter 的 Formula 保存原始字符串。不能从 schema 接受字符串推出完整语法或运行语义。

因此首个执行器需要明确的实现 profile，并记录它在此开发快照上的语义约定。固定规范支持 entity/value type、单继承、identify_by、derived_by 与独立 mapping 文档；没有现成完整执行器可以直接嵌入本扩展。

独立 mapping 以 `ontology_ref` 与 `semantic_model_ref` 按 name 引用文档，IRI 可选。固定规范说上游 parser/converter 尚未支持独立 mapping。schema 跨文件 `$ref` 仍指向 main；研究校验使用固定本地资源注册这些 URI，不改变原始 schema 字节，也不实时抓取 main。

## 原始业务语义与冲突

- workbook `metric_formula!E2` 是 `AVG(CASE WHEN ytd_gaap >= 10 THEN gaap_val END)`，`metric!E2` 是 USD；日期范围 `metric_formula!F2` 为 1d。`gaap_val` 是当日值，`ytd_gaap` 是分类条件；没有将两者互换或将 GAAP 再推断为其他收入指标。
- docx 段落 7/10 将 ValidUser 描述为 active paying customer，并指定 ytd_gaap>=10。原数据没有 active/paid 字段。因此只能证明“满足记录中 YTD 阈值的演示用户”；不能声称同时验证活跃和付费。研究样例采用这一可执行描述，属于待评审的语义裁定，不是修复原文或自动发现冲突的能力。
- workbook `dataset_column!J2:K8` 将所有列标为非主键、可空；原 PostgreSQL init.sql 将 user_id 设为 PRIMARY KEY，其余列 NOT NULL。原 475 行实测 ID 唯一、无 NULL。本次按明确的物理 schema 导入，保留 workbook 冲突，不能把 workbook 元数据当真实性预检。
- 同一 user_id 不跨日出现，虽然表描述为每日用户快照。它实际证明的是“固定批次、每个 user_id 一个观察记录”，不是现实多期用户状态。日/月过滤不会改变实体身份或把一个用户解释成多个独立用户。
- docx 中事件和因果陈述是故事来源。本次不验证其因果性，也不将故事变成查询规则。

## 最小执行设计草案

### 定义、绑定与加载

[研究定义目录](research/ossie-req-001/proposed-definitions/)含同一 core、同一 mapping，以及阈值 10/20 两个 ontology 版本。所有文件使用标准字段；四份文件通过固定 schema 的离线形状校验。两份 ontology 唯一差别是 `ValidUser.derived_by`，指标与 mapping 不变。它们尚未被扩展 parser 接受或执行。

建议 profile 暂记为 `gaap-scalar-population-v1`，身份及元信息放在外部 definition bundle manifest，不冒充 Ossie 标准字段。bundle 显式指定 profile、文档名称、来源哈希及本地文件；name 必须一致，SHA-256 必须一致，无网络自动追随。仅接受新版本 core/ontology/standalone mapping，拒绝旧包装、旧 embedded mapping 与冲突格式。

User 使用 identify_by 的单一二元 id 关系，映射到 user_id。product/region/user_type/date/ytd_gaap 是 User 的至多一个值的二元关系；mapping 的 root object_mapping 对应 User，children 对应这些值关系。ValidUser 只继承 User 的身份与已绑定属性，不单独映射人口，不覆盖身份或属性。首个切片只支持一个父类、一个派生表达式、一个人口根数据集、一组唯一绑定。

### 人口与求值

第一表达式仅需 `User.ytd_gaap >= 10`。受控 resolver 根据 ontology 关系与 mapping 将它解析为 logical field AST，再交给现有 AST 编译基础；绝不通过字符串替换名字或拼接 SQL。先支持标量引用、数值常量和比较，之后按 R3 验收逐项扩展算术、布尔、CASE、NULLIF/COALESCE。profile 不能被称为完整 ontology 语言。

在整个绑定批次、请求过滤之前，验证标识非 NULL、标识—记录双射以及每个标量属性唯一；重复完全相同的行也拒绝。不能通过 DISTINCT、单日过滤或任取一行掩盖多期记录。预检和执行必须共享不可变导入快照或同一事务快照；外部 batch_id 本身不是数据不变的证明。

本体关系是 set 语义且不含 NULL 链接；ManyToOne 是至多一个，不是必有值。实现 profile 约定：物理属性 NULL 表示缺失关系，缺失标量导航求值为 SQL NULL，派生谓词只有 TRUE 纳入人口。身份缺失仍是错误；不能把 NULL 创建成值对象。这个求值约定需要在后续 parser/执行测试中单独验证。

规则校验与请求过滤分别处理。正式规则禁止聚合、子查询、窗口、递归、非确定性或未知函数，且要验证展开后的映射字段。请求过滤只能以 AND 缩小已确定人口，不能替换规则、改变正式方言、放宽函数限制。派生规则作用于记录观察，在聚合前执行，不把记录分类冒充跨日期实体分类。

`requires` 是约束，不是人群筛选。执行依赖闭包含父类、标识关系、属性目标类型、mapping 和字段展开。闭包出现不支持的 requires、派生关系或语言构造时明确拒绝。无关未执行定义可以保留，并在发现接口标为不可执行。自然语言与正式规则的对应需要 hash-bound 的人工审阅记录；不能承诺任意自然语言冲突自动判定。

### 同源对象、指标和解释

共享人口计划至少包含根数据集、身份绑定、派生谓词、请求过滤、实际日期范围、影响人口的连接及数据快照约束。对象查询与指标查询都使用它，不能只共享一段 WHERE。

对象结果明确输出实体 ID 及记录标识；本 profile 二者双射，未来多个事实记录不因此自动去重。指标结果使用 `AVG(gaap_val)`，验证时同时保留 SUM、记录数、对象数与 COUNT(gaap_val)。NULL GAAP 不进入平均分母。对象查询、聚合查询、概念发现和编译说明需要不同契约，SQL 函数名称在接口设计阶段确定。

空组策略：默认只返回概念范围内有记录的分组；全空标量聚合 COUNT=0、SUM/AVG=NULL；全空分组查询无行。旧 CASE 指标可能保留无有效用户的组并返回 NULL，本设计不承诺这一展示行为不变。若需要完整日期骨架，单独定义补齐能力。

安全连接初期只接受明确 INNER fact→unique dimension 形态，并在同一快照验证非 NULL 事实外键、完整匹配与实际唯一维度键；缺失、重复、fan-out 或歧义时拒绝。对象和指标人口不能因投影不同维度而变化。LEFT、role-playing、多事实、多粒度组合仍待独立设计，不关闭现有保护。

编译产物来自实际绑定与规划结果，包含定义 bundle 哈希、profile、规则/属性 AST 对应、选中方言、实际过滤/时间范围、粒度、连接、限制和执行 SQL。服务记录导入 batch 与执行引用；仅编译返回 compile artifact，实际执行后才返回 execution artifact。排序、分页与截断必须声明；默认不保证顺序、不做静默截断。错误至少含 object/stage/reason/missing_capability。两个同名 ontology 版本每次必须以哈希唯一选定一个，不能在名称注册冲突后任取。

## 验证证据和未通过的验收

| 检查 | 本次结果 | 能证明什么 |
|---|---|---|
| 原始 475 行导入及独立计算 | 已运行，两种阈值每一天匹配；原阈值全 15 个公布均值匹配 | 原示例在固定 DuckDB 上的数值基准，不证明扩展编译 |
| 对象 ID 集合 | 两种阈值完整逐项比较 SQL 与 Python 选择，人数一致 | 独立人口预期，不证明概念执行 |
| 地区/用户类型分组 | 每组 SUM/COUNT 与独立逐行汇总一致 | 正确的分组基准，不将均值直接相加 |
| 人工边界 fixture | 已运行阈值相等、缺失 YTD、缺失 GAAP、零值、空集、月界及 CASE/WHERE 差异 | SQL 与拟定 profile 边界的预期结果，不证明 parser/扩展行为 |
| 四份研究定义 | 固定 schema 离线接受、引用 name 一致、仅阈值差异 | 文件形状与变更边界；不证明内部语义、规则求值或数据绑定 |
| feature CI | 当前 PR 记录检查成功；PR 描述报告既有测试 | 上游该提交的已有检查；本次未本地构建，不当作新需求验收 |
| A1—A8 | 全部仍待扩展实际验收 | 独立基准仅准备好了部分验收依据 |

[基准结果](research/ossie-req-001/baseline-results.json)、[边界结果](research/ossie-req-001/boundary-results.json)、[形状校验结果](research/ossie-req-001/definition-shape-results.json)均注明证据范围。[复现说明](research/ossie-req-001/README.md)列出固定依赖与运行方式。

### 扩展实现后必需的正反例

| 输入 / 操作 | 必需结果 | 当前状态 |
|---|---|---|
| 阈值 10→20，同 core/mapping/data | 实际规则 AST、SQL、对象 ID 集合与 SUM/COUNT/AVG 一致变化 | 已有独立预期，扩展待实现 |
| 阈值相等/缺失 YTD/缺失 GAAP/全不合格组/月界 | 遵守上述 NULL、空组与半开日期契约 | 已有人工 SQL 控制，扩展待实现 |
| 相同 ID 跨日、重复相同行、冲突属性 | 全批次预检拒绝，不能先过滤再掩盖问题 | 拒绝路径待实现及实测 |
| NULL ID、独立映射 ValidUser、属性覆盖或重复绑定 | 对象/阶段/缺失能力明确报错 | 待实现及实测 |
| 父类/标识关系/属性值类型含 requires | 执行依赖闭包拒绝；不转 WHERE | 待实现及实测 |
| 映射字段间接聚合/窗口/子查询/未知函数 | 展开后在执行前拒绝 | 待实现及实测 |
| 完整唯一维度连接 | 对象和指标人口一致、数值与独立基准匹配 | 多表 fixture 仍待准备 |
| 维度重复键、缺失键、NULL 外键、fan-out、歧义路径 | 预检或规划明确拒绝 | 新 fixture 及独立基准仍待准备 |
| 引用 name/hash 错误、未知版本、未固定 schema URI | 加载/绑定前失败 | 待实现及实测 |
| 固定定义重跑/改变数据快照 | 定义可定位；快照变化不能被旧预检误覆盖 | 待实现及实测 |

## 独立设计审阅

触发：ontology 语言、实体/记录和安全连接的架构取舍。使用 advisor-escalation 的单轮只读 Advisor；请求配置为 Astra/high，工具未给出可核实的有效模型、effort、usage 或 billing，这些均为 unknown；没有嵌套委派。

结论是有条件接受首个切片，不认定 R4/R5 已满足。已核实并采纳：缺失链接与 SQL NULL 的区别、CASE/人口过滤空组差异、COUNT(gaap_val) 分母、全批次双射预检、同一快照、人口连接共享、闭包 requires 拒绝、禁止派生概念独立映射。依据是固定规范、当前编译器代码，以及本次新增人工边界 SQL 控制，不以模型意见代替证明。

同一 Advisor 对具体四份草案做了一次限定复核，未发现致命定义冲突，提醒继承身份需要实际验证和同名版本按哈希选择。发现两处标注问题并已修正：草案不能称谓词已 approved；DuckDB 1.5.6 的 AVG(DECIMAL(18,2)) 实际返回 DOUBLE，Lead 以 typeof 查询独立确认，将 mean_gaap 的逻辑 datatype 改为 Float。底层金额仍为 DECIMAL(18,2)，没有隐式单位或金额换算。定义哈希和形状检查在修改后重新生成；不以 datatype 标签暗示实际强制转换。

## 剩余必要工作与下一步

本轮研究交付完成。整体 P0 未完成，必要工作仍包括：业务语义裁定评审、profile 与接口契约定稿、feature 本地构建和原有测试复核、新版本/元数据/表达式边界补齐、概念/mapping 编译、同快照预检、追溯产物、多表拒绝 fixture，以及 A1—A8 实际运行。

下一已授权工作包：在隔离 checkout 中固定现有 feature，先构建并运行现有测试；以研究样例补 R1/R2/R3 的缺口，再实现首个单表 User→ValidUser 执行切片。不能把“schema 接受研究样例”作为切片完成。业务定义和空日期输出已按用户接受的建议确定；推送、上游 PR、合并与发布分别保留授权边界。

本地实施准备：已创建并附加 `/Users/rongshen/.codex/worktrees/ossie-execution/duckdb-ossie`，分支 `codex/ossie-execution` 从固定 feature `c2947c7` 开始。三个子模块已按记录提交初始化：DuckDB `069cc9f9b5be802405797faecc284961b07c70ef`、ci-tools `3fd6109fc01555673b7c7123eba8d0d143fe8ce4`、fkYAML `1818a25156b9b1a65a9c9b6126631e2cd7cfa8f6`。这一准备没有修改扩展源码，也还没有构建结果。

### 用户决策的规范依据

- [固定 Ossie 的 Derivation expressions](https://github.com/apache/ossie/blob/8dd6732da354f22ca71f16d82626a46039ecdcd9/ontology/ontology.md#derivation-expressions)定义用表达式从父类派生人口，没有规定 ValidUser 的业务阈值，也没有从金额推断 active/paid 的规则。原示例的可执行条件是 YTD 阈值；其他状态仍缺数据。
- [DuckDB WHERE 与聚合的顺序](https://duckdb.org/docs/current/sql/introduction#aggregate-functions)规定输入过滤先于分组/聚合；[聚合与 NULL](https://duckdb.org/docs/current/sql/functions/aggregates#handling-null-values)规定 AVG 忽略 NULL、空聚合除 COUNT 外返回 NULL。本地 1.5.6 人工控制已独立验证相关行为。
- [dbt MetricFlow 日期与 NULL 配置](https://docs.getdbt.com/docs/build/fill-nulls-advanced)把 join_to_timespine（补日期）与 fill_nulls_with（填值）分为两个显式能力。补日期也可由查询引擎完成；之前限定服务层只是简化首个切片的提案，不是标准规定。默认遵循 SQL、将完整日历支持另列显式能力现已由用户接受。

## 相关既有记录

- [上游 PR #12 新格式与表达式支持](https://github.com/iqea-ai/duckdb-ossie/pull/12)：已核对代码、PR 说明及当前 CI；尚未合并，叠加在其他升级 PR 上，不能将 CI 通过解释为本需求验收完成。
- [上游 issue #6 分发产物加载验证](https://github.com/iqea-ai/duckdb-ossie/issues/6)：已关闭。说明构建树内测试与真实扩展产物加载是不同证据，应分别记录。

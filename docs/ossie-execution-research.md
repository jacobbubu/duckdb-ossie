# OSSIE-REQ-001 本体执行基线研究

日期：2026-10-10。当前状态：调整范围的 A1–A8 本地验收、跨环境复现/Linux质量和远端 CI 工作包已完成。远端 `b2d486a` 在 [run38027392941 attempt2](https://github.com/jacobbubu/duckdb-ossie/actions/runs/38027392941) 的17项任务全部通过；[失败与重跑归档](research/ossie-req-001/ci-artifact-timeout/final-state.json)保留首次上传超时。Linux ARM64 CI只证明构建，本地Ubuntu ARM64原生/SQL/stock/MCP/tidy证据另见[跨环境控制](research/ossie-req-001/portability-control-results.json)。检修归档 `e5db46f` 尚未推送，不称本地新提交已通过远端CI。真实 Agent 工作已从研究推进至[最小程序实施与本地验证](agent-minimal-acceptance.md)，用户指定了网关/模型/凭据变量；三场景真实运行及委托Lead的口径/数据复核已完成，用户接受限定结论；原始失败与pending字段保持历史状态，数据真实性局限另记。下一P1重复实体/观察粒度已完成研究包，尚未实施；分页未启动。历史记录保留各包当时的范围与计数，当前语义结论见[验收入口](p0-acceptance.md)。

本文件是 OSSIE-REQ-001 在本仓库的研究与验证记录，不另建实施 backlog。原始需求位于 `/tmp/duckdb-ossie-requirements.7kw5ic4l/需求.md`，保持不变；用户已决定仅支持固定快照的新格式，替代原 R1.3 的新旧共同支持要求，并相应调整 A8。

## 范围与授权

初始授权为基线研究、独立数据基准及最小执行设计。2026-10-09，用户对“本仓库本地实施、独立分支、先构建复核 feature 再补基础校验和最小单表执行”的第三项回答“没问题”，因此本地实现和测试已获授权；上游 PR 修改、推送、合并、发布或部署仍未授权。规范依据解释后，用户回复“好”，采用首阶段 YTD 阈值人口、active/paid 未验证、默认 SQL 分组与 NULL 行为、日历补齐另列显式能力。后续实现按此范围推进，不再重复询问。

研究的问题：现有 `feat/ossie-spec-0.2` 能复用什么；固定规范究竟定义了哪些执行语义；DataBridge 示例的对象、粒度、单位和结果是什么；最小概念执行链路如何验收。

验收：保存来源提交与文件哈希；逐项核对现有代码及规范；以独立 SQL 和独立逐行计算核对 475 行的逐日人数、金额与平均值；提出带拒绝边界的执行设计，并独立审阅。研究完成不等于 P0 或 A1—A8 完成。

## 结果与影响

固定 feature 原本具有新格式加载、表达式 AST 转换及粒度保护，没有 ontology/mapping 执行层。现已在本地分支实现有界单表人口入口（证据见下文）。首个切片需要证明：同一指标和数据批次下，只改变 `ValidUser.derived_by` 的阈值，实际对象范围、SQL 和聚合结果同步变化。以下是已审阅的最小设计；实施授权见上节。

原始数据的独立 SQL 与 Python Decimal 计算逐日一致，复核了原示例全部 15 个日均值。3 月 10 日：

| YTD 阈值 | 对象数 / 记录数 | GAAP 总额 USD | 未舍入均值 | 两位小数均值 USD |
|---|---:|---:|---:|---:|
| 10 | 37 / 37 | 1697.80 | 45.886486486486… | 45.89 |
| 20 | 21 / 21 | 1277.54 | 60.835238095238… | 60.84 |

这里的对象数等于记录数是本批数据实测事实，不是普遍规则。阈值 20 的结果是本次独立基准，不是原示例声明，也不是本体执行结果。两种阈值分别在完整期间选中 166 / 63 条产品 X 记录，总额为 2265.86 / 1472.73 USD。

## 研究开始时的基线与来源

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

[研究定义目录](research/ossie-req-001/proposed-definitions/)含同一 core、同一 mapping，以及阈值 10/20 两个 ontology 版本。所有文件使用标准字段；四份文件通过固定 schema 的离线形状校验。两份 ontology 唯一差别是 `ValidUser.derived_by`，指标与 mapping 不变。研究时它们仅是经过 schema 检查的草案；本轮已由独立产物实际执行，结果见下文单表工作包。

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

### 已完成的 core loader/元数据工作包

范围：固定 feature 的本地构建与原有测试；随后补 core 的固定版本检查、非空模型集合、ai_context 字符串/对象与 examples、datatype 枚举和时间角色默认值，并通过独立发现接口保留说明、标签与描述性扩展。它是 R1/R2 的一部分，不替代真实列/键预检、表达式白名单、本体执行或 A1—A8。

验证：先记录未修改源码的基线断言；新测试覆盖版本/格式拒绝、元数据内容及缺省/显式时间角色，失败加载不得替换上一个有效模型；完成后运行整个现有 SQL 测试集和 stock DuckDB 的产物加载验证。仅以 fixture 同源互转不作为语义正确依据。

已完成结果：固定版本 `0.2.0.dev0` 必须为精确字符串（包括拒绝嵌入 NUL 的伪装版本）；缺失、旧版本、未知版本和非字符串版本拒绝，legacy wrapper 保留迁移提示。空 datasets、ontology/mapping 混入 core、非法 datatype、错误 AI context 和时间角色类型拒绝。支持十个固定逻辑 datatype；Date/Time/DateTime/DateTimeTz 默认时间角色为 true，显式 false/true 优先。新增 `ossie_metadata()`，逐 model/dataset/field/relationship/metric 发现说明、字段标签、instructions/synonyms/examples、原 AI context JSON、vendor extensions JSON、选中/声明方言和显式时间角色。原发现接口列结构不变。单位扩展仅保留来源与内容，不执行或换算。失败加载不替换已加载模型。

证据：

- 源码未修改的 feature 基线：13 个 SQL 用例、340 条断言通过；实际版本 `v1.5.6 / 069cc9f9b5`。这与上游 PR 描述的旧断言数不同，以本次实测为准。
- 最终全量 SQL 回归：14 个用例、437 条断言通过；`make format-check` 通过。
- 独立安装的 DuckDB wheel 1.5.6 加载实际 `.duckdb_extension` 成功；17 类拒绝输入后保持原模型；核对参考模型 46 个元数据对象及全部 AI/vendor 内容；2,500 个字段跨输出批次无丢失/重复。产物、验证脚本、源码和输入 SHA-256 见 [loader-artifact-results.json](research/ossie-req-001/loader-artifact-results.json)。当时Python验证器已在Bun工具链切换中移除；当前复现用 [verify_loader_artifact.ts](../scripts/verify_loader_artifact.ts)，历史报告保持不变。此loader验证不证明ontology执行。
- 现有完整功能脚本：40 PASS，0 FAIL，0 XFAIL，0 SKIP。因 PATH 无 stock CLI，它使用 build-tree shell、静态链接模式；独立 wheel 的真实产物加载是另一项单独证据。这个 40 项运行发生在最终 embedded-NUL version 拒绝补充之前，该补充随后由全量 SQL 和独立 wheel 复核。

保留的发现和修复：浅克隆缺少版本标签使首次配置使用 dummy v0.0.1；只获取固定提交对应的 v1.5.6 标签并设置 core.abbrev=10 后重新构建，实际版本与 artifact 已确认，未改变 DuckDB 源码提交。原 expression fixture 的 `Timestamp` 非固定 schema 的合法逻辑类型，现改为 `DateTime`，表达式和数值预期不变，并新增 `Timestamp` 拒绝控制。新增 SQL 测试初版使用未构建的 JSON 扩展函数，改为直接核对保留文本，JSON 内容另由 stock wheel/Python 解析核对。格式检查缺少工具，已在 task venv 安装规定版本后复核。独立加载 harness 初版用 LOAD 参数占位符被 SQL parser 拒绝，改用正确引号转义路径后通过；不得把该失败解释为二进制不兼容。

该工作包完成时，必要工作仍包括单表 profile、身份/列预检、严格 AST、对象/指标共享人口和追溯。这些已在下文单表工作包落地；整体 P0 仍未完成，当前剩余必要工作以单表工作包末尾为准。现有 loader 不是完整 JSON Schema validator；AI 数组仍沿用既有 scalar 文本容错。可发现的自然语言和 descriptive extensions 不等于可执行规则。active/paid 仍缺数据，明确未验证。

随后已授权并完成的工作包：以独立 GAAP oracle 落地单表 User→ValidUser profile；先建立严格标量规则和引用/identity 预检，再让对象清单与指标查询共享人口计划。必要执行语义不能解释时明确拒绝。不能把“schema 接受研究样例”作为切片完成；业务阈值和默认空日期输出已确定，推送、上游 PR、合并与发布分别保留授权边界。

本地实现：附加 worktree `/Users/rongshen/.codex/worktrees/ossie-execution/duckdb-ossie`，分支 `codex/ossie-execution` 从固定 feature `c2947c7` 开始，研究提交已 cherry-pick。三个子模块仍为记录提交：DuckDB `069cc9f9b5be802405797faecc284961b07c70ef`、ci-tools `3fd6109fc01555673b7c7123eba8d0d143fe8ce4`、fkYAML `1818a25156b9b1a65a9c9b6126631e2cd7cfa8f6`。仅本地变更与验证，未推送、改上游 PR、合并或发布。

### 继续实施：单表人口执行工作包

问题：已有 core query 无法执行 ontology 派生人口，整批身份预检与对象/指标共享数据范围尚未落地。用户“继续”延续本地实施授权。本包在同一记录下实现显式 definition bundle（固定 profile、文档 name/SHA-256）、严格标量规则/映射闭包、整批非空唯一 ID、对象与指标入口、编译说明及独立 GAAP/边界 oracle 验证。多表、安全连接、完整 calendar、MCP 集成及完整 P0 暂不声称完成。

落实的数据契约：bundle load 捕获已提交根表的固定类型化观察批次；使用当前引擎和不可从 SQL 文本构造的 AST 依赖读取，整批预检后原子替换 bundle，失败保留上一有效 bundle。显式事务加载拒绝，避免独立读取连接看不到调用者的未提交写入。数据改变需重载，每次执行绑定同一 bundle/snapshot；此能力独立于原有 live core query。使用既有 Advisor 对此具体集成取舍做一轮只读复核，Lead 实现和实测。

验证必需：只改变 derived_by 10→20，与原475行完整对象集合、逐日/所有地区类型分组和边界期望核对；NULL/重复ID在请求过滤前拒绝；NULL属性/NULLGAAP/空组符合契约；不支持requires、派生关系/独立子类mapping、身份覆盖、多表、聚合/窗口/子查询/未知函数在运行前拒绝；name/hash/version错和加载失败不替换旧bundle；源表更新不改变已冻结批次，重新加载后捕获新批次；EXPLAIN/编译不执行源读取或装载；真实产物独立加载验证。

已完成行为：新增 `ossie_bundle_load`、`ossie_concepts`、`ossie_objects`、`ossie_concept_query`、`ossie_concept_compile`。manifest 按 profile/name/SHA-256 选择固定 core/ontology/mapping；仅新格式。单根/单继承、scalar mapping、整批非空唯一 ID/OneToOne、实际列和逻辑/聚合类型在发布前预检。未知规则函数、聚合规则、窗口/子查询、隐式数值布尔、NUL 字符串、requires 闭包、身份覆盖、多表等明确拒绝。字段展开后注入一次 AST 人口谓词，避免逻辑/物理同名重新展开。对象与指标绑定同一强引用批次，失败/取消加载保留旧批次，准备语句跨重载保留原批次；数据更新需显式重载。执行 trace 记录实际绑定的快照与定义哈希。完整契约与示例见 [population-profile.md](population-profile.md)。

实测证据：

- 最终 SQL 全量回归：15 个用例、503 条断言通过；新增人口用例 66 条断言。`make format-check` 通过；`git diff --check` 通过。
- stock DuckDB wheel 1.5.6/source `069cc9f9b5` 加载实际 `.duckdb_extension`。完整 475 行产品 X 对象集合与独立 CSV/Decimal 及历史 SQL 基准一致：阈值 10/20 分别 166/63 条，金额 2265.86/1472.73；两种定义各 15 个日组，所有日期×地区×类型组分别 101/45 个及逐组对象下钻全部核对。指标公式、mapping 和输入 CSV 未变，只有 ontology 的 derived_by 阈值变更。人工 11 行边界核对阈值相等、NULL、NULL GAAP、空组/空标量结果及过滤前整批身份拒绝。
- 实际产物同时核对定义冲突、类型/AST拒绝、失败原子性、准备语句快照保持、源更新显式重载、类型化 DATE/DECIMAL/NULL/引号字符串、计算字段只展开一次、单线程引擎并发调用、取消装载、宏不能覆盖实际 AVG、诊断 SQL 不能访问内部批次。关闭已装载快照的文件数据库后，另一 Python 进程重新打开成功，验证文件锁释放。共 46 个具名拒绝控制通过。
- [population-artifact-results.json](research/ossie-req-001/population-artifact-results.json)绑定产物、完整 C++/头文件、构建源清单、标准定义/manifest、CSV/独立基准、SQL fixture 及验证脚本 SHA-256。它是有界单表证据，不是完整 P0/A1—A8 验收报告。
- 原 loader 独立产物复核仍通过：17 类负例、46 个元数据对象、2,500 字段跨批次；新增 [loader-population-artifact-results.json](research/ossie-req-001/loader-population-artifact-results.json)，保留原先 loader 阶段的历史产物证据。原完整功能脚本 40 PASS，0 FAIL/0 XFAIL/0 SKIP；本机仍使用静态 build-tree shell fallback，真实可加载产物由 wheel 分别证明。该脚本的 MCP 检查仅覆盖既有 core resource，不证明新增 population MCP 接口。

保留的失败与取舍：构建树的静态引擎掩盖了初始私有 DuckDB 实例缺少 SUM/AVG core_functions 的问题；独立 wheel 首次执行聚合实际失败。尝试显式加载静态 core_functions 时，真实扩展链接失败，`ExtensionHelper::LoadExtension` 不是 DUCKDB_API。Advisor 指出直接复制函数集也不能可靠证明 builtin 来源或跨平台 ABI。最终不依赖这些符号，改用 host 引擎、ColumnDataCollection 和 TableRef ExternalDependency，实际查询执行携带 AST，SQL 文本仅诊断。发现缓存 bundle 长期持有 host Connection 会形成 DB 引用环后，改用弱 DB 引用和短期连接，以跨进程文件锁控制验证。DATE cast 的 UNBOUND 类型、空列表的 INTEGER[] 推断及显式事务失败后 ROLLBACK 检查顺序也已修正；不能把这些 harness/绑定失败包装成原始数据不正确。

Advisor 记录：当前集成取舍一轮只读建议加一次限定澄清；同一 Astra/high Advisor，无嵌套委派。有效 backend identity、effort、usage、billing 未由工具证实，仍记 unknown。Lead 核对 pinned 引擎头文件/实现，并用真实产物、类型/快照/生命周期控制验证采纳方案。Windows/Linux 本轮没有运行，不声称跨平台验证。

当前必要剩余与下一已授权动作：本工作包完成，整体目标继续。A1—A5/A7 相关单表支持路径已有上述实证，A6 仅多表拒绝控制，尚无安全多表正例；A8 按用户仅新格式决定部分覆盖，未完整覆盖 R3 语言集合。下一阶段先对 R3 的 NULLIF/除法、除零/精度/时间边界及 R6 的安全事实/维度连接建立独立正负控制并研究确定语义，再扩展实现。完整 calendar、更多 ontology 结构/约束、多期实体身份、population MCP 暴露及完整验收归档仍未完成；active/paid 仍缺数据。推送、上游 PR 修改、合并和发布仍不在本地实施授权中。

### 用户决策的规范依据

- [固定 Ossie 的 Derivation expressions](https://github.com/apache/ossie/blob/8dd6732da354f22ca71f16d82626a46039ecdcd9/ontology/ontology.md#derivation-expressions)定义用表达式从父类派生人口，没有规定 ValidUser 的业务阈值，也没有从金额推断 active/paid 的规则。原示例的可执行条件是 YTD 阈值；其他状态仍缺数据。
- [DuckDB WHERE 与聚合的顺序](https://duckdb.org/docs/current/sql/introduction#aggregate-functions)规定输入过滤先于分组/聚合；[聚合与 NULL](https://duckdb.org/docs/current/sql/functions/aggregates#handling-null-values)规定 AVG 忽略 NULL、空聚合除 COUNT 外返回 NULL。本地 1.5.6 人工控制已独立验证相关行为。
- [dbt MetricFlow 日期与 NULL 配置](https://docs.getdbt.com/docs/build/fill-nulls-advanced)把 join_to_timespine（补日期）与 fill_nulls_with（填值）分为两个显式能力。补日期也可由查询引擎完成；之前限定服务层只是简化首个切片的提案，不是标准规定。默认遵循 SQL、将完整日历支持另列显式能力现已由用户接受。

## 相关既有记录

- [上游 PR #12 新格式与表达式支持](https://github.com/iqea-ai/duckdb-ossie/pull/12)：已核对代码、PR 说明及当前 CI；尚未合并，叠加在其他升级 PR 上，不能将 CI 通过解释为本需求验收完成。
- [上游 issue #6 分发产物加载验证](https://github.com/iqea-ai/duckdb-ossie/issues/6)：已关闭。说明构建树内测试与真实扩展产物加载是不同证据，应分别记录。

## 质量控制工作包（2026-10-09，用户交接）

问题与范围：用户要求在下一功能阶段前执行 `/tmp/duckdb-ossie-quality-controls.attso3di.md`，先提交推送保留当前成果。该文件审查时 HEAD 为 9062549；现已复核最新 2b1e8b5 和完整单表实测，不能沿用“未实现/未验收”的旧判断。当前成果已按本次明确授权推送至 fork `origin/codex/ossie-execution`，不包含 main 合并、上游 PR 或远程保护设置授权。后续质量修改先本地交付，不把保留旧成果的推送授权默认为未来所有改动可推送。

沿用本文件的 local issue（fork issues 禁用）。目标是修复 core model/policy 组合读取、覆盖 scripts Python 格式、项目直接依赖边/头文件循环及真实产物验证 CI。复用现有 format/tidy、构建、SQL、MCP、独立 CLI 和 always CI gate；不重建 AST、不全量拆 parser/compiler、不改子模块/原样研究输入或历史 oracle。小范围 JSONText 异常安全与描述元数据不改变计算的控制在本包评估落地。Sanitizer/coverage 全局阈值和 GitHub 分支保护不自动扩大范围。

计划与验证：先建立受控交错的 legacy model A/policy B 复现，再以单锁 Snapshot contract 和真实 Compile 控制修复；检查旧模型共享生命周期、失败保持与 scalar chunk/bind时机。C++ 控制明确作为独立测试 target，避免 ci-tools 默认关闭 C++ unit tests 而漏跑。脚本格式门禁用隔离副本注入违规，保持 src/test 与原样来源保护。direct-include门禁测试禁止边、循环、合法 AST、漏扫/零输入。两验证脚本针对同一最终 stock 产物运行并接入已有 CI gate，保留数值/NULL/身份控制与哈希证据。完成后更新本节与计划 checkpoint；质量包完成不等于 P0 完成。

最新已知保护：Makefile tidy 已扫描整个 src；clang-format 与 pinned DuckDB 一致；现有编译器接收显式 model/options，无状态/文件执行依赖；直接项目头依赖尚无循环。loader verifier 的旧 8 名称断言已在 2b1e8b5 改为 13，本包应改成所需入口契约以免其他模块新增功能导致无关失败。现有 population oracle 已真实通过，但未接 CI；format-check 目前只扫描 src/test，scripts 未覆盖。源码及机制复核先于方案落地。

### 质量包结果与交付边界

已存在保护继续保留：固定 DuckDB AST/编译器显式输入、src/test formatter、整个 src 的 tidy 覆盖、原 build/SQL/CLI/MCP 和 always CI gate。没有改子模块、根格式配置、原样研究来源、业务规则或标准定义。本包没有全量拆文件或新增产品运行依赖。完整命令、工具/产物版本及输入哈希见 [quality-control-results.json](research/ossie-req-001/quality-control-results.json)，契约见 [quality-controls.md](quality-controls.md)。

已修复缺口：

- 单锁 `GetSnapshot()` 组合 model/policy；去掉独立 policy getter，读者持有 shared_ptr<const Model>。compile 每执行 chunk 读取一次并设 VOLATILE，query 在 bind_replace 捕获一次；描述/元数据 reader 同步 const。模型快照和 population 数据批次不混为一谈；跨 chunk 可读到后续发布，不宣称整条 scalar statement 冻结版本。
- 受控 pre-fix 复现实际产生 A model/B policy，并生成带 ABS filter 的 A SQL（本应由 A policy 拒绝），退出 1。修复后的独立 C++ runner 在 ENABLE_UNITTEST_CPP_TESTS=FALSE、BUILD_UNITTESTS=ON 下实际执行，19 个检查通过，覆盖 A/B 编译行为、旧对象生命周期、两连接、失败保持及 prepared scalar/query 时机。Make test 内显式先运行此 target，再运行 SQL；缺失隔离 fixture 的实际 Make 控制退出 2，不会悄悄跳过。
- scripts Python 纳入 pinned formatter，src/test 检查继续通过；使用 Black 24.10.0 与 CI 24.x 约定对齐，clang-format 11.0.1、cmake-format 0.6.13。局部 Python 3.14.7、CI 配置 3.12；实际 root CMake 标准为 C++11。脚本格式变更保留 mcp_check.py 可执行位；没有格式化第三方和研究来源。
- 项目图检查 25 个 src 源/头文件、42 条项目边，查禁止节点可达路径和直接 header cycle；可控例外精确到 parser/catalog adapter 的 DuckDB 边，不豁免 state/functions。注释、相对/尖括号引用、未解析项目 include、零输入和源码清单检查有隔离负例。只是项目图/显式外部 include 的边界，不是完整预处理/运行 I/O 证明。
- quality-local 的 19 个自测通过，包括脚本违规失败→恢复，以及实际 CI gate shell 对每个 required job 的 failure/skipped/cancelled 均非零。CI gate 新增 project-controls；同一 artifact-load Linux/macOS matrix 接两个 stock wheel 验证器，保留原 CLI/MCP、上传独立证据。loader verifier 从 13 名称总数改为八个必需入口及 query overload 契约，不因未来新增模块无理由失败。
- JSONText 的 yyjson write buffer 改为局部 RAII custom deleter，字符串构造异常也释放；解释原有局部 NOLINT，不扩大豁免。普通元数据验证通过，没有做 allocator 故障注入。人口验证新增 description/AI context/vendor unit 与 scale 的人工变更，完整对象与金额/均值保持一致；未改真实业务来源。

最终验证：`make test` 发现 15 个 SQL 用例/503 条断言及独立 19 项 C++ 检查，全通过；quality-local 19 个自测与格式/项目图通过；两个 verifier 针对同一最终产物、确认 stock 1.5.6/source 069cc9f9b5 并真实加载，loader 17 负例/46 元数据对象/2500 字段、population 46 拒绝控制及原475行/11行边界/元数据不变性通过。新增 [quality-loader-artifact-results.json](research/ossie-req-001/quality-loader-artifact-results.json) 和 [quality-population-artifact-results.json](research/ossie-req-001/quality-population-artifact-results.json)，保留旧报告。完整功能脚本另有 40 PASS 静态 fallback，不冒充真实产物证据。workflow YAML 本地解析通过；git diff --check 通过，原样来源和子模块无 diff。

保留的失败：初版 Snapshot 用默认成员值加 aggregate return，与有效 C++11 不兼容；改为显式成员赋值后编译通过。首轮架构 checker 没把 duckdb.hpp 标为外部入口而误报；精确声明 vendor umbrella header 后合法依赖通过。后来追加 `<missing.hpp>` 负例确实发现 unresolved angle include 漏检，补分类后所有 19 个自测通过。引入 scripts formatter 自动处理了原 MCP 脚本既有分号/行布局，额外恢复其可执行位；不是 MCP 行为修改。临时 legacy 复现源码未保留，仅保留父提交代码、受控步骤与实际日志，不虚构源码哈希。

Advisor 一轮有条件建议已采纳并核实：scalar 原默认 CONSISTENT 可在 PREPARE 被折叠，必须 VOLATILE 才满足执行 chunk 契约；shared const read 生命周期；独立 runner 真正接 Make/CI；项目 include 可达性而非只有第一条边。单一 Advisor、无嵌套委派，配置 Astra/high，有效 backend/effort/usage/billing 仍 unknown。

仅配置/待验证：fork Actions permissions 只读返回 enabled=true，但 workflows API 返回 total_count=0、该分支没有运行记录；没有推断原因，没有改 GitHub settings/规则或手动派发。新增质量修改尚未推送，远端 Linux/macOS CI/tidy 未运行；本地 clang-tidy 不可用，不能称其通过。受保护的 main 合并硬门禁仍是待用户独立授权的仓库动作，不借本包实现。Sanitizer、全局 coverage/复杂度/命名治理因缺成本与基线证据暂缓，不是门禁漏检的替代品。

本地质量工作包完成，P0 未完成；功能扩展在这轮暂停推进。下一步先审阅本包及确认其远端交付方式，再延续前节 R3/R6 的独立控制与研究。质量包提交保留在当前本地分支；此前成果已明确授权推送。不会把“先推送当前成果”扩展为后续质量修改、main 合并、远程规则或上游发布的无限授权。

### Bun + TypeScript 工具链迁移（2026-10-09，用户追加范围）

问题与授权：用户明确要求工具链改为 Bun + TypeScript。沿用本 local issue 和计划；迁移项目维护的架构门禁、门禁自测、MCP probe 和两个真实产物验证器，保留既有数值、取消、生命周期与失败控制。C++ 扩展、CMake/Make 和固定 DuckDB 上游 formatter 属于引擎工具，不改为 TypeScript；原样研究脚本及历史报告保留。当前质量提交 9fbb5b0 已本地保存，此前推送保留的功能提交为 2b1e8b5；本轮没有新增推送/合并/远程设置授权。

验证计划：固定 Bun/依赖和 lockfile，独立严格类型检查、TS 格式违规控制及 Bun tests，继续 C++/SQL 与 stock 1.5.6 真实扩展验证。先验证官方 Node Neo binding 在 Bun 下的产物加载、类型、interrupt 和 close API，再迁移 oracle；不以 Python 包装脚本宣称迁移完成。新的证据另存，历史报告不覆盖。当前 Bun 1.3.11 可用，尚未验证 binding；下一动作是最小 native smoke 与迁移门禁。

迁移结果：四个项目 Python 脚本及 unittest 已由 TypeScript/Bun 替代；固定 Bun 1.3.11、TypeScript 5.9.3、Prettier 3.6.2、官方 @duckdb/node-api 1.5.6-r.1 与 bun.lock。官方客户端来源见 [Node Neo 文档](https://duckdb.org/docs/current/clients/node_neo/overview)，具体类型/关闭/interrupt API 以已安装固定版本源码复核，并以真实产物验证。严格类型检查独立于运行；没有通过包装 Python 执行来冒充迁移。首次写错 LIST helper 被 tsc 拒绝，修正为 LIST(VARCHAR)，空列表类型明确绑定。

最终本地证据：`make quality-local` 的 25 个 Bun 控制通过（保留原19项，新增实际非零图、3项MCP传输控制、类型错误拒绝/恢复和CI迁移接线）；19项C++状态检查、15个SQL用例503条断言通过。架构图仍为25文件/42项目边/120未展开外部边。两验证器针对同一真实产物，在独立 stock Node binding 下确认 v1.5.6/source 069cc9f9b5；loader17拒绝例/46元数据范围/2500字段、population46拒绝控制与475行/11行边界、全部对象ID/金额/下钻/描述不变性通过。金额SUM以独立CSV bigint分值精确比对，AVG沿用DOUBLE容差；typed DATE/DECIMAL不经浮点或JSON转换。取消控制定时器在native load运行时实际触发interrupt、旧快照保持；明确关闭connection和instance后，父进程仍存活时子Bun进程重开同文件读取11行。并发来自同一instance的两个连接，不以顺序查询替代。

MCP迁移后的probe用独立下载stock CLI v1.5.6实际加载同一扩展，握手、唯一semantic_query、query/export拒绝、数据/词汇/粒度拒绝及不设置禁用flags的正向控制全通过；读超时/子进程清理另有控制。既有shell功能检查本轮为40 PASS、0 FAIL/XFAIL/SKIP，模式FAITHFUL，已不依赖静态fallback。Make质量聚合和CI接线已改为Bun frozen install/TS检查；上游复用format job没有Bun安装入口，所以C++格式在原job、TS格式/类型在required project-controls，不静默跳过。两workflow使用Bun YAML解析通过，diff检查通过。

新报告：[Bun工具链与门禁证据](research/ossie-req-001/bun-toolchain-results.json)、[Bun loader证据](research/ossie-req-001/bun-loader-artifact-results.json)、[Bun population证据](research/ossie-req-001/bun-population-artifact-results.json)。原样来源、子模块、历史Python研究脚本和历史报告未改；新报告绑定本轮源码/依赖/产物哈希。C++/CMake/Make、shell CLI smoke以及固定上游Python formatter/tidy作为引擎工具保留。限定Advisor只读审查提醒精确分值、空列表类型、真实interrupt、双层close、父进程存活文件锁和同产物加载；Lead逐项实测。有效backend/effort/usage/billing依然unknown。

当前结果与必要剩余：质量工作包及追加Bun工具链迁移本地完成，不等于P0。远程CI与Ubuntu clang-tidy仍未运行，无新增push/merge/settings动作。本轮仅macOS arm64，不能称跨平台已验收。原产品R3表达式/除法时间边界与R6安全连接研究，以及calendar/更广ontology/MCP完整验收仍待后续；当前不借工具迁移扩实现范围。下一动作是交付本包，再继续已有功能研究范围。

### 推送前遗留实现清理（2026-10-09，用户要求）

范围与授权：用户要求推送前清理遗留不用的老实现。本轮沿用 local issue，先清理再验证，交付当前 fork 分支；不合并 main、不改远程规则。复核发现：shell 功能检查仍内嵌 Python 生成与现有 nested_field_model.json 重复的模型，静态 fallback 可掩盖真实加载失败，已修复的嵌套字段路径仍记为非门禁 XFAIL；AIContext.instructions 独立成员只写不读，原始 ai_context 已在 raw_json 完整保存。还有 population-profile 的旧 Python verifier 链接和迁移后 Python cache 目录/ignore 项。原新旧格式拒绝控制和 core API 有实际使用，不能按“旧”字样误删；历史来源/报告/研究脚本保留。

计划与验证：复用已有 fixture，移除重复 Python、静态 fallback 和过期 XFAIL 分支；清除只写不读成员而保留 metadata 原样与 instructions 类型验证。脚本用同一真实产物，缺CLI/缺产物/加载失败必须非零并有隔离负例；继续 format/typecheck/Bun、真实 C++/SQL 与两个 artifact/MCP 验证。新的清理证据另存；成功后提交推送 origin/codex/ossie-execution，不扩大到合并/发布/远程设置。

清理发现与范围修正：首次引用搜索只查 `ai_context.instructions`，漏掉 MetadataRow 的 `ai.instructions` 别名访问。移除后实际编译明确报错，证明该成员仍使用；已完整恢复成员及 parser 两处赋值，不改变 metadata schema 或语义。这是本轮检查失误，保留失败证据，不把未实施的 C++ 删除计为清理成果。下一动作改为仅完成实际确认的 shell/cache/旧链接清理，并重建验证。同一轮 workflow diff 复核发现删除旧复制步骤时连带包含下载步骤，已保留下载、仅删复制/回退检查，并给接线控制补充下载必需断言。

本轮最终成果：删除 shell 内嵌 Python 重复模型生成，改用已有 nested_field_model.json；去掉静态build-tree fallback和已修复嵌套字段的非门禁XFAIL，真实加载失败与回归均非零。统一 OSSIE_EXTENSION 为绝对路径（macOS hardened CLI真实拒绝相对路径，首次失败日志保留）；CI直接使用下载产物，去掉复制和旧fallback包装，保留download步骤并新增接线控制。清掉旧缓存目录/ignore和population-profile旧verifier链接。所有C++实现最终无diff；metadata instructions、活跃core API和旧格式拒绝均保留。补充控制时曾有TS fixture引号错误，formatter/Bun明确拒绝，修正后最终类型/格式检查通过。

最终验证：重建通过；30个Bun控制（含缺CLI/缺artifact/加载失败/相对路径和同下载产物接线）、19项C++状态检查、15个SQL用例503条断言通过。loader17非法文档/46元数据范围/2500字段，population46拒绝控制/475行与11行边界/取消生命周期等控制再次针对同一最终产物通过。真实stock CLI检查40 PASS、0 FAIL/SKIP，MCP完整probe通过，shell语法/workflow YAML/diff检查通过。新证据：[清理报告](research/ossie-req-001/cleanup-results.json)、[清理后loader](research/ossie-req-001/cleanup-loader-artifact-results.json)、[清理后population](research/ossie-req-001/cleanup-population-artifact-results.json)，原报告不覆盖。原样输入/子模块/历史研究不变。

清理工作包本地完成，当前下一动作是提交并推送到fork的codex/ossie-execution。远端CI尚未验证，Ubuntu tidy没有本机证据；不称跨平台完成，不合并main/改设置。原功能R3/R6等必要研究仍在前节，清理不等于整个P0完成。

### R3 NULLIF 与受保护除法切片（2026-10-09，恢复功能开发）

恢复与授权：08c06e6 清理已实际推送 origin/codex/ossie-execution，quality/Bun/清理工作本地完成；只读 runs 查询无运行记录，仍不称远程CI通过。用户确认继续此分支并要求继续功能开发。本轮本地研究/实施先覆盖NULLIF和受保护除法，不扩安全多表、时间/calendar或一般表达式语言；本轮新功能不自动推送。

规范与缺口：固定expression_language.md列出算术/及NULLIF(a,b)相等返回NULL；core-spec/spec.md的Decimal未定precision/scale，没有明确除零契约。DuckDB官方numeric types与PostgreSQL compatibility说明Decimal除法采用近似浮点、/0遵循IEEE特殊值；以固定1.5.6实测为最终依据。现strict population只允许+,-,*及sum/count/avg等，NULLIF和/明确拒；core老API仍采用host行为，本轮不改其既有契约，也不称全部R3通过。

拟实施：在现单表profile允许严格双参NULLIF；/暂限直接分母为NULLIF(expr,数值常量0)，让保护来自明确模型公式，不自动改写。不允许schema/catalog限定用户函数或任意函数、未保护/、%或//；CASE保护尚未证实则明确拒。沿用原/lowered/expanded AST检查及system.main engine qualification，独立验证scalar field、derived rule、请求filter和aggregate metric。COUNT DISTINCT已有strict aggregate路径，本轮给实际控制。固定标准与host差异通过文档公布，Decimal转Double除法不冒充精确金额。

证据计划：先记录pre-fix拒绝及固定host边界，再以独立手算分子/分母数据验证零/负值/NULL/空集、精确SUM与近似比值、绑定types、未知/qualified/arity/未保护拒绝、失败保持和宏遮蔽；补真实SQL与stock Bun artifact verifier，并重跑原population/loader控制。使用一个限定只读Advisor评估语义/AST守卫；Lead实施验证。原不可变475行来源和旧报告不改，新报告另存，沿用本issue和计划追踪R3/R6剩余。


R3切片结果：NULLIF双参及直接分母NULLIF(expr,数值常量0)的/已实现，原表达式与展开后均检查。实际数值0允许整数/Decimal，字符串、Boolean、NULL、非零（含1e-18/1e-38）、计算零、CAST/CASE包裹、未保护嵌套/明确拒绝。普通NULLIF、core NULLIFZERO lowering、派生规则/请求过滤/字段/聚合均有正例；rules/request不新增NULLIFZERO别名。system.main绑定阻止调用方宏覆盖。另修正既有聚合识别：pinned VisitExpressionClass遇根FUNCTION后不递归，导致SUM比值被误判无聚合；Strict检查之后显式遍历，不绕过聚合/绑定限制。

实证：8行人工数据分别核对逐行比值与汇总比值，零/NULL/空集、正负分母抵消、COUNT DISTINCT排除NULL、精确Decimal SUM与DOUBLE类型、27具名拒绝及失败保持。明确0保护不能保证有限值，真实overflow为Infinity，报告用分类字符串保留该证据，不将JSON转换后的null冒充SQL NULL。比值容差1e-12，金额SUM不转浮点。30个Bun控制、19项C++状态检查、16个SQL用例566条断言通过；原475行/11行population及46拒绝、loader17拒绝/46metadata/2500字段、stock CLI40 PASS和完整MCP probe对同一实际产物全部通过。CI artifact-load已接第三表达式验证器，仍使用同一download产物。

保留失败：旧产物不支持/与NULLIF；初版新产物聚合存在性检查错误见上；NULLIF错误arity由parser先拒，harness调整预期；字段Decimal负例先被mapping拒，改为metric声明Decimal才验证DOUBLE聚合类型不匹配。后两项是测试设计修正，没有伪称早期错误证明后续binder行为。报告绑定失败/最终日志哈希及源码/产物。一个限定Advisor建议加一次澄清已采纳并经Lead核对固定引擎源码及真实产物；有效backend/effort/usage/billing unknown。

新证据：[R3控制报告](research/ossie-req-001/r3-control-results.json)、[表达式](research/ossie-req-001/r3-expression-artifact-results.json)、[同产物loader](research/ossie-req-001/r3-loader-artifact-results.json)、[同产物population](research/ossie-req-001/r3-population-artifact-results.json)。历史报告、不可变来源/原475行数据和proposed definitions、子模块没有修改。

本工作包本地完成，下一动作本地提交保留，不自动推送。整体目标仍未完成：后续沿既有R3研究日期时间/其余表达式边界，再研究R6安全连接的正负控制；calendar、更广ontology/身份和population MCP完整验收仍必要。仅macOS arm64实测，远程CI/Ubuntu tidy未运行，不称全R3/P0或跨平台通过。

### R3 DATE 边界控制（2026-10-09，继续）

恢复状态：f1e523b已本地提交且工作区干净；未推送。沿现local issue推进既有DATE与类型转换语义控制，先不扩Time/DateTime/DateTimeTz执行。规范原样来源已有Date/time construction、CAST/TRY_CAST；官方DATE/cast/comparison文档作为依据，行为最终由固定1.5.6真实产物证明。元数据接受时间类型不等于population能执行。

范围与验证：独立人工日历数据覆盖月界/年界/闰日/NULL、半开区间与BETWEEN差异、对象与指标同人口、稀疏分组/空区间、CAST无效值失败与TRY_CAST NULL、日期运算返回类型与infinity边界。验证不通过JS Date/时区序列化；失败加载保留旧批次。未知日期函数/current_date、TIME/TIMESTAMP及不受支持类型应明确拒。复核是否存在字符串转DATE的host宽松行为/时区依赖，再确定支持文档的准确限制。使用现Advisor做一次限定取舍；不添加通用日期函数或多期身份能力。本轮验证/本地提交，不自动推送。


DATE切片结果：11行人工Gregorian日历（各行不同身份）覆盖年/月界、闰日/非闰年、NULL与±infinity。2月半开区间User为3行/SUM12.00，带YTD条件ValidUser为2行/SUM8.00；BETWEEN包含3月1日，稀疏组/空区间、对象与指标均符合手算。DATE±INTEGER按天返回DATE，DATE−DATE按天返回BIGINT；CAST坏日历失败、TRY_CAST为NULL。引擎宽松解析允许斜线/垃圾后缀/带偏移时间后缀，取日期前缀而非时区换算；真实对象/指标在Honolulu与Shanghai两配置一致。21具名拒绝检查含时间typed literal/cast/physical、未知时钟/date_trunc、嵌套timestamp cast、interval构造、非法日期及失败原子性，保留snapshot_id/content_sha256。

发现与修正：固定Node日期wrapper原toString把非有限日期输出为超大普通日期，native isFinite=false及±sentinel才是正确证据。已修正共享artifact helper、加独立适配控制，以真实native与SQL特殊值比对；不改C++/引擎。限定Advisor一建议加一澄清提醒字符串宽松解析/特殊值和中间类型边界。Lead实测进一步否定interval建议的接受推断：原生DATE+INTERVAL是TIMESTAMP，但当前INTERVAL 1 DAY被解析成to_days/trunc函数并由Strict先拒，quoted interval被cast检查拒。因此field interval负例证明AST拒绝，不能记为Matches类型检查；没有新增timestamp执行能力。工具确认有效backend/effort/usage/billing均unknown。

保留harness失败：首次bundle结果records索引误用4（正确3）；CURRENT_DATE不带括号由qualified reference检查先拒，调整具体预期；sqllogictest首次写成statement error inline reason，固定parser要求----后错误文本，已修正；诊断字符串一次Perl插值被吃掉，改为结构化patch。新增interval正例推断被真实Strict拒，已改成明确负例，不以之后typecheck成功掩盖verifier失败。最终DATE脚本单独退出0并绑定最新SHA。

验证：31 Bun控制/99断言、19 C++状态检查、17个SQL用例610断言通过。共享helper修改后原loader17拒绝/46metadata/2500字段、population46拒绝/475行+11边界、expression27拒绝重新对同一f1e523b实际产物通过；本轮没有C++修改所以无需重建。CI第四DATE验证器已接同download，YAML/diff检查通过；远程CI/Ubuntu tidy未运行。新证据：[DATE控制汇总](research/ossie-req-001/r3-date-control-results.json)、[DATE原生产物](research/ossie-req-001/r3-date-artifact-results.json)。历史来源/475CSV/proposed definitions/子模块与旧报告保持原样。

本包本地完成，下一动作本地提交；本轮不推送。整体R3/P0未完成：TIME/DateTime/DateTimeTz、更多日期函数/一般转换及表达式边界仍有限，R6安全连接、calendar、多期身份/更广ontology与population MCP仍必要。下一已授权工作沿原计划研究R6安全连接前提及独立正负控制；日期扩展不能悄悄改变人口身份/时间基础。首段旧阶段计数是历史记录，本节是当前结果。

### R6 安全连接研究与控制（2026-10-09，继续）

恢复：fccce8d已本地提交且工作区干净，未推送。沿本local issue研究既有INNER fact→unique dimension契约，不先解除单表profile限制。固定core规范relationships不定义INNER/LEFT；当前core按声明键推导cardinality，Compile按所需投影/过滤拉入关系，但不验证真实数据唯一/外键完整。现population单表限制仍是保护，不能以core能查询来声称共享多表人口已实现。

本包范围：人工单事实/单维度fixture，独立TypeScript手算ID/分值oracle与真实stock产物core查询核对；完整匹配正例、重复维度键（含未用键）、NULL维度/事实键、缺失匹配、同名物理列与类型不等负例，证明缺少预检如何重复/丢失记录。继续验证既有fan-out、歧义、多粒度拒绝。研究原生单事务捕获两表及并发修改边界；SQL研究预检与生产扩展能力分开记录。不引入自动LEFT或distinct修复、聚合拆分、role-playing、多事实/复合键。一个限定Advisor审查集成约束；下一执行实现范围以证据确定。本轮研究/控制本地提交，不推送/合并/发布。

R6研究结果：6行fact/4行dimension（含无引用行），独立手工关系与bigint分值oracle核对SUM55.01、非NULL金额5、AVG11.002，North4/SUM24.98、South2/SUM30.03，active+eligible成员f1/f2/f5/f6与金额24.98。共享物理id列均限定来源，core以fact.id分组取得成员下钻并和native SQL比对；不能称双表ossie_objects已实现。11类研究SQL预检反例与实际core结果分开记录：引用/无引用重复维度（引用重复使实际8行/SUM85.04）、NULL维度键、缺失目标（4行/SUM60.06）、NULL外键、过滤外未匹配事实、重复/NULL事实ID、INTEGER→VARCHAR强转导致重复匹配、空事实仍有坏维度、空维度且有事实。实际COUNT(*)/ID multiplicity与COUNT(id)分别核对，避免NULL身份被COUNT隐藏。两表全空可接受，NULL非键属性允许。

原compiler的many-to-many/reverse fan-out/歧义/多粒度4拒绝仍有效，primary_key与unique_keys声明路径核对。维度COUNT可独立core编译得到4，展平到fact行会得到6；不能将Compile成功当指定fact粒度证明。实际两连接同一instance中，fact第一次读取后另一连接更新dimension，当前事务的dimension/join仍读取旧版本；commit后core看到新值。仅证明同库native事务实验，未证明生产bundle捕获/发布后冻结。v1多表拒绝实际仍有效。

限定Advisor一轮建议加一次澄清：独立新profile、全批次键/匹配检查、unused坏键拒绝但unused有效行允许、same underlying native DB、展平前原模型指定fact grain检查、最终AST不能保留live dimension BindTable。Lead采纳并由实际core/源码/事务实证核对；不复制第二套grain算法，下一步拟提取共享RequireMetricGrain式小接口。有效backend/effort/usage/billing unknown。集成契约见[安全连接研究结论](safe-join-research.md)，该文是技术边界，不是另建backlog或宣告生产能力。

保留失败：首轮harness把core dimensions-first结果顺序写反；第二轮误以为ORDER BY默认NULL在前，实测为后；均修正oracle展示顺序，分值/成员数据未改。检查中删除了一段无关的模糊id SQL及catch空数组代码，最终每个验证失败必须传播，未把捕获异常当正例。最终verifier单独退出0。原C++/source/submodules/proposed definitions/历史报告未改，不需要重建扩展。

本地验证：31 Bun控制/100断言、19 C++状态检查、18个SQL用例661断言通过；新R6真实产物verifier核对11数据反例/4规划拒绝/单事务及v1多表拒绝。CI追加research verifier到同download产物，明确其是core研究控制不是生产安全连接；当前其他四verifier输入/helper/artifact未改变，沿前轮同哈希证据复用，没有假称本轮重新运行。新报告[研究产物证据](research/ossie-req-001/r6-join-research-artifact-results.json)、[门禁汇总](research/ossie-req-001/r6-join-control-results.json)。远程CI/Ubuntu tidy未运行。

本研究包本地完成，下一动作本地提交，不推送。整体R6/P0未完成；下一已授权实施按上述边界先共享指定fact粒度验证，再独立双表profile冻结/预检/映射及对象指标共同人口，禁止关闭现有保护。R3其余语言/时间类型、calendar、多期身份/更广ontology、population MCP与完整验收仍是既有必要剩余。

### R6 指定事实粒度校验（2026-10-09，继续实施）

恢复：0f05afb研究包已本地提交且工作区干净，未推送。用户继续授权既有实现；本轮先共享原模型metric grain分析，并新增RequireMetricGrain指定dataset校验，不先实现完整双表profile。复用现Compile的FindMetric/FindWindow/CollectGrains/AggregateGrain/SafelyReachable，不复制第二套AST算法，默认core行为保持兼容。

范围与验证：每个指标必须有且仅有一个指定事实grain；维度grain、混合grain、窗口、未知metric、无aggregate和无grain aggregate明确拒绝。新校验不代替引用/path/physical binder或真实键/完整匹配预检；COUNT(*)继承在现core可用，但新指定grain路径不静默推断。C++直接控制核对原core维度metric能compile而新API拒绝、合法CASE/计算字段/函数包裹递归、旧SQL不变、非法表达式后续binder拒绝。独立runner明确接入Make/CI，不依赖ENABLE_UNITTEST_CPP_TESTS。构建真实产物后原五artifact verifiers继续复核。必要剩余仍是冻结双表profile/来源AST映射/预检/对象指标/失败保持与生命周期；本轮本地提交不推送。


指定grain结果：新增内部C++ RequireMetricGrain，与Compile共用AnalyzeMetric（FindMetric/FindWindow/CollectGrains），不新增公开SQL或Compile选项，不提前接入双表profile。逐metric要求被现分析器识别出的grain唯一且等于指定dataset；维度COUNT/DISTINCT、混合grain、无aggregate、窗口、空请求、未知metric/dataset及已识别无来源聚合拒绝。COUNT(*)/COUNT(1)及SUM+COUNT(*)在新路径不推断grain；旧core anchored COUNT(*)仍通过，SQL及模型不变。带维度CASE、计算常量字段、大小写/引号、SUM/AVG/COUNT和函数包裹比值实际计算与手工group/金额/计数/容差一致。

分层限制已经实测：nested/hidden SUM、字段内window及嵌套COUNT(*)可能通过前置grain分析，但binder拒绝；识别集合外的approx_count_distinct(dimension.id)+SUM(fact.amount)通过前置分析和binder，结果58.01。此控制是必要的限制证据，不是新profile已允许该函数。未来profile必须先在原表达式与展开后执行现Strict白名单，再原模型grain校验/Compile/实际绑定与数据预检；不能只凭binder或flatten后的单表grain称安全。现API不改legacy分析顺序，也不修复非穷尽aggregate识别的通用core能力。

验证：新独立C++ runner明确接入CMake/Make release/debug/reldebug及既有test路径，不依赖ENABLE_UNITTEST_CPP_TESTS（本机FALSE）。42grain控制、原19state检查、18个SQL用例661断言与31Bun/103断言通过；缺fixture实际退出1。重建后的同一真实artifact五验证器全部通过（loader17、population46/475行+11边界、expression27、DATE21、join研究11数据反例/4规划拒绝）。stock CLI40 PASS与MCP通过，diff/format/type/graph检查通过。保留旧报告；新证据见[r6-grain-control-results.json](research/ossie-req-001/r6-grain-control-results.json)。原样来源/proposed definitions/子模块未改。

保留失败：首次MetricAnalysis聚合初始化在extension实际-std=c++11编译下失败，改默认构造后显式赋值，未改变全局编译标准。初次MCP调用漏OSSIE_EXTENSION，实际尝试community注册产物404；重跑明确指向本轮产物后通过。这是调用配置错误，不能记为扩展回归。格式与初次build曾短暂并行，最终格式完成后重新完整构建并绑定源码/产物哈希验证，不以中间构建证明最终源码。测试编写期间删掉一处未运行的错误数值sentinel，最终采用两个组的独立成员/金额/人数oracle；最终runner单独退出0。

限定Advisor一轮只读建议，Lead核对代码与分层实际结果，采纳API-only/严格白名单边界，不扩大Compile选项或通用parser重构。有效backend/effort/usage/billing unknown。当前包本地完成，下一动作本地提交，不推送；整体R6/P0未完成。下一已授权阶段才是显式冻结双表profile：完整schema/data同事务捕获、整批键/匹配预检、dataset-column来源映射、原模型strict+grain校验、最终只冻结AST、共同人口及失败/取消/重载/生命周期。其余R3/calendar/多期身份/ontology/MCP和远端CI/Ubuntu tidy仍必要。

### R6 冻结双表执行（2026-10-09，继续实施）

恢复：1d5675d本地提交，工作区干净。沿OSSIE-REQ-001既有授权实现显式fact-dimension-population-v1，单表v1保持原契约。范围为单INTEGER等值关系、INNER完整匹配、同库同事务全表捕获、整批键检查、原模型strict/grain/path检查与结构化来源映射。复用冻结collection和原子发布；对象与指标共同人口，来源更新仅reload可见。验证需要合法/非法批次、unused维度、空表、原模型粒度拒绝、冻结/失败保持、prepared重载和关闭寿命。Advisor限定一次架构反证；不推送/合并/发布。R6/P0尚未完成，其他已记录必要剩余不扩入本包。

双表本地里程碑：新增fact-dimension-population-v1显式join manifest，指定fact/dimension/relationship及INNER/reject完整匹配。原模型原始/展开Strict、RequireMetricGrain和Compile检查在flatten前执行；同事务捕获两个不同native表，比较AttachedDatabase身份。完整维表非NULL唯一INTEGER键、事实String主键身份非NULL唯一、非NULL且完整匹配INTEGER外键均先于规则/过滤。C++精确查找按每个fact输出一次，实际collection再次扫描比对身份集合及multiplicity；unused维度也参与输入校验/hash。原映射identity仍需展开为fact单列PK，不能移到dimension。源数据/结果预算分别检查，不称严格峰值内存限制。

来源与查询：原始(dataset,logical field)/(dataset,physical column)分别保留，内部_field与_column命名空间不重叠，通过AST转换；dataset顺序不选择fact。公开mapping/filter/group/object aliases使用原始名称，内部名字不能绕过provenance。flatten清除relationships，最终AST只持frozen collection dependency，避免live dimension BindTable。编译execution_target在新profile为JSON，记录事实grain、实际关系与key、完整inputs/hash、mapping版本及logical/physical来源；单表target文本和执行契约保留，共用missing_capability诊断标签改为bounded population profile。两表完整有序schema/data、joined data、policy/definitions/manifest/provenance均参与hash，明确row-order-sensitive。旧快照原子发布、weak DB及短Connection继续复用。

实际验证：6个事实的身份/维度关联、COUNT6/非NULL5/SUM55.01/AVG11.002，North4/24.98与South2/30.03；ValidUser成员f1/f2/f5/f6，SUM24.98。24具名拒绝覆盖引用/unused重复或NULL维度key、缺失/NULL或过滤外FK、坏fact身份、强转key、空fact坏dimension、空dimension、两份输入/输出预算、不同attached DB、同一native table、原模型维度grain/未知/hidden/nested聚合、COUNT(*)先Strict拒绝、反向关系、LEFT和dimension身份。空双表COUNT0/SUMNULL无group、NULL非key允许、generated候选名字/quoted dataset和physical键、computed fields/guarded ratio均实际核对。源表更新/删除后保持冻结，成功reload才切换；prepared保留旧snapshot；失败/运行中取消保留旧bundle；parent-alive子进程close通过。

限定Advisor架构建议+一次代码澄清，没有发现必须改换结构的缺陷；Lead核对实际集合和产物，不以静态审查当运行证明。另一次限定native异常root-cause建议发现旧population verifier的Promise.all fail-fast清理及同connection outstanding交错路径：现每connection顺序查询、跨connection并行，创建/查询都等待allSettled，关闭所有成功连接并保留首次异常。共享artifact helper补SQL上下文诊断。没有改product所有权/全局锁来假修复。有效backend/effort/usage/billing unknown。

保留失败与不确定性：初次metadata断言忽略label渲染引号，改结构JSON及原始alias；nested SUM/COUNT(*)实际先Strict拒绝，不能记为grain阶段；SQL测试json_extract_string缺未自动加载json扩展，改core contains且TS真实JSON解析；CLI/MCP初次漏PATH而非产物失败。最终一次六verifier并行batch中single-population和joined-population报native mutex lock failed: Invalid argument；未取得失败阶段/native backtrace，根因未建立。单独重跑、双verifier并行、带临时query追踪六进程及最后六进程均通过，追踪已移除。已确认的旧harness清理缺陷不能宣称就是共同native异常根因；若再现需抓首次native抛错栈。报告保留该风险，不抹掉先前失败。

最终源码先format再build，同一真实产物SHA 54dc03ae658f72b7e5d891abd5abd7eb86ebafb3074340c8f5d3ef804169ee68：19state+42grain C++、19SQL用例713断言、31Bun/104expect，六artifact verifier、stockCLI40和既有MCP全部通过。新生产verifier接同downloaded artifact CI job，research verifier仍明确分开。证据：[控制汇总](research/ossie-req-001/r6-joined-control-results.json)、[生产双表](research/ossie-req-001/r6-joined-population-artifact-results.json)、[原单表回归](research/ossie-req-001/r6-joined-single-population-artifact-results.json)。旧报告/原样来源/proposed definitions/旧fixtures/submodules保持原样。

当前本地功能里程碑通过既定门禁，下一动作本地提交保留，不推送。R6/P0整体未完成：生产两次捕获之间writer确定性注入尚未验证（旧host事务实验不能替代），并行native异常若再现须定位，峰值内存不受canonical预算严格约束；其余R3语言/时间、calendar、多期身份/更广ontology、population MCP、完整验收、remote CI/Ubuntu tidy仍为必要剩余。下一授权工作优先加强production capture/concurrency证据，不能以本包通过宣告所有R6能力或远端CI完成。

### R6 生产捕获并发证据（2026-10-09，继续）

恢复：8e90672本地提交，工作区干净。沿OSSIE-REQ-001既有剩余，补事实捕获后/维度捕获前writer确定性提交，以及完整预检后/发布前取消。拟使用caller ClientContextState中的内部C++观察点和native独立runner；无SQL/env开关、不使用timer推断先后、不改事务/查询/发布政策。观察点默认不存在，callback不持manager锁，屏障超时会失败并回收线程。静态链接同production source的受控runner与stock artifact无观察点回归分别记录，不声称stock verifier已注入并发。检查旧/新source版本、对象ID、金额、规则、hash、旧prepared与失败原子性；固定门禁及源码/产物证据后本地提交、不推送。前轮native mutex共同根因仍未知；若实际重现须保留首次阶段并获取native栈，不以更多timer重跑称定位。

补充同范围判别控制：在AFTER_FACT_CAPTURE观察点也注入取消，分别覆盖仍开放的source事务与完整预检后的发布边界；这能区分native路径的两类清理，但不能单凭通过关闭前轮Node/Bun mutex异常。

本轮重现：stock joined verifier在取消后第一次concept metric query报mutex invalid argument，前一snapshot discovery成功；新增AFTER_FACT_CAPTURE确定性取消使native runner后台TaskNotifier析构触发NULL unique_ptr并abort。OS拒绝LLDB/debugserver attach，未拿到首次mutex抛错栈；实际macOS crash report提供TaskNotifier/ExecutorTask/scheduler frames。固定源码显示Run在caller中断时只Interrupt内部Connection就直接throw，PendingQueryResult析构为空，而ClientContext在异常展开中跳过Destroy；可能留下已调度任务访问销毁的context。拟在内部连接仍存活时显式CancelTransaction，走InitialCleanup/CancelTasks等待任务结束再抛caller中断，不改vendor/全局锁/超时。此处为源码与崩溃证据支持的根因推断，需确定性取消与同产物stock回归验证；不能将OS权限拒绝写成成功取得debugger栈。

本轮结果：内部C++ observer在caller context注册、开始query前安装、join后移除；只传checkpoint，不传可变batch/sourceConnection，无SQL/env开关，缺省Get返回空且不分配observer。受控writer在fact捕获后、dimension捕获前原子提交f1 amount109.01及维表label Changed/eligible=false。新发布批次仍旧SUM55.01、f1=10.01、ValidUser f1/f2/f5/f6，完整双输入metadata/hash等于旧版本；无observer reload后SUM154.01/成员f5/f6及新label才出现。旧prepared保持SUM55.01。原子发布前和fact捕获后两阶段取消均保留旧snapshot/hash及实际对象/指标，callback异常失败也保留旧批次；移除observer后正常reload并关闭。threads=4和1分别运行，最终138检查。condition-variable wait有5秒超时，但join不是整个进程硬超时保证。

确认并修复生命周期缺陷：Run现在对独立内部connection依次Interrupt→context.CancelTransaction→throw caller InterruptException。清理发生在内部pending/context仍存活时；固定API通过CleanupInternal/CancelTasks排空已调度任务，不对正在执行的caller context做清理。不吞清理异常。CancelTransaction名称不等于已回滚显式source事务，短命TransactionContext析构回滚；也不宣称所有异常退出路径都被这一条修改验证。旧harness fail-fast缺陷不是这次product任务寿命问题的替代解释。Native取消崩溃已由受控测试修复前后证明；首次mutex原生抛错栈仍未取得，机制与stock症状一致而非所有mutex异常的唯一原因证明。

验证及证据：最终固定产物SHA 2b906ca54582c8a1e15fd69b0cc51b520d95f951a5bbf42efe729f9efe1abc87。19state+42grain+138capture C++、19SQL/713断言、31Bun/107expect，六个stock artifact verifiers全部通过；CLI40/MCP通过，missing fixture runner明确退出1。新runner接release/debug/reldebug test_internal，在SQL之前明确执行，不依赖ENABLE_UNITTEST_CPP_TESTS（本机FALSE）。源代码/runner/产物hash归档，[本轮控制](research/ossie-req-001/r6-capture-control-results.json)、[修复前原生失败](research/ossie-req-001/r6-capture-failure-evidence.json)、[修复后真实双表](research/ossie-req-001/r6-capture-joined-population-artifact-results.json)。静态链接同production source的受控证据与stock loadable正常context回归分别记录，未说stock verifier自身注入writer。旧历史证据/来源/fixtures/submodules未改；未取得或执行remote CI/Linux tidy。

保留其他失败：一次make质量调用缺venv PATH导致black not found，修正调用后通过，非product回归；LLDB被OS拒绝attach，以实际crash report符号栈补充，未声称成功debugger抓栈。Advisor架构一建议+一代码澄清，复现故障另一次限定root-cause只读建议；有效backend/effort/usage/billing unknown。Lead核对API只清理内部query且确实drain，以及实际TaskNotifier crash/前后结果。

本地并发/取消证据工作包及确认的任务寿命修复完成，下一动作本地提交保留，不推送。整体需求未完成。对比既有剩余，先回到R7/P0完整验收差距与population MCP调用契约，再决定是否需要拓宽时间/calendar/ontology/多期身份；不把这些长期范围悄悄混入本包。远程CI/跨平台与峰值内存限制仍未证明。如果修复后native锁异常再出现，需首错native栈而非继续假定同一原因。发布check→Set之间的取消竞态不在两处受控屏障通过的保证内。

## R7 population MCP 调用契约 checkpoint（2026-10-09，恢复）

117aa1c已本地提交、工作区干净。下一本地工作包为独立population server示例和stock CLI JSON-RPC验证，复用已有bundle/冻结query/compile，不改C++语义或loader授权。受信operator先导入native数据并加载manifest，再开放概念发现resource、对象/指标/编译三个可区分tool；请求无SQL/load/reload/规则覆盖入口，五个generic工具关闭。检查原始475行阈值10/20、对象/计数/金额一致、真实trace/definition hashes、分隔符保持、坏参数/未声明字段/概念/函数拒绝及同会话失败后可恢复。先实测duckdb_mcp数组绑定，避免旧semicolon/comma分割约定损坏表达式；新增schema契约不能当作执行输入已验证。Advisor仅限定参数表示与绑定风险，只读无子代理；请求Astra/high，有效backend/effort/usage/billing若无工具证据记unknown。首次probe误用CALL而非PRAGMA导致发布函数不存在，已纠正，非产品失败。

此包不宣称完整R7计划说明（尚缺实际predicate/dialect/grain等机器可读说明）、P0、Agent自主回答、跨平台或远程CI。沿原计划保持calendar/时间/多期身份/更广ontology及完整验收剩余；本地提交不推送、不发布外部服务。

同范围发现：旧examples/server.sql用PRAGMA mcp_publish_query(name,SQL)，实际PRAGMA契约为(SQL,URI[,format,refresh])；其资源URI变成SQL文本，探针只检查资源数量，不能证明内容能读。官方server reference（https://duckdb-mcp.readthedocs.io/en/latest/reference/server/）与本机stdlib stdio实测一致；scalar的catalog参数名不同不能替代PRAGMA验证。新server初次四参数probe仍按scalar顺序且format非法，resources为空；已改正。必要修复纳入本轮发现契约：旧server改正确SQL/明确URI，旧probe真实resources/read并核对指标/字段，而非仅计数。此为已存在MCP示例错误修复，不扩普通query AST或交付权限。

本轮结果：examples/population_server.sql在受信operator导入/加载成功后，提供ossie://population/concepts真实resource与population_objects/query/compile三个tool，使用绑定参数和严格逐元素JSON字符串数组检查；all lists必填，无invalid→[] fallback。对象/指标固定include_trace=true，compile保持executed=false及原生产AST。六个generic工具（含execute）明确关闭；load/reload/unknown调用拒绝。原475行阈值10/20独立objects/totals/15days/all date-region-type groups及继承过滤drill、6fact双表4成员/24.98均通过真实stock stdio；逗号/分号/转义quote表达式完整，106具名坏参数/概念/名称/函数/SQL构造拒绝，恢复后结果/trace保持。加extra threshold/sql不能改变规则。错误检查JSON-RPC error及result.isError，不把有响应当成功。

固定transport限制：duckdb_mcp6f9c4ba（serverInfo2.0.0），artifact SHA 4b39fbcd59d164db282c2c2b55535935d931a1a6e9c2185cf95b93a7c6f9f97e；registry依赖未由repo锁定。声明type:string不能使该transport执行strict schema；native数组会序列化为相同JSON bytes并在元素类型有效时接受，额外未绑定参数被忽略。JSON数值不是任意Decimal/bigint无损协议；仅本fixture cents/AVG容差证实，native执行类型未改。空object/group结果无row trace，须保留compile及固定operator定义；跨reload两次请求不具原子性。概念resource取真实bundle，指标/字段仍由operator提供词汇，不错误发布独立core state的ossie_metrics。完整调用/数值/排序/空集/错误契约见[population MCP说明](population-mcp.md)。

本轮验证：187项实际MCP具名检查通过、保留106个实际拒绝响应与实际compile SQL/结果/metadata。原core MCP两份资源现在真实读取且核对total_sales/i_brand；40PASS0FAIL0SKIP stock CLI、make quality-local（format/strict TS/architecture/31Bun107expect）通过。本轮不改C++/SQL执行代码，因此沿用117aa1c同产物SHA2b906ca54582c8a1e15fd69b0cc51b520d95f951a5bbf42efe729f9efe1abc87及19state/42grain/138capture/713SQL的此前证据，未虚称重跑。结果/source/fixture/CLI/MCP hash归[r7-population-mcp-results.json](research/ossie-req-001/r7-population-mcp-results.json)。既有mcp-round-trip CI接新verifier并上传结果；未执行remote CI。Advisor一次只读建议和Lead实测确定逐元素检查/必填数组、trace空集及有效版本记录；requested Astra/high，有效backend/effort/usage/billing unknown。初始CALL/PRAGMA probe错误与resource参数顺序/非法format失败已保留，未更改旧证据或vendor。

R7验收差距回顾（沿现issue/计划，不另建backlog）：

| 条款 | 当前证据 | 必要剩余 |
|---|---|---|
| R7.1 compile/query同源 | 生产共用QueryAST，compile diagnostic SQL/hash经native/stock/MCP验证 | SQL AST依赖不可文本重放；不能声称可外部执行 |
| R7.2机器可读计划 | profile/definitions/snapshot，双表execution_target包含grain/input/来源mapping | 统一实际concept/rule/metric/expanded filters/time/dialect/粒度与连接说明仍缺 |
| R7.3来自实际状态 | 非空执行row trace、bundle discovery、compile均取bound state | 空集无行trace；完整执行说明须补实际plan字段 |
| R7.4结果关联与行为 | snapshot/content/definition hashes；unsorted/sparse/unpaged/no-template-truncation已公布 | JSON精确数值传输及超出fixture的transport大小/截断行为未验证；无外部execution record服务、不可冒充跨reload原子请求 |
| R7.5不同结果/错误契约 | 3tool+conceptresource；population AST错误含object/stage/reason/capability | reused core name-resolution及JSON参数错误尚非统一结构；population完整指标/字段发现仍缺 |

本工作包本地完成，下一动作提交保留、不推送。整体P0和完整A1–A8尚未完成。下一授权本地工作优先R7.2实际机器可读compile说明与同快照证明，继续复用shared生产AST；先核对既有模型方言/原表达式/展平provenance，再确定最小字段契约。后续时间/calendar/多期身份/更广ontology与remoteCI仍是显式剩余，不因本轮MCP通过扩大语义/发布范围。

## R7 词汇发现与接口版本 checkpoint（2026-10-09，恢复）

用户选择 GAAP 为主要验收用例、保留原 TPC-DS 示例，并授权继续本地开发。沿用 OSSIE-REQ-001：新增 bound bundle 的指标/字段发现及项目接口版本/能力说明；不复用另一套 core state 的 ossie_metrics/ossie_fields。发现保留原逻辑名字、定义表达式/方言、说明和时间角色，并关联实际快照/定义哈希。加载已经对所有字段及各指标做真实类型/AST/绑定预检；发现不得把单项预检宣称为任意组合请求可执行。计划复用该预检的实际类型，不执行新的业务查询。MCP 增加对应资源、移除说明中写死的 GAAP 词汇。验收包括 GAAP、非 GAAP/双表原 namespace、旧 prepared/reload/失败加载、不受独立 core load 影响、真实 MCP resource 内容和同产物验证。架构取舍使用现有只读 Advisor，requested Astra/high，effective backend/effort/usage/billing unknown。统一错误、精确数值 transport、超 fixture 截断/大小和整体 P0/A1–A8 仍是后续必要工作；本轮无 push/merge/发布授权。

## R7 实际编译说明 checkpoint（2026-10-09，恢复）

e26f2df已本地提交，当前工作区干净。用户确认MCP业务接口为本项目契约后授权继续下一步。当前包补ossie_concept_compile机器可读plan_json，项目schema独立版本，非Ossie/MCP标准字段。须分原始definition provenance和最终production QueryAST；保留原模型方言/字段/时间角色/metric定义及经检查的join策略，final WHERE/projections/GROUP BY/from均取同一statement。阈值/root-无派生规则、大小写canonical名称、NULL/空集、single/joined、旧prepared跨reload、实际MCP输出验证。时间仅公布实际谓词和声明时间角色，不从OR等复杂表达式虚构单一区间；不宣称physical optimizer plan或compile已执行。原SQL/版本hash/目标字段及最终executed=false保持可识别；新增列为接口schema变化，消费者应按列名而非固定列数。范围为本地生产metadata及必要测试，不改业务公式、query semantics、core API、vendor或服务发布/推送授权。Advisor限定review粒度/方言/原始与展平来源准确性；requested Astra/high，有效backend/effort/usage/billing unknown。当前R7.5错误统一、完整population词汇发现及JSON精确数值/超fixture截断仍保留，不悄悄混入本包。

结果：新增ossie-population-plan-v1项目plan_json，来自同一个最终生成statement（EngineFunctions之后），包含phase/requestkind/profile/actual engine version、三定义名称/version/hash、manifest/snapshot/content hash、整批捕获行数、concept/root/formal_rule是否应用、原身份/声明key/已验证输入grain、结构化source capture与INNER/reject load-time join、canonical有序metric所选/可用dialects/声明及lowered表达式、原声明字段及effective/explicit时间角色、requested filters与actual WHERE、actual FROM/ordinal+alias+expression/GROUP BY、结果行为和限制。definitions model在load的ParseModel之后/source改写之前保存，保留已parse/lower的声明来源，不冒充raw document AST。CaptureEvidence/已检查join字段在load保存，未从拼接source字符串反推。Plan存于PopulationBind，prepared跨reload保持旧definition/snapshot；file改动与失败load不替换已加载plan。对象root applied=false，无WHERE为JSON null；完整捕获rows不等于过滤人口/分组/AVG分母，filtered_rows_computed=false；复杂OR日期谓词不推断单一区间。双表query执行扫描冻结数据，无liveJOIN，原fact粒度与输出分组分别说明。

兼容及边界：compile新增plan_json位于executed前，前9列位置保留但总列数11、executed位置改变，固定10列SELECT*/解包受影响，应按列名读取。compile新支持include_trace，MCP compile模板开启该参数，使实际追溯投影可检查。对象/metric结果schema及其执行语义不变。compile没有最终engine Prepare/表输出绑定，不声称physical/optimizer plan、执行已发生或SQL可重放；重复metric可生成各自ordinal/alias，但实际table query随后拒绝duplicate column name。这是现有compile与执行绑定阶段边界，不记作计算错误，本包不额外设计重复名处理。R7.2当前生成语义AST及不可变来源说明已落地；R7整体/P0仍不等于完成。

证据：同产物SHA4fd024239e9c09ea1838efc4f4aab4f65f026c3b2735879480cf1954cf775e15。完整make quality-local test最终19state/42grain/138capture、741SQL断言/20用例无skip、31Bun/107expect通过；7个stock artifact verifiers通过（新plan36具名检查/10个实际观察，joined新增quoted names/CASE/ratio plan检查）。actual population MCP189检查、原core MCP真实资源内容、stock CLI40PASS0FAIL0SKIP通过。native runner仍明确执行同production source，其controlled evidence不冒充loadable注入并发。结果与hash归[r7-plan-control-results.json](research/ossie-req-001/r7-plan-control-results.json)，完整JSON由stock Node/Bun独立解析；SQL只检查text envelope/prepared快照，并明确不是JSON parser验证。现artifact-load Linux/macOS same-download matrix新增plan verifier；远程CI/Ubuntu tidy未执行，未修改远程设置。

保留失败：首次手动stockprobe用relative LOAD被macOS hardened CLI拒绝，已用绝对path和-bail纠正；一次patch位置受formatter变动未命中，一次临时Python编辑脚本引号语法错误均未成功写入，之后按实际源码patch。首次新增SQL require命令未空行，测试parser报错；补空行后发现require json会使该用例skip（build tree不含json），移除依赖并以text envelope控制+stock完整JSON检查替代；新增comment空行formatter要求随后修正，最终20用例均实际执行。新verifier先误认为重复输出可直接执行，真实binder拒绝后改成“诊断ordinal保留+真实执行拒绝+正常执行对照”，不放宽生产binder。初次OR手算漏4月1日，核对原不可变CSV e11 productX/ytd20/gaap100后，正确三组为03-01 count2/sum10（e04 NULL仍计身份）、03-31 count1/sum50、04-01 count1/sum100；未更改query/fixture以迎合预期。Advisor一次架构建议+一次针对这两处语义失败的限定澄清，独立复核三组及physical_plan/range_inferred边界；Lead按源CSV检查并由真实运行通过。requested Astra/high，有效backend/effort/usage/billing unknown。

当前本地编译说明工作包完成，下一动作提交保留、不推送。对比此前R7差距，下一授权本地工作优先完整population指标/字段词汇发现及接口版本，随后统一错误结构/剩余验收；编译不能保证最终绑定、MCP精确Decimal/大整数和超fixture size/truncation仍需明确处理。更广时间/calendar/多期身份/ontology和remoteCI为既有剩余，未因新说明扩大实现或发布权限。

## R7 词汇发现与接口版本结果（2026-10-09）

新增 ossie_population_metrics()/fields()/capabilities()，在 bind 一次捕获 Current bundle，不依赖独立 core state、不重读文件、不执行额外业务 query。指标/字段保留 original dataset/name、可直接用于 SQL 请求的 AST qualified_name、对象输出 literal object_column、声明表达式/方言/类型、说明/AI context/custom extensions 和显式/默认时间角色。实际 bound_type 在既有加载成功预检处保存，以 provenance 而非原 datasets 顺序映射展平字段。三个发现函数均带完整 snapshot/definition hashes；旧 prepared、失败加载、原定义文件改变和独立 ossie_load 不会改变 bound vocabulary。

接口说明在 [population-discovery.md](population-discovery.md)：项目 population API 1.0.0 为当前契约基线，discovery v1/plan v1、Ossie document version、active/supported profile 和实际 engine version/source 分离。load_preflight_validated=true 仅为加载字段投影和单指标无分组绑定证据，execution_verified=false；重复输出等组合请求仍可能拒绝。不制造 unavailable-definition 状态，因为本 profile 未通过声明会拒绝整个 load。能力说明公开输入/请求限额、真实 checked capture join 和尚未实现/未验证边界，MCP encoding 仅是示例 adapter 契约，不宣称 SQL 环境已安装或运行 server。

MCP 示例现在有四个可读资源（concepts/metrics/fields/capabilities）及原三个工具，移除描述中写死的 GAAP 名称。GAAP 继续需求主要验收用例，原 TPC-DS 示例保留；通用双表 amount/label、逆序 datasets、computed/quoted 名字以及 quoted dataset/物理 key 验证了原 provenance 和实际查询名称。实际 MCP 三个 session 均读新资源、比较 hashes 并从返回词汇构造聚合/分组查询，非 GAAP 无需改工具模板。既有概念/对象/指标/compile schema 未改；仅添加 SQL 函数和 MCP 资源，固定资源数量和函数数量的消费者需接受新增项。

验证最终同一 artifact SHA256 f796426fbda316bc204988b82d4be4caed4c262150d4c851092aa0fb20d5db16：release 成功；quality-local、19 state/42 grain/138 capture、780 SQL assertions/21 cases/0 skips、31 Bun tests/107 expects 通过；八个 stock artifact verifier 全通过，新增 discovery 17 checks、实际 MCP 195 checks/106 refusals、core MCP actual resource 内容和 CLI 40 PASS/0 FAIL/0 SKIP FAITHFUL。CI 增加同下载 discovery probe，未运行远端 CI/Ubuntu tidy。新 [控制报告](research/ossie-req-001/r7-discovery-control-results.json) 描述范围、源哈希、失败和后续；[发现证据](research/ossie-req-001/r7-discovery-artifact-results.json)、[MCP 证据](research/ossie-req-001/r7-discovery-mcp-results.json) 保留真实内容，旧报告原样保留。

保留失败：初版预期未加引号 dimension.label，实际 AST 为 dimension."label"；手动 CLI probe 把 .read 放入 -c SQL 被 parser 拒绝，之后用 actual stock binding 检查名字。新增 SQL 假设 gaap_sum/count 声明 datatype，原文件实际未声明，改为 (empty) 并独立核对真实 Decimal/BIGINT；mean_gaap 实际声明 Float，绑定 DOUBLE。旧函数 inventory 原 15 registrations/13 names 随新增三函数更新为 18/16。新增 verifier 初版误预期派生 label 有三组；原始行与规则证明只有 North/f1,f2,f5,f6/24.98，根概念另有 South/f3,f4/30.03，Unused 无 fact 不生成组；Lead 与 Advisor 独立复核后改完整 oracle，未改 source/formula。duplicate-column refusal regex 初版大小写不符、array equality 初版误用 equal 均为 harness 修正。一次文档 patch 无匹配 context 未写入，按实际 heading 修正。Advisor 一个只读架构 round 加一次针对失败的源证据澄清，无 nested delegation；requested Astra/high，effective backend/effort/usage/billing unknown。

本地发现/接口版本工作包完成，当前动作提交保留、不推送。对比此前 R7.5 缺口，metric/field vocabulary 和 project API version 已补；下一本地包优先统一错误结构，再处理剩余验收与 MCP 数值/大小证据。ontology attribute vocabulary catalogue 尚未新增，概念规则/identity 仍通过 concepts 发现。compile 最终表输出绑定、精确 decimal/bigint transport、超 fixture size/truncation、更广时间/calendar/多期身份/ontology、remote CI 均继续可见，整体 R7/P0/A1–A8 不因此完成。

## R7 统一错误 checkpoint（2026-10-09，恢复）

用户授权继续，cdeeab9 后工作区干净。沿用 OSSIE-REQ-001 补 population 插件边界的版本化机器可读错误，保留可读诊断和旧拒绝范围；复用 core 编译/name/list 错误应在 population 出口归一化，插件自有错误避免重复包装或覆盖实际 stage。取消和致命 engine 异常不能冒充普通无效输入；最终 engine table schema binding、函数签名和未知 MCP 工具属于外部边界，明确支持范围。MCP JSON list 目前由示例宏验证，需与插件错误契约一致，不能放宽 malformed/null/非字符串元素拒绝。验收覆盖未加载、定义/哈希/身份/load 回滚、未知名字/AST/类型/list/执行错误、特殊字符和伪造 marker、原 prepared/取消控制以及真实 MCP 错误载体和恢复。接口版本按真实兼容影响更新；成功结果 schema 不变。不修改 core API 语义、source/oracle、vendor 或发布权限。Advisor 限定错误封装/控制流/兼容审阅，requested Astra/high，effective backend/effort/usage/billing unknown。精确数值/超 fixture transport、更广语义、remote CI 和整体 P0 仍未完成。

## R7 统一错误结果（2026-10-09）

新增 [population-errors.md](population-errors.md) 定义项目 error schema v1；recoverable population bind/load/execute/scan 和严格 JSON list helper 均产生带 object/stage/reason/code、nullable missing_capability 和实际/source exception_type 的 envelope。保留 legacy_message 便于过渡，但其旧 missing_capability 占位文字不是机器事实。自身错误通过专用 DuckDB ExtraInfo payload 和 schema 检查原样转发，不靠用户文本辨认；load 外层不再覆写真实 hash/parse/identity/capability stage。复用 core 名称/list 编译错误由人口入口归一化。成功 query/object/compile/concept/vocabulary schema 不变，API 升为 1.1.0；原 uniform_errors boolean 保留，新增 error_contract 明确覆盖/排除范围。

异常语义：已存在的 Run/Prepare/规则 Parser/加载 INVALID_INPUT 转换保留，source_exception_type 另列真实底层分类；新包裹的 core 错误保持原分类。std catch 只按 ErrorData 序列化类型白名单包装，bad_alloc 先原样抛出，未知/严重/取消和解码失败重抛原异常；已成为 QueryResult 的错误按 ErrorData.Type/ExtraInfo 传播。保留 nested pending 任务取消/排空修复。MCP 例子去掉旧宏，调用新增 ossie_population_strings(VARCHAR)；SPECIAL_HANDLING 拒绝 SQL NULL、JSON null、畸形/尾随 JSON、非字符串成员，无 stringify/无空列表 fallback，字符串长度感知保留包括 NUL 的字节供后续校验。

真实验证：最终 artifact SHA256 615af62accc92fd62f0936c71b8234822ffa1fceb3bb4e9f8775b4dd91777623，release、quality-local、19 state/42 grain/158 capture controls、788 SQL assertions/22 cases/0 skips、33 Bun tests/112 expects 均通过；九个 stock verifier 同产物通过，新增 error 42 checks；MCP 301 checks/106 refusals，callback 错误实际解码，missing argument/unknown tool 作为外部边界验证；core MCP actual resource 和 stock CLI 40 PASS/0 FAIL/0 SKIP FAITHFUL。原生 capture runner 对 threads 4/1 检查 missing-file envelope、unknown system_error 无 envelope、取消 native type 与现有真实扫描恢复。显式事务中 core 和 population 的 INVALID_INPUT 均使事务 aborted，ROLLBACK 后恢复，未用错误格式更改行为。额外实际 stock Parser syntax probe `User.amount = ` 返回 rule/RULE_REJECTED/source Parser。新 [控制报告](research/ossie-req-001/r7-errors-control-results.json)、[错误证据](research/ossie-req-001/r7-errors-error-artifact-results.json)、[MCP 证据](research/ossie-req-001/r7-errors-mcp-results.json) 和其余同产物报告均验证源哈希；旧报告不变。CI 仅增加同下载 probe，未运行 remote CI/Ubuntu tidy。

保留失败：若干多 hunk/context patch 与格式化后源码不符，未写入，按实际内容拆分后成功。首次 Bun framing fixture 的 schema_version 推断为宽 string，使 strict literal check 失败；修正 fixture annotation。首次 stock missing-file error 没进入 DuckDB Exception 类 catch，裸 IO 被发现后改为 std::exception + 序列化类型白名单；native/stock 后续均通过。cross-DSO RTTI 是合理但未证实的假设，不声称定位了根因。曾在最终重建过程中启动 probe，未当作最终证据，完成后重新运行九个 stock verifier 和实际 MCP。Advisor 一个只读架构 round 加一次 IO/catch 澄清，无 children；requested Astra/high，有效 backend/effort/usage/billing unknown。

本地统一错误工作包完成，当前动作提交保留、不推送。对比既有下一项 R7.5，受控 population 边界及 adapter list 错误已补，SQL signature/outer output binding、协议/unknown tool、取消/严重/未知 native 错误明确在契约外，并不冒充全引擎统一错误。整体 R7/P0/A1–A8 仍未完成。下一本地动作先核对 R1–R7/A1–A8 剩余证据，再选择精确 decimal/bigint MCP transport、超 fixture size/truncation 或其他必要验收包；compile 最终绑定、ontology attribute catalogue、自然语言语义对应、更广时间/calendar/多期身份/ontology 与 remote CI 继续保留，未扩大发布权限。

## P0 逐项证据核对 checkpoint（2026-10-09，恢复）

用户授权继续，4a9c01d 后工作区干净。本包在现有 OSSIE-REQ-001 内核对原需求 38 子项与 A1–A8；按用户决定更新 R1.3/A8 解释，逐项区分已验证的限定实现、尚缺证据、显式拒绝和 P1/非目标。读取原始需求、固定规范/来源审阅、各本地合同和最后同产物报告，并检查引用/哈希，不重复跑已通过门禁。原需求与历史结果保持不变；不创建第二 backlog、不实现新的接口/计算/transport、不将 remote CI 或真实自主 Agent 验证冒充已有证据。比较候选下一包的要求来源与影响，必要时用限定 Advisor 审阅 P0/P1 边界；本轮只交付核对和具体下一步，不扩展 push/merge/发布权限。

## P0 当前证据矩阵（2026-10-10，基于 dc1801a；五轮第5轮）

本表是当前状态，前面的 checkpoint 和原始报告保留各包当时的状态。判定对象为固定 `0.2.0.dev0`、DuckDB 1.5.6 的两个受限 population profile；不把 live core 的宽松函数/列透传当作 population 白名单，也不宣称全量 Ossie 符合性。“已验证”指该限定范围有实际执行或拒绝证据；“有偏离”指支持路径有证据但与规范/需求仍存在明确差异；“待补证据”不等于已发现计算错误。用户已将 R1.3/A8 改为仅支持新格式，YTD 阈值分类已接受，active/paid 未验证。

证据索引（均为本地证据，远程 CI 未运行）：

- S：[固定来源清单](research/ossie-req-001/sources/manifest.json)、[固定表达式规范](research/ossie-req-001/sources/apache-ossie/core-spec/expression_language.md)、[原始提取](research/ossie-req-001/sources/business-source-extraction.json)、[对应关系审阅](research/ossie-req-001/source-correspondence-review.json)。后两者证明来源及人工对应，不证明视觉呈现或任意自然语言冲突自动检测；历史草案状态由本文后续用户决定与实际执行证据补充，原文件不改。
- L：[当前 loader 验证](research/ossie-req-001/p0-final-loader-artifact-results.json)，19 类拒绝、46 个元数据对象及 2,500 字段跨批次。实现映射见 [parser.cpp](../src/model/parser.cpp)、[model.hpp](../src/include/ossie/model.hpp) 和 [describe.cpp](../src/functions/describe.cpp)。它不是完整 JSON Schema validator。
- P：[当前单表执行](research/ossie-req-001/p0-final-population-artifact-results.json)、[独立原始基准](research/ossie-req-001/baseline-results.json)、[独立边界基准](research/ossie-req-001/boundary-results.json)，合同见 [population-profile.md](population-profile.md)。原 CSV、USD 来源与阈值 10/20 定义分别绑定哈希。
- X：[表达式控制](research/ossie-req-001/p0-final-expression-artifact-results.json)、[DATE 控制](research/ossie-req-001/p0-final-date-artifact-results.json)，覆盖独立比值/空值/精度/日期 oracle 和具名拒绝，不声称支持全部语言。
- J：[core 连接研究控制](research/ossie-req-001/p0-final-join_research-artifact-results.json)、[生产双表 profile](research/ossie-req-001/p0-final-joined_population-artifact-results.json)。前者报告中的“生产尚未实现”是研究脚本旧范围说明，不能覆盖后者生产正例；单表拒绝多表仍正确。[safe-join-research.md](safe-join-research.md) 保留独立粒度 oracle。
- C：[实际生成 AST/plan](research/ossie-req-001/p0-final-plan-artifact-results.json)，36 检查，合同见 [population-plan.md](population-plan.md)。不是物理计划或最终输出 binder 的保证。
- D：[当前词汇/版本发现](research/ossie-req-001/p0-final-discovery-artifact-results.json)，18 检查，合同见 [population-discovery.md](population-discovery.md)。单项 load preflight 类型验证不证明任意组合请求可执行。
- E：[当前错误](research/ossie-req-001/p0-final-error-artifact-results.json)，54 检查，合同见 [population-errors.md](population-errors.md)。取消/严重 native 错误和外部 SQL/MCP 边界明确排除。
- M：[真实 MCP 调用](research/ossie-req-001/p0-final-mcp-results.json)，330 检查/106 拒绝，合同见 [population-mcp.md](population-mcp.md)。实际资源、typed对象/聚合/compile及10,000对象/分组完整性已验证；整数/DECIMAL文本保真，Float仍近似，不证明自主Agent或任意大小。
- Q：[最后控制汇总](research/ossie-req-001/p0-final-control-results.json)，19 state/42 grain/158 capture、840 SQL assertions/23 cases/0 skips、38 Bun tests/124 expects、11 stock artifact verifiers 和 40 stock CLI PASS。所有当前 artifact 报告绑定同一 SHA-256 `4e3b2de0f88aed0ac9ff880e9e2fa0f3002ead3a8127f9f0e6c42667d6ff1307`；本次五轮实际重跑所列门禁和产物verifiers，最终复现入口另外实际运行通过。

- F：[50字段对应与偏离](core-field-correspondence.md)、[365字段控制](research/ossie-req-001/p0-final-correspondence-artifact-results.json)，严格字符串/非空/已知属性/扩展/方言及保真；不是完整JSON Schema承诺。
- T：[typed实际数值控制](research/ossie-req-001/p0-final-typed_population-artifact-results.json)，整数/DECIMAL文本保真、实际绑定类型、非有限Float/DATE、空集schema及重复列拒绝；Float近似与全量materialization仍有明确限制。

### R1–R7：38 个子项

| 子项 | 当前判定 | 对应行为、证据及边界 |
|---|---|---|
| R1.1 | 已验证 | S 固定 core/ontology/mapping/expression 到 `8dd6732…`，DuckDB gitlink `069cc9f9b5…`；Q 与各 artifact 报告保存实际 engine/source、源码/输入/产物哈希。当前扩展提交为本节基准。 |
| R1.2 | 已验证 | L/D 精确 `0.2.0.dev0` 开发快照与项目 API 2.0.0 分开；没有“最新/完整 0.2”承诺。 |
| R1.3 | 用户调整，已验证 | 仅新根级格式；L/P 验证新格式接受、legacy wrapper/embedded mapping 拒绝与迁移诊断。原需求不覆盖用户后续决定。 |
| R1.4 | 已验证（限定范围） | L/P/E 拒绝版本/格式/绑定/规则冲突；描述性扩展保留但不执行。未知执行 AST 在 population 原始与展开阶段均拒绝。 |
| R2.1 | 已验证（限定范围） | L/P/J：单模型、名字/引用检查；发布前核对真实字段/类型/全批键。未支持的 source、身份或关系形态拒绝；不是完整 schema 校验。 |
| R2.2 | 已验证（限定范围） | L/D/F：十种逻辑类型、时间默认/显式值、AI两种形态与严格子字段类型和长度保真通过；Time/DateTime metadata不代表population执行支持。 |
| R2.3 | 已验证 | L/D/S 保存说明、label、AI/custom extensions 和原来源；USD 来自 workbook `metric!E2`，不推断、不换算，描述变动不改变计算（P）。 |
| R2.4 | 已验证（限定范围） | D/M 分别发现概念、指标、字段、能力/版本/方言和限制，词汇来自实际 bundle；自然语言不参与计算。 |
| R2.5 | 已验证，对应与偏离已归档 | F与S/L/D/P/C完成50个properties映射及已发现校验缺口；quoted ASCII case、表来源、空unique key/关系列限制明确保留，不宣称全schema。 |
| R3.1 | 已验证（子集） | X/P/C：OSSIE_SQL_2026 确定 lowering 与 ANSI_SQL，支持/转换/拒绝合同；规则/filter 的更小语言不冒充完整 expression language。 |
| R3.2 | 已验证（子集） | P/X/SQL：字段、算术/比较/Boolean/CASE、SUM/COUNT/DISTINCT/AVG、NULLIF/COALESCE、用例 DATE/显式 cast；除法必须直接 NULLIF 数值零 guard。 |
| R3.3 | 已验证（声明边界） | X/DATE/P/T/M覆盖NULL/空集/除零、DECIMAL/大整数文本保真及非有限值；DOUBLE仍近似，quoted names遵循DuckDB ASCII case偏离，无通用精确算术承诺。 |
| R3.4 | 已验证 | L/C/D/X 保存声明与选中方言及实际 lowered expression，双方言优先 OSSIE_SQL_2026，不只改标签。 |
| R3.5 | 已验证（population） | P/X/J 原始与展开 AST/函数/引用白名单、宏隔离、间接危险表达式拒绝；live core 保留较宽行为，不纳入此结论。 |
| R3.6 | 显式拒绝符合边界 | P/X/J 拒绝窗口/嵌套聚合；未证明阶段语义前不启用，无完整语言符合性声明。 |
| R4.1 | 已验证（profile） | P/J 使用固定标准 ontology 与 standalone mapping，将标量关系解析为 logical field AST/实体键；manifest 为本项目外部契约。 |
| R4.2 | 已验证 | P/M：User/ValidUser、product/region/user_type/date/ytd_gaap 标准文件实际执行；J/D 非 GAAP 文件也运行，无工具模板硬编码依赖。 |
| R4.3 | 已验证（单继承） | P/C：非递归 scalar derived_by、同一身份/属性继承，覆盖/独立 child mapping 拒绝；多继承/一般推理不支持。 |
| R4.4 | 已验证边界 | P/J/C 区分 entity_id/record_id 与 fact grain；本 profile 限一批一实体一记录，不把原唯一 user_id 当多日期身份模型证明。 |
| R4.5 | 已验证 | P/M：独立 objects/query API，同绑定人口、类型化冻结数据，范围/计数/SUM/AVG 对照。 |
| R4.6 | 已验证 | P/J/E 缺失/重复/conflicting rule、identity、mapping 拒绝，失败不替换旧 bundle。 |
| R5.1 | 已验证 | S/P/M：阈值只在 ontology 正式 derived_by，原始阈值 10；指标为独立 AVG，不再内嵌阈值。 |
| R5.2 | 已验证 | P/C/M：共享生成 AST 将正式规则 AND 请求过滤在聚合前执行；实际 SQL/对象及数值都对照。 |
| R5.3 | 已验证 | P/M：10→20 只改变 ontology/manifest hash，同 core/mapping/工具模板，完整逐日和对象结果改变；不需要 Agent prompt 干预，不证明自主 Agent。 |
| R5.4 | 已验证边界 | C/P/S：规则 owner/applied、版本/hash、实际 WHERE、声明 DATE 角色与批次捕获说明；不从复杂 OR 推断时间区间，不声称跨日期实体状态。 |
| R5.5 | 显式拒绝符合边界 | P/E：执行闭包 nonempty requires 拒绝，不将 constraint 偷换成人口 WHERE。 |
| R5.6 | 人工对应已报告；自动检测未实现 | S 与本文用户决定明确原 active paying 描述缺数据，采用可证明的 YTD 分类。hash-bound review 是本例冲突报告证据；自然语言理解属服务/Agent，不能称插件任意 NL 冲突检测。 |
| R5.7 | 已验证 | P/M/X：正式定义/hash 固定；请求仅 AND 缩小范围，不能覆盖规则/选方言，额外 threshold/sql 参数无执行绑定。加载是 operator 行为，不替代可信授权。 |
| R6.1 | 已验证 | J/Q：保留 mixed grain、fan-out、ambiguous paths 拒绝及独立 grain runner；未放宽保护以迎合数据。 |
| R6.2 | 已验证（fact grain） | J/C：指定原始 fact grain、checked capture join 与最终 frozen scan 分开；维度自身 aggregate 拒绝，独立 oracle 展示重复累计风险。 |
| R6.3 | 已验证 | P/J：全批 non-NULL/unique 身份、维度键（包括未使用行）、INTEGER FK 类型实际预检，不只读声明。 |
| R6.4 | 已验证（INNER/reject） | J/C：manifest 明确 INNER/missing=reject；NULL/缺失 FK、空维度/非法 unused key 控制，不能由基数自动选 LEFT。live core INNER 丢行风险仍独立说明。 |
| R6.5 | 已验证（两表） | J：同一 DB 的两张 committed native tables、单 fact→dimension checked capture；独立 cents 正例及 24 拒绝，不承诺任意关系/多事实表。 |
| R7.1 | 已验证生成语义 AST | P/C/M compile/query 共用编译及最终 EngineFunctions AST，可检查实际执行语义 SQL；文本依赖内部冻结扫描，不可普通 SQL replay。 |
| R7.2 | 已验证（semantic plan） | C/D：机器 plan 含规则/正式来源/指标/方言/版本/hash/WHERE/time role/grain/capture join/限制；physical=false/range_inferred=false，不虚构未知值。 |
| R7.3 | 已验证边界 | C/P/D：最终 AST 与实际 bundle/capture 状态，prepared/reload/失败验证；executed=false，load preflight 不是执行，compile 尚未保证最终 output binding。 |
| R7.4 | 已验证（受控规模） | P/C/T/M提供绑定的typed envelope、实际row_count/schema、空集与定义/捕获哈希；10,000对象和分组完整实测，未分页/未截断，不保证任意大小或峰值内存，跨reload调用不原子。 |
| R7.5 | 已验证（受控出口） | D/E/M：概念/对象/指标/compile 契约分开，recoverable callback/list 错误有对象/阶段/原因/nullable capability；native serious/cancel、SQL binder/协议错误明确在契约外。 |

### A1–A8：支持路径与拒绝路径

| 用例 | 当前证据 | 尚须保留的边界 |
|---|---|---|
| A1 | P/M：原始 475 行，完整逐日 count/SUM/AVG，3 月 10 日 45.89，原 CSV 与独立 baseline 哈希。 | USD 来自来源，AVG 用声明 DOUBLE 容差，不由均值相加校验总额。 |
| A2 | P/M：同 content、同指标/工具模板，阈值 10/20 正式定义、实际 SQL 和独立结果同步变化。 | 两个 definition hashes 证明规则版本；没有自主 Agent 证据。 |
| A3 | P/M：完整对象 ID、同范围 count/金额/mean、record/entity 边界与 trace。 | 仅一批一实体一记录 profile，不扩至跨日期身份。 |
| A4 | P/M：全部 date/region/type 分组及 drilldown，父产品/日期/正式规则保留，SUM 与 COUNT 分别对照。 | sparse groups，无 calendar completion，不相加组均值。 |
| A5 | P/X/DATE/J：等阈值、NULL/空集/日期边界、缺失/NULL FK 和无匹配拒绝。 | 无匹配 join 是 load 拒绝；空 grouped/object 无行 trace，需外部记录 compile/bundle。 |
| A6 | J/M：双表 hand-derived cents 正例、duplicate/NULL key/FK/unused dimension 反例，core fan-out/歧义/mixed grain 控制。 | 只 checked 两表 fact grain，不能以 research SQL 冒充生产控制。 |
| A7 | P/C/D/T/M：同capture/定义可复核SQL/rule/binding，typed envelope与空集schema；prepared/failed load/reload行为及10,000规模完整性已验证。 | separate compile/query跨reload不原子；Float近似，结果不是通用output/内存上界，服务仍维护执行引用。 |
| A8 | L/F/P/X/J：按用户仅新格式接受；旧形态/未知版本/非法属性、扩展和方言名称拒绝，选择/lowering与展开AST控制。 | 不要求旧格式兼容；quoted ASCII case与声明执行子集保留，不宣称全规范。 |

### 剩余必要工作与候选下一包

按用户调整范围的A1–A8本地规则驱动验收已完成归档及实际复现；两个有界profile共同覆盖相关场景，不表示每个profile独立通过全部八项或P0全规范/P1/远端能力完成。完整 JSON Schema validator 不是隐含新增目标；发现的 scalar 容错应明确选择严格拒绝或声明偏离，并验证其兼容影响。Quoted case 已有明确 DuckDB 偏离，不为此承诺替换引擎 identifier semantics。

历史候选包比较（基于4a9c01d；已由后续包完成）：parser 严格化直接处理 R2 的实际偏离；MCP 数值/大小研究处理当前结果可能失真的未验证边界；直接归档 A1–A8 会遗漏这些限制的实测或裁定。compile 最终绑定与 ontology attribute catalogue 可改善诊断/发现，但不是原文独立要求的必需新功能。更广时间/calendar、多期身份、requires/一般 ontology、混合粒度/窗口明确留在 P1 或非目标；remote CI/Ubuntu tidy 是未运行的质量证据，不能用本地接线代替，也不自动授权 push。

历史取舍（基于4a9c01d）：下一本地包优先做 MCP 数值与有界大结果研究，不预设编码方案。按当前支持类型/公开输入限额构造大于 2^53 的整数、可区分相邻分的高精度 DECIMAL、聚合扩大精度、负值/NULL；先比较原生值、原始 MCP content token 和客户端解码，oracle 不先经过普通 JSON.parse。大结果核对完整 ID 多重集、字段/行数、消息字节及错误/截断；同时展示固定 operator 批次下空结果与 compile metadata 的关联。若失真，随后选择保真表示或明确限制传输支持范围，不能只加警告仍称该值正确。不在本审计包新增 adapter 接口。

采用 advisor-escalation：复用现有只读 Advisor，一轮范围反证、无 children/写入。Advisor 同意 transport 证据优先，并指出 AI scalar 容错是具体尚未接受的偏差，不能用“不做完整 schema”消解。Lead 核对 StringArray/ScalarText 确认该行为，已单列 R2.2/R2.5；R5.6 后续用户决定与历史审阅分开，空结果在固定启动/no reload 示例中可关联，不能推广到任意可重载服务。requested `gpt-6-astra`/high，effective backend/effort/usage/billing unknown。未把 Advisor 意见当测试结果。

核对结果与哈希引用记录见 [P0 审计记录](research/ossie-req-001/p0-evidence-audit-results.json)。本包只更新当前矩阵、计划和过期 limitations 描述；来源/业务定义/历史报告及实现未改，功能门禁没有重跑。此次审计工作包完成后本地提交保留，不推送；整体 P0 仍需 transport 证据、R2 偏离收口和最终规则驱动验收归档。

保留审计脚本失败：首次链接检查先检查了尚未写出的审计记录自身，因文件不存在退出，未生成报告；修正顺序，仅该输出在写出后检查，其余引用仍在生成前检查。该失败不涉及功能实现或既有证据哈希。

## MCP transport 研究 checkpoint（2026-10-09，恢复）

用户授权继续，基于 06b88c5，在本 issue 内验证当前三个工具模板与观察到的 duckdb_mcp 产物。新增独立 synthetic fixture 与 Bun/TypeScript verifier；原始 475 行/业务定义不改。数值 oracle 用整数/Decimal 文本与 BigInt，不先经 JSON number；同时记录原生类型/值、原始 content token 与普通客户端解码差异。大结果限制在 profile 的 10,000 行/16 MiB canonical 输入以内，核对全部 ID/字段/行数、实际 content 与外层 JSON-RPC 字节、空结果 compile/hash 关联以及超限拒绝不截断。先实际研究，不预设无损表示或修改生产接口；发现失真保留反例，由限定 Advisor 比较修复方案后进入后续包。测试研究脚本的真实执行、类型/格式检查；功能源码不改时复用既有 artifact，不重建/重跑无关门禁。无 push/merge/发布授权。

## MCP transport 研究结果（2026-10-09）

结果：原生冻结对象/聚合保持精确值，当前 MCP 原始数值 token 对这批输入也保持精确；普通 JSON.parse → JavaScript Number → 文本产生 18 处差异（对象、分组及汇总的观察次数，不是 18 个独立业务值）。因此问题位于普通客户端解码，不能归因于 MCP serializer。探针分别报告 raw_numeric_token_discrepancies=0 与 ordinary_json_decoding_discrepancies=18，明确不宣布当前 numeric client 合同无损。固定 same artifact SHA `615af62a…`、engine 1.5.6/source `069cc9f9b5`、MCP `6f9c4ba`/SHA `4b39fbcd…`；未追随 registry 更新。

| 对照项 | 原生/原始 MCP token | 普通 JSON 解码后再输出 |
|---|---|---|
| 大整数对象 n01 | 9007199254740993 | 9007199254740992 |
| 相邻分 n01/n02 | 9007199254740992.01 / 9007199254740992.02 | 都为 9007199254740992 |
| DECIMAL(38,2) 总额 | 123456789012345678910241767144864448.79 | 1.2345678901234568e+35 |
| 正整数 SUM（HUGEINT） | 9241386435364257792 | 9241386435364258000 |

独立 [synthetic fixture](../test/fixtures/population-transport/README.md) 使用标准定义派生副本，不改原业务资产、不将这些数值声称为 USD；native 对照用 CAST VARCHAR，期望用 literal text/BigInt，原始 token 用有界数值词法与有理数比较，不先转 Number。NULL 与负值保留；SUM 的实际类型分别为 DECIMAL(38,2)/HUGEINT。普通 round-trip 相等只说明该文本未丢位，不证明二进制浮点能精确进行 Decimal 运算。

大结果实测：10,000 对象的全部 14 字段和 ID 多重集通过，单行 payload 为 1,120 UTF-8 bytes，含中文/引号/反斜线/换行/tab；content 23,267,891 bytes，外层 JSON-RPC line（含换行）33,367,965 bytes。10,000 分组逐组 COUNT/SUM/integer SUM 与独立序号/cents oracle 通过，content 3,587,891 bytes、line 3,807,965 bytes；不假设顺序。输入 10,001 行及 over-16-MiB canonical value load 拒绝，旧批次未替换/截断。固定 operator 启动、无 reload tool 时，空 objects/groups `[]` 与 retained compile snapshot/content/ontology hashes 关联；空 ungrouped COUNT=0/SUM=NULL 有实际 trace。该证据不证明可重载服务跨调用原子性，不是最大 output/峰值内存上界，也不覆盖所有合法 computed projections/configurations。

证据见 [18 项真实探针结果](research/ossie-req-001/r7-transport-artifact-results.json) 和 [研究控制记录](research/ossie-req-001/r7-transport-control-results.json)。新增 TypeScript verifier 真实运行成功，make quality-local 类型/格式/graph/33 Bun tests/112 expects 通过；功能 C++/SQL/生产 tool templates 未改，复用原产物，没有重建或重跑不相关 SQL/native/301 项 MCP 门禁。CI 尚未接新研究 probe，也未执行远端；不是已修复 adapter 的门禁。

保留失败/纠正：首次 typecheck 在 assert.rejects 的 unknown error 参数处失败，改为显式 String(error) 解码已知失败载体。初版观察报告只检查 raw token，却使用含混 lossless_numeric_contract_verified 字段；发现普通 JSON.parse 失真后弃用该结论，最终报告分开 raw 与 decoded，不把研究成功称合同无损。初版 synthetic mean description 沿用原 workbook/USD 文案，经审阅改为无业务单位的合成描述并更新 manifest；大 fixture 导入也改用整数商/余数拼 Decimal 文本，避免 import oracle 先经过 DOUBLE。最终重新运行真实探针，源码/输入/产物哈希全部绑定最终文件。

Advisor：按 advisor-escalation 复用只读 Astra/high 一轮取舍、无 children/写入；确认 serializer 与 client decode 的区分、已测成功点不等于 output 上界，提出原 synthetic unit 文案风险。Lead 检查实际 token/原生值后采纳。requested gpt-6-astra/high，effective backend/effort/usage/billing unknown。后续推荐 MCP adapter 的稳定十进制字符串表示和实际类型说明，而非仅要求未知客户端选用 lossless parser；整数/Decimal 同类型不能小值 number、大值 string 混用，AVG DOUBLE 仍近似。编码变化应独立版本并复测普通 JSON 客户端、对象/分组/空集和当前大 fixture；不改原生 SQL 类型/计算。

本地研究工作包完成，当前动作提交保留、不推送。下一已授权本地包优先设计/实现受控 MCP typed numeric 表示，先明确实际绑定类型如何进入 adapter，再验证兼容；同时处理非有限 DOUBLE 的明确表示/拒绝边界。之后收紧 R2 字符串数组容错、完成字段对应偏离结论和最终规则驱动验收。整体 P0 仍未完成，calendar/多期身份/一般 ontology/混合粒度/窗口及 remote CI 保持既有边界。

## 后续需求：MCP 大结果分页（2026-10-09，用户新增）

用户要求将分页作为今后需求记录，明确先调研，不贸然设计或实施。本次仅记录于现 OSSIE-REQ-001，不创建第二 backlog，不修改接口，也不立即开展调研；不把分页加入当前数值传输修复包或 P0 必需完成条件。

问题与依据：当前 objects/query 一次返回全部结果，没有 page size/cursor/next-page 契约，也不保证顺序。10,000 行是输入批次上限，不是分页；已测 33.4 MB 对象响应完整，不证明 Agent 消费或模型上下文适用性。目标是让调用方能够分批消费大结果并明确完整性，具体方案待研究。

将来启动时先核对固定 MCP 协议与实际 duckdb_mcp 实现的分页能力/约束及可参考实现，再评估对象与聚合分组的不同语义、稳定顺序、冻结快照/游标生命周期、行数与响应字节预算、错误与兼容影响，以及调用方如何确认完整结果。游标或 offset、参数/返回 schema、默认页大小、上限和版本策略均未决定；这些是调研问题，不是已选方案。分页不能改变完整人口上的聚合口径；验收应独立检查重复/漏行、空页/结束标识、重载/失效边界及跨页合并结果。后续范围、设计与实施须在用户启动该需求后确定。

## MCP typed numeric 修复 checkpoint（2026-10-09，恢复）

用户授权继续其他工作，e2468ca 后工作区干净；本包落实已研究的数值传输修复，分页仍只为后续需求。沿现 issue 新增可选 typed JSON SQL adapter 出口，复用同一 QueryAST、actual engine Prepare names/types、冻结 bundle/取消错误控制；不改变原生对象/聚合返回类型、公式或输入限额。MCP 对象/聚合工具改用版本化 typed result，整数/Decimal 为稳定十进制字符串，NULL 为 JSON null；DOUBLE 的近似性及非有限值、DATE/Boolean/String 编码明确，类型不按声明/名字猜测。空结果也带实际 bound columns 和快照/定义 hashes。成功 MCP 返回 schema 有兼容变化，独立 result schema 与 API 版本说明，compile 保持诊断语义。Advisor 限定审阅类型、生命周期、异常/编码和兼容。必要验收覆盖原生类型不变、普通 JSON 客户端恢复精确值、对象/分组/聚合/NULL/quoted/computed/双表、prepared/reload/失败保持、非有限 DOUBLE 和既有大 fixture；重建最终同产物后运行项目门禁/真实 stock verifiers/MCP，不将旧 raw 研究结论冒充 repaired adapter。无 push/merge/发布授权。

## MCP typed numeric 修复结果（2026-10-09）

新增 ossie_objects_json/ossie_concept_query_json，可选 SQL adapter 返回单行 result_json。复用原 QueryAST/EngineFunctions/严格规则与 grain，保存同 statement 的实际 prepared names/types，并在执行验证 result schema。逐 chunk 读取 native Values 编码，不查询另一套 discovery 推断类型，也不改原生 SQL 的对象/聚合成功 schema。adapter envelope 为 [ossie-population-result-v1](population-result.md)，含实际 concept/request_kind、bound columns/one-based ordinal/encoding、完整 rows、实测 row_count、executed=true 与 bound profile/snapshot/全部定义 hashes；空 objects/groups rows=[] 也保留 schema 和 hashes。无排序、分页、截断，完整编码成功才返回，错误不返回 partial success。

整数（含 HUGEINT SUM）/DECIMAL 不论大小均用字符串；Boolean 保留 JSON Boolean，NULL 保留 null，DATE 含正负 infinity、VARCHAR 按长度 JSON escaping。FLOAT/DOUBLE 全部用近似数值文本，显式 NaN/+Infinity/-Infinity/-0，不恢复精确 Decimal 或 NaN payload bits。duplicate prepared names 按 DuckDB ASCII case 规则拒绝，避免单列 adapter 绕过原外层 binder 的重复名保护；Unicode Ä/ä 则仍是合法不同列。参考 decoder 使用相同 ASCII-only folding，按 actual bound type/encoding 校验，保留 numeric strings。

兼容：MCP 对象/聚合工具名称、参数、三个工具/四资源不变，成功格式从 row-object arrays 改为单 result_json carrier，需普通 JSON.parse carrier 后再 parse envelope，使用 columns/rows；compile/discovery 格式不变。按既有“项目 API 包括 documented MCP”政策，此不兼容成功格式变化将 API 升为 2.0.0，而非把 additive SQL 函数误标为整个契约的 minor update；它不是 Ossie document version（仍 0.2.0.dev0）。注册 inventory 为 19 names/21 registrations，元数据/CLI 控制已更新。capabilities 增加 typed result schema/numeric_encoding，保留 broad exact_numeric_transport_verified=false，不冒充任意 Float/客户端算术的精确性保证。

最终同产物 SHA256 `e95dd3606913d5485e5fc983e2fff18751a7da0a8f2926882dba00e217541d5d`。release、quality-local、19 state/42 grain/158 capture、814 SQL assertions/23 cases/0 skips、38 Bun tests/124 expects 通过；十个 stock artifact verifiers 同产物通过。新 [typed adapter 24 检查](research/ossie-req-001/r7-typed-typed_population-artifact-results.json) 实际核对 native/普通 JSON 客户端的大整数、相邻分、DECIMAL38、widened HUGEINT、非有限/负零 float、Boolean/NULL/infinite DATE/控制字符、ASCII duplicate 与 Unicode 不同列、prepared/reload/失败保持和 runtime CAST Conversion structured failure/recovery。timed typed query 确实触发 interrupt、保留 native cancellation/旧 batch 并恢复；未控制编码中途的精确 checkpoint，不把它称 deterministic serialization cancellation。真正 [population MCP](research/ossie-req-001/r7-typed-mcp-results.json) 330 检查/106 拒绝仍通过 GAAP10/20、全日/组/下钻、通用双表、实际词汇/plan/错误；原 core MCP actual resource 和 CLI 40 PASS/0 FAIL/0 SKIP FAITHFUL。最终 [控制记录](research/ossie-req-001/r7-typed-control-results.json) 绑定源码/输入/报告 hashes，旧报告不覆盖。CI same-download matrix 接入 typed verifier，远程 CI/Ubuntu tidy 未执行。

大 fixture 在新结果格式下再次逐字段/ID/组复核：10,000 对象、17 columns，carrier 43,340,457 bytes、外层 line 63,181,123 bytes；10,000 groups carrier/line 2,469,414/2,749,800 bytes。此版 fixture 增加 float/Boolean，不声称是与旧 33.4 MB 响应的受控体积比较。JSON/string materialization 和嵌套转义确实有额外成本；canonical input limit 不是 output/峰值内存保证。分页按用户要求仍只记录，未研究/实现。raw research probe 改为显式使用 native 函数并 [同产物复核](research/ossie-req-001/r7-typed-raw_transport-results.json)：旧 raw tokens 仍无差异、普通 Number 解码仍有 18 处差异；它是对照，不是当前 production adapter 行为。

保留失败/纠正：一次多文件 SQL/workflow patch 的 errors.json 名称不匹配，整次未写入；按实际文件名拆分后成功。两次 C++ format gate 失败，因为 Lead 误用默认 check-only 的 format.py，没有 --fix；检查固定脚本后使用 --fix --noconfirm，重建并重跑最终证据，先前报告不作为最终源码证据。Advisor 首轮指出 duplicate output bypass，已在 actual prepared names 层拒绝；限定澄清指出 decoder 的 Unicode toLowerCase 与 DuckDB ASCII-only 不同，已修正并用真实 native/MCP Ä/ä 控制验证。没有改变 fixture 数值以迎合预期。Advisor requested Astra/high，一轮架构与一轮限定澄清，无 children/写入；effective backend/effort/usage/billing unknown。

本地 typed numeric 工作包完成，当前动作提交保留、不推送。R3.3/R7.4 在当前 typed adapter 范围的数字/空集关联缺口已补，有界大结果仅已有测例证据；整体 P0 尚需 R2 字符串数组 scalar 容错收紧、最终逐字段偏离结论和规则驱动验收归档。下一已授权本地包为 R2 loader 字符串数组类型收口，不扩大为全 JSON Schema validator。compile 最终绑定/catalogue 改进、分页调研及更广 P1/非目标、remote CI 仍保留既有边界。

## R2 字符串数组 checkpoint（2026-10-09，恢复）

本包沿 OSSIE-REQ-001 修复已确认的 StringArray scalar 容错：固定 schema 的 AI synonyms/examples、dataset primary_key、relationship from_columns/to_columns 均为字符串数组。unique_keys 已逐元素拒绝非字符串，不扩大为全 schema validator，也不修改 RequiredString/OptionalString 的既有 scalar 容错。合法字符串（含数字外观、Boolean 外观、中文及转义字符）保留原值；非字符串成员明确报出字段和零基索引，失败加载保持旧模型。先记录旧产物真实接受反例，再验证拒绝、合法元数据、关联列/主键及 bundle parse 失败保持。重建同产物执行原生/SQL/工具门禁与 stock artifact/MCP 回归；结果与必要剩余维护此 issue/现计划，本轮只本地提交、不推送。R2 逐字段偏离结论与最终 P0 规则驱动验收仍需收口，分页保持后续调研。

## R2 字符串数组结果（2026-10-09）

本包完成 StringArray 的严格 JSON 字符串成员检查，JSON/YAML 共用此路径。覆盖五种 AI scope 的 synonyms/examples、dataset primary_key 和 relationship from_columns/to_columns；非字符串成员报字段/零基索引，不再 stringify。unique_keys 既有严格检查保持。合法数字/Boolean 外观字符串、空串/空数组、缺省数组、中文/引号/反斜线/换行/tab 与嵌入 NUL 的 AI 数组按长度完整保留；metadata 字节保留不授权执行 NUL 表达式或 identifier。单值 RequiredString/OptionalString scalar 容错没有修改，不称全 schema validator；这些剩余偏离必须纳入下一轮逐字段结论。

[旧产物真实接受证据](research/ossie-req-001/r2-strings-pre-fix-results.json) 绑定旧 artifact/parser 和完整反例输入，观察数字/Boolean 被转成字符串。[新 loader 证据](research/ossie-req-001/r2-strings-loader-artifact-results.json) 新增 35 个具名严格类型拒绝，逐次比较完整旧 metadata 保持；19 个静态坏文档、46 个 TPC-DS metadata 对照及 2,500 字段跨 chunk 控制仍通过。quoted "123"/"true" 真正声明为字段，所以主键/关系拒绝控制不能靠未知字段拒绝蒙混通过。YAML 引号内文本接受、未引用数值/Boolean 拒绝。

[实际 bundle 错误控制](research/ossie-req-001/r2-strings-error-artifact-results.json) 将 bad core 重新绑定正确哈希再加载，得到 LOAD_FAILED/load/source Invalid Input，并验证旧概念及后续实际查询恢复；错误发生在 loader，不把它冒充 hash gate 或 manifest JSON parse。项目 API 仍 2.0.0，本包收紧无效文档接受行为，合法 schema 字符串数组兼容，typed result/MCP 成功格式不变。

最终 artifact SHA256 `c15ace232f406e319bd27ec01254359171ba786e7765d3f48b4f21002879e49e`，release、quality-local、19 state/42 grain/158 capture、817 SQL assertions/23 cases/0 skips、38 Bun tests/124 expects 通过；十个 stock artifact verifiers、真实 population MCP 330 检查/106 拒绝、core MCP 实际资源和 stock CLI 40 PASS/0 FAIL/0 SKIP FAITHFUL 同产物通过。源码/输入/报告哈希绑定于 [控制记录](research/ossie-req-001/r2-strings-control-results.json)，历史报告不覆盖，远程 CI/Ubuntu tidy 未运行。

保留失败/纠正：pre-fix 探针初版 datasets=[] 被既有 nonempty 检查拒绝，改成有效单数据集后才观察 scalar coercion；没有把最初拒绝当缺陷证据。新增 bundle 控制最初错误假设 parse/DEFINITION_INVALID，真实输出为 load/LOAD_FAILED；按现合同纠正期望，不改生产错误 stage。typed verifier 首次漏 stock CLI PATH 而拒绝，core MCP 首次漏 OSSIE_EXTENSION 而尝试 registry（404）；补齐明确本地产物环境后重跑成功，没有 registry fallback。

本地 R2 数组工作包完成，提交保留、不推送。R2.2 的 StringArray 成员类型缺口已补；R2.5 逐字段支持/转换/拒绝及单值容错、quoted case 等偏离最终结论仍必要，之后串起规则驱动 A1–A8 验收归档，整体 P0 尚未完成。下一已授权本地动作是逐字段结论收口；分页后续调研、P1/非目标、remote CI 和交付权限沿原边界。

## R2 逐字段核对 checkpoint（2026-10-09，恢复）

c25d118 数组修复已本地交付；本包核对固定 core schema 的全部 properties 到 parser/model/describe 路径，区分合法字段映射、描述保留、lowering、执行限制及 validation 偏离。沿现 issue 建立字段对应表和真实 stock characterization（单值 scalar 转换、缺省/非法 optional 值、未知属性/custom extension、字符串长度、名称/键边界），不把接受非法文档测试作为符合规范的证明。先记录事实再决定必要修复；本包不扩 full JSON Schema validator、ontology general execution、分页或推送。原 R2 支持证据复用 c25d118 同产物，新增控制绑定源码/输入/报告哈希；最后规则驱动 A1–A8 验收仍必要。

## R2 逐字段核对结果（2026-10-09）

[Core 字段对应表](core-field-correspondence.md) 已逐项覆盖固定 schema 的 50 个 properties 声明，包括 root 引用和 SemanticModel 定义；无 properties 的 Dialect/DataType/Vendor 定义另有结论。每行记录 parser/model 路径、发现/执行用途及验证/转换/拒绝边界，区分选中原文与 lowered AST、声明键推导与 population 真实数据预检。ontology/mapping 的支持子集仍归既有 profile，不扩一般 ontology 执行或 full schema。

[十四个真实 characterization](research/ossie-req-001/r2-correspondence-artifact-results.json) 复用 c25d118 的 stock 产物 `c15ace232f406e319bd27ec01254359171ba786e7765d3f48b4f21002879e49e`，接受 13 个变体并拒绝一个 schema 允许的 empty inner unique key；此接受计数只是观测，不是符合规范或批准偏离。确认单值 name/description/label/expression scalar 转文本、optional 容器为空、空 name、未知 root 属性丢弃、非法 extension 结构原 JSON 保留、lowercase dialect 接受；合法 extra AI 属性按规范保留。保存完整输入/哈希和 metadata，源码读取与真实观测分别表述。

新发现必要缺口：合法含 NUL 的 AI string/object instructions raw JSON 完整，flattened instructions 截为前缀；unique_keys 同样截短。RequiredString/OptionalString 及 unique_keys 的非 length-aware string 构造可定位根因，description/label 等共享 helper 有潜在同类问题，本包不把源代码推断称所有字段均已实测。这是合法内容保真问题，应修复后才做 R2 最终验收。其余 scalar/minLength/additionalProperties/custom extension/dialect 大小写偏离已列明，但未得到用户作为兼容例外的接受；不能以“不做 full schema”自动批准。

本包仅新增对应文档与 characterization verifier；生产 C++ 未变，未重建或重跑已通过的功能门禁。quality-local 格式/类型/架构和 38 Bun tests/124 expects 通过，[审阅控制记录](research/ossie-req-001/r2-correspondence-control-results.json) 检查来源 manifest、50 项 schema inventory/table 对齐与源码/报告/产物 hashes。工具验证失败保留：numeric expression 控制初版错误读取 metadata.expression（此列不存在），改为实际 ossie_fields.expression 后成功，不改生产行为。证据审计初版假定全部 manifest 项使用 saved_path，业务原件实际使用 source_path；按记录路径校验后重跑成功，首次未生成报告/未提交。

逐字段审阅包本地完成，R2.5 对应表缺口已补，但 R2/P0 仍未最终验收。下一已授权必要动作是 length-aware 字符串保真修复及 raw/flattened/list 对照，再处理剩余校验偏离收口、最终 A1–A8 规则驱动归档。只本地提交，不推送；分页、P1/非目标和 remote CI/Ubuntu tidy 保留原边界，无新 backlog。

## R2 字符串保真 checkpoint（2026-10-09，恢复）

2470cff 的真实控制已定位 NUL 截短；本包修复 parser 的单值字符串、AI string instructions 和 unique_keys 成员读取长度，保留原始 JSON 与展开值一致。不修改数字/Boolean scalar 容错、minLength 或全 schema；既有数组严格成员检查保持。先将旧 defect characterization 改为完整值回归，增加说明/label、两种 AI 形态/所有 scope、复合键与合法控制字符的实测；执行表达式/名称边界单独核验，不把 metadata 保真当作授权 NUL SQL。失败加载保留旧状态，重建最终同产物运行质量/native/SQL/stock/MCP 回归。旧证据不可覆盖，交付只本地提交，不推送，R2 剩余校验偏离及最终 A1–A8 归档仍必要。

R2 保真包发现与范围补充：初次 length-aware 修复后的真实执行边界控制显示，core 的 ANSI_SQL 表达式 `amount\u0000 + 100` 被 DuckDB parser 按前缀解析成 amount，虽然发现出口保留完整字符串。这会改变公式，故本包增加所有 expression variants 的显式 NUL 拒绝，放在 AST parse 前。core 描述性文本继续完整保留；population 既有整份定义 NoNUL 拒绝保持，不将 core metadata 能力扩大为 population NUL 支持。未扩大为全字段/全 schema 校验。

## R2 字符串保真结果（2026-10-09）

修复 ScalarText 的字符串分支、AI string instructions 与 unique_keys 的读取长度；它们不再把合法 NUL 后内容截掉。所有 model expression variants 在 AST parse 前拒绝 NUL，避免 DuckDB prefix parse 改变公式；描述性 metadata 的保留和 SQL 可执行边界分开。project API 仍 2.0.0，单值数字/Boolean scalar 容错与其他 schema 偏离未改，population 整份定义 NoNUL 仍拒绝。

[26 个真实字段/保真控制](research/ossie-req-001/r2-fidelity-correspondence-artifact-results.json) 更新之前的 14 characterization，增加五个 scope 的 AI string/object 完整 raw/flattened 对照、模型/数据集/字段/指标 description 与 field label、复合 unique_keys、含 NUL expression 拒绝及完整旧 metadata 保持。测试使用实际 NUL 加中文/quote/backslash/newline/tab，而非仅文本字面量“\u0000”。[46 个实际错误控制](research/ossie-req-001/r2-fidelity-error-artifact-results.json) 另核对 hash-valid NUL core 的 population parse 拒绝/旧 bundle 保持。旧 truncation 报告不覆盖，字段对应表/limitations 更新为修复后的现状。

最终 artifact SHA256 `df2f3285e591df96ef70287771eb5384efd64ae89050eb3150bfe4a17cfbd43a`。release、quality-local、19 state/42 grain/158 capture、829 SQL assertions/23 cases/0 skips、38 Bun tests/124 expects 通过；11 个 stock artifact verifiers（原十项加字段对应/保真）、population MCP 330 检查/106 拒绝、core MCP 实际资源、stock CLI 40 PASS/0 FAIL/0 SKIP FAITHFUL 均同产物通过。[控制汇总](research/ossie-req-001/r2-fidelity-control-results.json) 绑定最终源码/输入/报告 hashes，固定 schema 50 项 inventory 与对应表对齐。新增 verifier 接 CI same-download artifact step；remote CI/Ubuntu tidy 未执行。

发现/纠正保留：初次仅改长度读取后，真实 ANSI_SQL `amount\u0000 + 100` probe 显示 metadata 完整而 Compile 实际生成 avg(facts.amount)，证明 parser 只用前缀；在本包增加显式表达式拒绝并重建后，最终 fixture/verifier 检查拒绝且旧模型完整保留。此中间产物不是最终通过证据。质量门禁未失败；最终哈希审计发现新增 CI verifier 后 plan/MCP 报告仍绑定旧 workflow hash，重新运行这两项同产物控制后通过，未手改报告哈希。既有 metadata 与 expression AST 的用途区别未改变。

本地保真工作包完成，只提交保留、不推送。必要剩余为 R2 单值 scalar/minLength/unknown properties/custom extension/dialect 校验偏离的明确收口（尚未接受为兼容例外），之后最终 A1–A8 规则驱动验收归档；整体 R2/P0 未完成。下一已授权步骤优先收紧单值字符串类型容错，再核对其他校验边界。分页只作未来调研、P1/非目标和远端验证维持原范围。

## R2 单值字符串 checkpoint（2026-10-09，恢复）

9bb9622 保真包后，当前沿 OSSIE-REQ-001 收紧 RequiredString/OptionalString：规范字符串字段仅接受实际 string，optional 仅在字段缺省时给空值，显式 NULL/数字/Boolean/容器拒绝。名称/来源/端点/方言/表达式、description/label/AI instructions 的合法字符串继续 length-aware。只修类型容错，不扩大 minLength/unknown-property/extension/方言枚举或 full schema；原数字类型业务列不受影响。真实 JSON/YAML 控制覆盖 helper 调用范围和旧完整 metadata 保持，hash-valid malformed core 的 bundle 错误及旧快照保持；更新 characterization 的旧接受期望，历史报告不覆盖。重建最终同产物门禁、stock/MCP 回归后本地提交，不推送；其余校验偏离与 A1–A8 仍必要。

R2 单值包回归发现：既有 YAML differential fixture 的 numeric_desc 在 YAML 是裸 1.5，但 JSON 对照已是字符串 "1.5"，旧测试依赖转换容错。首次 SQL gate 因该非法 description 拒绝而失败。原 YAML 原样保留为 invalid/yaml_numeric_description.yaml，并显式验证拒绝/旧模型保持；正例仅把该值加引号，与既有 JSON 对照的内容和类型一致，不改 1.5 的期望文本，也不改变业务数据/原始规范。其他 YAML 转义/anchor/block/comment 与全部 discovery differential 检查保留，重跑最终 SQL gate。

## R2 单值字符串结果（2026-10-09）

删除 ScalarText 容错转换。RequiredString 仅接受实际 string，缺省/NULL 必填值拒绝；OptionalString 在字段缺省时返回空值，显式 NULL、数字（integer/real）、Boolean、array/object 均拒绝并给出对象/字段诊断。所有 helper 字符串按实际长度读取；AI string/object 与 strict string arrays、metadata 保真、expression NUL 拒绝及 population NoNUL 边界保持。description/label 的缺省空值/SQL NULL 发现方式不变，合法 quoted 数字/Boolean 外观文本仍接受；此模型定义类型检查不禁止数据表的数字/Boolean 值。project API 仍 2.0.0，不扩为完整 JSON Schema validator。

[154 个真实字段控制](research/ossie-req-001/r2-scalars-correspondence-artifact-results.json) 包含 120 个具名拒绝（20 个字段位置 × 六类非法值）：十个 required name/source/endpoint/dialect/expression 位置、四个 description + field label、五个 AI object instructions 位置。每次拒绝均比较完整旧 metadata，而非仅检查模型名字。另检查缺省 optional、quoted scalar text、YAML 六类非法 description 和已有保真/其他偏离控制；154 计数不是全部成功接受，接受 22、拒绝 132。旧 scalar 接受报告保留于前缀历史记录，新 characterization 已改为拒绝。新增 static SQL required-number/optional-NULL 拒绝与旧模型保持；YAML 原非法 fixture 原样留作拒绝控制，合法 differential 与已有 JSON string "1.5" 对齐。

[48 个实际错误控制](research/ossie-req-001/r2-scalars-error-artifact-results.json) 包含 hash-valid bad core.description=false 的 LOAD_FAILED/load/source Invalid Input 和旧 frozen bundle 保持/后续查询恢复；未让 hash/NUL 检查提前替代本次类型拒绝。最终 artifact SHA256 `af5431eb7974ef81072c3716da329563711d76cdb0e8d21cd7529965c2f2a84d`。release、quality-local、19 state/42 grain/158 capture、834 SQL assertions/23 cases/0 skips、38 Bun tests/124 expects、11 个 stock artifact verifiers、population MCP330/106 refusals、core MCP 实际资源、stock CLI40 PASS/0 FAIL/0 SKIP FAITHFUL 均通过。最终 [控制记录](research/ossie-req-001/r2-scalars-control-results.json) 绑定源码/输入/报告/旧 YAML 原文 hashes；source hashes 全部一致，remote CI/Ubuntu tidy 未执行。既有 CI same-download verifier 会覆盖此次新增控制。

保留失败：首轮 SQL gate 的 YAML differential fixture 裸 1.5 触发严格 description 拒绝（811 assertions/1 failure，不能作为最终通过），已如上保留原文负例、仅给正例加引号，与既有 JSON 对照类型/值一致；重跑所有 SQL/native 门禁后 834 断言通过。其他功能/工具门禁没有失败，原始业务来源/固定规范不改。

本地单值字符串工作包完成，提交保留、不推送。R2 字符串 scalar 偏离已补；仍有 minLength、unknown properties、custom extension 成员结构和 dialect 大小写/enum 的明确校验偏离，未作为用户批准的兼容例外。下一已授权收口优先名称 minLength 与未知 core 属性检查，再处理 extension/dialect 并归档最终 A1–A8；整体 R2/P0 未完成。分页/P1/非目标/remote CI 与发布边界保持原范围。

## R2 名称/未知属性 checkpoint（2026-10-09，恢复）

1312b23 后本轮按固定 core schema 实施 minLength=1 的五类 name、dataset source、relationship from/to 非空检查，以及 root/dataset/field/metric/relationship/dimension/expression/dialect-variant 的 additionalProperties=false。按完整、大小写敏感、length-aware property key 校验，拒绝未知属性，不 trim 空白或拒绝 schema 合法空 description/label；AIContext additionalProperties=true 的 vendor keys/raw JSON 保留不变。custom_extensions 成员结构/未知键与 dialect enum 后续单独收口，不称全 schema。先更新旧接受 characterization，验证每个 scope 的未知属性（含 case typo/NUL key）、空 required name/source/endpoints、合法 empty optional/AI 扩展、JSON/YAML、失败完整 metadata/bundle 保持。保留 legacy/版本/格式特定诊断优先，重建最终同产物门禁/stock/MCP 后本地提交不推送；其余两类偏离与最终 A1–A8 仍必要。

本包构建失败保留：首轮编辑新 field context 的 C++ 字符串时错误转义双引号，formatter 将错误 token 格式化但编译器拒绝。已按真实 compiler error 修正该一行 quoted context，重新格式化/构建；没有把失败构建或旧 artifact 当最终验证。

## R2 名称/未知属性结果（2026-10-09）

RequiredNonEmptyString 在五类 name、dataset source、relationship from/to 上落实 schema minLength=1，保留 string 类型/实际长度检查，不 trim 或增加 schema 之外的空白限制。ValidateKnownFields 对 root/dataset/field/metric/relationship/dimension/expression/dialect variant 实施 additionalProperties=false，完整长度、大小写敏感比较属性名；未知键拒绝，JSON 编码诊断保留 NUL 后缀。AIContext 仍按 additionalProperties=true 保存 vendor 自定义键，不执行其规则；empty description/label 仍合法，custom_extensions 成员/enum 后续处理。legacy wrapper、未知版本/混合 ontology-mapping 特定诊断保持优先。

[193 个真实字段控制](research/ossie-req-001/r2-fields-correspondence-artifact-results.json) 包含 26 接受/167 拒绝，新增八个空 required 位置、八种 scope × unknown/case/NUL 三类未知键、YAML 未知 root 属性拒绝，以及五种 scope 的合法 AI vendor-property/raw JSON 保留和 whitespace name/empty optional 接受。每次新增拒绝比较完整旧 metadata；旧 empty-name/unknown-root 接受控制已更新为拒绝，历史报告不覆盖。[50 个实际错误控制](research/ossie-req-001/r2-fields-error-artifact-results.json) 验证 hash-valid bad core vendor_rule 的 LOAD_FAILED/load 和旧 frozen bundle 保持，后续原生查询恢复；不是 hash gate 替代本次拒绝。

最终 artifact SHA256 `b90af60183045005b92321517baac51654c1177e8adfed531bf408c7798458d5`。release、quality-local、19 state/42 grain/158 capture、837 SQL assertions/23 cases/0 skips、38 Bun tests/124 expects、11 stock artifact verifiers、population MCP330/106 refusals、core MCP 实际资源与 stock CLI40 PASS/0 FAIL/0 SKIP FAITHFUL 同产物通过。[控制汇总](research/ossie-req-001/r2-fields-control-results.json) 绑定最终源码/输入/报告/50 项对应表 hashes，remote CI/Ubuntu tidy 未运行。既有 CI same-download verifier 覆盖新增控制，API 仍 2.0.0。

失败保留：首轮新增 field context 的 C++ quoted string 转义写错，formatter 接受并格式化错误 token，编译器拒绝；按 compiler diagnostic 修正一行后重新格式化/重建成功，最终全部功能门禁通过。没有修改 fixture 或规则以隐藏此构建错误，也未使用旧 artifact 冒充修复产物。

本地工作包完成，只提交保留、不推送。R2 name minLength/closed core properties 偏离已补，尚待 custom_extensions 成员结构/字段检查与全部 dialect enum/大小写边界，再归档最终规则驱动 A1–A8；整体 R2/P0 未完成。下一已授权收口这两项规范检查，不改变 vendor 字符串自由取值或 AI 扩展许可，也不自行增加 extension.data JSON 内容验证（当前 schema 仅要求 string）。分页/P1/非目标与 remote CI/发布权限保持原范围。

## R2 扩展/方言 checkpoint（2026-10-10，恢复）

31d78c4 后本包沿 OSSIE-REQ-001 校验五种 scope 的 custom_extensions：缺省合法，出现则须 array；每个 member 为 object，仅 vendor_name/data 两字段且均为 required string。Vendor 为任意字符串，允许空串/自定义值；data 仅按 schema 校验 string，不新增 JSON 内容解析或单位推断。DialectExpression 每个 variant 的 dialect 须精确匹配固定十一项 enum，拒绝大小写变体/未知值（即使存在另一个可执行 variant）；仍仅执行 ANSI_SQL/OSSIE_SQL_2026，保留其他合法 variant 文本并确定选择。不扩大表达式执行子集或全 schema claim。更新旧接受 characterization，验证五种 scope/JSON/YAML/合法空扩展和字节保真/每次完整旧状态保持、hash-valid bundle 失败及同产物回归；历史报告不覆盖。本地提交不推送，最终 A1–A8 规则驱动归档仍必要。

## R2 扩展/方言结果（2026-10-10）

五种 core scope 的 custom_extensions 现在严格检查数组、成员对象、vendor_name/data 两个 required string 及 closed member properties。合法空数组、空字符串、自定义厂商名、非 JSON 的 data 文本和 core metadata 的 NUL 完整保留；AI additionalProperties=true 不收窄。全部 expression variants 的 dialect 必须精确匹配固定十一项 enum，包括未选中项；另有合法 fallback 也不允许非法名称。执行选择仍 OSSIE_SQL_2026 优先于 ANSI_SQL；其余九种合法方言仅保留描述，单独出现时拒绝执行。

[365 个字段控制](research/ossie-req-001/r2-vocabulary-correspondence-artifact-results.json) 包含35接受/330拒绝，新增五种 scope 的140非法扩展、10合法扩展、全部方言保留/选择、field/metric 非法名称加合法 fallback、九种合法但不可执行方言和两个 YAML 对照。拒绝逐次比较完整旧 metadata。[54 个错误控制](research/ossie-req-001/r2-vocabulary-error-artifact-results.json) 证明 hash-valid malformed extension/noncanonical dialect 在实际 bundle 加载中得到 LOAD_FAILED/load，旧 frozen bundle 保持且后续查询恢复。

回归发现并保留：旧 population descriptive 控制使用 vendor 键（应为 vendor_name），首次 stock population verifier 因 unknown field 拒绝而失败；旧 discovery 控制将扩展写成对象。原非法构造仍作为负例，不改原始规范或业务定义；合法对照采用规范成员并维持描述变化不改变对象/指标结果。新增负例第一次直接匹配未知 vendor 文本，因结构化错误 JSON 中引号已转义而断言失败；改用现有 populationError 解码器检查实际 code/stage/reason，重跑通过。该问题是验证器错误匹配，未放宽生产校验；旧证据报告不覆盖。

最终 artifact SHA256 `4e3b2de0f88aed0ac9ff880e9e2fa0f3002ead3a8127f9f0e6c42667d6ff1307`。release、quality-local、19 state/42 grain/158 capture、840 SQL assertions/23 cases/0 skips、38 Bun tests/124 expects、11 stock artifact verifiers、population MCP330/106 refusals、core MCP 实际资源与 stock CLI40 PASS/0 FAIL/0 SKIP FAITHFUL 同产物通过。population47拒绝控制、discovery18控制包含保留的旧非法扩展；[控制汇总](research/ossie-req-001/r2-vocabulary-control-results.json) 绑定最终源码/报告/输入/50行对应表，20份固定来源哈希核对不变。remote CI/Ubuntu tidy 未运行；project API2.0.0与result-v1保持。

[50行字段对应与偏离结论](core-field-correspondence.md) 已更新：已发现的数组/单值类型、字符串保真、minLength、unknown properties、extension成员和dialect enum/case缺口已补。表来源、最少数据集、空unique key拒绝、关系列非空/等宽、quoted ASCII case及有界执行profile仍为明确边界；不是完整JSON Schema/全表达式规范符合性承诺。本工作包本地完成，只提交保留、不推送。整体P0仍待最终规则驱动A1–A8验收归档，下一已授权动作是整理该验收结论与必要证据；分页只未来调研、P1/非目标和remote发布权限不变。

## 五轮自动执行 checkpoint（2026-10-10，用户授权）

用户明确授权继续自动执行5轮，沿现OSSIE-REQ-001与最终A1–A8验收必要工作推进，不启动P1/分页或远程交付。第1轮更新38项当前证据矩阵并核对同产物来源；第2轮保存A1–A4完整原始/规则变更/对象/分组/下钻证据；第3轮核验A5–A6边界与安全连接；第4轮核验A7–A8追溯、typed/MCP与格式方言；第5轮汇总验收结论与复现入口。每轮记录范围、实际证据、剩余及本地提交。第1轮仅核对已有证据，不冒充重跑功能；原始需求和历史报告不改。

### 自动第1轮结果：当前证据与范围（2026-10-10）

38项需求和8项验收索引已更新到3ca59c6同产物证据，R2.2/R2.5/R3.3/R7.4的旧待补状态已被后续真实校验/typed控制取代；保留quoted case、DOUBLE近似、规模/内存和跨reload边界。[证据审计](research/ossie-req-001/p0-round1-evidence-results.json)核对20份固定来源、12份同产物报告的源码/fixture/helper绑定、38/8索引和50字段清单。哈希核对不是计算正确性测试；本轮没有重跑原生功能门禁。新Bun审计脚本format/typecheck通过。本轮本地提交；必要剩余是第2轮完整查询证据、第3/4轮边界和追溯、第5轮最终结论。

### 自动第2轮 checkpoint：A1–A4完整查询证据

沿现issue扩展既有独立bigint-cents verifier的结果记录，保存完整对象/逐日/三维分组/子组对象与实际compile plan、trace。校验10/20只有derived_by变化、core/mapping/请求模板一致；原CSV/正式资产不改。实际重跑该stock verifier，声明均值容差、无序和单位来源。边界/追溯最后结论仍在后续轮次，不扩大生产范围。

### 自动第2轮结果：A1–A4完整查询证据

既有独立stock verifier已实际重跑，保存阈值10/20完整166/63对象、15/15逐日、101/45三维分组和全部下钻对象、实际SQL/plan/trace；同core/mapping/请求模板且ontology仅derived_by变化，完整结果对照独立bigint cents及AVG容差通过。3月10日37/1697.80/45.88648648648649与21/1277.54/60.8352380952381可直接复核。新记录[完整报告](research/ossie-req-001/p0-final-population-artifact-results.json)与[验收入口](p0-acceptance.md)在research record中链接；plan内链接以docs为准。本轮只增强证据输出，不改生产C++/原始输入；format/typecheck通过。本地提交后进入第3轮A5–A6；第4轮追溯及第5轮整体结论仍必要。

### 自动第3轮 checkpoint：A5–A6边界与连接

沿现issue保存已有人工边界fixture的完整对象/每日/空集结果，实际重跑单表、表达式、DATE、core连接研究与生产双表控制，以及原生state/grain/capture和SQL。研究join证据与生产拒绝分开，schema/规则/源资产不改；明确全批预检、INNER/missing=reject及无日历补齐。后续追溯/MCP与最终验收仍必要。

### 自动第3轮结果：A5–A6边界与连接

单表完整报告增加11行边界fixture实际对象/每日与独立期望，空对象/分组=[]，scalar COUNT=0/SUM=AVG=NULL；阈值相等/NULL/半开日期/sparse groups按声明执行。单表/表达式/DATE/core连接研究/生产双表stock verifiers实际重跑通过，生产双表6facts/55.01/11.002、4派生对象和24类拒绝，研究fan-out控制独立保留。原生19state/42grain/158capture与840 SQL/23 cases/0 skips实际通过，无生产C++修改。第2轮报告因新增边界记录重跑更新为本轮完整输出，旧r2证据不覆盖。本轮本地提交后进入第4轮A7–A8；最终结论仍必要。

### 自动第4轮 checkpoint：A7–A8追溯/格式/传输

沿现issue重跑loader/字段对应/plan/discovery/error/typed结果与真实population MCP、core MCP、stock CLI。独立进程重复固定定义/同CSV，对比完整对象与全部组/下钻及SQL/定义/content hashes，不以snapshot ID充当业务相等证明。合法新格式与两种执行方言、其他合法描述性variant、非法版本/类型/AST拒绝保持；10,000规模实测不是分页或任意大小承诺。第5轮最终结论仍必要，无远端行动。

### 自动第4轮结果：A7–A8追溯/格式/传输

固定CSV/定义在独立进程复现完整两阈值对象、aggregate、每日/分组/下钻及SQL/content/ontology hashes一致，记录p0-round4-replay-results.json；并非普通SQL-text replay/通用determinism。loader、365字段、36plan、18discovery、54error、24typed、实际MCP330/106拒绝、core MCP资源与stock CLI40/0/0 FAITHFUL实际重跑通过。10,000对象/分组完整传输与Float近似/输出内存限制保持，未实现分页或发布。新replay检查脚本format/typecheck通过。第4轮本地提交后仅余第5轮最终归档及复现入口；remote CI/Ubuntu tidy、P1/非目标均不扩大。

### 自动第5轮 checkpoint：最终验收与复现

沿现issue汇总A1–A8最终支持/拒绝证据、声明偏离与范围，提供Bun单命令复现及机器归档检查；复现写入调用方新目录，保留仓库历史结果。核对最终源码/输入/报告hash并执行quality-local。范围裁定使用既有只读Advisor一次反证，requested Astra/high，有效backend/effort/usage/billing未知；Lead自行核实。结论仅覆盖已调整要求的两个有界profile，remote CI/Agent/P1/分页不得以本地通过冒充。

### 自动第5轮结果：调整范围验收归档完成（2026-10-10）

[最终验收入口](p0-acceptance.md)与[机器索引](research/ossie-req-001/p0-final-acceptance-results.json)区分实际重跑和归档核对；新Bun runner在调用方新目录启动全部11产物verifiers、实际MCP、独立进程replay、core MCP和CLI，实际运行成功。源码/输入/报告/固定来源hash核对通过，quality-local最终format/strict typecheck/architecture/38 Bun124expects通过；第3轮已实际运行19/42/158 native与840 SQL/23 cases/0 skips，C++未改。同artifact完整证据归[最终控制](research/ossie-req-001/p0-final-control-results.json)。

保留失败：runner首轮使用Bun类型声明不接受的stderr="stdout"，typecheck拒绝；改同一文件描述符写日志时漏import openSync/closeSync，typecheck/quality失败并导致一次runner ReferenceError。补齐import、重跑format/typecheck/quality与全套实际runner通过；首失败目录failure.json及控制记录保留，不放宽校验或更改生产/fixture。最终链接核对还发现一处指向已删除Python loader脚本的历史链接；改为明确历史实现已移除、当前Bun脚本可复现，旧报告不改。

Advisor复用既有只读身份、无children/写入，一轮反证未发现限定语义结论的新阻塞；核对165个现存文件路径hash不是功能重跑。建议明确两个profile共同覆盖相关场景、manual来源冲突不等于任意NL自动检测、research join文字不覆盖生产证据，并分清归档核对与实际重跑。Lead分别以完整实际报告、runner退出0和最终source audit核实。requested Astra/high，effective backend/effort/usage/billing未知；建议不替代测试。

最终结论：按用户仅新格式/YTD分类/SQL NULL与分组契约调整后的范围，两个有界profile共同覆盖的A1–A8支持/拒绝路径通过本地规则驱动验收。50字段对应与声明偏离已明确；不称全量P0/全规范、真实自主Agent或远程CI完成。五轮本地提交完成，不推送/合并/发布。此已授权验收工作完成；分页、calendar/多期身份/一般ontology/混合grain/窗口等仍为独立后续需求，remote CI/Ubuntu tidy及自主Agent为未验证证据，后续启动须按具体范围与动作授权。

### 跨环境复现/Linux质量 checkpoint（2026-10-10，用户授权）

用户选择先补跨环境复现与Linux质量验证。沿现OSSIE-REQ-001解决来源审计的Mac原始xlsx/docx绝对路径、补原需求raw核对；保持raw和原manifest字节不变，研究hash-bound相对副本locator与严格missing/tamper拒绝，不降低fidelity。复现入口与CI局部source hash核对需覆盖新增依赖；Ubuntu24.04/Linux ARM64 Docker实际构建、clang-tidy、TS/原生/SQL/stock验收按可用环境验证。与远端Linux AMD64 CI分开，不推送/修改远端规则/发布，不扩P1。Advisor一轮只读审阅来源契约，Lead负责实现和证据。

### Linux clang-tidy 发现与修复 checkpoint（2026-10-10）

Ubuntu18.1.3 clang-tidy已在population.cpp发现137处大括号、4处range-copy、2处乘法widening、2处BIGINT narrowing、using namespace和非const全局atomic诊断（当前147项，完整门禁尚未结束）。本包按原质量范围修复这些实际诊断：仅让clang对大括号生成fix，其他显式using/宽类型常量/局部static原子/const引用/受10000行预算约束的转换由Lead审核；不关闭检查或扩大业务能力。C++变更使旧二进制/报告不能代表最终源码，须重建Mac/Linux并重跑原生/SQL/完整stock/MCP及最终hash核对。初次tidy失败原日志保留，formatter安装主动终止与AMD64格式检查单独记录。

### Linux运行验证发现 checkpoint（2026-10-10）

修复后Mac原生/SQL/43 Bun和完整stock/MCP通过；Linux原生capture因standalone Git自动缩写8位source ID拒绝，stock双表verifier一次2ms interrupt未产生预期拒绝。前者完整DuckDB commit已核对，按上游CMake接口显式传入真实commit再重建，保留严格10位身份检查。后者调查Node API参数化执行先异步prepare的时序，拟明确prepare/bind后反复interrupt并要求timer实际运行、严格拒绝/旧bundle恢复，不隐藏重试/放宽失败；复用只读Advisor限定反证该控制，Lead实现和实测。确定性取消阶段证明仍来自native屏障，stock timer控制不作阶段确定性声明。失败日志/failure.json及当时产物hash保留，最终Linux原生/完整验收仍必要。

### 跨环境复现/Linux质量结果（2026-10-10）

[Mac实际复现](research/ossie-req-001/portability-macos/acceptance.json)与[Linux实际复现](research/ossie-req-001/portability-linux/acceptance.json)分别绑定最终产物13d5317f…与4926122b…；两端19 state/42 grain/158 capture、840 SQL/23 cases/0 skips、43 Bun/136 expects、11 stock verifiers、365字段/54错误、MCP330/106、独立进程replay、core资源及CLI40/0/0 FAITHFUL通过。[跨环境完整结果](research/ossie-req-001/portability-cross-environment.json)20项对照完整对象、聚合、每日/分组/下钻、规则/content/SQL、边界和空结果；共享验证器相等不替代各自独立bigint cents oracle，也不称通用确定性。

UbuntuARM64 clang-tidy18.1.3全门禁退出0，13独立TU/26compdb项含root注册；[inventory](research/ossie-req-001/portability-linux/tidy-inventory.json)区分匹配清单与实际trace，上游成功调用静默。固定clang-format11.0.1在Mac和独立UbuntuAMD64只读checkout通过，ARM64未执行完整quality-local。原始20文件manifest和raw不改，新locator只补三条映射；原需求首次纳入内容校验，不能追溯称历史已查21份。5项source控制/14断言明确缺失、篡改、错误绑定、duplicate/unknown/missing、越界及manifest预期hash不能被locator覆盖。QwenPaw许可证实读为Apache-2.0，Advisor初始packet曾误写MIT，后由Lead核对并补NOTICE；原license不改。

[失败记录](research/ossie-req-001/portability-failures/)保留旧CI字面测试失败（修为完整runner接线控制）、首轮tidy诊断（同TU重复项；第二次因已补braces而只有其余10项）、ARM formatter源码安装主动终止、原生8位source ID拒绝、stock双表一次2ms取消未拒绝。clang只自动补137处braces，Lead审核explicit using、宽类型常量、函数local static原子、const引用及10000预算下BIGINT cast。严格检查不关闭。原生完整commit核对后传真实GIT_COMMIT_HASH，运行时确认v1.5.6:069cc9f9b5；不放宽identity。stock取消先prepare/bind再反复interrupt，最终Mac1次/Linux2次调用均得Interrupt，旧snapshot/content/6-5-55.01-11.002恢复；精确取消阶段没有注入。

Advisor沿原只读身份补一次限定取消反证：Node参数化执行异步prepare及执行时清interrupt有源码依据，原失败具体时点没有采样；建议只修harness，不接受成功/隐式retry/跳过，必须settle后销毁prepared。Lead采纳并在两端实际核验；requested Astra/high，effective backend/effort/usage/billing未知。来源反证和取消反证均不替代实测。

本包本地完成，最终文档/报告核对后提交保留，不推送/合并/发布。远端LinuxAMD64/macOS CI、真实自主Agent仍未验证；P1/calendar/一般ontology/多期身份/窗口/混合grain与分页调研仍为独立后续范围，本包不启动。历史p0-final/r2证据不覆盖；新归档保留运行时路径标签并在control中列明副本映射。

### 恢复与远端CI交付预检（2026-10-10）

用户在跨环境包0d79c73完成后要求继续。本地已授权验收/跨环境工作包完成，不自行启动P1或分页；比较剩余后，下一必要交付证据是远端Linux AMD64/macOS CI。只读预检确认push目标为jacobbubu/duckdb-ossie的codex/ossie-execution；远端tip为08c06e6668b7c033ea774ce2170526f22ea5db23，预检时本地0d79c73领先26提交。GitHub Actions enabled=true、allowed_actions=all，分支run list为空、workflow list为空；不把启用状态当成跑过。仓库Issues已关闭，使用现OSSIE-REQ-001记录，不声称读取远端issue。

本次没有重跑功能、推送、dispatch或修改远端设置；上一包源码/报告hash保留0d79c73时点，不重写历史控制。用户AGENTS.md规定“A pilot does not authorize push, main merge or deployment”及“Continue continues authorized work; proposals are not approval”；此前本轮只本地提交，因此推送这个具体fork/分支须明确授权。已完成reviewable工作并核实目标，下一待授权动作是普通fast-forward push（禁止force）触发现有CI，再检查实际jobs/失败并按本地范围修复；不合并main、不发布、不改远端规则。此预检说明本地提交后，push范围会再包含这一文档提交；实际推送前须重新核对remote tip与ahead/divergence，不能依赖本节旧快照。

### OSSIE-REQ-001：推送后 Actions 未注册（2026-10-10）

用户授权推送已完成：27提交fast-forward至codex/ossie-execution，远端tip ee78f5fcf7ec16c48d36f583f2fb548d78067ab8。用户随后要求判断Actions原因。只读检查：默认main/开发分支都有同名workflow且含push入口；fork enabled=true/allowed_actions=all；workflow与run总数0；上游同名workflow active。指定开发分支手动dispatch失败404（workflow在默认分支未识别），没有创建run。Token默认read且不能批准PR。可复用workflow内容在v1.5.6 ref可读取；本包未改YAML或设置。

问题范围是fork工作流注册/启用，不是已执行job失败。GitHub官方[事件说明](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#workflows-in-forked-repositories)说明fork默认不运行工作流、需在Actions页启用；这是当前最可能解释，未登录页面不能确证首次启用提示。推荐先核对已登录fork Actions页并完成首次启用，再dispatch开发分支、追踪CI gate与必要失败修复；不提前合并main、不放宽token、不发布。CLI没有可启用workflow对象、当前浏览器未登录，首次启用的具体操作和页面访问仍需落实。诊断记录本地保存、不额外提交推送；远端验收仍未完成，历史本地通过证据保留。

### Actions 首次启用后远端验证 checkpoint（2026-10-10）

用户确认首次启用已完成。GitHub已注册主工作流380187158，state=active；此前fork首次启用解释获得前后状态证据。Lead已成功dispatch codex/ossie-execution，run 38015505660，验证tip ee78f5fcf7ec16c48d36f583f2fb548d78067ab8，事件workflow_dispatch。运行链接：https://github.com/jacobbubu/duckdb-ossie/actions/runs/38015505660 。初查queued/jobs=[]，不称CI通过；下一必要动作跟进实际job和CI gate，保存失败/最终结果。未更改权限、合并或发布；诊断文档仍本地未提交。

### OSSIE-REQ-001：远端 AMD64 tidy 字节窄化修复 checkpoint（2026-10-10）

实际run38015505660的tidy job114104763139失败；原始日志保存/tmp/ossie-ci-tidy-38015505660.log。population.cpp JSONString的两处unsigned char追加string触发bugprone-narrowing-conversions，重复TU输出仍只有两处源位置。沿已有issue修复char原始输出/unsigned字节分类，不降低tidy规则、不改变JSON约定。验证覆盖所有字节在signed/unsigned char编译选项下的输出对应及UTF-8/quote/backslash/control转义，再重建和原生/SQL及stock typed/error验证；必要剩余为新提交远端tidy和CI gate。前一LinuxARM64tidy通过不能代表AMD64字符类型环境。用户push/跟进CI授权沿该fork开发分支复用，不合并或发布。

### AMD64 tidy 字节窄化本地修复结果（2026-10-10）

JSONString以原始char追加字符串、显式unsigned char用于分类；不改JSON编码或降低规则。实际重建后Mac产物hash仍为13d5317fb860f9ad81437a0a86cb8c996d7742a026f927f7b503c0fadf340c95（优化后的产物与前包字节相同，不声称源码未变）。signed/unsigned-char各256字节（含控制/quote/backslash/高位字节）独立输出期望通过，-Werror/-Wconversion编译通过；原生19/42/158和SQL840/23通过；stock typed24含UTF-8/转义/真实MCP、error54通过；clang-format11.0.1和diff whitespace检查通过。首次辅助harness自身int转换/输出循环-Wconversion失败，显式转换修复后通过；typed首次因stock CLI未在PATH失败，使用原独立CLI后通过，未放宽检查。

原CI tidy失败日志及byte脚本/结果、typed/error同产物报告保存在docs/research/ossie-req-001/ci-amd64-tidy/。当前工作包仅本地验证完成，远端tidy/构建/完整验收仍必要；沿用户该fork分支push和CI跟进授权提交推送修复，新push触发现有工作流，不改设置、不合并或发布。历史证据及失败日志不覆盖。

### 修复推送后的远端验证 checkpoint（2026-10-10）

修复提交96ac9682bb2a5750f6b8a3e7b8f98fb151d25d0f已正常推送；push自动创建run38016307549：https://github.com/jacobbubu/duckdb-ossie/actions/runs/38016307549 ，初查queued。旧run38015505660的tidy已确定失败，其日志已归档；取消旧运行以避免继续构建已被替代的提交。当前源码本地检查通过，远端新tidy/构建及CI gate仍待结果，不称远端通过。本段状态记录留本地，避免为运行状态重复推送触发新一轮CI。

### OSSIE-REQ-001：远端 MSVC 原生测试构建环境 checkpoint（2026-10-10）

新run38016307549已完成：tidy/format/project通过，LinuxAMD64/ARM64及两种Mac、三种Wasm构建job成功；WindowsMSVC在Test extension阶段首次编译ossie_state_test，找不到标准库memory（C1083），MinGW因矩阵fail-fast取消；整个build failure使stock artifact/MCP被跳过，CI gate失败。上游v1.5.6 workflow的Build extension cmd步骤调用vcvars；后续Test extension bash没有该环境。现原生目标EXCLUDE_FROM_ALL导致后续才编译，故按MSVC条件纳入主构建，保留所有测试和严格门禁，不修改上游或远端权限。验证本地CMake/原生检查及下一轮实际MSVC编译运行；无本地Windows，不能声称本地MSVC通过。失败原日志/tmp/ossie-ci-38016307549-failed.log保留。

同时更正Linux覆盖说明：上游workflow对linux_arm64明确跳过inside/outside tests，因此该远端job成功目前只证明ARM64构建；本地UbuntuARM64原生/SQL完整验收已有独立证据，远端独立P0仍配置LinuxAMD64和MacARM64。剩余必要为Windows修复后的CI gate及这些实际产物验收，不扩大Linux版本/P1/分页。

### MSVC 原生目标构建阶段修复本地交付（2026-10-10）

CMake仅在MSVC时将三个既有原生目标纳入ALL，编译发生于上游已初始化vcvars的主构建；其他工具链仍显式构建，测试调用/门禁保持。本地实际重新configure/build及19state/42grain/158capture通过，固定format-check通过；没有本地Windows，MSVC分支与运行仍待远端。失败日志、上轮各job状态、本地CMake/format/native日志保存docs/research/ossie-req-001/ci-msvc-environment/。沿已授权fork开发分支提交推送触发下一轮CI；整体远端验收尚缺Windows通过后的artifact-load/MCP与CI gate。本包不改业务源码、不扩测试发行版、不合并或发布。

### 远端 CI 验收完成（2026-10-10）

实际run38019831758验证65b9828fe1d85dd2985113bfed8dc779f65149f9，17个job均success，CI gate成功；包含tidy/format/project、Linux两架构构建、Mac两架构、WindowsMSVC/MinGW、三种Wasm、LinuxAMD64/MacARM64独立产物验收和MCP round trip。ARM64 Linux上游跳过tests的限制保留，不把构建success说成该平台测试跑过。Windows修复已获实际远端编译和测试步骤成功证据。

下载LinuxAMD64/MacARM64产物与独立报告，重新运行现verifyAcceptance：两端A1–A8/来源与verifier绑定及各16份日志hash通过；产物SHA256分别e24605ad2a00a53b6372d483aa7b268891739c3367a14f2b87860e62565c4ca7、0a928dcfe8baa5a679bbc0817c180aa665fd265484c115b1041c039a780f0088。原始报告ZIP与GitHub artifact digest逐字节核对通过，原样保存在docs/research/ossie-req-001/ci-final/，并附run/artifact元数据与下载复核结果。初次复核因CLI不在PATH、进程内PATH更新不可用和本地MacCLI与runnerCLI哈希不同失败；最终分别使用官方1.5.6 LinuxAMD64/MacARM64 CLI启动独立Bun进程后严格通过，未放宽hash检查。

报告中remote CI未运行文字来自独立验收器旧模板，本次CI结论由run及全部job状态单独证明；原始报告不重写。该模板文字改进作为现issue后续维护项，不阻塞已经实际完成的远端验证；自主Agent、分页/P1等仍不在此包内。当前跨环境/remote CI工作包完成，不启动新能力、不合并main或发布。此后仅本地提交原始证据和状态文档，不为了归档再触发整轮CI；远端已验证tip继续为65b9828。

### OSSIE-REQ-001：验收报告模板收尾（2026-10-10，恢复）

用户要求继续。远端CI工作包已完成，比较剩余后本轮只处理已记录的模板维护及归档推送，不启动P1/分页/Agent。独立验收/来源审计不应硬编码remote CI未运行，改为说明其不提供CI/native质量结论，状态由独立运行证据给出；replay说明仅约束该检查的证明范围。原始历史报告/ZIP不改。验证现绑定远端两端产物与原报告重算验收（文档性复核，不重跑功能）、TS格式/类型/既有工具测试，提交并普通推送现fork开发分支；新tip的CI与65b9828已通过运行分开记录。不合并或发布。

### 验收模板维护交付（2026-10-10）

三个报告模板只改证明范围措辞，去除硬编码remote CI未运行；CI结果仍由独立run/job证据确认，验证逻辑/业务源码/历史报告ZIP不改。对65b9828远端LinuxAMD64与MacARM64同产物及既有报告实际重算验收，A1–A8行为/status/evidence hashes逐项一致；这是文档性复核，不是功能重跑。新模板源码hash/复核报告及43Bun/136expects、format/typecheck通过日志归docs/research/ossie-req-001/report-template-maintenance/。旧ZIP的原始文字保留，原通过CI链接仍有效。

本轮模板维护完成；将ce6fae5归档和本轮提交普通推送已授权fork分支，新push的CI另行核实，不能把65b9828通过等同新tip已通过。已调整的P0/跨环境/远端CI工作包完成，当前剩余交付仅本次push及新CI状态；P1、分页调研与自主Agent等后续范围保持未启动，不合并main或发布。

### OSSIE-REQ-001：产物上传超时检修（2026-10-10）

用户要求检修。最新b2d486a的run38027392941 attempt1失败在MacARM64 upload-artifact；该job已通过原生42grain/158capture和840SQL/23cases，唯一失败步骤为CreateArtifact接口五次request timeout。其余构建/format/tidy/project已成功，后续artifact/MCP被上游依赖跳过，CI gate严格失败。没有代码断言或编译失败证据，不用改代码或放宽门禁。原始Mac日志保存docs/research/ossie-req-001/ci-artifact-timeout/attempt-1-macos.log，已使用gh run rerun --failed重跑失败任务与依赖。记录retry-state；下一必要动作检查attempt2上传、后续验收及gate。之前65b9828全绿证据仍是该提交结论，不代称b2d486a已全绿；本轮状态/日志先本地保存，避免推送触发另一轮，不改设置、不合并或发布。

### 产物上传检修完成（2026-10-10）

run38027392941 attempt2已completed/success，验证远端b2d486a4bec4941531e8a8daf5aa82ac3bc7c2af；17个job全部success，MacARM64上传、两端Load built artifact、MCP round trip及CI gate均成功，独立验收和MCP证据artifacts存在未过期。状态、逐job结果及artifact元数据保存ci-artifact-timeout/final-state.json、attempt-2-result.json、attempt-2-artifacts.json；attempt1原始超时日志和排队状态仍保留。重跑成功支持此次为瞬时上传故障的判断，不推断整个GitHub服务故障；无需生产代码、workflow或规则修改。

本轮检修及最新tip远端验证完成，不自动启动P1/分页/Agent，不合并或发布。检修记录仅本地提交，避免纯状态归档再次触发整轮CI；远端已验证tip保持b2d486a。当前已授权工作包无剩余必需修复，后续能力工作需要确定范围。

### OSSIE-REQ-001：交付状态与真实 Agent 最小验收研究 checkpoint（2026-10-10）

用户授权先整理交付状态，再研究真实 Agent 最小验收用例。范围为修正计划/验收入口的当前状态，复用已有真实 population MCP、固定475行来源和独立 oracle，提出模型调用与通过标准；不实现 harness、不安装依赖、不调用模型或启动 P1/分页。验证为归档 CI 的提交/attempt/17项结果核对、官方协议与工具调用资料核对、方案只读反证及文档链接检查。沿本 issue 记录，原始来源/历史报告不改。现有远端 b2d486a 全绿；本地 e5db46f 为尚未推送的检修归档，不冒充该提交也跑过 CI。

### 交付整理与 Agent 最小验收研究完成（2026-10-10）

当前计划首页和验收入口已区分本地语义验收、跨环境证据与远端17项CI通过，历史报告不改。只读实时核对run38027392941 attempt2的b2d486a及17项success，与归档一致；baseline中3月10日两阈值记录数/SUM/AVG与方案一致；当前文档本地链接核对通过。

[最小 Agent 方案](agent-minimal-acceptance.md)建议三场景：原规则、规则变化同题、active/paid能力边界；Bun/TS显式循环，宿主提供真实发现资源，模型选择查询参数，独立oracle和人工完整transcript评审分层核对。参照MCP2024-11-05官方resources/tools及官方工具调用循环资料，不决定供应商或升级协议。一次只读Advisor反证后修正：边界场景允许不查询；独立问题补齐日期；固定prompt与动态资源哈希分开；要求解释实际阈值并把数值引用定位到执行响应。模型相同参数在不同规则下返回不同结果不等于模型改变策略。requested Astra/high；有效backend/effort/usage/billing unknown，Lead核对并采纳，未调用模型。

本轮研究包完成，仅本地提交；没有新增运行依赖或修改生产代码，无真实Agent结果。下一步尚需确定供应商/模型/API版本/预算及实施与运行范围，随后才实施最小harness。P1/分页/main合并/发布仍未启动。文档性改动只做链接、diff和来源/状态核对，不重跑已通过的C++功能门禁，不因纯状态文档再推送触发整轮CI。

### OSSIE-REQ-001：Agent 运行条件与发现预检 checkpoint（2026-10-10）

用户要求继续；先推进方案尚未确定的模型/API/预算与真实词汇预检。项目无指定模型配置，已向用户询问服务/模型选择，等待期间只运行本地真实MCP发现检查。范围为两阈值的initialize/tools/list/resources/read及来源哈希，保存一性探针和原始响应；不写Agent harness、不调用模型、不安装模型依赖或更改生产代码。核对初始化协议、工具schema、概念派生谓词、指标单位与逻辑字段，失败保留，不把预检当Agent验收。

### Agent 发现预检完成，模型选择待定（2026-10-10）

两阈值独立进程的initialize、三个tools与四resources/list/read实际通过，原始响应/一次性探针/哈希归[预检目录](research/ossie-req-001/agent-discovery-preflight/summary.json)。资源含正式谓词、USD来源和日期/产品逻辑字段，不含active/paid具名字段。实际发现独立进程snapshot_id相同而ontology哈希不同，故跨场景追溯以完整来源/定义哈希组合核对，不能只凭snapshot_id。检查仍限本地Mac产物发现链路，未查询业务值、未验证模型行为、未证明当前远端产物身份；serverInfo自报版本与duckdb_mcp二进制哈希验证分开。

候选system/user提示词另存并绑定哈希，无oracle/预制参数。当前研究/预检切片完成；用户模型选择问题仍待回答，未默认供应商或运行模型。剩余必要为服务/模型/API/预算及具体实施运行范围；可用现有三场景方案继续，不添加第二backlog或P1/分页。文档与证据链接/diff/hash检查后只本地提交保留。

### OSSIE-REQ-001：最小 Agent harness 实施 checkpoint（2026-10-10）

用户要求继续，并指定本地兼容服务、模型和本地凭据来源；具体配置已移至非提交的本地文件，公共接口现统一为OpenAI命名。本轮实施既定三场景的Bun/TS显式循环与本地控制，不改C++/MCP/资产。先完成无模型prepare、工具参数原样转发、预算/错误终止/首次结果保存及独立oracle/人工评审入口；真实网关调用和费用单独记录，不将配置可用当协议兼容或Agent通过。凭据只从环境读取，不保存请求认证头。验证采用可注入本地provider/RPC控制及真实MCP准备，mock不能证明自主模型。门禁与失败保留，原始prompt及历史证据不覆盖。

### 最小 Agent harness 本地完成，真实调用预算待明确（2026-10-10）

显式Chat Completions循环及三隔离场景入口已实现，不新增依赖、不修改生产C++/MCP/正式资产。模型构造器不接收oracle，参数原样转发，工具名单/必填字符串校验、预算停止、正常stop、不可覆盖输出和逐步失败归档就绪。结果只给局部数值门禁；口径/当前规则/单位/引用对应仍须人工审阅，不自动宣称整体通过。

一次只读Advisor发现URL先归档后校验、环境覆盖目标、总deadline不足、非stop final及汇总失败状态问题；Lead已修复并增加控制。requested Astra/high，有效backend/effort/usage/billing unknown，审阅与Lead验证归[审阅记录](research/ossie-req-001/agent-harness-controls/advisor-review.md)。首次fetch注入类型失败及修复保留；最终54Bun/184expects、typecheck/format/现有架构检查通过，三场景真实MCP prepare重跑通过；所有原始准备和最终结果分别保留，不覆盖。完整模型数据路径与真实网关兼容性尚未测，mock不是Agent自主证据。

本地实施包完成，只本地提交。真实运行待明确预算：最多24次模型请求、每请求max_completion_tokens2048（总请求输出限额49,152）、12工具/场景、120秒/场景、请求131,072bytes；未知网关单价不等于金额上限。程序要求显式--token-budget-only，尚未选择或调用；下一必要动作是用户明确是否接受这一预算方式，然后进行单次三场景真实运行并逐份人工审阅。P1/分页/main合并/发布不启动。

### OSSIE-REQ-001：用户指定 Responses endpoint 修正 checkpoint（2026-10-10）

用户补充实际endpoint为 `/v1/responses`，修正此前Chat Completions假设。本轮只切换模型协议、循环状态/证据与控制测试，保留既定三场景和模型/凭据/预算界限，不调用网关或默认接受待确认预算。官方Responses/function calling说明用于input/function_call/function_call_output和completed/incomplete处理参考；实际内网网关兼容性仍未验证。先明确单场景continuation隔离，保存真实wire请求和公开输出，不归档私有reasoning，不静默回退chat端点。验证本地协议控制与原MCP prepare，沿现issue维护。

### Responses 修正包完成（2026-10-10）

用户指定 `/v1/responses` 后，移除chat adapter并切换实际wire到Responses input/instructions、平铺函数schema和function_call_output。单场景store:true/previous_response_id续接，跨场景首轮无continuation；每轮重复指令和设置，禁truncation、批量调用先完整校验、call_id关联、completed/incomplete/refusal状态门禁与缺失/重复response ID拒绝。公开响应投影与精确请求body分开标记，不保存本地私有reasoning，不声称无服务端存储；传输bytes不是累积context/费用上限。

只读Advisor反证后Lead落实隔离/续接/记录约束；requested Astra/high，有效backend/effort/usage/billing unknown，见[审阅](research/ossie-req-001/agent-responses-controls/advisor-review.md)。迁移脚本中断后的旧chatProvider import失败保留；最终59Bun/238expects、typecheck/format通过，三场景真实MCP prepare通过，源码hash与[准备证据](research/ossie-req-001/agent-responses-controls/prepare/summary.json)绑定。原chat阶段证据不覆盖。

本地协议修正包完成，只本地提交；未请求内网模型服务，网关Responses及store/continuation兼容性未验证。下一必要仍是用户明确接受token/调用/时间预算，然后单次三场景真实运行和人工语义审阅。用户提供端点不等于已确认待决预算。P1/分页/main合并/发布未启动。

### OSSIE-REQ-001：开源配置边界修正 checkpoint（2026-10-10）

用户要求公共程序采用OPENAI_BASE_URL/OPENAI_MODEL/OPENAI_API_KEY，内部服务配置仅保存在不提交的.env_test。本轮去除硬编码内部服务与模型、专用Key变量和固定目标限制，提供通用示例并显式通过Bun --env-file读取本地测试配置。同步清理当前跟踪的说明/测试/准备报告中的内部配置；原始准备记录保持字节不变、移入忽略的本地证据目录，公开文件标为脱敏衍生物并更新文件校验清单。不重写Git历史，不推送或调用模型。验证通用HTTPS配置、缺失模型/标准Key、credential URL拒绝、忽略规则、已跟踪内容无内部配置以及现有Bun/真实MCP prepare。

### 开源配置修正包完成，历史清理待交付前处理（2026-10-10）

公共入口只读OPENAI_BASE_URL/OPENAI_MODEL/OPENAI_API_KEY；BASE_URL默认官方API根地址，可选择其他无凭据HTTPS兼容地址；MODEL须显式指定，Key仅--run必需。移除内部网关/模型常量及专用Key变量，不硬锁目标。用户明确要求的.env_test已创建为0600且Git忽略，显式bun --env-file=.env_test加载；公开.env_test.example只有通用占位配置。实际凭据不写入证据或stdout，模型仍未调用。

当前跟踪的说明和三份准备run.json中内部配置已脱敏；run.json显式标为公开衍生物、绑定原始SHA256，不再将占位值冒充实际服务。旧原始记录按字节保存到忽略的本地证据目录，更新公开校验清单；[脱敏关联](research/ossie-req-001/agent-public-config-controls/redaction.json)保留边界。原始输入/业务资产不改。59Bun/239expects、typecheck/format通过，加载本地测试文件的三场景真实MCP准备通过，公开仅保存不含配置的summary。

当前配置/工作树脱敏工作包完成，本地提交、不推送。此前未推送的Git历史仍包含旧网关/模型记录（没有Key值），未改写历史；推送前需要清理未推送历史并复核完整待推范围，不能只检查当前树。既定实际模型预算与三场景Agent验收仍待独立推进，P1/分页不启动。

### OSSIE-REQ-001：清理未推送历史 checkpoint（2026-10-10）

用户明确授权清理未推送提交中的内部配置，保留当前工作和本地原始证据。只读核实远端fork分支仍为b2d486a，远端b2d486a到本地07042f5的待推范围共6个线性提交，起始工作树干净。本轮先在忽略的私有目录保存可验证bundle和既有原始证据/配置文件哈希，再重建含内部配置的提交树，保留作者/时间/消息与各工作里程碑；不改变远端或已发布base，不推送。切换前须核对当前tip树完全相同、待推commit消息/trees及新增可达blob无内部配置或Key、base祖先不变、备份可验证、本地私有文件字节不变。最后原子切换本地分支并记录映射；不以当前树扫描替代历史扫描。

### 未推送历史清理完成（2026-10-10）

六个工作里程碑保留，前三个不含内部配置的提交ID不变；两个含配置的历史树及其后继重建，tip由07042f5变为3db90ee。清理前后tip tree均为3d1ed86bf636dde6afa307512d3c3857aeb84dc3，源码/现有工作/文件模式完全相同。公开脱敏历史文件的checksum更新；历史实际执行源码hash及commit来源标签保留，不能解释为新提交已重新运行测试。原始历史bundle放在忽略的0700私有目录，bundle为0600，已在独立临时bare仓库恢复出旧tip和相同tree并通过fsck --strict；既有6个私有文件SHA256一致，.env_test保持0600。

扫描6个完整提交快照，共3516个路径记录、582个唯一blob，并核对commit metadata及新增可达blob；内部端点/主机/模型/旧变量名/Key的直接和转义形式均未检出。没有其他Git ref包含被替换的旧提交。原对象按用户要求仍在私有备份及可能的reflog/unreachable objects中，不宣称清除本机所有旧对象。沿既有Advisor作一次有界风险审阅，Lead实际核实恢复、树/私有字节一致、扫描和ref结果；有效backend/effort/usage/billing未知。证据见[历史核验记录](research/ossie-req-001/agent-history-cleanup-controls/verification.json)。

本轮仅本地历史清理和记录归档，不推送、不改remote、不调用模型。文档归档提交后复核完整最终待推范围。当前工作包完成；整体真实Agent验收仍需预算决定、真实有界调用及人工语义裁定，P1/分页不启动。

### OSSIE-REQ-001：首次真实Agent运行授权 checkpoint（2026-10-10）

用户明确确认三场景预算：最多24次模型请求、36次工具调用，各场景120秒、每次请求输出2048tokens，合计请求输出上限49152tokens；网关金额未知，无货币硬上限。使用此前指定并已保密保存的.env_test标准OpenAI配置，只进行首次有界运行，不自动重试/切换模型/扩大预算。原始记录写入忽略的本地私有证据目录，保留失败；公开结论脱敏并绑定原始哈希。核对首次数值门禁、实际调用/usage/产物身份，再逐项人工口径/规则/引用评审，不以数值通过等同Agent通过。本次不推送、不改生产C++/MCP、不启动P1/分页。

### 首次真实运行停止：401及继承Key覆盖（2026-10-10）

首次实验以干净2d304bf源码启动，baseline真实MCP发现完成，第一次Responses请求返回HTTP401后立即停止；0场景完成、没有模型选择的工具调用，后两场景未开始。原始run/events/failure保留在忽略私有目录，公开[失败核验](research/ossie-req-001/agent-live-first-controls/summary.json)包含脱敏事实与原始文件SHA256。usage/金额未知，无Agent通过或数值/语义结论。适用openai-api-troubleshooting技能归类为认证失败，未发送诊断请求。

无网络检查发现启动环境Key与.env_test不一致，端点/模型一致；Bun保留继承Key优先于env-file。清除继承的三个标准变量后检查均与指定私有文件一致，不输出值、不写凭据。401不能证明指定私有Key无效；adapter只保留HTTP状态，没有详细错误体。没有改生产代码/凭据或重试。证据归档脚本首次误用model_request事件名触发断言、未产生报告，改用实际provider_request后核对1次请求。下一步需用户决定是否以正确配置启动新实验，同预算、原始失败不可覆盖。

### 正确配置的新实验授权 checkpoint（2026-10-10）

用户在明确询问同预算正确配置重跑后回复继续，授权一次新实验。启动清除继承OPENAI_API_KEY/OPENAI_BASE_URL/OPENAI_MODEL，以此前指定.env_test加载三项配置；相同24请求/36工具调用/每场景120秒/2048单次输出预算及未知金额，不改Key/模型/端点。新私有目录独立保存首次结果，不覆盖401证据，不自动再次重试、不推送。完成后核对数值/口径/引用，失败保留。

### 正确配置三场景运行完成，最终人工验收待确认（2026-10-10）

干净40390db启动，三场景completed；5次模型请求/2次自主population_query调用，耗时分别6121/5913/31759ms。阈值10返回37/1697.80/45.88648648648649，阈值20返回21/1277.54/60.8352380952381，数值门禁通过；active/paid场景指出无定义、ValidUser不能替代，零工具调用/无数值。网关报告input34287/output1288/total35575，cached input19250、reasoning output394，金额未知。无额外模型调用。

原始字节不覆盖；公开[审阅](research/ossie-req-001/agent-live-corrected-controls/review.md)与[证据](research/ossie-req-001/agent-live-corrected-controls/evidence.json)含8份原始/衍生文件SHA256，内部端点/模型脱敏，公开serialized_body不冒充原始字节。离线重放三场景数值门禁通过；核对原始serialized_body等于body、第二轮previous_response_id/call_id对应真实结果、首轮场景独立、每次输出2048与总时限通过。Lead阅读资源/模型回答/实际参数与结果，核对日期产品、无额外筛选、正式规则阈值变化、SUM/AVG/COUNT含义、USD、引用列对应一致；compile未调用，该项不适用。原始与公开human_verdict均pending，不能以Lead审查冒充人工确认。

首次401保留；显式.env_test命令文档补充清除继承三变量，生产代码/凭据不改。本次单服务/模型一次成功不证明重复可靠性或所有协议/语言符合性。工作包真实运行/脱敏/核验完成，整体最小Agent验收还需用户确认三份回答的口径与引用。下一必要事项为该人工确认，不扩预算重跑，不推送/P1/分页。

### 验收裁定归档及下一研究范围 checkpoint（2026-10-10）

用户要求Lead判断数据/口径后，明确接受结论并要求记录局限、继续下一步。将记录三个最小用例的限定通过及用户接受该裁定，原始human_verdict=pending与所有raw/脱敏实验文件保持字节不变，新增后续裁定绑定原始/公开证据hash，不冒充用户逐份审阅transcript。补记原CSV475行/ID唯一/非空、阈值重算、单位源及合成数据95行gaap_val>ytd_gaap限制。

比较现有后续事项：跨环境/remote CI/P0及最小Agent工作包已有证据；分页按原要求仅未来调研；混合粒度/复杂关系也未实现。下一步仅沿原P1条目研究同一实体跨日期观察的身份/计数/平均分母边界，复用固定spec和已有拒绝控制，不修改生产接口/规则保护，不新增模型调用/推送或第二backlog。因实体与观察模型存在架构取舍，复用既有Advisor一次只读反证；Lead核实和形成具体研究结论，实施范围需以该结论单独明确。

### 限定验收裁定与跨日期下一研究完成（2026-10-10）

用户接受Lead判断并要求归档，三个最小Agent用例限定通过，新增[后续裁定](research/ossie-req-001/agent-live-corrected-controls/acceptance-decision.json)绑定原始/公开记录SHA256；raw和现有8个公开衍生文件哈希逐一保持不变，不改历史human_verdict字段或冒充用户逐份读transcript。独立Decimal重算两阈值正确，475行/ID唯一/非空，源生成器/文档哈希及workbook原始E2=USD核对通过；CSV数字与归档生成器完全对应（9.5/9.50仅文本小数位差异）。临时来源核对首次读取部分manifest缺saved_path触发KeyError、随后按相对归档定位核对通过；直接文本比较小数位触发断言，改用数值相等而不改数据。

95行gaap_val>ytd_gaap，二者由上游演示生成器独立生成；这是不能验证累计YTD一致性的证据，不无条件判定会计错误。数据为虚构且无重复实体，阈值20为变更实验，active/paid定义不足，一次运行不证明全规范/重复可靠性。

比较现有P1/分页/交付事项后，沿原P1多期身份项完成[跨日期观察研究](research/ossie-req-001/multi-period-research/research.md)。8行新造研究数据区分实体/观察、两个日期跨月、两产品、阈值变化及NULL；不借用GAAP/YTD业务含义。手算/独立stock SQL通过，6合格观察/3用户/5非空金额、SUM31/AVG6.2及竞争分母区别明确。首次归档误把CLI多JSON数组输出当JSONL解析失败，原输出保留；连续解码后正确，不是生产失败。

固定spec/生产代码/现有重复预检控制核对，现profile仍拒绝重复身份，不能静默DISTINCT/过滤后验证/拼接User ID。复用Advisor一轮只读反证，requested Astra/high、effective backend/effort/usage/billing未知；建议Observation表示仅为研究候选。研究包完成，下一必要动作是明确日快照唯一tuple及观察分类/期间用户投影/第一切片统计契约，再决定生产实现；P1生产、窗口/as-of/分页均未实现。本轮本地记录不推送、不重跑模型。

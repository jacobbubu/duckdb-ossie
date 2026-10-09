# OSSIE-REQ-001 本体执行基线研究

日期：2026-10-09。状态：首轮研究、core loader/元数据与单表人口执行工作包已完成并本地验证；P0 尚未完成。

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
- 独立安装的 DuckDB wheel 1.5.6 加载实际 `.duckdb_extension` 成功；17 类拒绝输入后保持原模型；核对参考模型 46 个元数据对象及全部 AI/vendor 内容；2,500 个字段跨输出批次无丢失/重复。产物、验证脚本、源码和输入 SHA-256 见 [loader-artifact-results.json](research/ossie-req-001/loader-artifact-results.json)。脚本 [verify_loader_artifact.py](../scripts/verify_loader_artifact.py) 可以复跑；它不验证 ontology 执行。
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

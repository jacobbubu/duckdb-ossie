# R2 Core 字段对应与偏离审阅

审阅基准为本地 `1312b23` 后的名称/未知属性修复、固定 Apache Ossie `8dd6732da354f22ca71f16d82626a46039ecdcd9` 的 core schema；仅支持 `0.2.0.dev0` 新根级模型。此表核对全部 50 个 properties 声明（root 引用与 SemanticModel 定义分别计数），不以字段数量证明行为正确，也不声明完整 JSON Schema 符合性。ontology/mapping 执行范围另见 [population profile](population-profile.md)，不是此 core 表的覆盖承诺。

字段支持与 schema 校验分别列出。已验证的正常路径复用 [数组/metadata 控制](research/ossie-req-001/r2-fields-loader-artifact-results.json)、[发现](research/ossie-req-001/r2-fields-discovery-artifact-results.json) 和 [表达式](research/ossie-req-001/r2-fields-expression-artifact-results.json)。当前 [真实字段控制](research/ossie-req-001/r2-fields-correspondence-artifact-results.json) 检查严格类型、保真和剩余校验偏离；它记录实现事实，不能当作批准偏离的验收。

| Schema scope.field | 内部对应 | 发现或执行用途 | 校验/转换与边界 |
|---|---|---|---|
| `root.version` | ParseModel → spec_version | ossie_load/发现版本 | 严格完整字符串 0.2.0.dev0；project API2.0.0 独立 |
| `root.name` | RequiredNonEmptyString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 仅实际非空 string，minLength=1；合法字符串按长度保留 |
| `root.description` | OptionalString → description | metadata.description；不参与公式 | 仅实际 string；缺省为空；显式 NULL/数字/Boolean/容器拒绝；NUL 按长度保留 |
| `root.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 按长度保留 |
| `root.datasets` | ParseDataset → Model.datasets/dataset_index | ossie_datasets + metadata；root 至少一个 | 数组/重复名检查；不在加载 core 时验证全部真实来源数据 |
| `root.relationships` | ParseRelationship → Model.relationships | ossie_relationships + metadata | 数组、端点、列宽/非空、重名检查；复杂连接另受执行器限制 |
| `root.metrics` | ParseModelExpression/ParseDatatype → Model.metrics | ossie_metrics + metadata | 数组/重复名/表达式数据集检查；加载不证明所有请求可执行 |
| `root.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |
| `CustomExtension.vendor_name` | 不单独解析；在 JSONText 中保留 | metadata.custom_extensions | schema string/required 未校验；不执行 vendor 行为 |
| `CustomExtension.data` | 不单独解析；在 JSONText 中保留 | metadata.custom_extensions | schema JSON 文本 string/required 未校验；消费者单独 parse，不自动换算单位 |
| `DialectExpression.dialect` | RequiredString + executable dialect selection | metadata.available_dialects + expression_dialect | 大小写不敏感接受扩展了 exact enum；非执行 variant 可保留；没有支持 variant 拒绝 |
| `DialectExpression.expression` | RequiredString → SQL parse → AST | fields/metrics.expression；compile/plan | 仅实际 string；含 NUL 明确拒绝（防 parser 前缀解析）；单表达式/作用域校验 |
| `Expression.dialects` | ParseModelExpression → variants/available_dialects | metadata.available_dialects | 数组必须包含可执行 variant；选择确定；variant/expression 未知属性拒绝；全部 dialect enum 另待收口 |
| `Dimension.is_time` | ParseField → is_time/is_time_explicit | ossie_fields.is_time + metadata.is_time_explicit | 严格 Boolean；未设时按四种 temporal datatype 默认；未知 dimension 属性拒绝 |
| `Field.name` | RequiredNonEmptyString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 仅实际非空 string，minLength=1；合法字符串按长度保留 |
| `Field.expression` | ParseModelExpression → ModelExpression/AST | ossie_fields/ossie_metrics.expression + metadata dialects | 选择 OSSIE_SQL_2026 优先于 ANSI_SQL；旧 fields/metrics 出口保留选中原文，compile/plan 使用 lowered AST；population 只执行公布子集 |
| `Field.dimension` | ParseField → is_time/is_time_explicit | fields/metadata | 必须 object；按 datatype 默认，可显式 opt-out；未知属性拒绝 |
| `Field.label` | OptionalString → Field.label | metadata.label | 空串/缺省展示 SQL NULL；显式 NULL/非字符串拒绝；NUL 按长度保留 |
| `Field.description` | OptionalString → description | metadata.description；不参与公式 | 仅实际 string；缺省为空；显式 NULL/数字/Boolean/容器拒绝；NUL 按长度保留 |
| `Field.datatype` | ParseDatatype → datatype | fields/metrics + population discovery | 仅十个 exact 逻辑类型；缺省未知；Time/DateTime metadata 不代表 population 执行支持 |
| `Field.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 按长度保留 |
| `Field.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |
| `Dataset.name` | RequiredNonEmptyString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 仅实际非空 string，minLength=1；合法字符串按长度保留 |
| `Dataset.source` | RequiredNonEmptyString + ValidateSourceShape + RebindMap | ossie_datasets.source/source_bound/resolved | 仅表来源路径；拒绝查询形状；resolved 在发现时实际查 catalog，不是 data freeze |
| `Dataset.primary_key` | StringArray → primary_key | ossie_datasets.primary_key；graph 基数推导 | 严格 string 成员；不要求声明为 fields；core 不验证实际唯一，population 整批预检另行执行 |
| `Dataset.unique_keys` | ParseUniqueKeys → unique_keys | ossie_datasets.unique_keys；graph 基数推导 | 严格 nested string arrays；empty inner key 拒绝（schema 允许）；NUL 列名完整保留 |
| `Dataset.description` | OptionalString → description | metadata.description；不参与公式 | 仅实际 string；缺省为空；显式 NULL/数字/Boolean/容器拒绝；NUL 按长度保留 |
| `Dataset.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 按长度保留 |
| `Dataset.fields` | ParseField → fields/field_index + ExpandFieldReferences | ossie_fields/metadata；表达式展开 | 缺省/空合法；数组/重名/循环/作用域检查；物理列可不声明，实际 binder 解析 |
| `Dataset.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |
| `Relationship.name` | RequiredNonEmptyString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 仅实际非空 string，minLength=1；合法字符串按长度保留 |
| `Relationship.from` | RequiredNonEmptyString → from_dataset + ValidateRelationship | ossie_relationships.from_dataset | 端点必须存在；不是强制事实/维度数据策略；端点为实际字符串，非字符串拒绝 |
| `Relationship.to` | RequiredNonEmptyString → to_dataset + ValidateRelationship | ossie_relationships.to_dataset | 端点必须存在；键声明不证明实际唯一，core 不替代 population 预检 |
| `Relationship.from_columns` | StringArray → from_columns | ossie_relationships.from_columns | 严格 string、非空、宽度匹配；不要求列声明为 field；实际 column binder 检查 |
| `Relationship.to_columns` | StringArray → to_columns | ossie_relationships.to_columns | 严格 string、非空、宽度匹配；键匹配用于声明基数推导，非真实数据证明 |
| `Relationship.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 按长度保留 |
| `Relationship.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |
| `Metric.name` | RequiredNonEmptyString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 仅实际非空 string，minLength=1；合法字符串按长度保留 |
| `Metric.expression` | ParseModelExpression → ModelExpression/AST | ossie_fields/ossie_metrics.expression + metadata dialects | 选择 OSSIE_SQL_2026 优先于 ANSI_SQL；旧 fields/metrics 出口保留选中原文，compile/plan 使用 lowered AST；population 只执行公布子集 |
| `Metric.description` | OptionalString → description | metadata.description；不参与公式 | 仅实际 string；缺省为空；显式 NULL/数字/Boolean/容器拒绝；NUL 按长度保留 |
| `Metric.datatype` | ParseDatatype → datatype | fields/metrics + population discovery | 仅十个 exact 逻辑类型；缺省未知；Time/DateTime metadata 不代表 population 执行支持 |
| `Metric.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 按长度保留 |
| `Metric.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |
| `SemanticModel.name` | RequiredNonEmptyString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 仅实际非空 string，minLength=1；合法字符串按长度保留 |
| `SemanticModel.description` | OptionalString → description | metadata.description；不参与公式 | 仅实际 string；缺省为空；显式 NULL/数字/Boolean/容器拒绝；NUL 按长度保留 |
| `SemanticModel.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 按长度保留 |
| `SemanticModel.datasets` | ParseDataset → Model.datasets/dataset_index | ossie_datasets + metadata；root 至少一个 | 数组/重复名检查；不在加载 core 时验证全部真实来源数据 |
| `SemanticModel.relationships` | ParseRelationship → Model.relationships | ossie_relationships + metadata | 数组、端点、列宽/非空、重名检查；复杂连接另受执行器限制 |
| `SemanticModel.metrics` | ParseModelExpression/ParseDatatype → Model.metrics | ossie_metrics + metadata | 数组/重复名/表达式数据集检查；加载不证明所有请求可执行 |
| `SemanticModel.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |

没有 properties 的定义也已核对：`Dialect` 的支持子集及选择在表达式表中说明；`DataType` 的十项 exact enum 由 ParseDatatype 检查；`Vendor` 是任意字符串，当前仅保留 JSON，未独立校验。`SemanticModel` 是 root properties 的引用定义，不能据此重新接受旧 semantic_model wrapper。

## 结论和必要剩余

合法目标结构已经有明确映射：根级单模型、datatype/时间默认、AI 两种形态、字符串数组、说明/标签及描述性扩展、方言选择与 AST lowering、键/关系和发现出口均可定位。声明 cardinality 来自 graph 对声明键的比较；只有 population profile 的数据捕获预检才检查真实唯一/非空/匹配，两个结论不能混用。quoted identifiers 遵循 DuckDB ASCII case-insensitive 规则，偏离 expression 规范的 case-sensitive quoted 名称；此既有执行限制见 [limitations](limitations.md)。

校验偏离仍保留：custom_extensions 只保存 JSON、未验成员结构/未知属性；dialect case-insensitive 接受和未统一验证全部 enum；空 unique key 比 schema 更严格。schema 允许的额外 AI 属性完整保存，属于 additionalProperties=true，不作为未知 core 属性拒绝。extension/dialect 偏离尚未得到用户作为兼容例外的接受，不能称“严格全部 schema”。

字符串保真缺陷已修复：RequiredString/OptionalString、AI string instructions 和 unique_keys 使用 yyjson 的实际长度读取；五个 AI scopes 的 string/object 原始 JSON 与展开 instructions 相符，description/label、复合键也有实际控制。metadata 保留 NUL 不授权 SQL 解析：所有 model expression variants 含 NUL 时在 AST parse 前拒绝，防止 DuckDB 只执行公式前缀；失败保持旧模型。population profile 仍按既有 NoNUL 拒绝整份定义，未放宽。

本包完成逐字段审阅与剩余问题定位，R2.5 的对应表缺口已补；R2/P0 的最终验收仍待其余校验偏离的收紧或明确处理，再进行 A1–A8 规则驱动归档。原始规范未改；保真修复重建并执行同产物 native/SQL/stock/MCP 回归，证据见 [保真控制记录](research/ossie-req-001/r2-fields-control-results.json)。分页只作未来需求，远程 CI/Ubuntu tidy 未运行，当前只本地提交。

单值字符串类型已收紧：required 字符串只接受 string，缺省/NULL required 拒绝；optional 只在缺省时保留空值，显式 NULL/数字/Boolean/容器均拒绝。154 个控制中的 120 个具名拒绝覆盖十个 required 使用位置、五个 description/label 位置及五个 AI instructions 位置，每次完整旧 metadata 保持；YAML 实际类型拒绝和 quoted scalar text 正例也有实际证据。extension 成员结构及 dialect enum/大小写偏离仍存在，未自动接受为例外。

名称和未知属性已收紧：五类 name、dataset source、relationship from/to 按 schema minLength=1 拒绝空串；不 trim、不发明 whitespace 或可选 description/label 非空约束。root/dataset/field/metric/relationship/dimension/expression/variant 的已知属性按完整长度、区分大小写匹配，未知键（含 NUL 后缀）明确拒绝，诊断以 JSON 文本保留键名。legacy wrapper、版本和混合 ontology/mapping 的特定诊断优先保持。193 个字段控制补充八个空值位置、八种 scope 的三类未知 key、YAML 未知 root 属性与五种 AI extra-property 保留；全部失败后旧 metadata 保持。custom_extensions 成员将独立收口；AIContext 的 additionalProperties=true 边界不收窄。

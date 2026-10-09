# R2 Core 字段对应与偏离审阅

审阅基准为本地 `c25d118`、固定 Apache Ossie `8dd6732da354f22ca71f16d82626a46039ecdcd9` 的 core schema；仅支持 `0.2.0.dev0` 新根级模型。此表核对全部 50 个 properties 声明（root 引用与 SemanticModel 定义分别计数），不以字段数量证明行为正确，也不声明完整 JSON Schema 符合性。ontology/mapping 执行范围另见 [population profile](population-profile.md)，不是此 core 表的覆盖承诺。

字段支持与 schema 校验分别列出。已验证的正常路径复用 [数组/metadata 控制](research/ossie-req-001/r2-strings-loader-artifact-results.json)、[发现](research/ossie-req-001/r2-strings-discovery-artifact-results.json) 和 [表达式](research/ossie-req-001/r2-strings-expression-artifact-results.json)。新增 [真实 characterization](research/ossie-req-001/r2-correspondence-artifact-results.json) 检查剩余转换/截短/容错；它记录实现事实，不能当作批准偏离的验收。

| Schema scope.field | 内部对应 | 发现或执行用途 | 校验/转换与边界 |
|---|---|---|---|
| `root.version` | ParseModel → spec_version | ossie_load/发现版本 | 严格完整字符串 0.2.0.dev0；project API2.0.0 独立 |
| `root.name` | RequiredString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 数字/Boolean 转文本；未统一检查 minLength；NUL 会截短 |
| `root.description` | OptionalString → description | metadata.description；不参与公式 | 数字/Boolean 转文本；NULL/容器视为空；NUL 截短 |
| `root.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 截短 |
| `root.datasets` | ParseDataset → Model.datasets/dataset_index | ossie_datasets + metadata；root 至少一个 | 数组/重复名检查；不在加载 core 时验证全部真实来源数据 |
| `root.relationships` | ParseRelationship → Model.relationships | ossie_relationships + metadata | 数组、端点、列宽/非空、重名检查；复杂连接另受执行器限制 |
| `root.metrics` | ParseModelExpression/ParseDatatype → Model.metrics | ossie_metrics + metadata | 数组/重复名/表达式数据集检查；加载不证明所有请求可执行 |
| `root.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |
| `CustomExtension.vendor_name` | 不单独解析；在 JSONText 中保留 | metadata.custom_extensions | schema string/required 未校验；不执行 vendor 行为 |
| `CustomExtension.data` | 不单独解析；在 JSONText 中保留 | metadata.custom_extensions | schema JSON 文本 string/required 未校验；消费者单独 parse，不自动换算单位 |
| `DialectExpression.dialect` | RequiredString + executable dialect selection | metadata.available_dialects + expression_dialect | 大小写不敏感接受扩展了 exact enum；非执行 variant 可保留；没有支持 variant 拒绝 |
| `DialectExpression.expression` | RequiredString → SQL parse → AST | fields/metrics.expression；compile/plan | 数字/Boolean 转文本偏离 schema；NUL 截短；单表达式/作用域校验 |
| `Expression.dialects` | ParseModelExpression → variants/available_dialects | metadata.available_dialects | 数组必须包含可执行 variant；选择确定；不验证全部 schema enum/未知属性 |
| `Dimension.is_time` | ParseField → is_time/is_time_explicit | ossie_fields.is_time + metadata.is_time_explicit | 严格 Boolean；未设时按四种 temporal datatype 默认；未知 dimension 属性未拒绝 |
| `Field.name` | RequiredString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 数字/Boolean 转文本；未统一检查 minLength；NUL 会截短 |
| `Field.expression` | ParseModelExpression → ModelExpression/AST | ossie_fields/ossie_metrics.expression + metadata dialects | 选择 OSSIE_SQL_2026 优先于 ANSI_SQL；旧 fields/metrics 出口保留选中原文，compile/plan 使用 lowered AST；population 只执行公布子集 |
| `Field.dimension` | ParseField → is_time/is_time_explicit | fields/metadata | 必须 object；按 datatype 默认，可显式 opt-out；未保留未知属性 |
| `Field.label` | OptionalString → Field.label | metadata.label | 空串/缺省/NULL/容器展示 SQL NULL；数字/Boolean 转文本；NUL 截短 |
| `Field.description` | OptionalString → description | metadata.description；不参与公式 | 数字/Boolean 转文本；NULL/容器视为空；NUL 截短 |
| `Field.datatype` | ParseDatatype → datatype | fields/metrics + population discovery | 仅十个 exact 逻辑类型；缺省未知；Time/DateTime metadata 不代表 population 执行支持 |
| `Field.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 截短 |
| `Field.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |
| `Dataset.name` | RequiredString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 数字/Boolean 转文本；未统一检查 minLength；NUL 会截短 |
| `Dataset.source` | RequiredString + ValidateSourceShape + RebindMap | ossie_datasets.source/source_bound/resolved | 仅表来源路径；拒绝查询形状；resolved 在发现时实际查 catalog，不是 data freeze |
| `Dataset.primary_key` | StringArray → primary_key | ossie_datasets.primary_key；graph 基数推导 | 严格 string 成员；不要求声明为 fields；core 不验证实际唯一，population 整批预检另行执行 |
| `Dataset.unique_keys` | ParseUniqueKeys → unique_keys | ossie_datasets.unique_keys；graph 基数推导 | 严格 nested string arrays；empty inner key 拒绝（schema 允许）；NUL 内部列名截短 |
| `Dataset.description` | OptionalString → description | metadata.description；不参与公式 | 数字/Boolean 转文本；NULL/容器视为空；NUL 截短 |
| `Dataset.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 截短 |
| `Dataset.fields` | ParseField → fields/field_index + ExpandFieldReferences | ossie_fields/metadata；表达式展开 | 缺省/空合法；数组/重名/循环/作用域检查；物理列可不声明，实际 binder 解析 |
| `Dataset.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |
| `Relationship.name` | RequiredString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 数字/Boolean 转文本；未统一检查 minLength；NUL 会截短 |
| `Relationship.from` | RequiredString → from_dataset + ValidateRelationship | ossie_relationships.from_dataset | 端点必须存在；不是强制事实/维度数据策略；单值 scalar 容错仍在 |
| `Relationship.to` | RequiredString → to_dataset + ValidateRelationship | ossie_relationships.to_dataset | 端点必须存在；键声明不证明实际唯一，core 不替代 population 预检 |
| `Relationship.from_columns` | StringArray → from_columns | ossie_relationships.from_columns | 严格 string、非空、宽度匹配；不要求列声明为 field；实际 column binder 检查 |
| `Relationship.to_columns` | StringArray → to_columns | ossie_relationships.to_columns | 严格 string、非空、宽度匹配；键匹配用于声明基数推导，非真实数据证明 |
| `Relationship.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 截短 |
| `Relationship.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |
| `Metric.name` | RequiredString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 数字/Boolean 转文本；未统一检查 minLength；NUL 会截短 |
| `Metric.expression` | ParseModelExpression → ModelExpression/AST | ossie_fields/ossie_metrics.expression + metadata dialects | 选择 OSSIE_SQL_2026 优先于 ANSI_SQL；旧 fields/metrics 出口保留选中原文，compile/plan 使用 lowered AST；population 只执行公布子集 |
| `Metric.description` | OptionalString → description | metadata.description；不参与公式 | 数字/Boolean 转文本；NULL/容器视为空；NUL 截短 |
| `Metric.datatype` | ParseDatatype → datatype | fields/metrics + population discovery | 仅十个 exact 逻辑类型；缺省未知；Time/DateTime metadata 不代表 population 执行支持 |
| `Metric.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 截短 |
| `Metric.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |
| `SemanticModel.name` | RequiredString → 各对象 name；索引按 DuckDB ASCII case-insensitive | metadata.name；重复名称拒绝 | 数字/Boolean 转文本；未统一检查 minLength；NUL 会截短 |
| `SemanticModel.description` | OptionalString → description | metadata.description；不参与公式 | 数字/Boolean 转文本；NULL/容器视为空；NUL 截短 |
| `SemanticModel.ai_context` | ParseAIContext → AIContext | metadata.ai_context 原 JSON + synonyms/examples/instructions | string/object 接受，其余拒绝；额外 AI 属性原样保留且不执行；单值 instructions NUL 截短 |
| `SemanticModel.datasets` | ParseDataset → Model.datasets/dataset_index | ossie_datasets + metadata；root 至少一个 | 数组/重复名检查；不在加载 core 时验证全部真实来源数据 |
| `SemanticModel.relationships` | ParseRelationship → Model.relationships | ossie_relationships + metadata | 数组、端点、列宽/非空、重名检查；复杂连接另受执行器限制 |
| `SemanticModel.metrics` | ParseModelExpression/ParseDatatype → Model.metrics | ossie_metrics + metadata | 数组/重复名/表达式数据集检查；加载不证明所有请求可执行 |
| `SemanticModel.custom_extensions` | JSONText → custom_extensions_json | metadata.custom_extensions 原 JSON | 描述保留，不执行/推断单位；未验证 array、vendor_name/data 的类型/required/additionalProperties |

没有 properties 的定义也已核对：`Dialect` 的支持子集及选择在表达式表中说明；`DataType` 的十项 exact enum 由 ParseDatatype 检查；`Vendor` 是任意字符串，当前仅保留 JSON，未独立校验。`SemanticModel` 是 root properties 的引用定义，不能据此重新接受旧 semantic_model wrapper。

## 结论和必要剩余

合法目标结构已经有明确映射：根级单模型、datatype/时间默认、AI 两种形态、字符串数组、说明/标签及描述性扩展、方言选择与 AST lowering、键/关系和发现出口均可定位。声明 cardinality 来自 graph 对声明键的比较；只有 population profile 的数据捕获预检才检查真实唯一/非空/匹配，两个结论不能混用。quoted identifiers 遵循 DuckDB ASCII case-insensitive 规则，偏离 expression 规范的 case-sensitive quoted 名称；此既有执行限制见 [limitations](limitations.md)。

校验偏离仍保留：单值字符串数字/Boolean 容错及 NULL/容器缺省化；名称 minLength 未统一执行；additionalProperties=false 未普遍执行，未知 core 字段通常丢弃；custom_extensions 只保存 JSON、未验成员结构；dialect case-insensitive 接受；空 unique key 比 schema 更严格。schema 允许的额外 AI 属性完整保存，不属于 unknown core field 丢弃。以上不能统一包装成“支持全部字段”或“严格 schema”。这些行为本包未修改，尚未获得用户作为兼容例外的接受。

真实控制还确认了合法字符串的保真缺陷：AI 字符串/对象 instructions 的 raw JSON 保留 NUL 后内容，flattened instructions 截短；unique_keys 列名也截短。与未知字段宽容不同，这直接损失合法内容，应在最终 R2 验收前修复并验证 raw/flattened/list 相符。description/label/RequiredString 同用非 length-aware helper，源码可定位潜在同类问题，本包不声称已逐一真实观察所有字段。

本包完成逐字段审阅与剩余问题定位，R2.5 的对应表缺口已补；R2/P0 的最终验收仍待合法字符串保真修复，以及其余校验偏离的收紧或明确接受，再进行 A1–A8 规则驱动归档。未改生产代码/原始规范，未重建或重跑已通过同产物门禁；新增 characterization/工具质量检查独立记录。分页只作未来需求，远程 CI/Ubuntu tidy 未运行，当前只本地提交。

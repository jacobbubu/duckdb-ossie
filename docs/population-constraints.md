# 显式概念 requires：有限可复用子集

`concept-scalar-requires-v1` 为单表与双源 profile 增加有界的 Integer/DATE 概念约束。
它沿用既有 profile，不新增 profile；支持的类型范围与边界语义见
[边界说明](concept-requires-boundaries.md)。

## 选择能力和兼容边界

在单表 `gaap-scalar-population-v1` 或双源 `fact-dimension-population-v1`
的 manifest 中显式增加：

```json
"capabilities": ["concept-scalar-requires-v1"]
```

这是项目 manifest 的能力选择，不是上游 Ontology/Mapping/Core 的新字段。
空、重复、未知或错误类型的选择拒绝；另外四个 profile 不接受此字段。
不带选择项的旧 manifest 保留原先的非空 requires 拒绝与 API 2.2.0；
只有启用能力的批次声明 API 2.3.0。这里的版本描述当前绑定批次的有效接口合同，
不表示其他旧 profile、客户端或现有二进制已经升级。旧客户端无需随之修改。

只有 EntityType 根概念与原有直接派生子概念的 requires 放宽；ontology 级、
关系级、自定义 ValueType、多层继承和任意关系图仍不在此能力内。

## 支持的语义

每一项只接受同一 owner、同一映射属性的两个子式：

```text
C.attr IS NULL OR C.attr >= k
C.attr IS NOT NULL AND C.attr >= k
```

guard 与比较可交换顺序。Integer 属性只接受 TINYINT、SMALLINT、INTEGER、BIGINT
四种有符号物理来源；Date 属性接受 DATE。属性必须直接映射到这些实际物理列；
双源映射沿原字段 provenance 绑定，不按字段名、模型名或业务场景选择算法。
整数常量必须非 NULL、无损适合实际整数类型；DATE 常量必须有显式 DATE 类型。
不接受计算列表达式、隐式字符串转换、两列比较、其他比较运算符、函数、聚合、窗口或子查询。
DATE 保持既有 DuckDB 1.5.6 类型/解析行为，包括其特殊值边界；没有新增严格 ISO、
有限性、timestamp、时区政策或 JS Date 转换。
文本解析范围与物理存储范围不同，见[DATE 边界说明](concept-requires-boundaries.md#date-storage-boundary)。

所有声明先进行 AST/实际类型预检，即使人口为空也不能跳过坏表达式。
根约束覆盖实际形成的全部根对象；子约束只作用于 derived_by 为 TRUE 的对象。
查询过滤不能缩小这个加载校验域。现有完整来源身份、唯一性和 FK 检查仍先执行。
任一约束不是 TRUE 就使整个新加载失败，不删除对象、不改分母、不发布部分合格快照。
合法约束在空人口上按全称条件成立。

例如，单表设备根可允许 capacity=NULL，但活跃子概念可显式要求 capacity 非 NULL 且至少 1。
另一份双源货品模型从批次维度映射 expiry；两个货品引用同一批次仍是两个对象。
无货品引用的旧日期维度不产生货品约束违约，但即使未引用，维度键重复仍被原身份检查拒绝。
这区分了来源行和概念人口，不是更换业务名称的两个同形样例。

## 发现、错误和生命周期

启用批次在既有 `capabilities_json` 增加自版本的 `concept_constraints` 对象：
列出 owner、零基 requires 索引、原表达式、属性/原逻辑字段、真实类型、NULL 政策、
阈值和完整根/实际子人口范围。`load_constraints_validated=true` 只表示成功加载时的检查，
`query_executed=false` 不否认加载阶段运行过校验。

SQL 返回列及 plan-v1 不变；compile 仍是诊断 AST，带 manifest/定义/快照追溯，
不重执行约束，也不能把其文本当可重放 SQL。约束不是成员规则或指标强制筛选政策。
违约复用 error-v1 的 `CONSTRAINT_VIOLATION`、`stage=constraint` 和 owner/索引；
语法、类型、能力与资源错误保留各自分类。取消保持原生 INTERRUPT。
失败或取消不替换旧快照；旧 prepared 请求保留其原来的数据和能力选择。

每概念最多 8 项，两概念共最多 16 项；每式 16 KiB/1024 AST 节点/深度 32，
总输入 128 KiB，继承原捕获行数、物理列数与 canonical 字节限制。
这些不是峰值内存、吞吐或硬执行期限保证。

## 显式字符串关联键

参见 [native-string-join-v1](string-join.md)：仅扩展现有两表单键 ManyToOne
INNER/missing=reject 路径，新增 capability 的批次使用 API 2.4.0，可独立组合概念 requires。
旧默认 INTEGER-only/API 2.2.0、仅 requires/API 2.3.0 及原断言保持。
字符串按完整 UTF-8 字节匹配，禁止隐式转换与正规化；NULL、未引用维度唯一性及
过滤前全批次 FK 检查不放宽。

## 多个直接派生子概念（显式选择）

[direct-derived-concepts-v1](direct-derived.md) 为现有普通单表/双源路径增加有界同根直接子集合，
共享身份与 Mapping、允许重叠，不扩多层/多继承或兄弟推理。新选择使用 API2.5/plan-v3；
默认、requires 与 String 的原选择行为保持。

# 显式概念 requires：有限可复用子集

已完成固定 Mac ARM64 / DuckDB 1.5.6 的限定原生验收，包含两种结构不同的场景、
类型与拒绝边界及既有回归。工作沿用 OSSIE-REQ-001，不新增 profile。
执行版本与结果见原生验收摘要，
类型范围与统计口径见[边界验收](concept-requires-boundaries.md#accepted-boundaries-007)。
本次仅同步说明，没有追加运行；这些结果不构成新能力的 CLI/MCP 或完整 Ontology 验收。

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
有限性、timestamp、时区政策或 JS Date 转换。基础 A/B 的离线 oracle 覆盖普通日期；
独立边界合同另以 BigInt 日序核对 BC、宽年份、解析极值与特殊值，已有实际原生结果。
文本解析范围与物理存储范围分别记录，见[DATE 边界说明](concept-requires-boundaries.md#date-storage-boundary)。

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

## 验证入口与已有验收

只使用已经安装的 Node.js 24.12+ 和 npm 依赖，不联网或安装：

```sh
node scripts/verify_concept_requires_artifact.ts --offline
node --test --test-concurrency=1 test/tooling/concept_requires.test.ts
```

两套输入分别位于 `test/fixtures/concept-requires-single/` 和
`test/fixtures/concept-requires-joined/`。manifest 绑定三个标准文档的精确字节；
这些 fixture 使用固定 JSON 序列化，离线入口另校验 schema、声明对应与普通日期/整数答案。
单表完整根/子身份为 d1,d2,d3 / d1；双源为 s1,s2,s3,s4 / s1,s2。
保留完整 NULL/整数/日期分组及手写 SQL 对照；COUNT 不替代身份核对。

下列是独立 A/B 入口的命令形状，本次文档更新不执行。新的运行须核对产物、SHA、
源码/依赖、预算和全新目录，并取得对应运行授权；可在已授权的连续 Goal 内推进。
已有 007 结果由当时冻结来源绑定，不能仅凭源码或本文更新推导其他产物具备新能力。
包含构建、边界与旧回归的完整入口见原生监督说明。

```sh
node scripts/verify_concept_requires_artifact.ts \
  --artifact "$ARTIFACT" --sha256 "$ARTIFACT_SHA256" --output "$NEW_EVIDENCE_DIRECTORY"
```

当前实际入口初限 Mac ARM64、Node.js 24.12+、DuckDB 1.5.6/069cc9f9b5、Node API/Bindings
1.5.6-r.1；AMD64 后置。只加载固定文件，拒绝预载 ossie，关闭扩展自动安装/加载；
新目录必须是仓库 `.local-agent-evidence/` 的直接子目录。单 worker 最多 200 条 SQL、
60 秒执行时间，随后最多 1 秒等待 close；零重试。stdout/stderr 合计最多 1 MiB、摘要最多 1 MiB，
超限失败，清理未知不报告成功。这不是完整入口 60 秒 SLO。

外层保留输入 pin、stdout/stderr、结果/失败收据及哈希；每项 SQL 的响应/错误保留在 worker JSON，
超时强停只能保留此前输出字节。受控负控在新子目录写明确的新定义和新 SHA，
不覆盖原 fixture，不修改失败查询去寻求通过。文件 pin 和加载状态不证明内存字节、
全部动态依赖或源码确实编译成该产物；这些字段明确标记未测/未证明。

007 原生验收已通过 A/B、capture 的 554 项检查、31 个 SQL 文件的 1122 项断言及旧
20 阶段，包含发布前取消、违约保留、prepared 跨重载与来源删除保护。类型/拒绝边界
及另行封存的来源越界检查也已通过；具体数量和版本见上述摘要，不称全部旧 profile
或全部类型语法均已验证。本次未运行这些入口，也未改默认 quality-local 或 CI。

## 旧证据与公共交付

功能实施前已保全旧源码和新旧哈希对应。旧 overlay-v1 已绑定相关旧源码；
它不应对新输入通过，不能重签、改旧报告或放宽字节检查。
新功能的离线/原生执行证据与新公共目标树分别交付；当前没有完成后者、M4–M5 或发布。
原 P0/P1、旧版本 CI、旧产物验收继续只证明各自版本。

## 显式字符串关联键

参见 [native-string-join-v1](string-join.md)：仅扩展现有两表单键 ManyToOne
INNER/missing=reject 路径，新增 capability 的批次使用 API 2.4.0，可独立组合概念 requires。
旧默认 INTEGER-only/API 2.2.0、仅 requires/API 2.3.0 及原断言保持。
字符串按完整 UTF-8 字节匹配，禁止隐式转换与正规化；NULL、未引用维度唯一性及
过滤前全批次 FK 检查不放宽。该新能力已通过独立封存的 Mac ARM64 限定原生验收，范围与产物绑定见上述说明；旧验收不覆盖它。

## 多个直接派生子概念（显式选择）

[direct-derived-concepts-v1](direct-derived.md) 为现有普通单表/双源路径增加有界同根直接子集合，
共享身份与 Mapping、允许重叠，不扩多层/多继承或兄弟推理。新选择使用 API2.5/plan-v3；
默认、requires 与 String 的原选择行为保持。新能力已通过单独封存的 Mac ARM64 限定原生验收，具体范围与版本绑定见上述说明；不能外推其他平台或完整本体规范。

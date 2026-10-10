# 真实 Agent 最小验收研究

2026-10-10，归属 [OSSIE-REQ-001](ossie-execution-research.md)。这是待实施的验收方案，尚无真实模型运行结果。用户本轮只授权整理交付状态和研究；生产 C++、现有 MCP 接口、固定资产及独立 oracle 不改。

## 要证明什么

已有 `verify_population_mcp.ts` 真实启动 stock DuckDB/duckdb_mcp，但查询参数由脚本写定。下一切片应证明：给真实模型业务问题和运行时发现的词汇，由模型选择概念、指标、过滤和工具，得到正式规则限定的结果，并以正确口径回答。只覆盖这一条闭环，不称 Agent 的一般推理、全部 A1–A8 或生产可用性已验收。

## 最小路径与参考依据

推荐 Bun + TypeScript 的显式工具调用循环：模型提出调用，宿主校验并转发到真实 stdio MCP，将实际结果返回模型，直至最终回答。无需先引入 Agent 框架。模型供应商、模型 ID、API/SDK 版本尚未选择；OpenAI [function calling 官方说明](https://developers.openai.com/api/docs/guides/function-calling)只作为工具调用循环的一个参考实现，不构成供应商决定。

MCP [2024-11-05 resources 规范](https://modelcontextprotocol.io/specification/2024-11-05/server/resources)允许宿主决定如何注入资源；[tools 规范](https://modelcontextprotocol.io/specification/2024-11-05/server/tools)描述模型选择调用工具。选择这个协议版本是因为现有真实探针使用它，不能推断当前 SDK 默认版本兼容。现有 `RpcProcess` 是串行测试探针，不是通用 MCP 客户端；实施前须核对初始化协商、超时/退出/关闭与响应关联，决定是否足够复用。若改用官方 TypeScript SDK，先验证其所选固定版本在 Bun 与现有服务器上可用，不为追新升级服务器。

比较选择：完整 Agent SDK 会增加额外会话/框架行为；现成桌面 Agent 可探索体验，但难固定提示词、模型、调用边界与证据。显式循环最便于本次有界验收。这个建议不授权写 harness 或安装依赖。

宿主负责：

- 以明确的已构建 Ossie 产物启动服务器，记录 DuckDB/duckdb_mcp/Ossie 的实际版本及哈希。示例默认安装 community 扩展；验收不能不经核对直接使用那个路径。
- 复用 `examples/population_server.sql` 的受信 operator setup、475行 CSV 及 bundle-10/bundle-20，每个场景独立服务器和全新对话；会话内不 reload。
- 实际调用 `tools/list` 和 `resources/list/read`，将四份词汇/能力资源及实际工具 schema 原样注入模型上下文。保存注入内容及哈希。宿主自动读取资源是本方案明确条件，因此只声称模型在发现上下文中选择查询，不声称模型自主决定资源读取。
- 仅暴露现有 population_compile/population_objects/population_query；名单不从模型输入扩展，原 SQL/import/reload 等入口保持关闭。参数取自模型输出；宿主只做结构/白名单校验，不补默认 concept/metric/filter，不改写错误请求使其成功。供应商 schema 适配另存转换记录，参数字符串内容不变。模型请求构造器只接收固定指令/问题、发现的工具/资源和真实会话结果，不接收 oracle 或场景期待值。
- 返回真实 MCP 原始结果；格式无损解包须同时保留原文及转换记录，不把精确整数/Decimal转成 JS Number，不将查询结果替换为 oracle。

模型可看到规则资源中的当前阈值，这是理解正式定义所必需。模型不可看到 oracle、旧运行答案、验收表或验证器源码；也不获得原始 CSV、文件/终端/Web 读取能力。模型不接收预制工具参数，不通过强制 tool_choice 替它指定 population_query。通过标准由评审器检查，无调用或答错均失败。

## 三个隔离场景

前两个场景使用同一 system prompt、用户问题、工具 schema、数据与模型设置，仅 operator 加载的 ontology/manifest 变化。场景间不共享对话、前次答案或 provider 会话状态；分别记录固定 system/user prompt、工具 schema 和模型设置哈希一致，动态资源及完整模型输入的哈希另存，后两者应随正式规则不同。模型可自然看到不同正式规则，但宿主不额外提示阈值。

固定用户问题：

> 按当前正式定义，2026年3月10日产品X的有效用户平均GAAP是多少？请给出记录数、GAAP总额、平均值和单位，并说明有效用户口径及可追溯的查询依据。

| 场景 | 受信启动资产 | 独立评审器预期，禁止发给模型 |
|---|---|---|
| 基准 | bundle-10 | 37条记录；SUM 1697.80 USD；AVG 45.88648648648649，展示45.89 |
| 规则变化 | bundle-20 | 21条记录；SUM 1277.54 USD；AVG 60.8352380952381，展示60.84 |
| 能力边界 | bundle-10；另一个全新对话 | 问“2026年3月10日产品X的活跃且付费用户平均GAAP是多少？请说明口径与依据。”；须明确缺少活跃/付费依据，不能将 YTD 分类冒充这个口径或提供无根据数值 |

边界场景允许解释已支持的 YTD 定义，但不能擅自把替代问题的数值当作所问答案；是否查询已有 YTD 指标本身不决定通过。

system prompt 只约束依据已提供的定义/工具、无法确定则说明限制、引用实际证据、输出约定回答字段；不列 ValidUser、具体指标/字段/阈值/预期值或调用顺序。用户题中的日期、产品与口径是任务输入，可作为过滤；有效阈值必须来自正式定义，不能复制为额外请求过滤或覆盖规则。

## 分层通过标准

下列第1–4项用于基准与规则变化场景；能力边界场景使用第5项与共同的来源/隔离/预算检查，允许零工具调用。探索调用及自行纠错均保留；范围和数值门禁用于最终回答实际引用的成功执行响应，不要求每次探索都正确。首次实现可由确定性检查核对来源/数值，由人工审阅完整三份 transcript 核对口径和解释，不先引入模型评审器。

1. **调用来源**：至少一次由模型生成的 population_query 实际执行成功；concept/metrics/dimensions/filters 原始参数与转发记录一致。可使用多次查询，但不能由宿主代选参数。compile 和 objects 可选，不以固定调用顺序认定“自主”。
2. **范围与数值**：支撑最终数值的实际查询只针对指定日期、产品X和正式有效概念，无地区/类型等额外收窄。日期精确等值或等价半开一天范围均可；模型可选择每日分组或不分组，评审按实际语义核对。记录数与非空值分母分开，若讨论分母须有实际数据依据。独立逐行 oracle 以 bigint 分核对 SUM，AVG 绝对容差 `1e-10`；两位展示核对45.89/60.84。不得仅比较两条共用编译器路径。
3. **规则与证据**：最终解释须分别指出发现的正式谓词 `User.ytd_gaap >= 10` 或 `User.ytd_gaap >= 20`，引用对应概念资源。模型生成的查询参数可以完全相同；这证明模型使用当前正式概念并理解口径，不证明模型改变查询策略。实际执行结果的 content/core/mapping 哈希对应同一数据及定义；ontology/manifest 对应各场景正式资产。核对已有查询 trace/生成 SQL 中规则确实改变。若模型调用 compile，另核对 compile/query 的 snapshot/hash 相同，不能用 compile 未执行结果代替 query。证据检查不得在回答前将评审结论喂给模型。
4. **最终回答**：回答与成功调用结果一致，包含记录数、总额、均值及 USD 来源，正确说明 YTD 分类及 active/paid 未验证；不能声称跨日期实体去重、币种换算或完整本体推理。数值引用宿主分配的真实执行响应 ID，并可定位对应列/行；规则引用发现资源，评审检查可解析引用。snapshot/hash关联须保留，但它单独不能证明答案数值取自哪次执行，不强求复述整条哈希。最终回答可使用固定 JSON 字段便于检查，同时保留解释原文；字段约束不预填答案。
5. **边界回答**：明确缺少 active/paid 定义或数据，不静默以 ValidUser 替代。返回版本化错误时不得伪造成功或忽略限制。

USD 可从已发现 mean_gaap 的 description 获得，其来源是原 workbook metric!E2；没有发现依据就不能凭常识推断单位。独立 oracle 复用现有 `baseline-results.json` 与原始 CSV 哈希，并在运行前核对该天两组结果；不更改来源或期待值迎合模型。

任一适用于该场景的层失败即记录该场景失败，禁止只看最终数值碰巧正确。三场景各一次构成本次最小实验；即使全部通过，也只证明该固定模型/提示词/资产的一次有界闭环，不证明稳定成功率，也不能绝对排除公开fixture已在模型训练中出现；可证明的是本 harness 没有把基准答案提供给模型。多次重复与更多问题留到后续明确范围。

## 停止与证据

建议每场景最多8轮模型请求、12次MCP工具调用、120秒总时长；执行前根据选定 API 的语义固定 token 上限和总费用上限。数字是拟议预算，不是已测性能。模型本轮内收到原始错误后可自行修正；所有调用计入预算。耗尽、进程退出、协议错误或服务不可用停止并保存失败，不自动换模型、修改规则或重试到成功。传输失败可标记基础设施阻塞，但不能标记 Agent 通过。

保存：repo提交及dirty状态、资产/CSV/二进制/提示词哈希、协商的MCP版本、供应商及请求/返回模型标识、实际可用的采样参数、token使用/耗时、原始模型可见输入输出、模型产生的工具参数、RPC响应与最终答案、分层评审和失败。供应商未报告的模型修订/有效推理强度/费用写 unknown；不采集私有思维链，不保存 API key/认证头。保存首次结果，后续修正必须产生新的有编号实验，不能覆盖失败。

## 实施前剩余与下一步

尚待确定供应商/模型、凭据方式、固定 API 或 SDK 版本、预算及实施授权。预检先验证词汇资源和 result/trace 足以供模型理解与评审；缺信息时先保留具体观察，不扩大插件能力。当前推荐下一步仅实现这三个隔离场景的 Bun/TypeScript 验收 harness，再在明确模型调用范围后运行；不调整生产 C++、MCP 分页或 P1。研究完成与实现/运行完成分开记录在原 issue。

## 只读反证结论

Advisor 建议保留三场景与显式循环，先修正上述场景门禁、独立问题、资源哈希及引用要求。Lead 根据原 MCP/定义/基准核对后采纳；这些仍是验收设计，真实模型行为和运行时资源是否足够尚未验证。requested Advisor Astra/high；有效 backend/effort、usage/billing 未获可验证信息，记为 unknown。

## 运行条件预检（2026-10-10，实际执行）

[真实发现预检](research/ossie-req-001/agent-discovery-preflight/summary.json)已通过两个独立启动资产。使用现有Mac ARM64产物 `13d5317f…` 和stock CLI，协商MCP `2024-11-05`，实际发布三个population工具/四份资源；规则分别为 `User.ytd_gaap >= 10` 与 `>= 20`。mean_gaap description含USD及workbook来源，日期/产品字段存在，没有名为active/paid的字段。每场景四资源解析后合并JSON为15,420 UTF-8 bytes；这不是模型token测量。原始响应、一次性探针和[校验清单](research/ossie-req-001/agent-discovery-preflight/checksums.json)保留，没有调用population工具或真实模型，不是三场景Agent通过。

两个独立进程的snapshot_id恰好相同、ontology哈希不同。这证明snapshot_id不能独自作为跨进程定义身份；评审必须一起核对content/core/ontology/mapping/manifest哈希。每个进程内部四资源的snapshot/ontology一致。serverInfo.version仅为服务器自报值，此预检没有核验duckdb_mcp二进制哈希；后续真实验收仍须补齐运行时产物身份。

[候选固定提示词](research/ossie-req-001/agent-discovery-preflight/prompts.json)将原问题、边界问题和无答案的system prompt独立保存并绑定哈希。它不含具体概念名、指标名、阈值、oracle或预制调用参数。最终JSON便于检查，口径与引用仍需人工核对。供应商/模型、API或SDK版本及费用预算仍待选择；候选提示词尚未送给模型，实施前如调整应保留新的实验版本。

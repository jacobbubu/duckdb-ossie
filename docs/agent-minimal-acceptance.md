# 真实 Agent 最小验收研究

2026-10-10，归属 [OSSIE-REQ-001](ossie-execution-research.md)。已按用户确认预算完成一次正确配置的真实三场景运行：两个数值场景通过，能力边界拒绝数值回答；Lead语义核对一致，最终人工确认仍待完成。此前401失败保留。实际配置仅本地保存。生产 C++、现有 MCP 接口、固定资产及独立 oracle 不改。

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

## 历史研究结论与实施前条件

研究时尚待确定供应商/模型、凭据方式、固定 API 或 SDK 版本、预算及实施授权；当前选定服务与已完成实现见后续章节。预检先验证词汇资源和 result/trace 足以供模型理解与评审；缺信息时先保留具体观察，不扩大插件能力。研究时推荐下一步仅实现这三个隔离场景的 Bun/TypeScript 验收 harness，再在明确模型调用范围后运行；不调整生产 C++、MCP 分页或 P1。研究完成与实现/运行完成分开记录在原 issue。

## 只读反证结论

Advisor 建议保留三场景与显式循环，先修正上述场景门禁、独立问题、资源哈希及引用要求。Lead 根据原 MCP/定义/基准核对后采纳；这些仍是验收设计，真实模型行为和运行时资源是否足够尚未验证。requested Advisor Astra/high；有效 backend/effort、usage/billing 未获可验证信息，记为 unknown。

## 运行条件预检（2026-10-10，实际执行）

[真实发现预检](research/ossie-req-001/agent-discovery-preflight/summary.json)已通过两个独立启动资产。使用现有Mac ARM64产物 `13d5317f…` 和stock CLI，协商MCP `2024-11-05`，实际发布三个population工具/四份资源；规则分别为 `User.ytd_gaap >= 10` 与 `>= 20`。mean_gaap description含USD及workbook来源，日期/产品字段存在，没有名为active/paid的字段。每场景四资源解析后合并JSON为15,420 UTF-8 bytes；这不是模型token测量。原始响应、一次性探针和[校验清单](research/ossie-req-001/agent-discovery-preflight/checksums.json)保留，没有调用population工具或真实模型，不是三场景Agent通过。

两个独立进程的snapshot_id恰好相同、ontology哈希不同。这证明snapshot_id不能独自作为跨进程定义身份；评审必须一起核对content/core/ontology/mapping/manifest哈希。每个进程内部四资源的snapshot/ontology一致。serverInfo.version仅为服务器自报值，此预检没有核验duckdb_mcp二进制哈希；后续真实验收仍须补齐运行时产物身份。

[候选固定提示词](research/ossie-req-001/agent-discovery-preflight/prompts.json)将原问题、边界问题和无答案的system prompt独立保存并绑定哈希。它不含具体概念名、指标名、阈值、oracle或预制调用参数。最终JSON便于检查，口径与引用仍需人工核对。供应商/模型、API或SDK版本及费用预算仍待选择；候选提示词尚未送给模型，实施前如调整应保留新的实验版本。

## 最小程序已实现，本地验证完成，真实调用待运行

用户选定本地兼容服务与模型，实际值只保存在本地测试配置中，公开接口统一为 `OPENAI_BASE_URL`、`OPENAI_MODEL` 和 `OPENAI_API_KEY`。凭据仅在 `--run` 时从环境读入，认证头不进入归档；初始实现未写凭据文件；当前本地 `.env_test` 约定见后文，无SDK或其他新增依赖。按用户补充端点采用 Responses API，实际目标由 `OPENAI_BASE_URL` 追加 `responses` 得到。参照官方 [function calling](https://developers.openai.com/api/docs/guides/function-calling) 和 [conversation state](https://developers.openai.com/api/docs/guides/conversation-state)：input、function_call/function_call_output与call_id、completed/incomplete状态。网关实际兼容性尚未验证，不因“兼容”名称假定它支持全部参数；原Chat Completions实现已移除，不静默回退。

实现入口为 `scripts/agent_acceptance.ts`，模型调用循环为 `scripts/agent_loop.ts`。`--prepare`只做三场景的真实MCP发现；`--run`才调用模型。每次必须指定新的输出目录；固定prompt、二进制/源码/CSV/定义哈希、git HEAD及dirty状态、实际MCP版本/二进制身份、每轮模型可见请求/响应、原始工具参数/RPC响应及失败逐步归档。模型私有reasoning字段不保存；不发送oracle。最后才独立核对答案与引用的实际执行列，口径/规则/引用语义仍需人工审阅；`pending human review`不能当作通过。

本地准备命令（stock CLI按调用方实际路径指定）：

```sh
bun scripts/agent_acceptance.ts --prepare \
  --cli /absolute/path/to/stock/duckdb \
  --extension build/release/extension/ossie/ossie.duckdb_extension \
  --out /tmp/ossie-agent-prepare-new
```

真实运行命令（需已决定本次运行预算，凭据在当前环境）：

```sh
OPENAI_BASE_URL='https://api.openai.com/v1/' \
OPENAI_MODEL='configured-tool-capable-model' \
bun scripts/agent_acceptance.ts --run --token-budget-only \
  --cli /absolute/path/to/stock/duckdb \
  --extension build/release/extension/ossie/ossie.duckdb_extension \
  --out /tmp/ossie-agent-live-new
```

当前硬限制：每场景8次模型请求、12次工具调用、准备/发现/模型工具循环合计120秒；单次模型请求131,072 UTF-8 bytes，每次请求 `max_output_tokens=2048`。三场景总计最多24次请求，请求输出上限合计49,152 tokens。请求字节限额不是输入token测量；输出上限取决于网关是否遵守该API参数。usage按网关实际返回记录，未返回写unknown；没有网关单价资料，费用unknown，程序不提供货币金额硬上限。因此真实运行需用户明确接受这次token/调用/时间预算，并显式指定 `--token-budget-only`；这项选择尚未作出。启动三场景前的stock/MCP身份查询另有10秒预检上限。失败不自动重试、切换模型或扩大预算，场景关闭最多额外约1秒。后续如需不同预算，须另存实验配置，不能覆盖首次结果。

[本地控制证据](research/ossie-req-001/agent-harness-controls/prepare/summary.json)：三场景独立真实MCP发现通过，没有模型调用。50 Bun tests/167 expects、TS类型、格式与现有架构检查通过，新增7项控制为模拟provider/RPC，并非真实Agent验收。首次typecheck因Bun fetch额外preconnect属性导致注入类型不匹配，已保留错误并修正为可调用签名，随后检查通过。生产C++、MCP接口、正式资产与既有独立oracle未修改。本轮只本地提交；下一必要证据为一次有界真实网关运行及完整人工口径审阅。


只读实现审阅后的最终本地验证：54 Bun tests/184 expects、typecheck/format通过，三场景真实准备重跑通过，见[最终准备结果](research/ossie-req-001/agent-harness-controls/prepare-final/summary.json)与[审阅和修正](research/ossie-req-001/agent-harness-controls/advisor-review.md)。该历史阶段曾限制网关/模型；当前已改为通用标准配置（见后文）；带凭据或query的URL在归档前拒绝；事件含时间戳/耗时；准备与模型共用场景deadline；当时Chat协议仅正常stop才可评审最终答案（当前Responses以completed状态为依据），数值失败在summary中明确fail。没有真实模型请求，本地程序完成与Agent验收完成分开。


## Responses 端点修正与当前状态

用户明确实际端点为 `/v1/responses`。当前程序只用Responses：function工具schema平铺name/parameters并显式strict:false；每轮重复instructions/tools/model/输出限额，设置truncation:disabled避免静默丢弃前文。首轮发送固定指令、真实发现资源和问题；后续使用本场景上一轮的previous_response_id及每个call_id对应的真实function_call_output。每场景全新loop，不从旧实验或其他场景读continuation ID。

设置store:true依赖网关保存单场景状态，避免本地重放或归档私有reasoning；不声称网关没有存储推理。服务端持久化和续接是否可用仍需观察，不支持则停止。归档的入站响应明确为公开字段投影，保留输出索引/顺序、item/call IDs、状态、消息phase、usage及错误/不完整原因；出站请求保存精确序列化body。输入bytes限制只约束本次传输大小，不能当作服务端累积context或费用上限。

[Responses控制与准备证据](research/ossie-req-001/agent-responses-controls/prepare/summary.json)通过59Bun/238expects、typecheck/format及三场景真实MCP准备；[只读协议审阅](research/ossie-req-001/agent-responses-controls/advisor-review.md)的隔离/续接/记录约束已由Lead核对和落实。迁移编辑脚本首次中断导致测试仍引用旧chatProvider，错误保留，更新测试后通过。历史Chat验证不覆盖，生产C++/MCP/资产未改。真实模型仍未调用；预算方式仍待用户明确接受，不把端点纠正视作预算确认。

## 开源配置约定（当前）

公共程序只读取 `OPENAI_BASE_URL`、`OPENAI_MODEL`、`OPENAI_API_KEY`。BASE_URL默认 `https://api.openai.com/v1/`，可配置其他HTTPS兼容服务；MODEL必须显式选择，Key仅真实运行时必需。BASE_URL是以`/v1/`结尾的API根地址，程序追加`responses`，不能填完整`/responses`地址。不绑定公司的网关、模型或专用Key变量；环境变量是本程序约定，其中MODEL并不是OpenAI SDK自动选模型的功能。

复制 `.env_test.example` 为 Git 忽略的 `.env_test` 并填写个人配置，显式加载：

```sh
env -u OPENAI_API_KEY -u OPENAI_BASE_URL -u OPENAI_MODEL \
  bun --env-file=.env_test scripts/agent_acceptance.ts --prepare \
  --cli /absolute/path/to/stock/duckdb \
  --extension build/release/extension/ossie/ossie.duckdb_extension \
  --out /tmp/ossie-agent-prepare-new
```

真实运行时改为 `--run --token-budget-only`，仍须先确认预算。程序不自动加载专用配置文件，不自动运行模型。`.env_test`权限设为0600；私有配置和原始本地实验记录不提交。历史公开准备记录已标为脱敏衍生物，哈希关联的原始字节保存在忽略的本地目录；公开值是占位值，不能据此推断当时实际服务配置。六个未推送提交的历史清理现已完成，见[历史清理核验](research/ossie-req-001/agent-history-cleanup-controls/verification.json)。当前tip文件树与清理前相同；原始提交保存在忽略的本地bundle，已实际恢复验证。历史执行commit ID与源码hash仍是原始观测的来源标识，不表示改写后重新执行。

## 正确配置的真实运行结果（2026-10-10）

用户确认原预算，首次请求401后授权一次正确配置的新实验。无网络检查发现继承OPENAI_API_KEY覆盖.env_test，故上述显式本地配置命令先清除三个继承变量；普通程序仍遵循环境变量配置，无需改生产代码。两次原始记录分别保存在忽略私有目录，401不覆盖。

本次三场景完成，共5次模型请求、2次模型选择的工具调用。阈值10：37条记录、1697.80 USD、平均45.88648648648649；阈值20：21条记录、1277.54 USD、平均60.8352380952381。“活跃且付费”场景指出业务定义缺失、ValidUser不能替代，未查询/给出数值。网关报告输入34287、输出1288、总35575tokens（含缓存输入19250与reasoning输出394）；费用unknown，usage不当作金额。

[三场景语义核对](research/ossie-req-001/agent-live-corrected-controls/review.md)及[机器证据](research/ossie-req-001/agent-live-corrected-controls/evidence.json)包含公开脱敏记录、原始/衍生SHA256和实际产物/源码身份。离线重放数值门禁、原始请求字节/续接call_id/场景隔离/预算核验通过。Lead核对范围、规则、单位、记录数与实体数区分及实际列引用一致；human_verdict仍pending，最终人工语义确认由用户完成，不自动宣布整体Agent验收通过。本次仅一个服务/模型的一次运行，不证明稳定性、所有Responses参数或完整Ossie语言符合性。

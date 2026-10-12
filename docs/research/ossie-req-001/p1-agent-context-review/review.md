# P1三场景实际上下文、预算与公开范围审阅

2026-10-12，代码基线f7eec83。三个场景的封存上下文未夹带独立答案或预期调用模板；字节预算满足当前合成fixture，真实输出预算充分性未知。真实入口前需明确状态资源适用范围、响应投影及清理时间合同。本轮仅审阅和记录，不修改运行代码、提示词或预算，不调用真实模型、不读取配置、不启动新MCP、不推送。

沿[OSSIE-REQ-001](../../../ossie-execution-research.md)及[现计划](../../../../ossie-duckdb-plan.md)推进。原P1实现完成与[28项离线控制](../p1-agent-offline-entry-controls/review.md)保持；本包完成不表示真实Agent验收完成。[receipt](receipt.json)绑定prepare/positive两份seal、56份来源、六份实际serialized_body及当前统计；原交付清单的312文件哈希全部保持。当前positive纯离线重放再次complete，不提供外部服务真实性或模型自主性证明。

## 实际发给适配器的上下文

这里的“实际”是模拟运行构造并封存的请求字节，没有发送到真实模型。首次body由固定system instructions、实际发现资源的system消息、场景问题和实际tools/list构成；每轮重复instructions与工具schema，strict=false、tool_choice=auto、stream=false、store=true、truncation=disabled。第二次input只有带tool-1证据标识的完整实际MCP返回，previous_response_id仅指向本场景首次模拟响应。客户端第二份body没有重复首轮资源，但服务端续接语义并不等于上下文已丢弃或输入计费变小。

| 场景 | 首次/第二次body UTF-8字节 | 工具返回input字节 | 完整最终回答字节 | 发现资源/工具 |
| --- | --- | --- | --- | --- |
| 多事实 | 13,428 / 8,886 | 2,580 | 2,160 | program/surface；4工具 |
| 每日状态 | 16,901 / 11,616 | 5,432 | 4,058 | capabilities/concepts/state-program/state-surface；2工具 |
| 历史关系 | 8,964 / 7,955 | 2,954 | 2,456 | history-program/history-surface；2工具 |

六份body均低于131,072字节，prepare与positive三份context逐项相同。首次资源包含定义、规则、身份、单位、加载统计、版本、hash、限制和拒绝范围，没有成员答案、每日完整状态或历史完整链接，也没有原始CSV。加载统计是模型已知信息：例如状态资源已给出captured_user_count=10，复述10不能单独证明模型推导分母；仍要检查执行请求和完整状态集合。模拟器读取评估侧模板构造调用、复制实际返回，独立答案在最终回答后评估；成功只能证明编排、执行与验收链路。

状态资源有适用范围混淆风险：通用capabilities描述观察查询的稀疏结果、无calendar completion、旧result schema；concepts描述请求内User投影。专用state-program与工具则要求整个captured_users域、逐日补齐UNKNOWN。它们属于不同操作，不能把通用描述套在状态操作上，也不能把program的mcp_registered=false解释为实际tools/list未注册。

建议后续明确一条操作边界说明：可调用能力以当前tools/list为准；本状态工具以state-program/state-surface的专用合同为准，通用population资源描述其他操作。保留原发现字段，不删改原证据，不提供正确查询模板或隐藏答案。此为待授权修订，未改变当前提示词。

## 调用预算与停止边界

每场景最多4次请求尝试、3次工具调用，三场景最多12/9；候选输出参数每次4096 tokens，总请求额度最多49,152，不能表示实际用量或金额。当前模拟正路径每场景2次模拟响应/1次实际MCP调用，合计6/3；另有5/7/5次发现RPC和各1次通知，不能混计模型调用。没有自动重试、错误查询纠正或预算失败后再给正确答案。

4096不是4096字节，也不是4096可见回答tokens。官方[reasoning文档](https://developers.openai.com/api/docs/guides/reasoning)说明输出上限包含推理、可见输出和非可见格式tokens；达到上限可能在无可见答案时已经产生费用。模拟器的usage=0是合成字段，未按token限制生成。上述最终回答字节只能证明当前回答规模，不能证明兼容模型能在4096内完成推理和完整JSON。保留预算作为限定pilot候选；不得自动加大额度或把incomplete误报成业务答案错误。

128KiB限制完整序列化出站body；2MiB限制投影前入站响应体，不是“仅公开字段”大小。native内层1MiB、MCP carrier外层8MiB是其他边界；即使native返回合法，二次转义后仍可能超出站上限，届时应明确失败。本地模拟器先构造raw字符串，JSON校验也先解析再检查结构，所以这些限制不是全进程RAM上界。

180秒覆盖每场景setup/discovery/loop及评估前检查；共享运行时预检在场景外。清理代码分别给close与work收束新的30秒窗口，可选测试callback还有第三个；没有一个覆盖全部清理的30秒deadline。同步文件操作、进程调度与外部callback没有硬实时保证。此次修正[使用说明](../../../agent-p1-offline.md)的描述，不修改计时逻辑。后续真实入口应选定“逐步等待”或“共享清理deadline”并作对应控制。

代码使用store=true和previous_response_id。官方[conversation-state文档](https://developers.openai.com/api/docs/guides/conversation-state)说明续接中的历史输入仍计费，官方响应默认保存30天。此处不能推断兼容网关的有效后端、缓存计费、存储/留存或可用续接能力；本轮没有请求这些服务。无累计输入token或金额硬上限，客户端中止也不证明服务端已停止计费。

## 私有与可公开证据

当前私有档案存放实际发现、context/setup、原始调用参数、RPC、工具结果、公开字段投影、模拟响应base64字节前缀、最终回答、评价和失败/清理原因。权限0700/0600、Git忽略与seal验证存储边界及字节完整；不构成通用脱敏或外部来源认证。失败自由文本可能含路径/SQL，业务结果可能含身份、控制字符与金额。当前没有配置读取，但PrivateArchive.create默认无脱敏词，不能据此保证未来真实档案无敏感信息。

尤其是simulation_response前缀位于Responses公开字段投影之前。它只适用于当前合成模拟；真实入口不能照搬为raw provider response归档，否则可能保留适配器随后跳过的reasoning项。应先限定真实响应可记录字段，再为其验证证据完整性与脱敏，不要求或记录私有思维过程。

建议P1公开摘要采用以下白名单；这是审阅设计，尚无新增导出器或导出/发布授权：

| 字段类别 | 可公开候选 | 默认留在私有档案 |
| --- | --- | --- |
| 来源 | 仓库基线、相对源码路径/hash、产物与seal hash、合成fixture声明 | CLI/install绝对路径、内部端点、凭据、部署模型标识 |
| 执行 | scene、模式、成功/失败/pending枚举、固定失败类别、调用/字节计数、预算参数 | 原始错误reason、SQL、context、serialized_body、RPC及调用arguments |
| 验收 | mechanical状态、scripted/pending人工审阅状态、范围/局限、控制名称及结果 | 完整result/最终回答/自由文本definition、业务身份/金额、响应ID及原始响应前缀 |
| 完整性 | 原始seal、选定原件hash、摘要自身hash、衍生声明 | 整个私有目录或可跟随的元数据路径 |

当前receipt按上表摘要统计，固定工具/资源名与字段名来自本仓库公开示例，不复制任何context/events正文或业务值。hash对应私有原件不表示公开者可独立重放，也不表示改写衍生物是原始wire。公开合成样本如需完整数据，可引用已提交fixture与人工答案，不能将未来真实业务数据套用同一公开许可。旧Observation exporter限定旧schema/场景和provider metadata，不适用于P1；不能改schema绕过它的门禁。

## 验证、反证与下一步

Lead核对封存两份archive及当前56项来源、312项历史原件、六份请求重构/准确字节、续接证据包装与实际返回、场景独立引用、资源适用范围、预算和export门禁；positive/prepare纯离线重放完成，原档案未修改。审阅统计脚本首版漏掉function_call_output中的evidence_id/mcp_response包装，断言失败；纠正统计方式后通过，首版脚本保留私有，未修改产品或原始轨迹。私有信息扫描首版将原issue历史绝对路径也纳入检查而失败，改为新增文档/新增差异的交付扫描后通过，历史文本未删改。文档/hash/链接与两次审阅检查失败另记[verification](verification.json)。

复用advisor-escalation一轮只读反证：接受其状态适用范围、分步骤清理、入站投影前边界及模拟不证明tokens充分四项；Lead已逐项核实。Advisor未运行模型/服务/测试或写文件；请求Astra/high，effective backend/effort/usage/billing未知。

本次审阅工作包完成。下一建议先实施最小协议说明与真实入口的离线保护：明确state资源边界，真实响应仅存允许的公开投影，选定清理合同，增加P1公开摘要白名单与相关反例，并保留预算超限/pending停止。采用标准OPENAI_BASE_URL/OPENAI_MODEL/OPENAI_API_KEY配置，私有配置不提交；继续先无模型验证。这些修订需要下一步实施授权。真实运行另需明确指示，本轮没有运行、自动重试或增加额度；分页、通用Agent和发布保持范围外。

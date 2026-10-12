# P1 三场景最小真实 Agent 验收候选

建议首次只运行三个隔离场景：多事实人群/金额、每日三值状态、历史关系。验收模型能否根据实际发现资源选择工具、构造范围、保留完整结果并说明口径；不测试未经指导的概念发现或通用本体推理。当前只完成研究，没有实施真实入口或调用模型。

本工作沿 [OSSIE-REQ-001](../../../ossie-execution-research.md)，基线 `9dd7956`；P1实现及 `209180a` 的17项远端CI已通过。用户授权研究提示词、独立答案、调用预算与证据范围，暂不调用真实模型。只新增研究文档与离线答案，不修改生产代码、旧模拟合同或原始证据，不读取 `.env_test`，不推送。三次单次样本不能估计成功率，不扩充为全部P1或真实业务验收。

## 固定问题与独立答案

复用现有合成 fixture，三个服务器独立进程、独立模型会话；scene ID明确以facts/states/history放入用户上下文，禁止跨场景共享 `previous_response_id` 或结果。资产名称、单位、规则、限制来自实际MCP发现；原始行、oracle源码、预期请求、答案文件、预检结果均不进入模型上下文。[完整逐成员答案](independent-answers.json)仅供评估侧；[来源绑定](source-hashes.json)包含38份源码/fixture/既有验收记录。

| 场景 | 固定数据和范围 | 完整预期数据与必须核对的口径 |
| --- | --- | --- |
| facts | `population-fact-program/rows.sql`，original；全冻结两事实，共同User域；全部共同域减去expense>50为TRUE集合；income/expense/balance | 共同A/B/C三人，选中B/C两人。B为80.00/30.00/50.00 USD；C三值均NULL。A收入375.00、成本85.00被排除；D/E各只有单侧来源不入共同域。不能原始行join放大，不得NULL当0；选后2不是筛选前分母3。没有请求比率 |
| states | 原扩展观察CSV及threshold10正式资产；2026-01-31至2026-02-02含首尾，仅产品X；整个冻结观察User域 | A/B/C/D/E/F/G/H/J/K共10人；30状态、20请求内相邻日对，10对changed=true；逐行答案见下表。J仅在请求外出现仍保留，D仅有Y观察仍在User域；H金额NULL不决定资格。缺失/全NULL为UNKNOWN，不能继承前日，也不能删未变化的比较 |
| history | 现14观察/12原始历史记录fixture；2024-02-29至2024-03-03含首尾，仅产品X | 7条完整(User,DATE,product,Team)链接，见下表。A在2/29两同目标记录只生成一条；3/1是V，3/2缺口无link，3/3为T。A的Y排除，absent没有有效历史；不延续关系。保持大小写、Unicode与NUL身份，record_id不是输出身份 |

状态人工核对表（日期依次为1/31、2/1、2/2）：

| User | 1/31 | 2/1 | 2/2 |
| --- | --- | --- | --- |
| A | FALSE | TRUE | UNKNOWN |
| B | TRUE | TRUE | UNKNOWN |
| C | UNKNOWN | TRUE | UNKNOWN |
| D | UNKNOWN | UNKNOWN | UNKNOWN |
| E | TRUE | UNKNOWN | UNKNOWN |
| F | TRUE | TRUE | UNKNOWN |
| G | TRUE | UNKNOWN | UNKNOWN |
| H | TRUE | TRUE | UNKNOWN |
| J | UNKNOWN | UNKNOWN | UNKNOWN |
| K | TRUE | TRUE | UNKNOWN |

每日TRUE/FALSE/UNKNOWN分别为6/1/3、6/0/4、0/0/10；全范围state_totals为TRUE12/FALSE1/UNKNOWN17，单位User/day，combined_object_count=50。答案JSON是评估数据，不是完整native envelope：days应比required_day_count，changed_count应比changed_comparison_count，实际result仍保留正式字段与完整metadata。逐对比较必须含未变化值；例如D、J两对都是UNKNOWN→UNKNOWN、changed=false；A两对均改变。人数只指冻结输入域，不是注册用户或真实总体。CSV中classification_value与amount_usd是不同字段，不据金额空值推断状态。

历史完整集合：

| User | DATE | product | Team |
| --- | --- | --- | --- |
| A | 2024-02-29 | X | T |
| A | 2024-03-01 | X | V |
| A | 2024-03-03 | X | T |
| B | 2024-02-29 | X | T |
| `N\u0000U` | 2024-02-29 | X | `T\u0000M` |
| a | 2024-02-29 | X | t |
| 界 | 2024-02-29 | X | 队 |

表内NUL以JSON转义展示；实际身份包含单个U+0000，不是六个文字字符。全部行及顺序由答案JSON保存。人工枚举再与现有纯独立helpers对照，历史oracle按原始record逐条匹配，不复用插件的区间合并/二分索引。来源对应与本次实际执行仍是未来preflight必需，计算正确不自动证明来源对应。

## 实际发送的提示词候选

所有提示词单独保存于 [prompts.json](prompts.json)。预期请求只在评估侧答案文件中；不预填API参数、正确工具调用或最终数字。固定问题明确业务口径是验收任务的组成部分，不能把有指导的正确调用描述成无提示自主推理。

共同system要求：从当前隔离服务的实际工具和资源确定概念/关系、规则、单位、粒度及支持范围。所有工具参数符合实际schema，request_json必须为String。只通过注册工具获取执行证据；compile不能证明成员或数值。保持实际结果内层完整envelope、精确字符串、NULL、原始身份和绑定，不重算替换。最终只输出一个JSON对象，不加围栏；字段为status、scene、result、definition、evidence_ids、limitations。正常回答status=answered；scene为本次给定ID；result为实际成功执行的完整内层envelope；definition用中文解释实际统计对象、范围及规则；evidence_ids至少一条实际成功执行tool-N；limitations为非空字符串数组。若无法获得满足问题的证据，返回status=failed、scene、reason、evidence_ids、limitations，不伪造result；无可用证据时evidence_ids=[]合法，失败引用只能指向真实已有事件，不要求虚构成功执行。此协议是候选，需要新入口/decoder实现后离线验证；不是现有模拟合同。

| scene | 固定用户题 |
| --- | --- |
| facts | 按当前正式定义，查询两份冻结事实共同拥有的用户。排除其中“累计成本严格大于50美元”为TRUE的成员；成本未知者不按“超过50”排除。给出保留用户的完整income（收入）、expense（成本）、balance（差额），并说明筛选前共同用户数和筛选后人数。使用全部捕获数据，保持NULL与金额精度；各来源按自己的粒度归约后再组合。请说明实际规则、单位和执行证据。 |
| states | 按当前正式资格规则，查询2026-01-31至2026-02-02（含首尾）、仅产品X的全部每日三值状态和请求内相邻自然日比较，包含未变化的比较与统计。用户域取整个冻结观察输入中的用户，日期/产品只筛观察，不缩小用户域；缺失观察不延续前日状态。请解释TRUE/FALSE/UNKNOWN口径，并给出实际规则和执行证据。 |
| history | 查询2024-02-29至2024-03-03（含首尾）、仅产品X的观察对应历史团队，返回完整(User,DATE,product,Team)关系集合与数量。按当前声明的有限半开有效期[start,end)匹配；缺口无链接，不延续或猜测关系。同目标的不同record_id见证不重复完整关系；保留大小写、Unicode和控制字符身份。请说明范围、关系依据与执行证据。 |

只提供该服务实际发现的全部只读工具和资源：facts四工具/两个URI，states二工具/四URI，history二工具/两个URI。不把题目所需工具预选为唯一可用工具，不强制先compile或固定调用顺序；服务隔离本身限制了选择空间。资源是正式资产说明与捕获元数据，不含查询答案；必须逐字段检查是否有原始行/演练答案夹带。无需开放SQL、load/reload、文件或网络工具。

## 独立答案与执行证据分层

1. 来源核对：固定fixture和定义hash、规则版本、实际stock CLI/MCP/扩展及server hash、原始行数和六snapshot绑定；已有CI证明当时版本对应，不能替代将来所用二进制/数据的preflight。
2. 模型交互：工具选择、原始arguments String、原始JSON含重复键/未知键的严格校验、真实RPC/result、同服务绑定。运行前只做通用schema/类型/预算/只读能力校验，不拿隐藏expected request要求模型走正确路线。
3. 结果核对：最终引用必须指向实际成功执行；完整内层envelope与该执行一致。执行范围/指标/User域/四角色和独立逐成员答案另比对；facts还必须核对共同域、entire_capture及实际cost>50集合语义，不只看B/C相同结果（例如cost>80在本fixture也保留B/C，但口径仍错误）。不能只验证结果复制正确，也不能只验证schema、count或两共用decoder一致。
4. 解释审阅：Lead另审definition/limitations是否正确解释粒度、NULL、分母、日期/产品、历史缺口及局限。哈希/公式合法不替代这项；机械项通过仍pending，人工审查完成后才给限定场景结论。

格式合法但选错日期/产品/指标的只读调用保留真实轨迹；不改参数、不替换结果，不把oracle不一致的错误提示反馈给模型。模型可在预算内自行重新查询；最终答案必须由满足任务范围的实际完整执行支持。相同语义的指标顺序或合法请求形式可按字段名/正式绑定规范化审阅，不为通过而改oracle；有歧义标pending并停止自动裁定。

协议/JSON重复键/未知外层或内层键/越界/非注册工具错误在RPC前失败或由正式native拒绝，原错误保留。正式业务错误如错误产品范围属于答题证据，不冒充协议保护。现agent_loop的外层arguments仍直接JSON.parse，重复键会丢失；provider仍先response.json，没有2MiB流式读取门禁。这两项候选保护尚未实现，需在新入口或适配层实现并离线证明；研究不改旧行为。upstream会忽略未知外层参数并转换object-to-String，因此客户端严格校验不能交给上游代做。native mcp_registered=false不是实际服务未注册；注册证据来自tools/list。

## 首次调用预算候选

按三个隔离场景顺序执行，Lead完成一场机械与解释审阅、给出限定通过后才继续下一场，无新增逐场景人类确认。机械项通过但解释尚未核实记pending，暂停后续模型调用；有合法协议而任务范围/答案错误记task_failed，不能降成pending；只有语义证据确实无法裁定才pending。一次首次批次，无自动retry、模型切换或增加轮次；任一失败停止后续场景并封存未完成记录。允许一轮有明确限制的“先compile/再query”，不要求这样做。

| 项目 | 每场景候选 | 三场景配置合计 | 实际含义 |
| --- | ---: | ---: | --- |
| 模型请求尝试 | 最多4次 | 最多12次 | 第四轮仍只返回tool_calls则无额外最终回答轮；请求失败也计尝试，无自动重试 |
| 模型选择的工具调用 | 最多3次 | 最多9次 | 多调用批次在dispatch前检查总数；发现RPC另记 |
| 发现RPC | facts/history各5次；states7次，另各1次initialized通知 | 17请求、3通知 | 每个注册URI只读一次，无自动重试/额外资源读取；启动服务和发现均纳入场景deadline |
| max_output_tokens | 每次4096 | 请求额度最多49152 | 合计发送额度，不是实测usage/推理量/费用或完成保证 |
| 单个出站JSON body | 最多131072 UTF-8 bytes | 各请求独立检查 | 128KiB，不等于输入token上限，也不限制服务端续接上下文 |
| 单个入站provider body | 候选2MiB | 不以累计乘积保证RAM | 新入口需先做实际流式有界读取；不能先无界response.json再声称限制 |
| 原生内层结果JSON | 既有完整1MiB预算 | 不能截断后通过 | 这是native编码预算，外层carrier与再次转义更大；不代表模型可续接 |
| 现有外层carrier解析 | decoder最多8MiB | 不代表HTTP或内存硬限 | 128KiB出站可能先拒绝；必须检实际序列化完整body。fixture仅几十行，不要求模型复制10000行 |
| 场景deadline | 180000ms，含setup/discovery/loop/评估前检查 | 配置合计540秒 | 主动取消/停止新调用；清理、同步封存和进程调度不能保证整批恰好9分钟硬退出。模型loop用剩余时间，不另加180秒 |
| 清理/封存 | 候选单独最多30秒主动等待 | 名义90秒 | 保留超时/清理失败；强制退出后恢复证据策略仍需实施研究，不声称已实现 |
| 输入tokens、金额上限 | 未设硬限 | 未设硬限 | 沿旧入口token-budget-only口径；usage尽量记录，缺值/计量/价格未知明确保留。未来调用授权需明示接受没有金额硬上限 |

现事实默认8轮/12调用，状态和历史模拟3轮/2调用；本次4轮/3调用是候选新实验预算，不改变旧模拟控制。4096能否完成真实模型输出尚无证据；先离线测上下文/完整envelope/归档字节和模拟截断，再有授权地尝试。若实际incomplete/length/deadline/HTTP错误，失败保留，不能补一轮或改提示后把原试验标通过。

[OpenAI推理预算说明](https://developers.openai.com/api/docs/guides/reasoning)明确输出额度同时约束可见输出与推理，incomplete甚至可能没有可见答案但已发生计费；本项目function调用上限仍须客户端计数实现，不声称HTTP参数已经保证。[会话状态说明](https://developers.openai.com/api/docs/guides/conversation-state)说明previous_response_id续接仍计先前输入；因此body变小不表示计费输入减少。以上是官方契约，未验证兼容服务实际限额、计费或有效后端；不从模型ID推断价格。

候选沿用现Responses适配器的store:true/previous_response_id及每轮重发instructions/tools，最小改动、不新增Conversations。实际compatible服务能否保存/续接和留存期限未知；此选择不提供“服务端不存储”保证。改stateless/store:false需要另核对完整output items续传与重放，不能仅翻转开关。本次未发API请求验证任一模式。[Function calling说明](https://developers.openai.com/api/docs/guides/function-calling)的strict要求不证明现schema或网关支持；首批沿现strict:false，现有结构/类型校验保留；原始外层重复键的严格验证是待实现前置项，不顺带改SDK/模型/采样参数。

模型配置沿现标准OPENAI_BASE_URL、OPENAI_MODEL、OPENAI_API_KEY，仅未来显式live入口读取调用者提供的本地ignored配置。公有源码/研究/CI不提交端点、内部模型ID或密钥；研究和prepare/replay不得读取配置，不调用/token计数/模型列举接口。effective model版本、sampling、价格或billing无法核实时写unknown。

## 证据范围与公开边界

私有新目录每场景保留：源/fixture/server/CLI/MCP/扩展hash和版本、setup与批次/六binding、真实发现tools/resources、提示词原字节与hash、每次出站JSON（移除授权头和密钥，保留serialized_body）、公开Responses响应items/usage/id/status/incomplete字段、tool request/原始arguments/RPC/完整返回、可见final answer、逐项机械检查/独立oracle/人工解释裁定、计数/耗时、失败和清理结果、seal及文件inventory。不请求或记录私有chain-of-thought；摘要usage不等于可信账单。

模型输入只含固定提示词、实际资源/schema、前轮公开输出及真实工具证据，不包含本研究文件、oracle、任何正确调用模板或来自评估侧的修正。出站上下文必须含本次scene ID（该ID不是正确工具或参数）；先保存每次实际发送的请求，对上下文与oracle隔离做可检查证明；不是只看system字串。

公开导出只取经审查摘要：scene/状态、支持/拒绝范围、脱敏结果、调用计数、明确局限、源seal/hash与审阅结论。屏蔽内部端点/模型标识/授权头/密钥、绝对私有路径、服务响应自由文本中的内部配置。只含fixture已公开值，不公开实际网关错误原文/完整会话；导出新文件，保持私有封存原字节不变。credential清洗改变捕获时须标capture_redacted，不再声称精确wire。公开导出不能自动推送或发布，本工作只本地提交研究包。

离线重放必须消费同序provider/RPC/tool/final事件、scene/context/setup/source binding和同一seal；公开投影重放只证明所记录的公开轨迹及工具证据一致，不能重建provider端的私有reasoning/续接状态。用调用者显式artifact来源，不跟随档案install_path去打开文件，不访问配置/fetch/启动MCP。当前各模拟replay强制mode=simulated，不能把归档改成live来复用。原观察live入口的事件投影/封存可参考，但新scope及答案审阅需要独立适配。再封存篡改、正确工具+错误答案、错范围工具与答案共同一致、漏行、重复/额外字段、失序/重复ID、incomplete、预算超限、入站流式超限、无可见输出的incomplete、首场task_failed/pending暂停、文件替换和公开泄漏均应在实施后的无模型控制覆盖。

## 下一授权步骤与未完成项

当前完成的是三场景研究候选和离线独立答案；尚无这三个真实入口实现、live正向重放或预算充分性结果，更没有真实模型通过结论。建议下一工作包先实施仅prepare/simulation/replay的新三场景入口与门禁，确保scope错误不被运行入口修正、oracle不进上下文、预算与失败封存可复现。然后审查实际出站上下文/预算/公开范围，再单独授权首次真实运行；不复用旧观察真实调用授权。分页、通用Agent、多服务协作、合并/发布不混入该工作包。

[离线研究验证](verification.json)绑定最终候选文件、独立完整答案和来源；
[Advisor裁定](advisor-review.md)保留反证及Lead处理。验收记录9dd7956的
[文档tip CI](https://github.com/jacobbubu/duckdb-ossie/actions/runs/38202969452)也已17项通过，
不冒充本研究新提交的CI或真实模型证据。

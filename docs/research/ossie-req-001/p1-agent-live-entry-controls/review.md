# P1真实入口接线与无模型验收

2026-10-12，启动基线b4e6373。真实入口已接入统一OPENAI配置、原预算、响应白名单、凭据回显停止、固定失败诊断与私有封存；单场景投影重放/公开摘要同步接入。当前验证全部使用虚拟配置与受控传输，没有实际网络/模型请求、没有读取.env_test或实际服务凭据，没有推送。本包完成不表示已获准真实调用或已完成模型验收。

沿[OSSIE-REQ-001](../../../ossie-execution-research.md)和[现计划](../../../../ossie-duckdb-plan.md)；保留[四项保护](../p1-agent-safety-controls/review.md)、原P1实现及历史raw。新来源变化会拒绝旧档案，不改旧hash迁移。[receipt](receipt.json)绑定当前源码、同一macOS98b0aeba产物、当前演练和质量证据；新CI任务已配置，未推送所以无远端结论。

## 入口与运行合同

p1_agent_run.ts仅在作为主程序、完整匹配`--run --token-budget-only --scene facts|states|history --cli PATH --extension PATH --out NEW_PRIVATE_DIR`时调用live路径。导入无副作用；旧p1_agent.ts的prepare/rehearse参数保持。每次真实入口只选一题，history不会被按facts索引解释。事实、状态和历史分别绑定自己的发现/提示/fixture/oracle。单场景让运行与pending审阅边界清楚，不是隔离的唯一机制。

配置只在显式live执行时从标准环境名读取：OPENAI_API_KEY用于Authorization头，OPENAI_BASE_URL默认https://api.openai.com/v1/并拼接responses，OPENAI_MODEL显式传入请求model。要求HTTPS，拒绝URL账户/密码、query/hash；redirect=error，不自动重试。不会猜模型、创建密钥、自动加载/修改.env_test或采用非标准别名；运行者可以显式选择忽略的本地配置文件。导入、prepare、旧模拟、重放、公开摘要不读取配置。

库类型与运行时双重隔离：live禁止simulate、review、cleanupFault及外部transport callback；transport-rehearsal必须显式给出虚拟env/传输，不能默认读取process.env或访问服务。两者都走相同请求构造、投影前2MiB、响应白名单、完整工具参数/结果解码、独立scope/答案门禁、清理与封存。live和transport-rehearsal即使机械通过或解释恰好等于模拟文本，review也固定pending，不能自动scripted_pass或继续下一题。

每题最多4个请求尝试/3工具调用、4096请求输出tokens、128KiB完整body、180秒场景时间；清理仍逐步骤30秒，正常最多两个等待窗口。共享preflight和同步封存不在硬实时保证内。没有金额或累计输入tokens硬上限，真实tokens充分性、兼容服务有效后端/续接/计费/留存未知。额度是每次独立运行的边界，重启不会维护跨运行累计额度；一次各跑三题才对应原候选12/9，不能把它称任意重复运行的总上限。

## 响应、失败与证据

记录明确区分构造请求数、传输尝试数、收到HTTP响应数、接受投影响应数和实际MCP工具调用数。HTTP401仍是一次尝试/一次响应，投影为0。run初始化记录0并指向summary最终计数；live的real_model_requests表示客户端尝试，不证明后端执行或费用。模拟/演练该字段为0。provider_projection保存白名单Responses body再交现有适配器，只有投影没有raw字节/头/认证/私有推理。HTTP状态作为数值元数据，响应原文和异常自由文本不入档或console。

若密钥回显到可记录的text、arguments、响应ID等，保护层在保存投影/派发工具之前停止，封存credential_echo固定诊断，不用脱敏参数继续执行。已知密钥的Archive脱敏保留为后备保护；这种停止轨迹只能证明固定失败记录一致，不能声称原始响应/参数被逐字重放。非法配置/preflight失败也会封存，计数为0；主程序早期参数/档案错误仅输出固定入口失败类别。

重放重新核对selected-scene、当前来源、发现合同、精确body、HTTP状态/投影、适配器、loop、实际参数/result binding、独立scope与完整答案；复核计数与pending。HTTP/解析/超限/凭据回显或未收到投影的超时，仅重放固定诊断，无法证明原始被丢弃的响应原因或服务端已停止。晚到传输不得写sealed档案或派发工具。仍不证明原wire、provider真实性、私有续接或模型自主性。

公开摘要升级ossie-p1-public-summary-v2，支持单场景、live/transport-rehearsal与上述分项计数；取已重放的原hash字节，保留semantic_acceptance=not_proven。端点/model/头/参数/业务结果/自由文本仍不公开，诊断不冒充完整执行。旧v1摘要和旧raw作为历史证据保留；v2本地衍生物不等于发布。

## 无模型验证与保留失败

新20项transport控制验证三个独立单场景、首次/续接URL/header/body、真实stock MCP、pending重放/摘要，及HTTP/网络/2MiB/incomplete/错误scope/三类密钥回显、原请求/工具/body预算、截止和迟到停止、缺配置、selected-scene/计数重封存伪造。另在测试进程中替换global fetch验证live分支确实接到fetch；该档案mode=live且记录2次客户端尝试，但两次均为mock，实际网络/模型请求为0，不能拿该档案当真实模型证据。外部报告绑定这项测试来源与mock计数。

旧33项离线控制保持通过。新单元验证标准配置、URL/别名拒绝、显式单场景参数、live覆盖项拒绝、头/错误/凭据回显，以及新进程导入四入口的OPENAI环境读取/fetch均0。完整164个Bun测试/808expects、格式/类型/架构通过；CI YAML语法用Ruby标准解析器确认（系统Python无YAML模块，未据此声称解析通过）；C++未改，复用同产物原生与tidy证明，不称本轮重跑或新跨平台验收。CI共同产物任务新增清除OPENAI环境、Bun禁dotenv、模拟传输验证命令，仍不调用真实模型。

失败保持：联合类型接线首版令旧测试工具的Partial范围过宽，已限制为旧模式；单元首版只匹配refused而实际拒绝为isolation，属断言措辞错误；第二轮transport演练发现超时没有投影但重放仍期待projection，补固定cancelled诊断并保留原失败档案/日志。首次18控制、中间19控制/旧33成功均保留，最终当前源码另封存。HTTP响应与成功投影原曾共用计数，已拆开并用401摘要控制核对。交付检查发现存在但不可读的扩展路径可能在startup失败处理中再次hash报错，已用null未验证声明回退并补诊断封存反例；首次候选交付记录保留，新源码另封存。

复用advisor-escalation一轮只读反证，由Lead核实并落实密钥回显不可当精确对应、投影同路径、分项计数、单场景绑定、live无scripted审阅和预算跨运行边界。requested Astra/high，effective backend/effort/usage/billing未知。凭据保护沿用户已有兼容服务与标准配置决定，不创建新OpenAI key、不读取既有私有文件。

本包必要实施与无模型验证完成。下一建议先审阅真实运行命令、单题次序和最终预算/证据范围，再单独决定是否进行一次限定真实调用；当前调用禁令有效。分页、通用Agent、main合并、推送和发布不扩展。

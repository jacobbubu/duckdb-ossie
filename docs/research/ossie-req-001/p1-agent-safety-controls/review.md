# P1状态上下文与证据保护实施验收

2026-10-12，启动基线841a3a5。四项最小保护已实现并作无模型验证：状态操作适用范围、响应记录白名单、公开摘要与逐步骤清理合同。没有真实运行入口、配置读取或模型请求，业务查询/独立答案/调用额度与旧profile未改。沿[原issue](../../../ossie-execution-research.md)和[现计划](../../../../ossie-duckdb-plan.md)，原P1实现完成保留；本包不证明真实模型验收、预算充分性或服务留存。

## 行为和审阅依据

[上轮实际上下文审阅](../p1-agent-context-review/review.md)发现状态通用资源与专用合同容易混用。当前只在states instructions追加固定操作边界：可调用工具以tools/list为准，状态操作以state-program/state-surface和专用工具为准；通用观察查询的稀疏、User投影和旧schema不能覆盖状态合同。原发现资源原样保留，其他两题提示不改，没有成员答案、期望人数或正确调用模板。

新增p1ResponseBoundary只接收显式Response、字节限额与取消信号，没有默认网络transport、密钥/配置或真实CLI分支。投影前入站2MiB限制继续生效，完整JSON与重复键先校验。重建白名单Responses：id/model/status、完整function_call参数/身份/状态、assistant可见text/refusal、已知incomplete原因及有限非负整数usage。整项reasoning、annotations/logprobs、未知元数据和usage额外字段移除；未知行为项/内容、非法role/status、HTTP失败均拒绝，不修正为成功。未知usage保持未知，数字不证明计费；推理tokens计数是指标，不是推理文本。

解析器或流异常可能引用原始响应片段，现映射固定诊断，不能把raw片段带进failure文本。可见text/refusal/arguments仍可包含业务值，属于私有记录；结构投影不是内容脱敏。projection中output_index属于过滤后的序列，不能称原始wire位置。simulation_response明确synthetic_only，raw前缀仅属当前模拟控制；无真实响应raw记录回调。重放走同一边界核对投影、实际MCP与答案，真实服务端续接/推理状态不在重放证明范围。

新增p1_agent_public.ts从当前源码对应、seal完整且纯离线重放通过的档案派生固定ossie-p1-public-summary-v1。仅输出模式/枚举状态、逐场景请求/调用/最大字节、预算、固定清理政策、seal/artifact/inventory hash和局限。各计数从已验证事件计算；run/summary/events消费的同一份字节逐项绑定原seal，重放seal与导出前后seal也必须相同。不展开任意元数据，不输出reason、provider/model标识、路径、response ID、context、参数、完整结果、定义或响应前缀。startup仅标diagnostic，全部semantic_acceptance=not_proven，scripted_pass不冒充人工/模型通过。

导出只写新的独立文件，拒绝覆盖、源目录内写入与路径各级符号链接。摘要是衍生物，不是公开可重放的raw档案或外部真实性认证；单独返回摘要hash。旧Observation exporter未改、不适用于P1。生成本地衍生物不等于推送/发布授权。

清理政策固定记录于run并由重放核对：依次close、可选测试cleanupFault、work收束；每步骤独立最多主动等待30秒，首失败停止后续并封存cleanup_failure。正常最多两个窗口（请求等待额度60秒），测试路径可有三个（90秒）。不承诺总计30秒或硬实时，不证明被race的任意callback已停止；场景180秒仍不包含共享运行时预检与同步封存。库注入预算还须为整数，CLI既有4请求/3调用/4096tokens/128KiB未改。

## 实际验证和局限

[receipt](receipt.json)绑定当前来源、同一macOS ARM64扩展产物、封存记录和检查日志；[摘要示例](summary-example.json)来自实际成功导出。固定DuckDB1.5.6 stock CLI，扩展SHA98b0aeba1f97f2e61d8570be43c671e3270d9ffe36120b15d5e6908ab82dab3a。三场景prepare/正路径与投影哨兵模拟、实际MCP、纯离线重放通过。33项控制保留原28项并新增投影/摘要/元数据重封存/语义伪造/启动诊断/来源变化等检查；公开保护控制覆盖拒绝覆盖/源内/符号链接写入和输出正文排除。

新安全测试覆盖完整可见文本/工具参数、reasoning/annotations/usage哨兵、未知行为及非法status、refusal/incomplete/HTTP/重复键、解析/流异常固定诊断、清理首失败与超时停止、原hash字节绑定。完整157个Bun测试/774expects通过；格式、类型、架构与差异检查通过。C++与原生执行器未改，复用此前同产物证明，不宣称重跑新native/tidy或远端CI。既有CI产物任务使用同一个verify入口，新增控制将随后续授权推送运行，本轮未推送。

复用一轮Advisor与一次限定澄清，Lead核实并落实：投影不吞掉未知行为/拒绝、不补completed；公开摘要逐字段构造且原hash消费；startup不冒充执行证明；逐步骤清理不冒充总上限。澄清发现events读取曾绕过字节绑定，已补齐。requested Astra/high，effective backend/effort/usage/billing未知。

失败保持历史：首次类型检查发现启动metadata重复cleanup_policy，已纠正；两次来源变化控制按猜测的read次数做spy但没有命中，记录为控制失效，不能评价产品通过。后改纯字节绑定测试与异步导出中修改独立测试副本的确定性拒绝；副本变化已确认、输出未创建。它证明持续变化被拒绝，不证明精确命中某个文件系统竞态窗口。两次失败原档案/日志及中间成功档案保留，不覆写原312份交付证据；最终新目录绑定当前源码。

本包完成。下一建议研究并实现真实运行入口的显式配置、已定预算/失败封存与公开投影接线，再无模型演练；真实调用仍需另行明确指示。真实token充分性、兼容服务续接/费用/留存未知；分页、通用Agent、发布和main合并不扩展。

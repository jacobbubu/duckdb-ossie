# P1 三场景 prepare、模拟演练和离线重放

三个隔离场景已具备本地入口：多事实指标/人群、每日状态、历史关系。prepare实际发现MCP能力但不查询；rehearse采用本地模拟Responses响应，实际执行stock MCP；replay只检查封存轨迹，不启动MCP或访问模型配置。旧入口仍拒绝`--run`和环境配置参数；独立真实入口已接线，须显式run/token-budget-only/单场景，见下节，本轮没有真实调用。

使用固定DuckDB1.5.6 stock CLI、本仓库的扩展产物及Bun1.3.11。CLI和产物路径由调用者显式提供；每次输出必须是新的Git忽略私有目录，不能复用或覆盖已封存记录。以下命令中的路径按本机安装替换。

```sh
bun --no-env-file scripts/p1_agent.ts --prepare \
  --cli /path/to/duckdb --extension /path/to/ossie.duckdb_extension \
  --out .local-agent-evidence/p1-new-prepare

bun --no-env-file scripts/p1_agent.ts --rehearse \
  --cli /path/to/duckdb --extension /path/to/ossie.duckdb_extension \
  --out .local-agent-evidence/p1-new-rehearsal

bun --no-env-file scripts/p1_agent_replay.ts \
  --source .local-agent-evidence/p1-new-rehearsal \
  --artifact-sha CALLER_VERIFIED_SHA256
```

重放的产物SHA必须来自调用者可信来源。重放不跟随档案中的install_path、CLI路径或端点，不读取`.env_test`；源码/fixture/提示词/独立答案清单与当前文件逐项对应。源码改变会拒绝旧记录，不能重写旧source_hashes冒充新代码已验证。初始失败与后续成功记录分别保留。

完整离线控制入口：

```sh
bun --no-env-file scripts/verify_p1_agent_offline.ts \
  /path/to/duckdb /path/to/ossie.duckdb_extension \
  .local-agent-evidence/p1-new-controls /tmp/p1-new-controls-report.json
```

该入口检查实际MCP正路径、错误查询、预算/失败停止、协调重封存篡改，以及重放时fetch/子进程调用均为0。CI已为Linux AMD64和macOS ARM64产物验收配置此命令；新提交尚未推送，因此没有本增量的远端CI结论。

## 运行与独立评估

状态instructions追加固定操作适用范围说明：以state-program/state-surface和专用工具合同为准，通用观察资源不覆盖状态合同。发现原文不改。模型上下文由[已审提示词](research/ossie-req-001/p1-agent-live-research/prompts.json)、实际发现的工具schema和资源构造。首次出站body精确对应这些字段，续接只带真实工具返回；重放重新走同一发现合同并核对全部serialized_body。原始数据行、oracle文件和预期调用模板不进入模型上下文。模拟响应生成器会读取评估侧的预期请求作为测试模板；这是模拟，不能称模型自行选择成功。

运行校验不比较隐藏任务scope。合法的成本>80、错误结束日期等会原样到达MCP，完整返回后才在任务scope/独立答案审阅中判错。不会补正确参数、替换结果或把oracle差异反馈给模拟器。数字碰巧相同也不能通过，例如>80同样选B/C却不是要求的>50。金额/成员/状态/历史链接和请求口径分别检查；只接受有限的指标顺序、大小写/空白及50.00等已声明等价形式，不宣称任意表达式等价证明。

成功回答必须引用实际执行、完整保留内层结果、六绑定及元数据；compile不作数值证据。新运行路径严格拒绝外层/内层重复键、未知键和非String参数。概念/关系名称在结果对应中按固定native名字的大小写规则检查，不提前改成正确名。

三个场景的独立预期：共同域3人、保留B/C；10用户/30状态/20比较/10变化；7条完整历史链接。完整人工枚举和独立helper核对见[独立答案](research/ossie-req-001/p1-agent-live-research/independent-answers.json)。这些是合成fixture范围。facts场景排除了A，本场景不重复375/525 fan-out反例；那项独立原生证明属于此前P1验收。

## 预算、停止和记录

每场景最多4个模型请求尝试、3个工具调用；三场景至多12/9。输出请求参数4096 tokens，单个出站body128KiB、投影前入站响应体2MiB。入站按实际UTF-8字节流读取、到限即取消；模拟raw仅保存准确字节前缀（base64）、截断标记及synthetic_only标签；显式Response保护层在投影前校验并重建白名单，真实响应只记录provider_projection白名单衍生物，不沿用raw归档；密钥回显先停止，不派发脱敏参数。JSON采用线性重复键检查，避免大字符串令旧token正则失效。内层native结果1MiB与外层carrier8MiB不是出站预算，完整转义后body仍可能先超128KiB；不截断后宣称通过。实际tokens、金额、服务端续接与留存未测，不能据此声称费用硬上限或真实预算充分。

场景180秒含服务setup/discovery/loop及评估前检查；共享固定运行时元数据预检在场景前，失败另封存diagnostic。发现请求固定5/7/5，通知各1次，不自动重试。超时关闭活动MCP、取消等待并等待work收束；迟到模拟响应不能再写业务事件。固定cleanup_policy随run封存并重放核对；close与work收束分别最多主动等待30秒（两个窗口60秒），可选测试callback另有30秒窗口（三个窗口90秒），首清理失败停止后续，没有总计30秒或硬实时的保证；失败单独封存并停止后续。同步文件读写、运行时预检、进程调度和任意外部测试callback不是硬实时或RAM上界保证。

协议失败、task_failed、incomplete、预算失败或清理异常均保留；pending也暂停后续场景。默认模拟器的固定解释仅标scripted_pass，mechanical_pass仍带真实语义审阅pending，不等同人工模型验收。无成功证据的failed回答允许空引用。prepare/rehearse返回failed或pending时退出非零；replay可对失败记录返回正常诊断结果，必须读取status，退出成功不能表示原试验通过。

私有档案保存实际发现、setup/context、原始arguments、RPC/工具/公开Responses事件、完整最终回答、评估、失败/清理和seal。startup/discovery失败只有诊断轨迹，不虚构完整执行；部分发现响应可缺失，但不能当成功发现。hash证明记录完整及当前来源对应，不证明外部真实性、私有续接状态、真实业务数据或模型可靠率。独立真实入口已接线，完整会话仍不公开导出。公开P1摘要先验证seal/当前来源和离线重放，逐项绑定原字节，仅派生固定状态/计数/预算/hash，不复制业务结果、自由错误文本、路径或provider标识；全部semantic_acceptance=not_proven。未知metadata即使重封存也不导出。

[本地实施验证](research/ossie-req-001/p1-agent-offline-entry-controls/review.md)记录当前产物、28项控制、质量与原始失败。[实际上下文/预算/公开范围审阅](research/ossie-req-001/p1-agent-context-review/review.md)已完成，相关保护已在本轮落实；真实入口接线及无模型演练也已完成；下一步先审真实运行命令及范围；当前授权仍不允许真实模型调用、推送、合并或发布。

## 公开摘要衍生入口

```sh
bun --no-env-file scripts/p1_agent_public.ts \
  --source .local-agent-evidence/p1-new-rehearsal \
  --artifact-sha CALLER_VERIFIED_SHA256 --out /tmp/p1-new-summary.json
```

仅创建新文件，拒绝覆盖、源目录内写入或路径各级符号链接。输出是摘要衍生物，不能声称包含原始wire或可公开重放；startup失败仅为诊断。不会访问模型、启动MCP或读配置。源码改变会拒绝旧档案，必须保留旧raw并重新产生当前来源的新证据，不能改旧hash绕过。

[四项保护验收](research/ossie-req-001/p1-agent-safety-controls/review.md)已在本地完成，含当前33项离线控制与针对性安全回归。真实入口接线及无模型验证见[本轮验收](research/ossie-req-001/p1-agent-live-entry-controls/review.md)；下一建议先审真实运行命令与范围；当前仍不允许真实模型调用或推送。

## 独立真实入口（本轮未执行）

入口只接受一题，沿标准OPENAI配置读取。OPENAI_BASE_URL默认官方v1地址，OPENAI_MODEL和OPENAI_API_KEY必填。配置由运行者显式提供，不提交凭据/内部端点/model，也不自动读或改.env_test。

```sh
bun --no-env-file scripts/p1_agent_run.ts --run --token-budget-only \
  --scene facts --cli /path/to/duckdb --extension /path/to/ossie.duckdb_extension \
  --out .local-agent-evidence/p1-new-live-facts
```

以上是接口说明，不是本轮运行指令。真实执行仍需用户另行明确授权。每次4请求/3调用/4096输出tokens的候选预算不随重试扩大，没有跨运行金额/输入token累计硬限。机械通过也返回pending/非零退出，等待语义审阅；不会自动执行下一题或把模拟解释当审批。

无模型transport入口显式使用测试虚拟配置，不读实际OPENAI环境或.env_test、不访问网络；包含live分支mock fetch检查，actual network/model=0。

```sh
bun --no-env-file scripts/verify_p1_agent_transport.ts \
  /path/to/duckdb /path/to/ossie.duckdb_extension \
  .local-agent-evidence/p1-new-transport-controls /tmp/p1-new-transport-report.json
```

公开摘要当前为v2，区分构造/尝试/收到HTTP/接受投影/工具调用。live字段只代表选用入口和客户端尝试，不能证明后端执行或费用；startup与无可保存响应的失败仅作诊断。单场景history按自身发现和oracle重放，禁止伪造scene/计数；全部semantic_acceptance=not_proven。

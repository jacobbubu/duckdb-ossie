# P1真实运行命令、单题次序及预算/证据审阅

2026-10-12，代码基线90af7ee。建议候选顺序facts → history → states，每题独立运行一次、封存后审阅再决定下一题。原预算和公开白名单保持，命令配置加载方式已用虚拟文件验证。本轮只审阅，没有真实模型/新MCP请求、没有读取实际.env_test内容、没有改运行代码/提示词/额度或推送。

当前还不能称“可直接运行”：已验证的临时stock CLI不在原路径，构建目录另一个CLI与最近封存的stock SHA不符；实际私有配置的标准变量/有效目标尚未核对。先完成这两项前置核对，再决定首次facts调用。沿[原issue](../../../ossie-execution-research.md)和[现计划](../../../../ossie-duckdb-plan.md)记录，不新增实施backlog。[receipt](receipt.json)绑定当前62来源、三个封存演练、请求字节与命令候选；[入口实施](../p1-agent-live-entry-controls/review.md)的20/33控制和164测试是已有证据，本轮不冒充重跑。

## 单题次序与停点

| 次序 | 场景 | 本题审阅重点 | mock首次/续接body字节；最终回答字节 |
| --- | --- | --- | --- |
| 1 | facts 多事实 | 两来源按自己的粒度归约后组合；共同User域、成本严格>50的排除、NULL及USD金额精度；请求scope与数值分别核对 | 13,431 / 8,889；2,160 |
| 2 | history 历史关系 | 显式日期/产品范围、有限[start,end)、缺口无link、同目标见证去重、完整关系身份与控制字符保留 | 8,967 / 7,958；2,456 |
| 3 | states 每日状态 | 专用状态合同、完整captured_users域/自然日、FALSE与UNKNOWN、缺失不延续、请求内相邻日及未变化比较 | 17,330 / 12,045；4,058 |

这是逐步覆盖金额与集合、历史边界与身份、完整日历与三值状态的审阅顺序，不是依赖关系或模型难度/费用预测。states演练输出较大也不能换算真实tokens。旧三题模拟顺序不改；真实CLI单题history已有独立绑定控制。

每题一个新进程、新私有目录，没有跨题previous_response_id。最多一次启动；不要shell循环、并行、自动串联或非零退出自动重试。live机械通过固定pending且退出1：它可能已发生多次服务尝试，既不表示执行失败也不表示接受。检查summary/stage、实际请求scope、全量结果、单位/分母/身份、解释和局限，并做同source/seal的私有重放；Lead人工结论另存衍生记录，绑定scene/seal/产物/hash，不改原pending。未接受、失败、incomplete、预算/凭据回显/清理异常均停在本题；不擅自修正参数、扩大额度或进入下一题。审阅接受也不自行新增真实调用授权。

## 候选命令与配置来源

下面是待运行候选，不是已执行命令。三个命令分别保留在私有command-plan，参数已由现入口解析器核对；公版用占位路径，必须替换为已核对的worktree、stock CLI和扩展。不能选另一个checkout或未验证的本地构建代替。

```sh
env -u OPENAI_API_KEY -u OPENAI_BASE_URL -u OPENAI_MODEL \
  bun --cwd=/path/to/reviewed-worktree --no-env-file \
  --env-file=/path/to/reviewed-worktree/.env_test \
  /path/to/reviewed-worktree/scripts/p1_agent_run.ts \
  --run --token-budget-only --scene facts \
  --cli /path/to/verified-stock-duckdb --extension /path/to/verified-ossie.duckdb_extension \
  --out /path/to/reviewed-worktree/.local-agent-evidence/p1-live-reviewed-facts-once
```

history、states分别使用同一入口的`--scene history`、`--scene states`，并换成全新对应目录；不将三个命令自动串联。只有已显式导出并核对有效标准变量时才使用原`--no-env-file`无env-file形式；它本身不会加载.env_test。

虚拟试验证明：Bun1.3.11可同时禁自动dotenv并加载指定文件；继承的OPENAI变量会优先于文件，所以候选先清除这三个变量，再用固定绝对路径指定私有文件。专属auto-only陷阱变量未加载；只使用新临时虚拟文件，没有读真实私有配置。真实.env_test目前只确认存在、非symlink、Git忽略；这不证明实际配置正确。

运行前需脱敏核对有效OPENAI_BASE_URL/MODEL/KEY来源、必填值及与此前选定私有服务的目标一致性，不打印key或把内部目标写入公开文档。尤其BASE_URL缺失时程序默认官方地址，不能以“变量文件存在”推断会访问预期兼容服务。当前配置核对尚未授权/进行，不能用默认地址替代未知的私有目标。

最近演练记录的stock CLI SHA为2c75d4758c9d74cb0f4642f4e8d3c1a13e8a888a1e475fffd90934ad5ae3dd17，原临时路径已失效。现本地构建SHA970f79abd67566dfbfa4ff996e17aa77fd2e7709ed14d6935060041deaf14c87，不作为替代验证；固定产物98b0aeba1f97f2e61d8570be43c671e3270d9ffe36120b15d5e6908ab82dab3a仍存在且hash相同。后续恢复/重新核对stock CLI后再给可执行路径。本轮未安装、下载或运行替代CLI。

## 最终候选预算

| 边界 | 每题一次启动 | 三题各启动一次的推导 |
| --- | --- | --- |
| 模型请求尝试 | 最多4 | 最多12 |
| loop接纳的工具调用 | 最多3；实际RPC不超过已接纳调用 | 最多9 |
| max_output_tokens请求参数 | 每请求4096；每题请求额度最多16,384 | 请求额度最多49,152 |
| 完整序列化出站body | 每请求128KiB | 非累计输入token限额 |
| 投影前入站响应体 | 每响应2MiB | 非整个档案/进程RAM限额 |
| 场景时间 | 180秒，含setup/discovery/loop及评估前检查 | 不是整轮硬实时限额 |
| 清理 | 正常close/work各独立30秒等待窗口 | 每题正常请求等待额度60秒，不保证总退出时间 |

共享runtime预检、同步文件操作与调度没有硬实时上界。真实入口禁cleanupFault，测试第三个30秒窗口不属于真实运行。没有自动重试；重启会重置额度，没有程序内跨启动累计门禁。因此12/9/49,152只对应“每题一次”的操作纪律，不是任意重跑总上限或实测tokens/费用。

4096是发送给服务的请求参数，兼容服务是否执行它、真实推理/最终JSON是否够用都未知，不能根据mock usage=0或上述字节数证明。官方[reasoning文档](https://developers.openai.com/api/docs/guides/reasoning)说明额度包括推理、可见及非可见格式tokens，incomplete可能在没有可见答案时已产生输入/推理费用。保持限定pilot候选，不自动按官方一般探索建议扩大预算。

当前请求store=true、previous_response_id续接、truncation=disabled、stream=false、strict=false、tool_choice=auto，instructions/tools每轮重复。官方[conversation-state文档](https://developers.openai.com/api/docs/guides/conversation-state)说明续接中的历史输入仍计费，官方response默认保存30天；不能据此推断兼容服务的缓存、费率、留存或有效后端。没有金额或累计输入token硬上限，也不证明客户端取消已终止服务端工作。真实响应usage只是私有报告值，不是账单证明。

## 记录与公开边界

私有保存run/config来源声明、实际发现/context/setup、精确serialized_body、传输尝试/HTTP状态、白名单Responses投影、实际调用参数/RPC/解码后结果、最终JSON、机械评价/pending、固定失败与cleanup及seal。model配置与服务响应标识、数值usage仅属私有；没有headers/key、raw provider body或私有推理文本。密钥回显先停止，不把脱敏参数当实际执行轨迹。

计数必须分层：构造请求、传输尝试、收到HTTP响应、接受投影、tool_request意图、tools/call RPC派发、匹配RPC回复、成功解码及native执行标志。当前public v2的tool_calls数tool_request，不等于RPC已发出或执行成功；三份正路径四个工具层计数都是1，不能推广到失败路径。compile解码通过仍不作数值证据。后续原始验收必须独立列RPC/回复/解码/执行对应；如修订公共计数命名，另行实施，当前只澄清口径。

HTTP/网络/无可保存投影等失败只重放固定诊断，不证明原服务端原因。纯离线重放成功也不证明当前CLI可用、真实模型曾调用或解释已被接受。权限/Git忽略/hash保护本地归档与当前来源对应，不证明业务数据真实性、模型可靠率或外部时间戳。

公开范围维持v2白名单：固定schema/模式/状态/局限、分项计数/预算、source seal/产物/source inventory hash；semantic_acceptance恒为not_proven。不公开端点、model、response IDs、usage原文、自由解释/失败文本、完整result、context、调用参数或RPC。人工限定接受结论若需公开，另以source/seal绑定衍生记录说明合成范围/限制；不把v2字段改成自动通过，不发布私有文件。

## 审阅验证与下一步

本轮核对62当前来源与2315上轮原文件，三候选单题参数解析、三个已封存模拟轨迹的纯离线重放均pending且一致；没有新服务。虚拟配置加载控制、命令前置条件、计数层级和文档链接/hash另见[verification](verification.json)。审阅脚本首版假设临时CLI仍存在而读hash失败；保留首版，改从最近封存runtime取stock SHA，明确当前不可用。初次临时比对引用更早CLI hash，已纠正到最近封存2c75；本地构建仍不匹配，不作替代。原raw和程序不改。

复用advisor-escalation一轮只读反证，接受并核实配置来源未知、pending退出语义、tool_calls意图口径与总额度靠逐题纪律四点。requested Astra/high，effective backend/effort/usage/billing未知。Advisor未运行服务/重放或读私有配置。

本次审阅完成。下一建议只做已验证stock CLI恢复/核对与实际私有配置的脱敏核对，不发送模型请求；之后再决定是否单独授权facts一次。当前三题运行方案是条件候选，不是运行授权。无需调整业务口径、提示词、额度或公开范围；通用Agent/分页/发布保持范围外。

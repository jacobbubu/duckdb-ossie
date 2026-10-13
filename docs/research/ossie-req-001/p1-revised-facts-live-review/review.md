# 新版 facts 模型上下文与单题运行审阅

2026-10-13，基线70df95e。新版发现信息、客户端序列化上下文、单题候选命令、原调用预算及私有/公开证据范围已完成本地审阅。只复用当前源码下封存的实际stock MCP＋虚拟传输轨迹，纯离线重放和候选参数解析通过；没有执行run、创建新MCP会话、读取实际.env_test内容、更改源码/提示词/额度或推送。

这里“实际上下文”指已经由stock MCP返回并被客户端序列化的发现信息，不表示真实模型已经收到新版内容。虚拟响应由模拟器提供，工具选择不是自主模型行为。原真实facts首次失败仍为未接受，原source/seal保持。沿[原issue](../../../ossie-execution-research.md)、[现计划](../../../../ossie-duckdb-plan.md)及[声明实施验收](../p1-fact-discovery-controls/review.md)记录，不新增第二份backlog。

## 上下文和输入边界

当前4工具、3资源和6组发现RPC都对应封存轨迹：原program提供实际指标、单位/粒度、共同域、来源统计和六绑定；surface说明本服务工具边界；新request-contract载荷12,148 UTF-8字节，提供完整集合结构、谓词/数值限制和6个通用例子。查询/compile两工具说明包含同一语法摘要，String参数保持。

通用例子假定另一个program声明order_count/refund_count，明确说明这些名字不属于当前pilot、不能直接执行；例子只说明语法，没有实际成员、指标值或答案。它们是本轮授权公开的任务无关构造示例，不是隐藏验收请求模板。原program仍拒绝命名/自引用递归，共享说明明确支持预算内嵌套二元树，不把“recursive”误解释为禁止嵌套。

首请求由原系统说明、仅discovered_resources的system输入、原facts问题和4工具描述/schema构成。实际body逐项与context及当前构造函数一致，没有新增独立答案、预期请求或后台任务提示。模拟器生成响应时会读取oracle，但没有把它加入出站首请求；评价在回答产生后才比较独立口径与答案。窗口工具仍可被选择，tool_choice=auto；运行不会先把正确答案或固定正确工具选择喂给模型。

续接请求input只含实际function_call_output，使用previous_response_id；instructions/tools再次发送。工具结果可以成为答案证据，compile只能解释，不能证明执行/人数。有效但错误口径不会自动纠正，原生拒绝或最终评价失败也不能触发自动补正和重试。

[receipt](receipt.json)绑定64来源、4,447份上轮私有文件、单题seal、context/contract和请求hash。当前虚拟单题body分别32,211、11,969字节；另一模拟首请求为32,208字节，两首请求只差model字段。上述数字不是实际token数或未来真实body的精确大小。真实model字符串的JSON转义和每轮完整工具回复都影响大小，运行会逐请求重新按原128 KiB门禁检查。

## 仅 facts 的新命令候选

旧候选的p1-live-reviewed-facts-once目录已被首次失败占用，不能复用。新版私有候选只有facts，使用全新p1-live-contract-facts-once及全新日志文件；原失败、其他题候选和旧证据不覆盖。实际CLI、扩展、已安装MCP文件hash与近期封存一致；候选参数由入口解析器接受。没有执行入口main或发送任何模型请求。

下面仅为占位路径示意；私有command-candidate保存已核对的工作树/CLI/扩展绝对路径。当前开发仍在原codex/ossie-execution工作树，迁移后的主目录是另一个旧checkout，不应把工作目录或配置文件换成它。

```sh
(
  umask 077
  set -C
  env -u OPENAI_API_KEY -u OPENAI_BASE_URL -u OPENAI_MODEL \
    bun --cwd=/path/to/reviewed-worktree --no-env-file \
    --env-file=/path/to/reviewed-worktree/.env_test \
    /path/to/reviewed-worktree/scripts/p1_agent_run.ts \
    --run --token-budget-only --scene facts \
    --cli /path/to/verified-stock-duckdb \
    --extension /path/to/verified-ossie.duckdb_extension \
    --out /path/to/reviewed-worktree/.local-agent-evidence/p1-live-contract-facts-once \
    > /path/to/reviewed-worktree/.local-agent-evidence/p1-live-contract-facts-entry-output.json \
    2> /path/to/reviewed-worktree/.local-agent-evidence/p1-live-contract-facts-entry-stderr.txt
)
```

清除继承的三标准变量、禁止自动dotenv，再显式加载此工作树的.env_test；不使用旧别名或把secret写进命令。本轮只确认文件存在、普通文件/非symlink、Git忽略，没有重新读取有效配置。此前脱敏匹配及一次HTTP成功属于当时证据，不能当作当前配置/认证/可达性已复验。将来明确授权运行时，若配置变化需先脱敏核对仍为既定服务；尤其缺失BASE_URL会触发程序默认地址，不能用文件存在来推断目标。

单次启动、全新私有目录/日志，不shell循环、自动重试或串联其他场景。pending机械通过也退出非零：要读summary/阶段，而不是把退出1直接当成“没有调用”或自动重试条件。返回失败或pending后封存即停；人工语义结论另存source/seal绑定衍生记录，不修改原轨迹。接受该题也不自动授权下一题。

## 原预算保持

| 范围 | 每次新版 facts 启动 | 解释 |
| --- | --- | --- |
| 模型请求尝试 | 最多4 | 客户端传输尝试，不等于后端实际执行或收费次数 |
| loop接纳工具意图 | 最多3 | 包含compile；不是实际RPC成功数 |
| max_output_tokens | 每请求4096 | 单次启动请求输出额度最多16,384，不是实测用量或费用 |
| 完整序列化出站body | 每请求128 KiB | 包括转义后的资源/工具/结果，不是输入token总上限 |
| 投影前入站body | 每响应2 MiB | 超限停止，不截断后宣称通过 |
| 场景时间 | 180秒 | 含setup/discovery/loop，共享runtime预检在前，非整个进程硬实时保证 |
| 清理 | close/work各30秒等待 | 正常两窗口最多主动等待60秒；测试第三窗口不属于真实候选 |

历史已经发生的一次真实请求保留；新启动会重新计预算，没有跨启动累计门禁。本候选不恢复三题串行计划、不增加额度。reasoning effort没有显式设置，4096能否满足实际推理和最终完整JSON仍未知；兼容服务是否落实参数也未证明。没有金额/累计输入token硬上限。store=true、truncation=disabled、stream=false及服务端续接保持；服务留存、历史续接计费与取消后是否停止后台工作均未知，本轮未发服务请求验证。

当前封存虚拟轨迹构造/尝试/HTTP/接受投影均2，工具意图/RPC派发/匹配回复/成功解码均1，最终机械通过但review=pending。这些是受控演练计数，不能代替真实重试结论；也不能假定失败路径中各层计数仍相等。

## 证据记录与公开范围

私有保留实际run/配置来源声明、source/artifact/runtime hash、setup/discovery/context、精确serialized_body、传输尝试/HTTP状态、Responses白名单投影、请求参数/RPC/解码结果、最终回答、评价/pending、固定失败/cleanup/seal。新共享契约原始String字节进入discovery/context，独立契约版本/hash与native六绑定分别对应。

不保存认证headers/请求key、原始provider响应体或type=reasoning整项（包括summary/encrypted字段）；未知注释/metadata删除。允许的可见output_text/refusal/工具arguments和标识保留完整，不能据此声称已过滤其中所有敏感内容。numeric usage含可能报告的reasoning_tokens只是私有统计，不是推理文本、实际账单或token真实性证明。当前key字面量/JSON转义回显门禁先停止，不能把被修改参数冒充原执行。

公开v2保持固定白名单：模式/状态、预算、构造/尝试/HTTP/投影/工具意图计数、最大出站字节、source seal/产物/source inventory hash、固定局限。semantic_acceptance恒为not_proven；不导出端点、model、key、response IDs、usage原文、上下文/完整结果/参数/RPC或自由解释。人工限定结论另用source/seal绑定摘要，不改v2为自动通过。tool_calls仍表示loop意图，真实审阅要另列RPC派发/匹配回复/解码与native执行标志，compile不能替代计算证据。

## 审阅结论与停点

新语法已在客户端拟发送输入中出现，命令路径/原预算/记录边界一致，本地候选可供单独决定；本轮没有重新验证实际配置，也没有证明模型理解、正确选择或首次失败已修复。原实现170测试、37/48、8场景30负控及P1 33/20结论保持，本轮仅参数/来源/文件metadata、冻结body和纯离线重放检查，没有新全量测试/native/tidy/远端CI。

[verification](verification.json)记录来源保留、原body/候选hash、隐私与文档链接核对。本轮审阅到此结束。下一建议另行决定新版facts一次；执行前确认所选配置仍符合既定目标，结束即停下审阅答案、口径、各层调用证据和失败。不得自动运行history/states或推送，三题真实验收仍未完成。

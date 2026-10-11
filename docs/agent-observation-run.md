# 观察 Agent：真实运行入口、离线重放与私有证据

真实运行入口已实现；本次交付只用模拟 Responses 和真实 MCP 演练，真实
模型请求为0。四场景的语义裁定保持 pending，模拟回答来自脚本和 oracle，
不能作为模型自主选择或业务验收的证据。

## 显式真实运行入口

准备模式继续使用原来的 `agent_observation_acceptance.ts --prepare`，它
没有真实调用入口、不读取模型配置。另一个程序只有完整显式参数才运行：

```sh
env -u OPENAI_BASE_URL -u OPENAI_MODEL -u OPENAI_API_KEY \
  bun --env-file=.env_test scripts/observation_agent_run.ts \
  --run --token-budget-only \
  --cli /absolute/path/to/stock/duckdb \
  --extension /absolute/path/to/ossie.duckdb_extension \
  --out .local-agent-evidence/new-observation-live-run
```

以上命令供后续明确授权的真实实验使用，本轮未执行。服务配置仅使用
`OPENAI_BASE_URL`、`OPENAI_MODEL`、`OPENAI_API_KEY`；本地原始值保留在
ignored `.env_test`，不创建/改写密钥，不自动加载该专用配置文件。示例
清除继承变量避免覆盖本地配置；程序仍按标准环境变量取值。

四场景分别启动新 MCP 进程/Responses 会话，复用现有 Responses 解析与
循环。正式规则从实际资源获取，oracle 和预检结果不送模型。最大每场景
8次模型请求、12次工具调用、120秒（包含发现/工具阶段）、每请求2048
输出 tokens、出站请求131072 bytes；四场景最多32次请求、48次工具调用、
65536请求输出 tokens。不是输入 tokens 或金额上限，费用未知。
`store:true` 和场景内 `previous_response_id` 依赖服务端状态保存；不回退
到另一协议、不自动重试或扩大预算。

每次成功执行/编译检查与发现的绑定一致；随后进行既有逐主张数值门禁。
机械门禁通过仍保持语义裁定 pending。失败停止后续场景、保存已发生的
事件、封存失败归档；超时时终止所属 MCP 并等待工作结束，不留下继续
发请求或写证据的后台任务。

## 私有归档与公开衍生物

真实记录只允许放在实际 Git-ignored、未tracked的 `.local-agent-evidence`
新子目录。根/父路径及文件拒绝 symlink（含悬空链接），目录0700、文件
0600，排他创建并拒绝覆盖。事件同步追加；结束后 `seal.json` 列出实际
文件名和 SHA256，自身不自引用。缺seal的中断记录是不完整档案，不得
称完整实验或自动删除。已有失败、原始证据和本地配置不覆写。

归档包含出站 JSON body/serialized_body、入站 **公开 Responses 投影**、
实际 MCP 请求/响应、运行时与源码/fixture哈希、场景时间和预算、最终
回答/机械评估。它不保存认证头、完整HTTP响应字节或 private reasoning。
已知凭据回显也会删去，事件标记 `capture_redacted`；此时不能声称保存了
未改写的原始请求字节。哈希证明相对封存记录的字节一致，不认证外部
服务来源或时间。失败诊断只保存类别、阶段及可识别HTTP状态，不复制
任意异常文本、headers 或环境变量。

公开导出须单独显式执行：

```sh
bun scripts/observation_agent_replay.ts \
  --export .local-agent-evidence/new-observation-live-run \
  --out docs/research/ossie-req-001/new-observation-public-derivative
```

先在内存中构建已知实验文件的衍生内容，递归解析嵌套 JSON 字符串并
脱敏服务 URL/主机/模型，扫描已知标记后才创建公开目录；拒绝未知归档
文件及覆盖。`derivative.json` 记录 mode、原seal SHA、每份raw/derived SHA，
明确 `exact_original_request_bytes=false`。公开 `serialized_body` 是改写的
衍生字段，不能冒充原始精确字节；原私有文件不改变。免费文本中的其他
未声明敏感业务数据仍需操作者审阅，自动脱敏不等于任意秘密识别保证。

## 离线重放

```sh
bun scripts/observation_agent_replay.ts \
  --replay .local-agent-evidence/new-observation-live-run \
  --out .local-agent-evidence/new-observation-replay.json
```

输出必须是另一个新私有路径，不能写进已封存源目录。重放不读取模型
配置、不fetch、不启动MCP；不执行归档携带的代码。源码/fixture与归档
哈希不同则停止，不静默迁移或切换版本。写报告时另行核对Git忽略状态。

重放先验证文件清单、权限、canonical seal、字节哈希和完整终态，再按
**单一有序事件游标**重新构造发现/模型循环。逐项比较请求body与序列化、
实际RPC ID、工具参数/响应、场景内 continuation/call_id和无多余尾部。
同时从公开 Responses 投影再次解析 completion，不能只信归档中已经
整理好的 message/finish_reason。固定问题/system/工具和setup/发现绑定
都重算核对；最终重新执行逐主张门禁并与保存的评估对应。

原事件的时间/计数必须符合记录预算；快速重放不证明原运行时间。
`live`、`simulated`贯穿run/seal/replay/export，不能因重放成功升级来源。
结论为内部一致的重放且语义pending；不证明服务真实执行、完整Ossie
符合性或真实业务答案。失败/partial档案可检查完整性或导出诊断，但不能
通过完整实验重放。

## 本次验证与剩余工作

[验证记录](research/ossie-req-001/observation-agent-entry-controls/verification.json)
绑定源码、测试、日志、公开衍生与原seal/文件哈希。四场景模拟演练7次
模拟完成、4次真实MCP工具调用；额外一次模拟incomplete证明停止、封存
及完整重放拒绝，真实模型请求均为0。原私有记录保持不变。

新增16项归档/重放控制覆盖错continuation/call_id/RPC ID、重算seal后的
语义篡改、乱序/重复/尾部、投影与整理结果不一致、来源/上下文变化、
缺/多文件、失败/时间/路径/权限、嵌套编码泄漏与原始目录保护；网络和
进程启动桩未被重放调用。单测使用公开模拟记录构造 **新的synthetic**
私有回归fixture并重绑定当前源码，不冒充原私有实验。

全部94项 Bun/391 expects及格式/类型/架构通过；同一b012058e扩展下新
观察MCP43检查/38拒绝、旧两个profile330检查/106拒绝通过。未改生产
C++/fixture/旧示例或旧Agent行为，未推送或执行本轮远端CI/tidy。

当前入口/重放/归档工作包本地完成。[四场景/预算/证据范围审查](research/ossie-req-001/multi-period-research/agent-run-review.md)
已完成，建议先补齐提示词与门禁的引用/单位/精度/JSON约定，并消除两题
口径歧义；修订尚未实施。现预算保持候选，私有档案与服务端存储边界已
明确，不能以模拟推断真实预算足够。下一必要动作是最小修订和无模型验证，
随后由用户明确指示是否运行真实四场景；暂不调用。

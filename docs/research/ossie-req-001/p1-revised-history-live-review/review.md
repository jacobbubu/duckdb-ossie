# 新版 history 单题真实运行前审阅

2026-10-13，基线9e8ed9b。本轮审阅完成：查询与编译工具均把完整内层语法和
紧凑通用示例送入实际模型上下文，原先的构造信息缺口已补齐；单题命令、
预算与证据范围已核对。**尚未调用真实模型，下一步只建议另行授权一次history
启动，先脱敏复核本地配置，结束后停下审阅。** 本轮没有改实现或预算，没有
新MCP会话、读取配置内容、推送或进入states。

沿[OSSIE-REQ-001本地issue](../../../ossie-execution-research.md)、
[最小声明与无模型证据](../p1-history-discovery-controls/review.md)，复用同源的
实际stock MCP发现＋虚拟Responses轨迹，不以此当模型自主验收。原缺口审阅
和旧候选命令仍保留为历史记录，本审阅对应新声明及新的候选输出路径。

## 实际模型输入

实际tools/list两工具、history-program/history-surface两资源、5发现RPC均未变。
两工具各自包含共同源的完整dates all/range、products all/subset结构和分支字段，
String封装、DATE/字节/原始身份约束、原生排序与当前Agent预排序的区别，以及
查询执行与compile仅解释的区别。通用membership_at_observation/Catalog-A例
是明确假想的另一program语法示例，本pilot未声明该关系，无成员/结果/数量，
不得直接照用；实际relationship须来自history-program。

首请求的instructions为原系统说明，input仅含实际发现资源和原问题，tools含
实际两工具及完整描述；未加入隐藏oracle、评估结果、正确请求模板或样例答案。
原问题查询2024-02-29至2024-03-03（含首尾）、仅产品X的捕获观察对应历史团队，
要求完整(User,DATE,product,Team)集合、数量和中文口径/执行证据。与同源另一
模拟演练首请求比较，仅model字段不同；系统和原问题没有偷偷修改。

请求日期首尾包含，历史有效期为有限[start,end)，缺口无link、不沿用团队、不
补日历或未捕获观察。完整关系去重，record_id只标识历史见证，不充当输出
身份；原始大小写、Unicode、NUL身份保留。日期/产品只筛观察，不能隐藏已载入
历史冲突。数量口径是完整四元关系数量，不是去重用户数、观察源行数或索引
段数。compile不能提供执行/关系数量证据；最终完整envelope需对应实际query。
原生mcp_registered=false不代替本次tools/list注册事实。

续轮input为已有工具执行的function_call_output并携带previous_response_id，
instructions与工具描述再次发送。已封存模拟提议参数与tools/call一致，没有
自动修正查询；未来真实模型若提议合法错范围，仍会执行，之后独立scope/成员
核对拒绝，不因碰巧答案相同就通过。记录要求是公开口径/证据，不要求私有推理。

虚拟首/续body为12,911/11,902 UTF-8字节，同源模拟首body为12,908，仅model
长度不同；这些不是未来真实body大小、token消耗或输出充分性保证。已核对
serialized_body与body、实际发现、共同源和context对应。[核对记录](receipt.json)
绑定66来源清单、原context/seal与固定产物，离线重放结果一致pending。

## 单题运行候选与预算

私有档案保存完整、未执行的单题命令及stdout/stderr路径，入口参数解析通过。
候选清除继承的OPENAI_API_KEY/OPENAI_BASE_URL/OPENAI_MODEL，禁止自动dotenv，
显式加载忽略的本地.env_test；指定原固定stock CLI/扩展、scene=history和全新
p1-live-contract-history-once目录。新目录及两个日志路径未占用，原证据不复用。
CLI、原生扩展、已安装MCP的SHA与既定产物一致。配置本轮只核存在、普通非符号
链接、Git忽略；没有核内容、目标、认证或服务可用性。真正启动前仍须按既有
标准变量规则脱敏复核有效配置，不打印或提交值。

| 单次启动限制 | 含义 |
| --- | --- |
| 4请求、3工具调用 | 模型循环限制，发现RPC另计；不自动重试或另跑题目 |
| 每请求4096输出token | 最大累计请求输出额度16,384；不是实际消耗或费用预算 |
| 出站131,072字节 | 完整序列化body含转义和描述；超限拒绝 |
| 入站2,097,152字节 | 响应投影之前的完整body门禁 |
| 场景180秒 | 设置/发现/循环窗口；共享运行时预检在此窗口前 |
| 清理close/work各30秒 | 顺序等待，首失败停止；合计最多请求60秒，非硬实时保证 |

这些不限制输入token、真实账单或多次启动累计额度。请求保持store=true、
truncation=disabled、tool_choice=auto、stream=false、tools.strict=false；未显式
指定reasoning effort，不能猜测实际后端身份或推理配置。代码/客户端尝试不证明
服务端执行次数、留存或计费。原生查询仍限16KiB内层请求、64产品/每项256字节、
10,000完整links/1MiB内层结果，超限整体拒绝，无分页截断。不能把原生内层1MiB
误称外层RPC或模型输出一定容纳得下。

若后来获单题授权，只启动一次；失败、pending或机械相符均停止，不重试或自动
转states。live原始pending即使机械通过也退出1，这是既有审阅合同，不是让操作员
以退出码自动重跑。原始seal/status保留，Lead语义审阅与用户接受各写独立衍生
记录，逐项核完整关系、scope、去重、数量分母、日期与身份，再决定是否限定接受。

## 私有与公开证据

私有保存实际发现/context、来源/运行时绑定、序列化请求、客户端尝试/HTTP状态、
白名单响应投影、工具意图/实际RPC/匹配回复/解码/完整结果、最终答案、评估、
清理与seal。分别统计请求构造、尝试、HTTP收到、投影接受、意图、实际查询、
回复/解码和执行，不把发现RPC或compile当实际关系查询。序列化body留在私有封存；若另作逐请求hash测量也保留私有。实际数据、模型名、
响应标识与numeric usage不进入公开摘要。

不保存Key、请求头、原始provider wire或reasoning内容（包括其summary/encrypted
内容）；允许的可见文本/拒绝/函数参数仍完整留在私有区。白名单丢弃未知元数据/
annotations，但不是全能脱敏；当前key的原样和JSON转义回显检测不能证明剔除
所有敏感信息。numeric reasoning_tokens是计数，不是推理内容或费用证明。

公开仅固定v2计数/预算/来源绑定与限定审阅，不导出内部端点/模型/凭据、响应ID/
usage、原始context/RPC参数/结果/模型自由说明。v2 semantic_acceptance始终
not_proven，不能替代另存的人类接受记录。未来若有受限公开结论，应绑定原seal
和审阅范围，原始失败同样保留，不把提供商响应投影冒充完整原wire。

本轮66来源与实施1224私有原件hash不变，纯离线重放阻断fetch、不启动MCP或模型进程；候选命令只解析，配置只看元信息。上一轮173Bun/873断言、history七场景29
负控、MCP15/24/28及P1离线33/传输20保持，不重复全套/native/tidy/远端CI。
审阅工作包完成，真实history/states仍未执行；facts仅已有本fixture限定接受，
不能推断整体真实Agent可靠率。

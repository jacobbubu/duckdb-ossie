# states 真实运行前审阅：存在产品范围构造缺口

2026-10-13，基线2ccfdb3。本轮已审实际模型上下文、单题候选命令、预算与证据
范围。**建议先研究states查询/编译工具的最小完整语法声明，暂不真实运行。**
没有调用模型、创建新MCP、读取.env_test内容、实施语法补充或推送。

用户对history样例限定结论的接受已另存
[接受衍生记录](../p1-contract-history-live-controls/user-acceptance.json)，绑定历史
receipt/seal，原raw pending、Lead receipt和失败记录保持不变。当前history已限定
接受，历史文件中的not_recorded是当时状态；states仍未真实验收。
沿[OSSIE-REQ-001本地issue](../../../ossie-execution-research.md)和现计划继续。

## 实际上下文与发现缺口

实际stock MCP发现两工具population_daily_states/compile、四资源capabilities/
concepts/state-program/state-surface，7发现RPC。系统中操作适用范围说明已进入
首请求，明确通用稀疏观察查询/请求内User投影/无日历补齐描述不覆盖专用state
合同；实际工具以tools/list为准，原生mcp_registered=false不证明未注册。

原问题要求2026-01-31至2026-02-02含首尾、仅产品X的全部每日三值状态，以及
请求内相邻自然日比较（包括未变化），全冻结用户域、不延续缺失观察，并说明
正式规则与执行证据。实际程序披露当前资格概念和正式规则。首请求由该原题、
系统说明、实际两工具和四资源组成，独立答案、正确请求模板和评估逻辑未加入。
同源模拟首请求仅model不同；这些资源含合法加载元数据，不等于泄漏查询答案。

外层String concept/request_json已有schema，查询描述也列出start_date/end_date/
products/user_domain=captured_users/transitions=within_request_days。但发现信息
没有说明products的嵌套对象：`{"mode":"all"}`或
`{"mode":"subset","values":[非空String,...]}`；mode/values字段与分支精确约束
没有进入实际context。compile描述仅引用query，不能独立完整构造。
客户端另要求subset预先按原始UTF-8字节排序，原生接受任意顺序并规范化，
这一层区别也尚未披露。通用资源的JSON字符串数组描述不能替代state内层合同。

上述是发现缺口，不是states模型已经失败，也不预测它必然失败。已封存模拟器
直接提供合法请求，成功重放不能证明模型能自主猜中结构。本轮不实施；下一
候选先研究两工具完整构造摘要、String编码与通用示例是否可按history最小方式
补齐，再决定实现/验证。不要直接复用history的日期支持域或请求字段。

## 状态、身份、统计对象与支持边界

用户身份来自全冻结观察输入的去重原始User，日期/产品只筛观察，不缩小用户
域。仅其他产品或范围外有记录的用户也在状态日历中，可能每一天均UNKNOWN。
观察完整身份和每日状态身份不同：观察(User,DATE,product)，状态(User,DATE)，
比较(User,from_DATE,to_DATE)。大小写/Unicode/NUL等原始身份不折叠。

当前正式资格规则对观察求BOOLEAN，再按用户/自然日归约：任何TRUE胜出；
非空且全部FALSE才是FALSE；缺失、全NULL或FALSE+NULL为UNKNOWN。缺失不是
FALSE，不延续前日；结果属于本冻结当前规则，不推断历史规则或真实业务状态。
产品只筛参与该日归约的观察，不要求所有产品合格；不擅自重算/改变已发现规则。

比较仅在请求内相邻自然日之间，每个用户返回全部相邻对，包括未变化。changed
表示标签是否不同，涉及UNKNOWN的标签变化也计为changed；UNKNOWN到UNKNOWN
不变。它不是只计TRUE/FALSE变化，也不证明真实业务事件或去重变化用户数。

对N用户、D包含首尾的日期，完整状态数N*D（单位User/day），完整比较数N*(D-1)
（单位User/相邻日对），两者合计N*(2D-1)。changed_comparison_count为变化的
比较对数量；每日状态totals的分母是全部User/day，不是源行/用户人数或非NULL
观察数。source/captured计数属于加载元数据；完整状态矩形、所有比较、totals和
scope都需核对，形状正确或总数匹配不证明状态真值。compile不扫描或计算这些值。

state日期仅规范YYYY-MM-DD、年份0001..9999、首尾包含且1..366天，不继承
history的BC/多位年份。产品all或1..64个非空原始String子集、每项256 UTF-8字节；
原始内层JSON16KiB（含空白/转义），重复/未知键拒绝。states+comparisons最多
10,000对象，分配前按N<=10000/(2D-1)门禁；完整内层JSON1MiB含元数据/转义，
超限拒绝，无分页或截断。没有registry用户域、请求外前驱、carry、历史有效期/
规则推断、任意SQL或重载工具；不能把原生内层上限当外层RPC或模型输出保证。

## 命令、预算与运行停点

私有新候选只指定scene=states，入口参数解析通过，out及stdout/stderr未占用。
清继承三个标准OpenAI变量，禁止自动dotenv，显式加载忽略的本地.env_test，
固定CLI/原生扩展；候选未执行，发现缺口尚未处理，不当启动授权。配置仅核
存在/普通非符号链接/Git忽略，未读内容或验证目标/认证。CLI、扩展、已安装
MCP SHA与既有产物一致；未来真正运行前仍须脱敏复核有效标准配置。

预算保持每次启动4模型请求/3工具调用、每请求4096输出token（累计最多请求
16,384额度，非真实消耗或费用上限）、出站131,072字节、投影前入站2MiB。
场景设置/发现/循环180秒，运行时预检在此前；正常close/work各30秒顺序清理，
首失败停止，非硬实时保证。不是输入token、收费或多次启动累计上限。

已封存虚拟首/续body为17,330/12,045字节，同源模拟首17,327只差model长度；
不是实际token消耗、未来请求/响应大小或输出充分性证明。请求保持store=true、
truncation=disabled、tool_choice=auto、stream=false、tools.strict=false，续轮
previous_response_id；不能由代码flags确认服务端留存、后端身份或计费。

未来若另行获单题授权，只启动一次，失败或pending也停下，不自动纠参、重试、
继续别题。合法错scope可执行，随后独立核对拒绝；模型若只compile，不能作为
状态执行验收。机械通过仍保留raw pending/seal和既有退出1，Lead语义及用户
接受另存衍生记录，不按退出码重跑或用JSON形状当语义接受。

## 记录范围与核对

私有保存发现/context、来源/运行时、序列化请求、尝试/HTTP状态、白名单投影、
提议参数/实际RPC/回复/解码/完整状态与比较、最终口径、评估/失败/清理与seal。
分别核请求构造/尝试/HTTP收到/投影接受、工具意图/实际查询/回复/解码/执行，
不把7发现RPC、compile或循环意图当业务执行。实际模型名、响应ID与numeric
usage留私有；逐请求body hash如另作测量也留私有。

不保存Key、请求头、原provider wire或reasoning内容。允许的可见文本/拒绝/
函数参数仍完整留私有；numeric reasoning_tokens是计数而非推理文本/账单。
当前key回显检测及白名单未知metadata/annotation剔除不保证所有敏感信息消除。
公开仅固定v2计数/预算/绑定与有界审阅，不导出内部端点/模型/凭据、响应ID/
usage、原始context/RPC/结果或模型自由说明；semantic_acceptance=not_proven
不替代用户接受，模型和实际wire/后端/费用/留存仍需分开评价。

[核对记录](receipt.json)绑定实际context/seal、66来源与固定产物。阻断fetch的
纯离线重放一致pending；核首请求对应及隐藏答案隔离，没有新MCP或模型进程。
实施1224和history真实审阅23私有原件hash保持，原history receipt/seal未改。
上一173Bun/873断言和P1离线33/传输20保留，无需重复全套/native/tidy/远端CI。
本审阅包完成，下一建议先研究states最小语法；history/facts仅限定接受，不能
代替states真实验收或整体可靠率，当前不自动真实运行。

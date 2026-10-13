# 新版 states 单题真实运行前审阅

2026-10-13，基线eeb3cc3。本轮审阅完成：两工具的完整内层结构和紧凑通用
示例已进入实际模型上下文，产品构造语法缺口补齐；单题候选命令、原预算和
证据范围已核对。**未调用真实模型，下一只建议另行授权一次states启动，先
脱敏复核有效本地配置，结束后停下审阅完整答案、统计口径与调用证据。**
本轮没有新MCP、读取配置内容、改实现/提示词/额度、重跑全套或推送。

沿[OSSIE-REQ-001本地issue](../../../ossie-execution-research.md)、
[最小声明与无模型回归](../p1-states-discovery-controls/review.md)及现计划。
旧[缺口审阅](../p1-states-live-review/review.md)和候选保持历史事实，本记录对应
新版完整描述、新候选路径；不把旧封存重封为当前代码下已经通过。

## 实际模型上下文

实际stock MCP仍两工具、四资源、7发现RPC；工具各有完整五根字段及products
all/subset精确分支、String封装、常量、日期/字节约束、原生排序与Agent预排序。
共同语法和例子来自同源，query/compile各自专用carrier/schema/API、执行或
BOOLEAN投影/计划解释也保留；compile不计算执行结果，但绑定时仍检查元数据
容量。STATE_SCOPE_NOTICE明确通用稀疏观察/请求内User投影/无日历补齐不能
覆盖专用states合同。mcp_registered=false不是实际注册探测。

首请求instructions为原系统加state操作适用范围说明，input仅实际四资源及
原问题，tools含实际两工具完整描述。原问题为2026-01-31至2026-02-02含首尾、
仅产品X，完整每日三值状态及请求内相邻自然日比较（含未变）、全捕获User域，
要求实际规则与执行证据。没有加入oracle、正确请求模板、评估结果或答案。
实际program披露当前资格概念/规则和加载计数，这些合法元数据不等于查询答案。

通用EligibleVisit/Catalog-A等例是假想另一program，pilot未声明该资格概念，
没有本题日期/用户/状态结果/数量，明确不能直接照用；实际concept与规则须从
state-program读取。与同源另一模拟首请求比较，仅model字段不同。续请求仅
已成功工具输出并携带previous_response_id，系统与工具定义重复传送；模拟
提议到实际RPC参数没有纠正，不证明未来模型会自主选择正确范围。

序列化body与记录结构对应，虚拟首/续22,912/17,627 UTF-8字节，同源模拟首
22,909只差model长度；小于原131,072出站限，但不是未来真实大小/输入tokens/
响应充分性保证。[核对receipt](receipt.json)绑定68来源、实际context/seal与产物。

## 审阅时必须守住的口径

日期边界既定义输出日历，也筛贡献观察；产品筛贡献观察，均不缩小全冻结
输入去重User域。范围外/其他产品才有记录的用户仍输出每日状态，不当登记
用户全集。状态身份(User,DATE)，比较身份(User,from_DATE,to_DATE)，原始User
大小写/Unicode/控制字符保留，不按观察源行身份统计。

规则为该快照当前正式资格规则，任何TRUE胜出，非空全FALSE才FALSE，缺失/
全NULL/FALSE+NULL均UNKNOWN；不将未知当FALSE，不延续前日、不推历史规则。
日期限规范Gregorian YYYY-MM-DD、0001..9999、含首尾1..366天，不能沿用
history的BC/多位年份或半开请求范围。

比较仅请求内相邻自然日，含全部未变对，无请求外前驱。changed表示两个
状态标签不同，包括UNKNOWN参与的变化，非真实业务事件或去重变化用户。
N捕获用户、D日期：states=N*D，comparisons=N*(D-1)，合计=N*(2D-1)。
state_totals单位User/day，changed计User/相邻日对，非源行/非NULL观察数/人数。
加载captured_user_count/源行数不是查询计算证据。最终要核完整状态矩形、
全部比较、totals、scope和正式规则的独立对应；计数或schema一致不证明真值。

只有已执行query提供结果，compile不扫描/计算状态或比较，不能当实际答案；
元数据容量门禁对两工具同样有效。原生内层request16KiB、64产品/每项256字节、
states+comparisons<=10,000、完整结果/计划1MiB，含元数据/请求/转义，超限拒绝，
无分页截断；这些不是外层RPC或模型答案一定容纳得下的承诺。

## 未执行命令、预算与停点

私有candidate-command保存完整单题argv与新stdout/stderr路径，参数解析仅
scene=states，p1-live-contract-states-once及日志未占用；没有执行run。清除继承
OPENAI_API_KEY/OPENAI_BASE_URL/OPENAI_MODEL，禁自动dotenv，显式载入Git忽略
的.env_test；固定stock CLI/扩展/MCP SHA匹配既定产物。本轮配置只核存在、
普通非符号链接与忽略状态，未读内容、核认证/目标或确认服务可用性。未来获
启动授权后仍须脱敏核对有效配置，不打印/提交Key或所选内部服务/模型值。

| 单次启动限制 | 含义 |
| --- | --- |
| 4模型请求、3工具调用 | 7发现RPC另计，不自动修参/重试/其他题 |
| 每请求4096输出token | 最大累计请求额度16,384，非实际消耗或费用预算 |
| 出站131,072字节 | 完整body含转义/描述，超限拒绝 |
| 投影前入站2,097,152字节 | 原完整响应门禁 |
| 设置/发现/循环180秒 | 共享运行时预检在此前，非整个进程硬实时上限 |
| close/work顺序各30秒 | 最多请求60秒，首失败停止，无硬实时保证 |

没有输入token、收费或跨多个启动累计预算。请求store=true、truncation=disabled、
tool_choice=auto、stream=false、tools.strict=false，未指定reasoning effort；
不由这些flags推断真实后端、推理配置、留存或计费。若真实输出耗尽/截断，
按失败封存和原合同停止，不能自动增预算、裁掉完整envelope或再次运行。

未来若授权，只启动一次，失败或pending也停止、不自动纠正查询或继续其他题。
合法错scope可以执行，独立评估之后拒绝；完整答案必须与实际执行envelope
匹配。机械通过的live仍保留raw pending与退出1，不按退出码重跑；Lead语义
审阅与用户接受另写seal绑定衍生记录，原始status/失败不覆盖。

## 证据私有与公开边界

私有保存实际发现/context、来源与运行时、序列化请求、客户端尝试/HTTP状态、
白名单投影、提议参数/实际RPC/匹配回复/解码/完整状态与比较、最终说明、
评估/失败/清理和seal。分别核构造、尝试、HTTP收到、投影接受、意图、实际
query、回复/解码与native执行，不把发现、compile或意图当计算证据。模型名、
响应ID、numeric usage留私有，逐body hash如另测也留私有。

不保存Key、请求头、原provider wire或reasoning文本（含summary/encrypted）。
允许的可见文本/拒绝/函数参数完整留私有；numeric reasoning_tokens为计数而非
私有推理或费用证明。当前key回显检测/未知metadata与annotations剔除不是全能
脱敏。公开只固定v2计数/预算/绑定和有界审阅，不导出内部服务/模型/Key、ID/
usage、原始context/RPC/状态数组或模型自由文本。v2 semantic_acceptance始终
not_proven，不能代替独立Lead/用户接受；provider投影不当原wire或后端真实性。

本轮阻断fetch纯离线重放一致pending，原seal、68源、991实施私有文件保持；
不新MCP或模型进程。原176Bun/892断言、states六场景18负控/MCP19/23/25、
共享P1离线33/传输20保持，未重跑全套/native/tidy/远端CI。
本审阅包完成，下一仅建议另定states一次真实启动并先脱敏配置检查；states
真实自主构造/语义尚未验收，facts/history限定接受不能推导整体可靠率。

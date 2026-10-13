# states 最小完整构造声明研究

2026-10-13，基线c7908d8。建议沿用history的最小方式：**查询与编译工具各自
提供完整、相同来源的构造声明和一个紧凑通用示例，先不新增资源。** 五个根
字段和两种产品分支足以用短声明补齐。结构schema和八个示例留作研究参考，
不全量追加到模型请求。本轮不实施、注册资源、改模型上下文/提示词/预算，
不读私有配置、不启动MCP/原生compile或查询、不调用模型、不推送。

沿[OSSIE-REQ-001本地issue](../../../ossie-execution-research.md)、
[states实际上下文审阅](../p1-states-live-review/review.md)和现计划。根字段已披露，
但products.mode/values嵌套结构及compile独立合同未披露。人用
[每日状态文档](../../../population-daily-states.md)有示例，不等于进入模型上下文。
尚未发生states真实模型失败，不能预测补说明后一定成功或把模拟模板当自主构造。

## 两工具各自必须说明的语法

下式S/E/P是说明占位符，不可原样提交；真正JSON示例在后文文件。

```text
外层：{"concept":已发现资格概念String,"request_json":String(一个JSON对象)}
内层：{"start_date":S,"end_date":E,"products":P,
       "user_domain":"captured_users","transitions":"within_request_days"}
P = {"mode":"all"}
  | {"mode":"subset","values":[非空String,...]}
```

五根字段全部必需，无默认值；常量user_domain/transitions不能更换或省略。
各分支字段必须精确匹配，不允许未知/重复原始键、NULL/数字/布尔值替代。
内层必须是一个对象，不能把对象直接传给String参数，也不能再次编码成JSON
String值。concept在外层，精确匹配state-program的qualification_concept，不用
User、根观察概念或假想名称替代；实际正式规则与冻结绑定从program读取。
查询和compile使用同一语法和输入预算，各描述均完整，不只互相引用。

S/E为规范Gregorian YYYY-MM-DD字符串，年份0001..9999，日历合法且start<=end，
首尾都含、1..366自然日，可同日。**不沿用history的BC、多位年份、dates.mode或
[start,end)请求有效期。** subset有1..64个按原始身份唯一的非空UTF-8字符串，
每项解码后<=256字节，保留大小写、组合/分解Unicode及NUL；用合法JSON转义，
不trim、casefold或Unicode规范化。原生接受乱序并排序子集顺序，保持解码字符串不变，当前Agent要求调用前
已按原始UTF-8字节升序，这个调用者要求与原生门禁分开说明。
原始内层JSON<=16,384 UTF-8字节，包含空白/转义。MCP外层可忽略未知参数或
coerce非String，当前Agent则发送精确两个String；补声明不改变此边界。

## 状态与统计对象声明

日期边界同时定义输出日历及参与归约的观察；产品筛参与归约的观察，两者均
不缩小全冻结输入去重User域。只在其他
产品或日期出现的用户不被剔除。当前正式规则求BOOLEAN，任何TRUE胜出，非空
且全部FALSE才FALSE，缺失/全NULL/FALSE+NULL为UNKNOWN。每个User/自然日
完整返回一个状态，不延续、不推历史规则，不要求每个产品都合格。

比较只在请求内相邻自然日，全部相邻对包括未变化；changed按两个标签是否
不同，涉及UNKNOWN的标签变化也算变化，不是只看TRUE/FALSE或去重变化用户。
状态totals单位User/day，变化计数单位User/相邻日对，非观察源行/非NULL行/人数。
N捕获用户、D日期产生N*D状态和N*(D-1)比较，合计N*(2D-1)<=10,000，绑定时
按captured_User_count<=10000/(2D-1)门禁，query和compile绑定时都执行，
不能把compile不计算执行结果数量误称无容量检查。完整结果或计划内层JSON<=1MiB，
含元数据/请求/转义，超限拒绝，无分页截断；schema/请求形状不能保证此门禁。
compile只绑定/解释，不扫描/计算状态、比较或其数量；captured_user_count仅
加载元数据，projection_sql不含C++归约，不能独立重放或保证execute大小。

保留现有STATE_SCOPE_NOTICE和state-program/state-surface边界：通用资源里的
稀疏观察、请求内User投影、无日历补齐不能覆盖专用state合同。mcp_registered
原生声明不代替tools/list。共同语法必须和两工具的专用执行/plan说明相容。

## 最小放置方式与依据

| 候选 | 判断 |
| --- | --- |
| 同来源生成两份完整描述 | 推荐；补已证明的构造缺口，资源仍4、工具2、发现RPC7 |
| 描述加新资源 | 只有另有机器可读消费者需求才考虑；增加读取/上下文/封存义务，无当前必要性证据 |
| 改外层为对象 | 改接口与调用者，超过最小范围 |
| 只URI/contentSchema/compile引用query | 不足，不能依赖未进入上下文的说明或自动解码 |

固定协议2024-11-05的[工具规范](https://modelcontextprotocol.io/specification/2024-11-05/server/tools)
支持description/inputSchema；当前外层参数仍String。
[资源规范](https://modelcontextprotocol.io/specification/2024-11-05/server/resources)由应用
决定如何纳入内容；[JSON Schema内容词汇](https://json-schema.org/draft/2020-12/json-schema-validation)
属于注释，不能依赖它自动解码并验证String里的对象。不升级协议或API，不修改
严格native program字段，不把本项目请求合同伪称上游OSSIE通用规范。

候选完整文本见[声明草案](contract-candidate.json)。未来若获实施授权，可参考
history共同JSON源→两SQL描述生成/质量检查和实际发现精确对应方式；整合旧说明的重叠内容，生成两
工具同一完整构造片段＋各自精简carrier/执行或plan专用说明，不能机械附加
造成重复或让compile描述成返回states。候选已保留schema/API、integer-text
counts、完整BOOLEAN投影、当前规则/六hash核对及重载跨调用边界；不必泛化所有
profile为新框架。当前仅研究，草案文件不是运行资产或发布资源。

## 通用例与分层离线验证

[八例](generic-examples.json)假设另一program已声明EligibleVisit、Visit.signal>=7，
pilot未声明这个概念，不可直接执行其外层参数。例子不使用本题日期/产品/
资格概念或用户、数据行、状态答案/数量。覆盖all/subset、同日、闰日、跨年、
组合/分解Unicode与NUL、0001/9999边界；内层对象和真正String外层同时给出。
模型可见紧凑候选选择一个含五根字段的subset例，八例仅保留研究参考。

[结构schema](inner-request.schema.json)以required/const/oneOf/additionalProperties/
uniqueItems和日期词法pattern核形状，不冒充日期语义：不用format=date或字符串
maxLength代表原生日期/字节检查。日历合法性、先后/366天、重复原始JSON键、
UTF-8/代理字符/字节预算/产品排序、实际资格绑定和N*(2D-1)预算均另有门禁。

[验证](validation.json)：8正例通过schema及query/compile当前客户端；20结构
反例、5日期词法反例被双方拒绝；5日历/逆序/乱序/孤立代理字符反例schema
接受而客户端拒绝。366/367天、64/65产品、ASCII256/257与多字节256/258共8
预算边界，以及原始16,384/16,385字节和重复键分别核对。JSON.parse丢掉重复键
后可过schema，严格原始解析器仍拒绝；未知概念可过客户端形状校验，实际
native绑定要求当前资格概念。绝不能把这些形状通过等同原生执行/状态真值。

70来源绑定当前66运行源、native解析器、人工文档与上轮审阅；上轮states审阅4、
实施1224和history真实审阅23私有原件保持，封存states旧seal不改。本轮网络/MCP/
模型/native查询或compile均0；结构schema不进入旧模型context或源码清单。

## 后续候选与停点

下一候选为两描述共同来源的最小实施＋无模型验证，须另行授权。实际核
queries/compile两份完整描述、四资源/7发现、context/body对应及128KiB出站
原预算；保留User/day/比较单位、三值与全域语义、操作适用范围与旧behavior。
新共同来源、生成/解析依赖纳入封存/重放，旧seal保留，不重封成新语法验收。
验证错误分支/重复键/字节/日期/排序、独立答案隔离及必要state/P1回归，再先审
新版单题上下文/命令/预算/证据；真实模型另行决定，不能自动执行。

## Advisor反证与Lead核实

复用advisor-escalation的一名Advisor做一轮只读反证，支持两描述的最小方案，
不增资源。Lead接受并修正：日期也定义输出日历，query输出须明确条件，
原生仅排序子集顺序而非Unicode规范化，compile也做元数据容量门禁；拟整合
重复旧说明，并保留各工具carrier/schema/API/整数计数与当前规则/六hash核对。
Advisor的完整stateRequest验证因Node strip-only无法处理传递TypeScript
parameter property而受阻，仅较低层dailyRequest/封装校验成功；Lead使用项目
Bun运行完整query/compile客户端门禁，八例与所有分层负控通过，不是原生执行。
请求Astra/high，有效backend/effort/usage/billing未报告，均unknown；无写入、
子代理、MCP/native或服务模型调用。模型表现仍未验证。

本研究包到此停止，不实现、不推送。facts/history已分别限定接受；states仍未
真实验收，整体真实自主可靠性不由这些单次样例或本研究证明。

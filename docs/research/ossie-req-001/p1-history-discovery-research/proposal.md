# history 内层 JSON 最小发现声明研究

2026-10-13，基线9376318。建议保留两项 String 参数，在查询与编译工具中都放入
完整、可独立构造请求的共同摘要，先不增加资源。结构 schema、语义限制与八个
通用示例保留为研究参考；共享资源只作为有额外消费者需求时的备选。本轮仅为研究草案，未改工具描述、注册资源、修改模型上下文、
提示词、预算或运行代码，未读取私有配置、启动 MCP、原生编译/查询或调用模型。

沿[OSSIE-REQ-001 本地 issue](../../../ossie-execution-research.md)和
[history 实际上下文审阅](../p1-history-live-review/review.md)。现发现声明有 dates/
products 的语义，但没有 mode/start_date/end_date/values 的嵌套语法。源码和人工
文档已有这些字段，却未进入模型输入。模拟器提供合法模板不证明模型自主构造；
尚无 history 真实失败，不能预测补充声明后必然成功。

## 放置方案

| 候选 | 取舍 |
| --- | --- |
| 两工具仅补完整描述 | 推荐；语法较短，能独立解决构造缺口。未来由同一来源生成两摘要并验证一致，避免两份手工维护 |
| 两工具摘要＋共享静态契约资源 | 备选；有机器可读契约或其他消费者需求时再考虑。增加发现/上下文/重放义务，当前无必要性证据 |
| 改结构化外层参数 | 改变 String 参数契约和调用者，超过本轮最小范围 |
| 只加 contentSchema 或 URI | 不足；不能依赖字符串内容被自动解析、验证或加入上下文 |

当前固定协议2024-11-05的[工具规范](https://modelcontextprotocol.io/specification/2024-11-05/server/tools)
允许工具 description/inputSchema。[资源规范](https://modelcontextprotocol.io/specification/2024-11-05/server/resources)
由应用决定如何纳入资源，资源可读取不等于模型已得到内容。
[JSON Schema 内容词汇](https://json-schema.org/draft/2020-12/json-schema-validation)
是注释而非自动解析内层 String 的断言。本研究未升级协议，也不新增协议要求。

备选资源候选 `ossie://population/history-request-contract`，独立 schema_version
`ossie-history-request-contract-v1`，只适用于 history query/compile、原生API1.0.0。
静态语法版本/hash与原生 program 的快照/六绑定分开；实际关系名、角色、来源和
有效期仍以 program 为准。不将草案塞入严格校验的 program_json，或伪称上游
OSSIE 自带规范。发布时可参考 facts 普通表载荷方案，TEMP 跨资源连接失败的
旧证据须保留；普通表静态载荷也不是不可修改性保证，客户端仍须核对实际字节。

## 两工具必须各自说明的构造语法

下式 D/P/S/E 是说明占位符，不可直接提交：

```text
外层：{"relationship":已发现的精确关系名String,"request_json":String(一个JSON对象)}
内层：{"dates":D,"products":P}
D = {"mode":"all"}
  | {"mode":"range","start_date":S,"end_date":E}
P = {"mode":"all"}
  | {"mode":"subset","values":[非空String,...]}
```

每个分支的字段必须完全对应：不可省略依赖默认值、增加未知字段、重复原始键，
不可用 NULL、数字或布尔值替代 String。内层是一个对象，既不能直接向 String
参数传对象，也不能把内层再编码为 JSON String 值。relationship 位于外层，精确
匹配 history-program 声明，不放进内层、不从 Team 概念名猜测。

S/E 为有限 DuckDB DATE 规范文本，start<=end，查询日期首尾均含，允许同一天。
支持 BC、多位年份和原生有限日期边界，拒绝 infinity、非规范格式及无效日历；
原生文本上限32字节，不用仅四位年份的日期格式约束冒充全部支持域。日期筛选
只作用于捕获观察，不生成日历、不填缺失观察或沿用前日团队。历史关系有效期
始终是[start,end)，与请求范围的首尾包含不同。

subset 为1～64个按原始身份唯一的非空 UTF-8 String，每项解码后最多256字节。
保留大小写、Unicode、NUL等控制字符；不trim、不折叠大小写、不去NUL，使用
JSON合法转义，不将控制字符裸写在JSON字符串中。**原生接受任意输入顺序并
排序规范化；当前Agent客户端要求请求数组已经按原始UTF-8字节升序。** 摘要应
照实注明两层边界，不新增原生排序拒绝，也不悄悄放宽客户端。产品不在观察中
有见证可以得到空结果，不代表名称必须预先声明或有匹配观察。

原始完整内层JSON最多16,384 UTF-8字节，包含空白和转义；两工具使用完全一致
的请求结构和请求限制。MCP后端仍可能忽略外层未知参数或coerce非String，
当前Agent只发送精确两个String；新增文档不改变后端行为。

查询返回完整已执行(User,DATE,product,Team)集合和数量，compile仅绑定/解释，
不扫描观察、不匹配目标、不计算数量。全历史冲突与record_id重复在加载时校验，
请求不能过滤隐藏冲突。同目标不同见证保留，输出按完整关系去重；缺口无链接。
加载源行/用户/索引计数不替代link_count。完整输出最多10,000链接、1MiB内层JSON，
含请求/元数据/转义；超限整体拒绝，不分页不截断。正常结果与compile计划的
结构版本不同，不能混用执行标志或以计划当结果。

候选共同摘要完整文本见[契约草案](contract-candidate.json)，应在查询和compile
两描述中一致出现；两者再各保留现有execute/plan专用说明。不能仅让compile
引用query，而假设客户端或模型必然同时读取另一工具的说明。

## 通用示例与离线证据

[结构schema](inner-request.schema.json)和[八个示例](generic-examples.json)均为
研究文件，不进入运行资源。示例假定另一program声明 `membership_at_observation`，
实际pilot未声明这个关系，不能直接运行其外层参数。产品Catalog-A/catalog-a/商品/
cafe\u0301/caf\u00e9/unit\u0000east，日期2031/2032、BC与12000年均不使用验收题的日期/产品/身份。
没有数据行、团队/用户答案、输出或数量。四组all/range×all/subset覆盖分支组合，
另有同日、BC、多位年份以及不做Unicode规范化的两种原始拼写/NUL产品。每例同时给出内层对象及真正String外层封装。

[离线验证](validation.json)记录8例通过结构schema及两工具当前Agent门禁；14种
缺失/未知/错误分支与值类型/重复产品被双方拒绝。7个日历、顺序、无穷、零年、
格式、乱序产品、孤立代理字符反例说明schema可接受而客户端拒绝。64/65产品、
256/257 ASCII字节、256/258多字节产品和原始JSON16,384/16,385字节另分层核对。
原始重复键经普通JSON.parse丢失后可过schema，但严格原始解析器拒绝。

结构schema的oneOf/required/additionalProperties/uniqueItems只验证解码后的形状；
不验证重复原始键、UTF-8字节预算/代理字符、日期规范/有限域/先后、原始字节排序、
关系实际声明或结果真值。maxLength不能替代UTF-8预算，format=date不能覆盖BC/
多位年份。未知relationship可通过当前客户端形状门禁，原生精确绑定才拒绝；
不能以两门禁通过宣称示例在pilot原生可执行或成员正确。

68来源绑定当前64运行来源、原生解析器、人工文档与上一审阅记录；上轮私有
4个history审阅文件和4447个实现原件保持。没有新生产测试或原生实测；本轮
schema/客户端离线验证不冒充新MCP发现、编译或模型能力证明。

## 后续实施候选与停点

若后续获实施授权，先由一个来源生成两个完整构造摘要并验证一致，保留两工具/
两资源和5发现RPC、String参数及原生API1.0.0。每份描述都能独立构造四种分支
组合；紧凑内层对象＋外层String示例注明假想关系，八例不必全部加入模型上下文。
保存实际新tools/list描述与新context/body，来源清单覆盖共同摘要及依赖；旧档案
保持原schema/hash，不能重封为新描述后已验收。若将来另选共享资源备选，必须
显式更新HistorySession两资源断言、读取/上下文/来源绑定与重放；逐资源读取
才会由5变6发现RPC，发布URI本身不能证明模型已得到内容。

随后无模型验证实际tools/list、resources/list/read、context与serialized_body
对应及原128KiB出站预算；不能以草案存在或预测大小代替实际载荷检验。保护旧
history行为、compile无执行和source/index计数边界，覆盖重复字段/不合法日期/
产品身份与排序/字节上限/协调篡改/独立答案隔离；若影响共享P1重放，复验其
必要回归。模型实际运行仍需独立授权，并先审新版上下文/命令/预算/公开范围。

## Advisor反证与Lead决定

使用advisor-escalation技能一轮只读，问题是资源必要性、语法完整性和证据边界。
Advisor建议仅描述作为最小补救：两字段各两分支的缺口可由完整摘要解决，资源
增加新的整合义务但没有证明额外收益；Lead接受并将共享资源改为备选，移除
摘要中强制读取未注册资源的措辞。补充组合/分解Unicode原始身份例和紧凑String
封装示例。Advisor对源码对应高置信，对模型表现中置信；没有实际模型验证。
请求角色Astra/high，工具未报告有效backend/effort/usage/billing，均记unknown。
无写入、子代理或MCP/原生执行；Lead核对源码与离线例，不把建议当执行证据。

本工作包停在研究交付，不实现、不推送。新版facts已有用户限定接受，history与
states真实验收仍未执行；本研究不能当作整体真实验收完成。

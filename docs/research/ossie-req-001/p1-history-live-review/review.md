# history 真实运行前审阅：存在发现语法缺口

本轮完成已封存实际模型上下文、单题候选命令、调用预算和证据范围审阅。
**建议先研究最小发现声明，暂不进入真实运行。** 没有调用模型、新建 MCP 会话、
读取 `.env_test` 内容、修改运行实现或推送。新版 facts 的用户限定接受已另记
[独立衍生记录](../p1-contract-facts-live-controls/user-acceptance.json)，历史 Lead 记录、
原始 pending/seal 和首次失败均保留；当前接受状态以该新记录为准。

## 实际上下文与语法缺口

核对当前 64 来源的单题 history 封存轨迹：实际 stock MCP 发现两工具
`population_history_links` / `population_history_links_compile`、两个 history-program/
history-surface 资源，5 次发现 RPC。首请求由发现资源、原中文问题及工具 schema
组成，系统要求只读、String 参数、完整执行 envelope 与证据引用；独立答案、
模拟器查询模板和评估逻辑没有加入首请求。原问题为 2024-02-29 至 2024-03-03
（含首尾），仅产品 X 的观察对应历史团队，返回完整关系集合和数量。

外层 schema 已声明两个 String：`relationship` 与 `request_json`；资源公布关系名
`team_at_observation`。但内层只说明 dates/products 的 all/range/subset 语义，
**没有说明 `dates.mode/start_date/end_date` 与 `products.mode/values` 的字段名称、
嵌套结构和分支约束**。两个工具共享该缺口；compile 描述仅指向 query。
此外当前客户端要求 subset values 按原始 UTF-8 字节排序，这一调用者约束也未在
发现信息中说明。源码中的正确类型/模板不是模型可见的发现信息。

这是披露缺口，尚未发生 history 的真实模型失败，不能预言它必然失败，也不能
因为模拟成功就声称模型能自主构造。下一候选是研究两工具的最小语法摘要与
共享契约是否合适，采用与本题日期、产品和答案无关的通用示例；本轮没有实施。

## 统计对象与支持边界

观察身份为完整 `(User, DATE, product)`；输出关系身份是完整
`(User, DATE, product, Team)`。日期筛选首尾包含，历史有效期则为有限
`[start,end)`；只对已捕获观察匹配，不扩展日历、不延续前日团队、不填缺口。
产品筛选只影响观察，全部历史仍在加载时校验。不同 record_id 的同目标重叠
见证保留，完整输出关系去重；重复 record_id 或同 User 不同目标有效期重叠
拒绝整批加载，包括没有观察的用户。大小写、Unicode、NUL 等原始身份保留。

源记录数、历史用户数与索引段数是加载元数据，不是查询关系数量；用户数也
不能替代 link_count。查询才提供执行结果，compile 不扫描观察、不匹配团队、
不计算关系数。独立答案在执行后核对范围、完整集合与数量，不能仅核 JSON。
资源中原生 `mcp_registered=false` 不是本次实际 tools/list 未注册的证明。

查询内层请求最多 16 KiB、subset 最多 64 个唯一非空原始产品字符串、每项
最多 256 UTF-8 字节；完整输出最多 10,000 关系及 1 MiB 内层 JSON（含转义与
元数据），无分页或截断，超限拒绝。不是任意 SQL、加载/重载、资格筛选或通用
历史规则引擎，也不证明真实业务总体完整性。

## 命令与预算

私有目录已保存只含 history 的未执行候选命令并通过参数解析；清除继承的
三个标准 OpenAI 变量，禁止自动 dotenv，显式载入忽略的本地 `.env_test`，
指定固定 stock CLI/原生扩展和未占用的独立输出、stdout、stderr 路径。
只核对配置文件存在、普通非符号链接且 Git 忽略，没有读取内容或验证认证。
CLI、原生扩展和已安装 MCP 的 hash 与既有固定产物一致。候选不是调用授权；
语法缺口处理后，真正启动前仍须另行授权并脱敏核对有效本地配置。

单次启动最多 4 次模型请求、3 次工具调用，每请求 max_output_tokens=4096
（最多 16,384 请求输出额度，不是实际消耗或费用上限）；出站 JSON 128 KiB、
入站投影前响应 2 MiB，场景设置/发现/循环窗口 180 秒，运行时预检在此窗口前。
清理 close/work 顺序各有 30 秒等待窗口，遇首次失败停止，非硬实时保证。
没有自动重试、纠正错误查询或题间自动继续；合法错范围可执行，之后独立评估
拒绝。没有输入 token、费用或跨多次启动累计限制。

请求使用 store=true、truncation=disabled、tool_choice=auto、stream=false、
工具 strict=false；续请求携带 previous_response_id。代码配置不证明服务端
身份、留存、计费或完整原始 wire。已封存虚拟首/续 body 为 8,967 / 7,958 字节，
不是未来真实大小或 token 充分性保证。

## 证据范围与验证

私有证据保存发现/context、来源与产物绑定、序列化请求、尝试/HTTP状态、白名单
响应投影、工具提议/实际 RPC/回复/解码/完整执行结果、最终答案、评估、清理和 seal。
模型名、响应标识与数字 usage 留在私有区；不保存 Key、请求头、原始提供商响应
或 reasoning 内容。白名单中的可见文本/函数参数仍完整保存在私有区；凭据回显
防护针对当前 key，不能保证消除所有敏感信息。usage 计数不是账单证明。

公开只保存固定摘要与有界审阅结论，不导出内部端点/模型/凭据、响应 ID、usage、
原始上下文/请求参数/结果或模型自由文本。tool_calls 是循环意图计数，必须另外
核实际 tools/call、回复、解码与 native executed；发现 RPC 不是业务查询。
单题机械通过仍保留原 raw pending 和退出 1，再以独立语义审阅/用户接受衍生
记录收口，不改原 seal、不把 pending 等同真实失败或自动继续下一题。

[核对记录](receipt.json)绑定实际发现和虚拟传输档案；纯离线重放在阻断 fetch 下
得到一致 pending，两轮虚拟响应、一次真实 MCP 查询已封存，本轮没有新查询。
来源 64 文件及既有私有清单原件保持。无需重复全套测试、native/tidy/远端 CI。
三题真实验收当前只有新版 facts 获限定接受，history 与 states 尚未真实运行。

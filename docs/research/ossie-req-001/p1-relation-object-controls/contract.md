# 完整关系对象与逻辑解释

`observation-relation-program-v1` 现在提供 `ossie_relation_objects_json(relationship)` 和 `ossie_relation_compile(relationship)`。结果分别为一列 `result_json` 和 `plan_json`。关系参数按本体声明名称大小写无关查找，返回规范名称；NULL、未知名称拒绝。旧三个 profile 对这两个入口拒绝，新 profile 的旧对象/指标入口继续拒绝。

完整 link 为 `[sourceUser,date,product,targetUser]`，保留源观察完整身份，不能只 distinct User。结果范围为整个已捕获根人口中所选关系谓词为 TRUE 的 link；不附加旧 QualifiedObservation 条件，不额外套用每日查询日期/产品范围。多条规则为并集且同一根/目标不重复。当前形状只有同一映射 User 的单目标关系，不能据此声称支持任意实体图或跨事实查询。目标身份关系为本体 `id`，其映射字段为 `user_id`，两者分别声明。

排序依次为 User 原始 UTF-8 字节、实际 DATE 日数、产品原始字节、目标 User 原始字节。DATE 使用 DuckDB 有限 DATE 文本，允许纪元前和五位年份，不限定为每日请求的十位日期；后续 MCP decoder 必须遵循本接口。输出保留 NUL、控制字符、Unicode、引号和反斜线对应身份，通过 JSON 转义。上限为完整 10,000 links 和包含元数据的 1,048,576 UTF-8 字节；超限报 RESOURCE_LIMIT_EXCEEDED，不分页、不部分成功。该限制是最终 JSON 输出限制，不是峰值内存、临时元数据分配、MCP 外层报文或取消硬期限保证。

逻辑解释预检冻结四列投影的实际 VARCHAR/DATE/VARCHAR/VARCHAR 类型，输出 `full_link_projection_types_bound=true`。这不证明任意外层 SQL 或执行成功。compile 不进入执行、排序或成员编码检查点，`executed=false`、`members_computed=false`，不包含 links/link_count。计划步骤描述未来排序和完整编码，不是物理优化器计划；包含私有 AST 冻结依赖的 SQL 文本不能重放。结果和解释均保留规则、依赖、目标身份、范围与六项 snapshot/hash 追溯；不同调用之间若重载仍可能捕获不同批次。

每次绑定只捕获一次 bundle，实际投影、类型预检、谓词与执行共用该对象。prepared 结果和计划跨成功重载保留旧规则/批次；删除原表后仍可查询。执行/Fetch、排序比较、JSON 转义及结束检查原生取消，取消不返回部分成功。内部 native test observer 无 SQL/环境开关；确定性取消证据覆盖执行前、排序前、成员编码前及恢复，不宣称每个 CPU 指令可中断。

[最终实际产物证据](gate-final/verification.json)提供独立完整 18/7 link 对照、上游多规则并集/低于旧资格条件反例、声明重命名、大小写查询、prepared 重载、源删除、原始身份、空集、纪元前/10000 年、完整 10,000 links、精确 1 MiB/多一字节和转义膨胀。12 项检查、5 项拒绝；同 SHA 的加载完整约束回归和旧 A1–A8、观察、每日查询/MCP及两组无模型 Agent 演练另存。原生 capture 308 检查含 threads4/1 的三检查点取消恢复和 compile 零执行计数；26 SQL/982 断言、110 Bun/468 expects、格式/类型/架构通过。

保留初次关系名/字段名误断言、BC 文本输入误用、验收参数 TS 类型错误，以及快照序号 9→10 导致精确输出调节多一字节的原始失败。修正 BC 字面量为 DuckDB 要求的 `(BC)`；边界调节显式计入下次序号文本宽度，不放宽生产预算。Advisor 只读 diff 复核后，Lead 补排序取消/无字符串复制、明确投影类型声明和宽 DATE 控制；请求 Astra/high，实际 backend/effort/usage/billing 未知。

program 发现的 `objects_queryable=true` 仅指这些声明关系的完整 links，MCP 仍 `mcp_registered=false`。两个函数有注册描述和示例，注册计数为27/25名称；产物 verifier 已加入原跨平台 CI 命令，但未推送，也未运行远端 CI/Linux tidy。新关系 MCP、严格 decoder、无模型 Agent 场景尚未完成，不能以旧 MCP 回归代替。之后继续多事实/混合粒度、人群、窗口、待决业务语义和原五类 P1，整个 goal 保持未完成。真实模型调用为零，没有推送、合并或发布。

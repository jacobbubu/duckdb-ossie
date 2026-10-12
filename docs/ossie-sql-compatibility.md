# P1 函数与窗口兼容矩阵

本项目支持固定 OSSIE 0.2 快照中的有限表达式子集；不宣称完整符合
OSSIE_SQL_2026。本页按表达式所在接口区分支持范围。普通 DuckDB SQL 能解析
或执行一个函数，不证明模型、正式规则或人口请求允许使用它。

规范基准为提交 `8dd6732da354f22ca71f16d82626a46039ecdcd9` 的
[原始表达式文档](research/ossie-req-001/sources/apache-ossie/core-spec/expression_language.md)。
其聚合、条件、类型转换、排名/偏移/聚合窗口章节包含 REQUIRED 项；P1 的目标
是覆盖原需求第5节用例实际需要的函数，继续公开未支持项。兼容矩阵不是删减
尚未完成的状态变化、历史关系或整体验收要求。

这里分开三层证据：固定文档定义语言；解析/绑定门禁决定允许的 AST 和位置；
实际执行及独立完整行/精确金额对照证明有限场景结果。声明或解析成功不替代
后两层，计划与结果互相一致也不替代独立计算。

## 入口与语言范围

| 接口/profile | 允许位置与范围 | 不能据此推断 |
| --- | --- | --- |
| 通用 core `ossie_query` / `ossie_compile` | 字段/指标使用选中的 OSSIE_SQL_2026 或 ANSI_SQL；过滤器有独立函数开关和 lowering | 通用 core 方言转换不是各人口 profile 的白名单；普通窗口位置没有本页新增验收 |
| `gaap-scalar-population-v1` | 已声明标量概念/指标，严格人口表达式 AST；保护除法、NULL、规则/请求范围 | 不能自由访问数据集、任意函数、窗口或子查询 |
| `daily-observation-population-v1` | 固定五个观察聚合形状；正式资格为分类整数与整数阈值比较；每日集合做自然日见证归约；专用状态 SQL 保留 BOOLEAN/NULL 后归约全部 User/day 与请求内相邻比较，独立 MCP/无模型已本地验证 | 每日归约不是 SQL 窗口；没有历史有效期；新增代码 Linux/tidy 未复验 |
| `observation-relation-program-v1` | 声明的同根 User 关系调用、正向非递归 DAG、有限整数条件和显式 NULL 约束 | 关系调用是 ontology typed 语法，不能当普通 SQL UDF；没有任意聚合/窗口 |
| `multi-fact-user-program-v1` | 两个事实分别按 User 归约后组合；另有同源 User/date 行窗口 | 不支持原始事实连接后聚合、跨来源窗口、比率或任意 SQL 请求 |
| `historical-observation-relation-v1` | 直接物理字段与明确 typed 角色，完整有限 DATE 双源捕获/索引、program、完整历史 links、只绑定投影的逻辑 compile 与独立 MCP；时间匹配是项目算子 | 未执行通用 ontology derived_by，非空未执行规则拒绝；历史无模型与新代码 Linux/tidy 尚待验收 |

## P1 相关形状

“实测”只覆盖下列形状和固定 fixture，不泛化为所有类型、参数或表达式组合。
拒绝项在解析之后仍需通过 profile 门禁；不能因固定 DuckDB 已实现它而放行。

| 形状/规范位置 | 当前允许位置与语义 | 边界及证据 |
| --- | --- | --- |
| `SUM`、`COUNT(expr)`；规范核心聚合 REQUIRED | 多事实 `SUM(mapped amount)`、`COUNT(mapped User或amount)`；逐来源归约后共同 User 域。SUM 全 NULL 为 NULL，COUNT User 计来源行、COUNT amount 计非 NULL 值 | 不接受跨事实叶、裸物理列、嵌套聚合、DISTINCT/FILTER/ORDER BY 修饰或限定函数名；native 程序及实际 fact SQL/MCP完整 oracle |
| `COUNT(*)`、`COUNT(DISTINCT expr)`、`AVG`；规范 REQUIRED | 每日观察固定 `COUNT(User)`、`COUNT(DISTINCT User)`、`COUNT(amount)`、`SUM(amount)`、`AVG(amount)`；原标量 profile 的已声明聚合维持原合同 | 多事实声明全部拒绝这些额外形式，包括 COUNT DISTINCT；内部共同域分母使用系统 count_star 不代表声明 COUNT(*) 开放。旧观察 SQL/MCP 与新增 native 拒绝控制 |
| `MIN`、`MAX`；规范 REQUIRED | 不属于多事实首批指标 | 新增 native MIN/MAX 声明拒绝；不推断通用 core 所有类型的执行兼容性 |
| 算术 `+`、`-` | 多事实已声明指标展开，金额 SUM 组件同币种/scale 加减；精确 DECIMAL(38,2) | COUNT 与金额混加、除法、常数/任意标量包装拒绝。A 的 375−85=290；512组件/10000行极值另有 BigInt 对照和精度界限 |
| 比较、TRUE 集合交/并/差 | 多事实一个 regular 指标与精确常量；六个比较运算。每个原子先形成 TRUE 集合，再做二元集合运算 | 右侧 UNKNOWN 不属于排除集合；窗口指标、人群任意 SQL、NULL/函数常量拒绝。A/B/C 完整成员与独立共同域分母对照 |
| `NULLIF`、`COALESCE`、CASE、CAST；规范条件/转换章节 | 标量人口严格 AST 内的受限表达式；除法分母必须直接 NULLIF(expr,数值0)；类型与相位另经绑定 | 不能由标量支持推断多事实支持。多事实 CAST 和 COALESCE 包装实际拒绝；标量保护除法/NULL 的 SQL 与既有对应验收 |
| `LAG`；规范偏移窗口 REQUIRED | 多事实仅 `LAG(amount,1,NULL) OVER(PARTITION BY same_source.User ORDER BY same_source.DATE ASC)`，来源必须 User/date；返回上一观察行，包括其 NULL | 不一定上一自然日，不跳 NULL；缺 offset/default、offset2、非 NULL 默认、IGNORE NULLS、无分区、错误排序字段及不同来源粒度拒绝。跨月、缺日、全 NULL、空表及 BC/DATE 极值实测 |
| `SUM ... OVER`；规范窗口聚合 REQUIRED | 同源 User/date，明确 `ROWS BETWEEN n PRECEDING AND CURRENT ROW`，n为0..366字面整数 | n0/366执行通过；n367、RANGE、UNBOUNDED、FOLLOWING、倒序、产品粒度来源拒绝。先全部捕获计算窗口，后过滤显示日期；2月1日前值200、两行和250 |
| `LEAD`、`FIRST_VALUE`、排名及其他窗口；规范 REQUIRED | 没有新增允许位置 | native直接实测 LEAD/FIRST_VALUE/ROW_NUMBER/AVG窗口拒绝；其余排名与NTH/LAST等未逐项实测，不以共用拒绝分支冒充每项证据 |
| 日期/类型、自然日集合 | daily 请求为真实公历0001..9999、首尾包含、最多366自然日；fact/relation为完整有限 DuckDB DATE canonical文本、支持 BC/宽年份 | DATE 身份不截 TIMESTAMP、不猜时区；fact显示范围不补日。daily缺见证不能证明真实不合格；两个接口日期域有意不同 |
| 历史有效期、每日三值状态变化 | 已接受口径：每日三值归约与相邻自然日比较全接口本地通过；历史半开区间匹配/重叠拒绝/缺口/完整 links、逻辑 compile 与 MCP 本地通过 | 历史无模型、新代码 Linux/tidy 与整体 P1 验收仍必需；已有 LAG/ROWS 不替代这些验收 |

窗口采用已验证 AST 副本直接嵌入捕获来源，不把 DuckDB 窗口 ToString 诊断
文本重新解析成执行树。系统函数明确绑定 `system.main`；诊断文本可能省略
catalog，compile 的 `sql_replayable=false` 不能被忽略。Prepare 成功只证明绑定，
不保证完整结果满足1MiB预算；执行超限整体失败，无截断或分页。

## 通用 core 的转换与偏离

以下维持现有方言行为，并由 `test/sql/ossie_sql_2026.test` 核验其 fixture。
这些别名不自动进入人口规则白名单。

| 表达式 | 转换/边界 | 证据及限制 |
| --- | --- | --- |
| NVL、IFF、NVL2、ZEROIFNULL、NULLIFZERO | 降为 COALESCE/CASE/NULLIF 等树 | NULL、零值与方言优先选择实测；不覆盖任意 coercion组合 |
| CHARINDEX、STARTSWITH、ENDSWITH | 转为 DuckDB 对应函数 | fixture字符串结果实测 |
| CONCAT、GREATEST、LEAST | 显式传播任一参数 NULL，不采用 DuckDB 默认跳过 NULL 的行为 | NULL fixture实测；这是项目对固定语言的解释，见 limitations |
| DATE_TRUNC、DATEADD、DATEDIFF、单参 TO_DATE、CAST | 明确日期部分/类型 lowering；DATE_TRUNC/DATEADD 的 DATE 输入得到 DuckDB TIMESTAMP | 月末/闰日/NULL及实际文本实测；结果类型偏离记录于 limitations |
| REGEXP_LIKE、格式参数 TO_DATE/TO_TIMESTAMP、非法参数数或日期部分 | 语义未定义/实验性或参数不匹配，加载时拒绝 | SQL2026拒绝用例；可以提供定义明确的 ANSI_SQL variant，不能猜引擎语义 |
| 30天与1个月同时投影 | 固定 DuckDB 的 common_subexpressions 优化错误可能合并不同 interval | 旧SQL回归保留失败语义，文档记录禁用该优化器的办法；不宣称本项目修复了引擎 |
| 未识别普通函数 | 通用core可能保留到DuckDB binder，不属于本页验证子集；严格人口profile独立拒绝 | 不把“未拒绝加载”称已支持，不扩大完整语言符合性 |

通用 core 带引号标识符仍随 DuckDB 大小写不敏感；与规范 exact identifier 有
偏离。固定11个方言名称严格校验；两个可执行 variant 中 OSSIE_SQL_2026 优先。
其他合法方言仅保留描述，单独出现不支持执行。

## 实际证据与剩余验收

本矩阵恢复检查绑定扩展 SHA
`9ddb20dbc36bd92301b0a49178562f28a30b5e2ce774129964e7a9bd1216b5a9`，
固定 DuckDB1.5.6。新证据见
[函数边界复验](research/ossie-req-001/p1-function-matrix-controls/receipt.json)。
SQL全套包含SQL2026、标量、每日、关系、多事实；native分别验证程序门禁。
独立数值/成员证据沿用同SHA的
[多事实API](research/ossie-req-001/p1-fact-api-controls/review.md)、
[MCP](research/ossie-req-001/p1-fact-mcp-controls/review.md)和
[八场景无模型演练](research/ossie-req-001/p1-fact-agent-controls/review.md)。
原始证据与当时源码绑定，后续复验不能覆盖原始报告。

P1 尚需历史无模型、状态与历史新代码 Linux/tidy、最终同产物
全套质量检查与待推历史私有信息审计。本地命令通过不证明远端跨平台/tidy已运行；
无模型演练不证明模型自主选择。完整P1的验收范围保持原需求与现有计划。

后续[当前产物质量与回归](research/ossie-req-001/p1-tidy-controls/review.md)
记录样式修复后的66ec8a7c产物：Linux15编译单元tidy、六native、27SQL、
完整P0、四服务MCP与四组无模型控制复验。历史9ddb报告和原源码/文档
hash仍保留为当时版本；新receipt绑定当前源码，不能覆盖旧原件。

当前源码的[Ubuntu ARM64 功能复验](research/ossie-req-001/p1-linux-functional/review.md)
补齐此前未运行的 Linux 功能项：精确 ae0d0f4、Linux a5bef339，六 native、27 SQL、
P0/11 接口/四无模型通过。此证据不改变矩阵支持范围，也不代替远端 AMD64 CI。
[时间承载候选研究](research/ossie-req-001/p1-time-carrier-research/research.md)
明确剩余状态/历史的承载与预算问题，尚未实现或获得业务口径答复。

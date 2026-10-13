# 两事实指标、人群集合与观察行窗口

`multi-fact-user-program-v1` 面向同一User的两种独立事实粒度：User/date与User/date/product。先各自聚合到User，再组合金额和筛选人群，避免原始明细相连造成重复累计。原生加载、专用SQL接口、独立MCP及无模型Agent集成/重放已本地验收；[完整P1逐项本地验收](research/ossie-req-001/p1-final-acceptance/review.md)已通过。示例采用合成的加性USD观察值，不代表GAAP或累计年内数值。

加载[完整示例](../test/fixtures/population-fact-program/bundle.json)前，先执行[来源表脚本](../test/fixtures/population-fact-program/rows.sql)。manifest引用固定core/ontology/mapping，program只指定两个dataset/concept/显式currency，以及core中的窗口指标名。User身份关系必须相同且映射对象/完整关联树一致；金额实际类型DECIMAL(18,2)，User和product非空原始VARCHAR，DATE非NULL且有限，完整源身份唯一。错误、重复、取消或任一来源捕获失败都不替换当前有效快照；源改变需显式重新加载。

```sql
CALL ossie_bundle_load('test/fixtures/population-fact-program/bundle.json');
SELECT * FROM ossie_fact_program();
SELECT * FROM ossie_fact_users_json(
  '{"domain":"common","input_scope":"entire_capture","metrics":["balance","income","expense"],"cohort":{"operation":"all"}}'
);
```

共同域按两张捕获中都存在User行定义，与金额是否NULL无关。例中A收入375、成本85、差额290；B为80/30/50；C两边金额全NULL但仍在共同域；单侧D/E不在共同域。金额SUM全NULL保持NULL。COUNT(User)为来源行数，COUNT(amount)为非NULL金额值数，C分别是1/0。

返回一行 `result_json`，内层schema为 `ossie-fact-users-result-v1`。columns保留规范名、实际bound_type、encoding和单位；rows第一列为完整原始User，其后按请求顺序返回指标。金额/整数精确字符串编码，NULL为null。`common_user_count`是cohort之前共同User人数，`selected_user_count`为筛选后的完整User人数；两者及row_count都是整数文本。空筛选仍返回实际共同域人数，不能把结果为空解释为分母为零。没有比例指标。

人群条件只允许一个已声明regular指标与一个精确数字的比较：=、<>、>、>=、<、<=。COUNT条件常量必须是整数，金额常量最多2位小数，不能使用浮点、NULL、其他列、函数、任意SQL或窗口指标。允许使用没有投影的regular指标。选择名大小写不敏感，返回声明中的规范拼写，大小写重复选择拒绝。

```sql
SELECT * FROM ossie_fact_users_json(
  '{"domain":"common","input_scope":"entire_capture","metrics":["income"],"cohort":{"operation":"difference","children":[{"operation":"predicate","predicate":"income>0"},{"operation":"predicate","predicate":"expense>50"}]}}'
);
```

all仅有operation；predicate另有predicate；intersection/union/difference另有恰好两个有序children。每个原子条件归一为TRUE成员集合，再求交、并或有序差。右侧UNKNOWN不是右侧成员，因此不能用普通nullable NOT替代差集。所有集合运算仍限制在共同域内，union不能带入单侧D/E。

```sql
SELECT * FROM ossie_fact_windows_json(
  '{"input_scope":"entire_capture","windows":["previous","rolling"],"display":{"mode":"date_range","start_date":"2026-02-01","end_date":"2026-02-01"}}'
);
```

窗口schema为 `ossie-fact-windows-result-v1`，输出身份为User/date，columns第一、二列为user_id、ds，随后为选中窗口。输入是该窗口来源的全部捕获行，包含非共同User。先按实际User分区和DATE升序计算，再筛选显示日期，因此A在2月1日的前值仍为200，两行和为250。LAG取前一个观察行，可能不是前一天；前值NULL不跳过；无前行与前值NULL均返回NULL。SUM ROWS n PRECEDING..CURRENT ROW只观察最多n+1行，n为0..366，不补缺日，也不能推断状态变化或历史关系。

显示范围必须显式all或date_range；all不能携带日期，date_range必须有有序start_date/end_date。日期文本须与固定DuckDB DATE的ToString规范一致，包括 `0001-01-01 (BC)`、宽年份和全部有限极值；有限INT32_MIN的规范文本由项目显式解析（上游文本cast不能往返此值），窗口查询树保留typed DATE常量。不会把计划文本可重放作为承诺。不接受infinity、epoch快捷字、非规范简写或NULL。范围只过滤输出，不限制跨度或改变基础行。没有按日期/产品缩小聚合输入的选项。

两种查询都有对应的 `ossie_fact_users_compile` / `ossie_fact_windows_compile`，参数完全相同。计划schema分别为 `ossie-fact-users-plan-v1` / `ossie-fact-windows-plan-v1`，说明实际绑定类型、逻辑阶段和诊断SQL，executed/members_computed/counts_computed均false。不计算或返回common/selected/结果行计数，不提供物理计划或普通SQL重放保证。冻结表依赖存在于AST中；诊断SQL不能独立执行，固定DuckDB窗口文本还可能省略catalog。来源captured_rows属于加载事实，counts_computed仅指本次查询的计算计数。

`ossie_fact_program()` 返回 `ossie-fact-program-v1`，列出两来源的完整身份、物理映射与实际类型、捕获行数、child hash；regular/window原定义、单位、类型、窗口来源与实际函数catalog/schema；以及支持/拒绝边界、资源限制和五个SQL函数。发现不执行对象查询或计算共同域人数。native的mcp_registered=false不推断外部服务注册。通用旧人口接口拒绝此profile，其他profile使用原接口。

每次bind捕获同一个不可变父，计算分母与成员使用该父的两个child。预编译查询跨重新加载和来源表删除继续使用旧快照；执行始终经过私有binary Engine，不受调用者nocase或默认降序影响。同名数据库宏不能替换内部系统聚合、计数、金额加减或窗口函数。每个独立RPC会重新绑定：发现、编译与执行跨调用不保证相同快照，应比较返回的snapshot/content/core/ontology/mapping/manifest六项身份。

资源范围是两事实；每来源10000行/256物理列，完整身份预检和捕获包括未使用列；两份canonical合计16MiB，包含空表元数据。最多32个regular、16个窗口，声明名至多128字节ASCII；单规则16KiB、总规则128KiB、表达式32深度/1024节点、展开程序32768节点、指标依赖16层；人群64节点/16深度，完整UTF8请求16KiB。每响应最多10000行、完整内层JSON1MiB，发现与计划同样受1MiB限制，元数据/请求/转义内容均计入。无分页、无截断；超限以统一population错误契约拒绝，不返回部分JSON。canonical/JSON上限不是峰值内存或MCP外层wire字节保证。

独立[MCP示例](../examples/fact_server.sql)只公开`population_fact_users`、
`population_fact_users_compile`、`population_fact_windows`、
`population_fact_windows_compile`四工具和`ossie://facts/program`、
`ossie://facts/surface`、`ossie://facts/request-contract`三资源。运营者先创建来源并加载bundle；使用包含新函数的
本地产物时，将示例的community安装/LOAD改为该产物的LOAD，再在项目根目录
启动标准DuckDB1.5.6客户端。此本地验收不代表community已发布该能力。

客户端先读取program、实际surface和共享请求契约，再调用工具。四工具唯一必填参数都是
`request_json`字符串，内容与上文SQL请求相同。固定duckdb_mcp上游实际忽略
外层未知参数且可能转换非字符串；`limit`或外层日期不会限制结果，客户端必须
把范围放在受native严格校验的内层请求中。通用查询、导入/reload和其他profile
工具未在此服务注册。program的`mcp_registered=false`描述native发现的边界；
实际tools/list和surface才证明该服务的暴露。

[结果校验器](../scripts/fact_result.ts)保留精确文本数值与全有限DATE，核对请求、
规范选择、单位、来源粒度、六项快照绑定及完整键集合；计划不接受计算标记或
伪造成员。外层8MiB是本地decoder保护，不是服务器wire保证。schema或hash
一致仍不能证明金额/成员真值，实际[MCP验收](research/ossie-req-001/p1-fact-mcp-controls/review.md)
另以独立BigInt及完整身份oracle核对，包括10000行与先窗口后展示。

[无模型演练入口](../scripts/verify_fact_agent_offline.ts)以标准客户端运行实际MCP，
注入模拟Responses，覆盖八个有限场景并严格顺序重放。CLI参数依次为客户端、
新扩展产物、项目下全新的`.local-agent-evidence/...`私有目录、全新的receipt路径。
用`bun --no-env-file`启动；入口没有live模式或服务配置读取。原始RPC、上下文
和答案留在忽略的私有封存目录，receipt只输出选择的审计结论。

答案保留完整执行envelope与执行引用；仅解释场景不调用查询、不提供成员/金额/
计数。独立oracle核对完整行、精确金额、共同分母和先窗口后展示。八场景是
脚本集成验收，不证明模型自主选择工具、真实输出token预算足够或生产数据
业务口径正确。模拟reason/limitations采用固定声明，不能外推为真实模型文本审计。
[工作包证据](research/ossie-req-001/p1-fact-agent-controls/review.md)区分源文件/定义
绑定、事件对应和独立计算；hash封存不提供执行历史的真实性认证。

金额同币种/scale才可组合；只支持SUM(mapped amount)、COUNT(mapped User或amount)、同单位SUM组件加减。AVG、声明COUNT(*)、嵌套/跨事实聚合、聚合修饰、比率、混合单位、原始明细fan-out、递归程序、跨来源窗口、未声明SQL、日历补齐/状态/历史推断均拒绝。受限输入与表达式预算内，Prepare证明类型，实际精度仍应对照执行证据；超范围金额必须在加载实际类型/数据转换阶段拒绝，不把未构造的溢出场景称已验收。

本profile的冻结输入及表达式预算还给出金额界限：每个SUM组件绝对cents≤10000×(10^18−1)，1024节点指标最多512组件叶，所有中间/最终值≤512×10000×(10^18−1)<10^38−1。最大合法输入/行数/1023节点实际执行已与BigInt cents对照；更宽源类型与越界展开拒绝。此证明不覆盖未来放宽后的类型、行数、运算或预算，也不声称已触发运行时金额溢出。

本地复验（输出目录须全新）：

```sh
bun --no-env-file scripts/verify_fact_objects_artifact.ts build/release/extension/ossie/ossie.duckdb_extension /tmp/ossie-fact-new
bun --no-env-file scripts/verify_fact_stock_cli.ts /path/to/duckdb build/release/extension/ossie/ossie.duckdb_extension /tmp/ossie-fact-cli-new
```

各profile的函数/窗口位置与固定规范差异见[兼容矩阵](ossie-sql-compatibility.md)。

共享[请求契约](../examples/fact_users_request_contract.json)单独标识为
`ossie-fact-users-request-contract-v1`，只适用于users查询及compile；两工具描述
都包含完整集合构造语法。schema只检查显式解码后的结构，原生负责指标与
谓词数值绑定，不能把contentSchema当作自动验证。通用例子假设另一个program
声明order_count/refund_count，不含当前验收答案，也不能直接在本例快照执行。
载荷在服务启动时读入独立普通表，通过资源返回一行contract_json String；
客户端核对其内容和原始字节与本地已审文件一致，并封存实际发现/context。
这不是native六快照绑定的一部分，也不是可由其他宿主自动获取的保证。
旧原件和源hash保留，新发现必须生成新演练/重放记录，不能重封旧试验。

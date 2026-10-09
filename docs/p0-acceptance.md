# P0 有界本体执行验收

本验收针对固定 Apache Ossie `8dd6732da354f22ca71f16d82626a46039ecdcd9` 的 `0.2.0.dev0`、DuckDB 1.5.6，以及本项目两个有界 population profiles。它验证审核后的业务规则实际进入查询并改变结果。仅支持新根级 core 格式；不承诺完整 Ossie/JSON Schema、一般本体推理或真实 Agent 自主回答。

当前归档到自动第4轮：A1–A8相关计算、边界与追溯已实际核验，第5轮整体结论及可复现入口仍待收口。需求、授权与38项对应沿用 [OSSIE-REQ-001](ossie-execution-research.md)，本文件是验收入口。

## A1–A4：原始基准与规则变化

[实际完整查询报告](research/ossie-req-001/p0-final-population-artifact-results.json) 保存两个阈值下的全部对象ID、逐日结果、date/region/user_type全部组合及每个组的下钻对象/过滤、实际SQL/plan和聚合trace。

| 正式 YTD 阈值 | 对象/记录 | GAAP SUM，USD | 每日分组 | 三维分组/下钻 |
|---|---:|---:|---:|---:|
| 10 | 166 | 2265.86 | 15 | 101 |
| 20 | 63 | 1472.73 | 15 | 45 |

输入为固定475行CSV；产品X、半开日期区间 `[2026-03-01,2026-04-01)` 保持。阈值10在3月10日得到37条记录、1697.80 USD、平均45.88648648648649（两位小数45.89）；阈值20得到21条、1277.54 USD、平均60.8352380952381。

只改变正式 ontology 的 `derived_by`：两个资产去除该字段后结构相同，core/mapping哈希与请求模板不变。相同捕获content hash下SQL条件和对象/结果同步改变；snapshot ID不同代表两次明确加载。指标AVG自身不包含阈值，也未更改执行代码或提示词。

独立oracle逐行读取原CSV，以bigint分计算SUM；AVG使用明确的 `1e-10` 绝对容差。完整对象集合、每个组SUM/COUNT和下钻对象分别核对，不相加均值。USD来自原始workbook `metric!E2`及已审阅对应记录，扩展不换算单位；YTD分类不能证明“活跃/付费”。原CSV和正式资产保持不变。

查询结果无默认排序；验证器仅对对象ID排序以比较集合。plan来自实际生成AST，compile的 `executed=false` 和不可普通SQL replay边界保留；实际聚合trace核对同snapshot/content/ontology。

复现本段（需Bun依赖和本地release产物）：

```sh
bun install --frozen-lockfile
bun scripts/verify_population_artifact.ts build/release/extension/ossie/ossie.duckdb_extension --output /tmp/ossie-population-acceptance.json
```

生产扩展以C++实现，验证器以Bun+TypeScript运行；独立stock `@duckdb/node-api`加载真实产物，不回退静态内核或mock。

## A5–A6：边界与安全连接

[单表完整报告](research/ossie-req-001/p0-final-population-artifact-results.json) 的 `boundary_cases` 保存11行人工边界fixture的实际对象/每日结果和独立期望。阈值相等被纳入、NULL规则值不满足谓词；NULL金额不计入AVG分母。阈值10选出e01/e04/e07/e08/e10，阈值20选出e04/e07；日期3月31日保留、4月1日排除。未命中对象/分组返回空数组，未分组聚合为COUNT=0、SUM/AVG=NULL；没有自动补齐日期。

[表达式](research/ossie-req-001/p0-final-expression-artifact-results.json) 与 [DATE](research/ossie-req-001/p0-final-date-artifact-results.json) 对照独立边界：NULLIF零guard、NULL/空集、COUNT DISTINCT、明确转换、半开日期及配置。guard避免除零不保证结果有限，DOUBLE近似和可能Infinity明确保留。TIMESTAMP/时区的host实验不证明population接受这些类型。

[生产双表报告](research/ossie-req-001/p0-final-joined_population-artifact-results.json) 采用hand-derived bigint cents：6条fact、5个非NULL金额、SUM=55.01、AVG=11.002；派生对象f1/f2/f5/f6。24类实际拒绝覆盖重复/NULL维度键（包括未使用行）、NULL/缺失FK、重复/NULL身份、类型coercion、错误grain/关系/policy、跨数据库和预算等，失败保持旧bundle。

[core连接研究](research/ossie-req-001/p0-final-join_research-artifact-results.json) 独立展示fan-out/丢行风险和many-to-many/歧义路径/mixed-grain拒绝。它的research preflight不能替代生产双表验证：生产profile先捕获整批事实与维度，再检查唯一/非NULL/完整匹配，指定fact grain，按manifest的INNER/missing=reject策略冻结结果。缺失关系被拒绝，不会静默丢掉该事实；不扩展到LEFT、多事实或跨粒度聚合。

本轮实际运行原生19 state/42 grain/158 capture控制及840 SQL断言、23用例、0 skips。capture runner含生产代码上的受控并发/取消屏障；独立loadable双表verifier没有注入同一确定性writer屏障，两者证据各自限定。全量输入预算不代表输出/峰值内存上界。

## A7–A8：追溯、格式与传输

[固定定义独立进程复现](research/ossie-req-001/p0-round4-replay-results.json) 比较两次stock-client实际执行：每个阈值完整对象、aggregate、daily/groups及下钻、SQL和content/ontology hashes一致。各进程独立导入同一不可变CSV；集合比较消除未指定结果顺序，不以共用loader之间的相等代替独立数值oracle。

[plan](research/ossie-req-001/p0-final-plan-artifact-results.json)、[discovery](research/ossie-req-001/p0-final-discovery-artifact-results.json) 和 [错误](research/ossie-req-001/p0-final-error-artifact-results.json) 分别通过36、18、54控制：定义/模型/规则、实际生成AST/WHERE与声明时间角色、输入批次/粒度/连接说明；prepared保持旧批次、失败load不替换、源变化需显式reload。compile/query分开调用跨reload不原子，plan不是physical/binding保证，也不能直接作为普通SQL replay；服务维护完整执行引用。

[loader](research/ossie-req-001/p0-final-loader-artifact-results.json) 与 [字段对应](research/ossie-req-001/p0-final-correspondence-artifact-results.json) 验证新格式/未知版本/legacy诊断、合法metadata及365字段控制（35接受、330拒绝）。全部variant严格十一项方言enum/case；仅OSSIE_SQL_2026/ANSI_SQL执行，优先顺序确定。描述保留不表示执行；quoted identifier沿用DuckDB ASCII case规则，偏离明确记录。

[typed控制](research/ossie-req-001/p0-final-typed_population-artifact-results.json) 和 [实际MCP](research/ossie-req-001/p0-final-mcp-results.json) 验证24、330控制及106拒绝。INTEGER/HUGEINT/DECIMAL文本不先经JavaScript Number，实际绑定类型/有序columns与rows、SQL NULL/Boolean、非有限Float/DATE和空结果schema明确保留。API2.0.0/result-v1是本项目契约，Float仍近似，类型化传输不承诺调用方任意计算正确。

10,000对象、17列的完整typed响应实测wire为63,181,123 bytes；10,000分组wire为2,749,800 bytes，核对完整ID/组集合。不是任意大小或输出/峰值内存上界；输入限额不等于输出限额。分页尚未设计，仍待将来调研。MCP资源/工具在固定operator启动下运行，不允许tool换规则/导入/reload；真实自主Agent未测试，remote CI与Ubuntu tidy未运行。

本轮core MCP实际读取资源通过；stock CLI为40 PASS/0 FAIL/0 SKIP，使用真实loadable artifact（FAITHFUL）。这些是实际本地协议/功能测试，不证明远端发布或服务授权。

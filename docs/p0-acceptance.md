# P0 有界本体执行验收

本验收针对固定 Apache Ossie `8dd6732da354f22ca71f16d82626a46039ecdcd9` 的 `0.2.0.dev0`、DuckDB 1.5.6，以及本项目两个有界 population profiles。它验证审核后的业务规则实际进入查询并改变结果。仅支持新根级 core 格式；不承诺完整 Ossie/JSON Schema、一般本体推理或真实 Agent 自主回答。

结论：按用户调整后的新格式、YTD分类及SQL NULL/分组契约，两个有界profile共同覆盖的相关支持路径与拒绝路径通过A1–A8本地规则驱动验收。每个profile并非分别执行全部八项；此本地语义结论不代表全规范、真实自主Agent或P1能力通过。远端 CI 已有独立通过证据：`b2d486a` 的 [run38027392941 attempt2](https://github.com/jacobbubu/duckdb-ossie/actions/runs/38027392941) 共17项任务成功；[重跑归档](research/ossie-req-001/ci-artifact-timeout/final-state.json)与本地验收分开。需求、授权与38项对应沿用 [OSSIE-REQ-001](ossie-execution-research.md)，本文件是验收入口。

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

10,000对象、17列的完整typed响应实测wire为63,181,123 bytes；10,000分组wire为2,749,800 bytes，核对完整ID/组集合。不是任意大小或输出/峰值内存上界；输入限额不等于输出限额。分页尚未设计，仍待将来调研。MCP资源/工具在固定operator启动下运行，不允许tool换规则/导入/reload；真实自主Agent未测试；五轮验收时remote CI与Ubuntu tidy未运行，后续本地Linux质量证据单独记录。

本轮core MCP实际读取资源通过；stock CLI为40 PASS/0 FAIL/0 SKIP，使用真实loadable artifact（FAITHFUL）。这些是实际本地协议/功能测试，不证明远端发布或服务授权。

## 最终归档与复现

[机器验收索引](research/ossie-req-001/p0-final-acceptance-results.json) 逐项关联A1–A8的实际报告及hash；[最终控制记录](research/ossie-req-001/p0-final-control-results.json) 记录门禁、源码/输入/报告绑定、独立进程来源、失败与声明边界。索引检查数和hash一致性不是计算正确性测试；数值结论来自各verifier的独立oracle和实际执行。

实际重跑（先按[构建说明](../README.md#building--contributing)准备Bun依赖、DuckDB1.5.6 release产物，以及PATH中的stock DuckDB1.5.6 CLI）：

```sh
bun scripts/run_p0_acceptance.ts build/release/extension/ossie/ossie.duckdb_extension /tmp/ossie-p0-new-run
```

来源核对读取仓库内的不可变副本，原始绝对路径只作来源标签。原清单保持不变，新locator绑定清单哈希，为xlsx/docx与原需求指定相对副本；预期内容哈希仍来自原清单。核对20份来源及1份原需求，缺失、篡改或越界路径均拒绝。历史五轮审计只查了20份来源，不能据此声称此前已查原需求。输出目录须新建或为空。命令实际启动11个独立产物verifiers、population MCP、另一个独立进程population replay、core MCP及stock CLI，写出报告、日志和 `acceptance.json`。失败时停止、保留部分报告及 `failure.json`；不会覆盖仓库历史证据。入口先通过stock CLI安装duckdb_mcp和tpcds，首次安装需网络与可写的DuckDB扩展缓存；安装失败保留日志并终止。报告保存实际duckdb_mcp版本/hash，并非本项目固定的上游API规范。QwenPaw来源副本沿用仓库保存的Apache-2.0许可证。

原生与工具质量门禁另行运行，实际重跑入口不冒充已执行这些门禁：

```sh
make quality-local test
```

只核对本包Mac归档（需要本包同一产物及stock CLI在PATH以比对其hash，不重跑功能；其他环境应先生成该环境的新报告）：

```sh
bun -e 'import {verifyAcceptance} from "./scripts/verify_p0_acceptance.ts"; console.log(JSON.stringify(verifyAcceptance("build/release/extension/ossie/ossie.duckdb_extension", "docs/research/ossie-req-001/portability-macos"),null,2));' > /tmp/ossie-p0-archive-audit.json
```

历史p0-final/r2报告保留原样，不能拿当前源码/新产物重写其hash或称它们刚刚重跑；当前核对入口显式选择本包目录。

五轮历史本地证据包括840 SQL断言/23用例/0 skips，19 state/42 grain/158 capture，38 Bun tests/124 expects，365字段与54错误控制，真实MCP330检查/106拒绝和CLI40/0/0。实际单命令复现同样通过；C++产物未在五轮中修改，SHA256为 `4e3b2de0f88aed0ac9ff880e9e2fa0f3002ead3a8127f9f0e6c42667d6ff1307`。

后续跨环境工作包已在Mac ARM64与独立Ubuntu ARM64重建并复现：两端43 Bun/136 expects、19/42/158 native、840 SQL/23用例和完整stock/MCP/CLI通过；Ubuntu ARM64 clang-tidy、Mac及独立Ubuntu AMD64固定格式检查通过。最终产物分别为Mac `13d5317fb860f9ad81437a0a86cb8c996d7742a026f927f7b503c0fadf340c95`、Linux `4926122b73590b3724e6b46d561c5ae88a330f15f2ce32b80181ab2e8c612dd6`。完整结果20项跨环境比对一致，仍限固定fixture；[新控制记录](research/ossie-req-001/portability-control-results.json)保留失败和各环境实际报告。

五轮验收当时均本地提交，没有推送/合并/发布；随后用户授权推送开发分支并完成远端 CI，当前远端已验证提交为 `b2d486a`，没有合并或发布。此次调整范围的本地验收、跨环境及远端 CI 工作包已完成；后续分页、calendar/多期身份、一般ontology、混合粒度/窗口等P1仍需独立启动。独立真实Agent工作包现已完成三个有界最小用例并获用户接受Lead裁定，见[限定验收与数据局限](agent-minimal-acceptance.md)；该结果不扩张本P0验收范围，也不以脚本调用MCP替代真实Agent证据。本地Ubuntu ARM64构建及质量验证见[跨环境复现说明](linux-reproduction.md)，与五轮历史证据分开。

## 后续最小Agent验收裁定

三个真实最小用例的数值/规则/范围/引用核对通过，用户接受委托Lead的判断；[裁定](research/ossie-req-001/agent-live-corrected-controls/acceptance-decision.json)独立记录，不覆盖历史raw/pending字段。原始475行仅是合成演示数据，不能证明累计YTD一致性或跨日期重复实体语义。下一[研究包](research/ossie-req-001/multi-period-research/research.md)仅研究身份与观察粒度，当前P0生产支持/拒绝边界保持不变。

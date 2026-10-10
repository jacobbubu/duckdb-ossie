# OSSIE-REQ-001：跨日期实体与观察粒度研究

本包只完成研究与独立计算见证，不是新production profile、规范扩展或多期执行实现。沿原需求P1第2项（两个月、重复用户及状态变化），不创建第二backlog。固定规范仍为8dd6732；现有475行数据、生产定义及重复身份保护不改。

## 为什么选这个下一步

现有调整范围P0、跨环境、远端CI与三场景最小Agent验收已收口。当前fixture所有用户唯一，无法证明跨日期身份或去重。混合粒度/多事实指标会同时引入连接和聚合阶段；非递归派生关系/requires会引入其他执行语义；分页仍按用户要求留作未来调研。先研究实体与观察粒度，能用小数据揭示明确的分母与去重错误，也为后续能力定义统计对象。此选择只授权研究，不默许实现其余P1。

## 已证实边界

[现有profile](../../../population-profile.md)将整个批次的User ID绑定到唯一观察，不能通过单日筛选隐藏重复。代码[identity绑定](../../../../src/population/population.cpp:709)只接受一个identity关系、二元标量关系和单列主键；[现有重复控制](../../../../test/sql/ossie_population.test:121)说明筛选范围外重复也使整个批次拒绝。本轮阅读这些控制，未重跑扩展或改变行为。

[固定ontology规范](../sources/apache-ossie/ontology/ontology.md:270)允许多个二元标识关系；[mapping示例](../sources/apache-ossie/ontology/ontology.md:550)展示复合identifier和嵌套referent mappings。标准没有替本项目决定日快照唯一键、期间有效用户口径、金额分母或latest-wins。这些表达能力不能当现有executor支持证明。

## 候选语义与替代方式

候选是保留User以user_id标识，另建Observation概念，把日期、产品、分类值和金额放在观察粒度。Observation可考虑由(User,date,product)标识，但必须显式约定“一个用户/日期/产品最多一条日快照”；这是候选契约，不是从原475行推导的业务事实。审阅前不生成可执行core/ontology/mapping，不宣称schema兼容。多事件/修订场景可能需要其他观察标识，不能自动选择最新记录。

另一种表示是高元关系将User/date/product/value连接；需要比较关系角色和mapping树深度，现有profile并不支持。先研究实体化Observation，暂不实现两套表示。把User改名为(user,date,product)拼接ID会把人改成观察对象；静默DISTINCT、先日期过滤后验证会掩盖身份/值冲突，都不能解决问题。

## 独立计算见证

[8行表](observations.csv)全为新造研究数据；amount_usd是指定的合成金额，classification_value是无单位分类值，以>=10选观察。不称GAAP/YTD，不改原业务指标，也不把随机分类值当真实会计累计。跨月日期为2026-01-31/02-01，产品X/Y，A状态跨阈值，C金额NULL，观察ID单列明确保存。

选中o2/o3/o4/o5/o6/o8；对应实体集合{A,B,C}。规则定义的是“合格观察”；期间实体集合明确采用存在至少一条合格观察的投影，不能称每一天均合格、期末合格或追溯改变旧状态。

| 范围 | 观察数 | 独立用户数 | 非空金额数 | SUM | 非空观察金额AVG |
| --- | ---: | ---: | ---: | ---: | ---: |
| 全产品两日 | 6 | 3 | 5 | 31.00 | 6.2 |
| 只产品X | 4 | 3 | 3 | 20.00 | 20/3 |
| 1月31日全产品 | 1 | 1 | 1 | 10.00 | 10 |
| 2月1日全产品 | 5 | 3 | 4 | 21.00 | 5.25 |

每天独立用户集合分别{B}与{A,B,C}，人数1+3=4，期间人数仍3；产品/日期人数不可直接相加。C符合分类且有观察，但金额NULL：计入用户与观察数，不进入金额AVG分母，不能静默填零。

不同“用户平均”问题有不同公式：

- 观察金额平均：31/5=6.2。
- 先求每个用户的非空观察均值再平均：A=7，B=17/3，C=NULL；忽略NULL用户后均值19/3。
- 每个用户期间总额再平均：A=14，B=17，C=NULL；忽略NULL用户后均值15.5。
- 总额除以全部合格用户数：31/3；这把无金额用户也算入分母，是另一个必须明确命名的口径。

[普通SQL](independent.sql)没有LOAD扩展；[实际stock CLI输出](sql-results.raw.txt)与手算/Decimal逐行计算一致，来源与CLI哈希见[核验](verification.json)。首次归档脚本误把多行、多JSON数组输出当JSONL，解析失败；保留[first-parser-failure.raw.txt](first-parser-failure.raw.txt)，改用连续JSON解码后通过。不是生产失败或扩展多期验收。

## 必须拒绝的候选契约情况

| 情况 | 示例 | 候选处理 |
| --- | --- | --- |
| 同tuple完整重复 | o9重复o2的A/02-01/X/6.00/15 | 在最多一个日快照约定下拒绝，不DISTINCT吞掉 |
| 同tuple值冲突 | o10为A/02-01/X/60.00/15 | 拒绝，不能任取或latest-wins |
| 相同用户日期不同产品 | o2与o3 | 不属于同观察tuple冲突，但实体仍为同一A |
| 分类合格金额缺失 | o6 | 合格观察/实体计数保留，金额分母排除NULL |

前两项是设计拒绝要求，未执行生产loader测试；现有扩展对这种重复User批次按已有保护拒绝。不能以本SQL数值通过声称新形态已能load。

## 审阅、剩余与下一动作

触发advisor-escalation是实体/观察表示的架构取舍。复用既有Advisor一轮只读反证，requested Astra/high，effective backend/effort/usage/billing未知。Advisor认可选择此研究问题，要求显式日快照tuple契约、存在式用户集合、NULL和竞争分母、两产品及重复冲突，不批准生产设计；Lead核对固定spec/代码/控制并落实计算见证。

研究包完成。下一个可评审动作是确定最小生产切片契约：业务是否接受每user/date/product最多一条日快照、规则究竟定义观察还是期间用户、第一切片只提供哪些明确统计。之后才评估mapping/parser/plan/result/discovery改动。多事件、as-of历史、carry-forward、calendar补齐、窗口、混合粒度、多事实、分页仍留在原issue，不隐含支持。本轮不修改生产实现、不追加模型调用、不推送。

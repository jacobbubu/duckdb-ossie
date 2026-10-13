# facts、history、states 真实最小验收收口

2026-10-13，汇总基线dee13e1。**三个场景各自的合成fixture真实调用、Lead
解释/口径审阅及用户限定接受均完成。** states接受另存
[衍生记录](../p1-contract-states-live-controls/user-acceptance.json)，没有改原raw
pending、Lead当时receipt或seal。本轮只梳理状态、验证记录绑定和更新文档，
没有追加模型/MCP、读取配置或推送，也没有扩展实现。

沿[OSSIE-REQ-001本地issue](../../../ossie-execution-research.md)及
[现计划](../../../../ossie-duckdb-plan.md)。这是三场景最小验收工作包完成，不是
一般自主Agent可靠性、完整OSSIE/SQL2026、真实业务数据或已发布版本的证明。
原[第5节P1能力本地验收](../p1-final-acceptance/review.md)已另行完成；本包补真实
服务调用样例，不以更广未来研究重新宣称原P1实现未完成。

## 当前接受状态

| 场景 | 真实执行来源 | 受限结论 | 用户接受 |
| --- | --- | --- | --- |
| [facts](../p1-contract-facts-live-controls/review.md) | 782cdd7，64源 | 两事实独立归约、共同用户域/有序差、金额/NULL和前后分母对应 | [已接受](../p1-contract-facts-live-controls/user-acceptance.json) |
| [history](../p1-contract-history-live-controls/review.md) | fb85254，66源 | 有限半开关系、范围/缺口/同目标见证去重、完整四元关系/数量/原始身份对应 | [已接受](../p1-contract-history-live-controls/user-acceptance.json) |
| [states](../p1-contract-states-live-controls/review.md) | 70d7a6c，68源 | 全捕获用户域、完整日历与三值、全部相邻日比较和计数单位对应 | [已接受](../p1-contract-states-live-controls/user-acceptance.json) |

每题均2客户端请求/HTTP200、1实际query/匹配回复/解码、1完整最终答案，与
执行envelope和独立scope/成员/值机械一致，Lead审阅为limited_pass。被接受
三题合计6客户端请求、3实际query；发现RPC6/5/7另计，不当业务查询。

**原始试验历史不能删去：** [首次facts失败](../p1-agent-live-facts-once/review.md)
有1客户端请求/HTTP200，但集合JSON结构不合约，在派发前拒绝；没有实际
query或最终答案。补发现声明后经用户另行授权执行新版facts，后续history/
states也分别单题授权。全部已记录试验为4次入口、7客户端请求、3实际query，
不能报告为无失败的首批“3/3”或从这些适应性修订样例计算可靠率。

raw三个pending及公开v2 semantic_acceptance=not_proven仍保持原合同；当前
用户接受由独立seal/Lead receipt绑定文件承载，两层并不矛盾，不改原历史字段。

## 版本与证据边界

三题同一个原生macOS扩展产物，分别在64/66/68来源阶段运行，**不是最终
68源版本的一次三题真实联测**。后续新增描述/来源绑定使旧facts/history清单
与当前精确sourceHashes不同；不能改旧hash或重封原件让当前重放伪装通过。
每题当时已完成同源纯离线重放，当前汇总仅核seal/文件hash、记录/receipt/
接受绑定和最终envelope对应，不声称用现代码重新重放旧源版本。

最终68源的实际MCP与无模型三题回归已有独立
[证据](../p1-states-discovery-controls/review.md)：176Bun/892断言，P1离线33/
传输20，states六场景18负控/MCP19/23/25，类型、格式、两个生成一致性和
架构通过；它证明当前无模型集成，不替代同一最终版本的真实联测。

本次重新核三个私有审阅清单65文件、全部原seal、首失败seal及三接受文件；
当前68运行来源不变。哈希与对应说明记录完整/来源一致，不证明provider原wire/
实际模型身份、数据真实性、未公开的服务端续接/留存或计费。

## 数据与统计局限

| 场景 | 已审对象/单位 | 数据局限与不能推断的结论 |
| --- | --- | --- |
| facts | 捕获共同User域、筛前/筛后成员；金额精确USD/scale2，NULL不当0 | 小型合成两事实，只取已捕获行共同域；有序差仅排右侧TRUE，UNKNOWN保留不表示金额低或业务合格；未证明其他币种/金额尺度/事实数量、复杂集合及业务人群完整性 |
| history | 完整(User,DATE,product,Team)关系数，非用户数/源行/索引段 | 有限日期/单产品/单声明；缺口无link是数据见证限制，不证明现实无团队；同目标见证去重/大小写UnicodeNUL样例不证明所有冲突、全部日期域和高量场景的真实模型覆盖 |
| states | 状态User/day；变化User/相邻日对，非变化用户数/源行 | 全域为捕获User而非登记全集，UNKNOWN不当现实不合格；单产品范围每User/day最多1贡献观察，未直接覆盖同日FALSE+NULL/TRUE+FALSE/TRUE+NULL；固定规则/日期/产品，不推历史规则、其他规则或资源极值 |

所有答案都受冻结快照及正式声明约束。缺失/NULL、观察稀疏与产品范围分别
影响归约，不能用schema合法、计数一致或原始数值重算替代对象/范围/单位/
语义核对。真实模型没有直接运行compile流程、大结果或每种拒绝路径；这些
先前无模型控制不冒充新增真实覆盖。通用例明确是假想声明，不在pilot加载。

三题每次最多4请求/3工具/4096输出token，128KiB出站/2MiB投影前入站/180秒及
顺序清理不变；一次未耗尽不证明别题或大结果预算足够，没有input-token/
费用/跨启动累计硬上限。未独立验证后端身份/真实收费/原wire/数据留存；
白名单投影不含reasoning内容，numeric usage不作为账单或私有推理证明。

## 剩余工作与下一建议

| 类别 | 状态与必要性 | 授权边界 |
| --- | --- | --- |
| 本三题最小验收/接受记录 | 已完成，没有必须追加模型才完成的事项 | 不能自行加样本或声称一般可靠率 |
| 当前未推版本交付前检查 | 完整待推提交/blob/消息的私有信息审计、工作树与CI入口/来源对应核对，仍待做；单包扫描不替代全历史审计 | 建议下一无模型工作包，不推送 |
| 新提交跨平台/CI | 当前本地质量已通过；历史9dd7956的17项成功不覆盖本轮新代码 | 本轮不联网刷新或推送；只有后来明确授权推送，才检查由该推送触发的新CI |
| 当前本地子模块路径 | 普通git status仍因duckdb旧工作树路径失效；父仓用ignore-submodules检查，本轮未修复 | 后续完整仓库Git/原生构建前需处理；不表示已封存产物的功能验收失败 |
| 更广真实验收 | 同一最终源联测、重复运行、多数据/规则/模型、状态混合组合与大结果/拒绝路径均未覆盖 | 是独立研究/设计决定，不在本次最小接受条件或授权中 |
| 更广产品需求/发布 | 分页仍是现issue中的未来先调研需求；完整上游/一般图、合并、release/部署非本包范围 | 不创建第二backlog，不擅自设计分页或发布 |

本地缓存origin/codex/ossie-execution仍9dd7956，至本汇总基线dee13e1有22个
未推提交；这是缓存比较，不声称最新远端HEAD，本汇总提交另计。旧文档tip
[run38202969452](https://github.com/jacobbubu/duckdb-ossie/actions/runs/38202969452)
17项成功有历史[记录](../p1-agent-live-research/verification.json)，本轮未刷新
GitHub，也不把该成功归给这22个新提交。CI工作流已含无模型state/history/
P1离线及虚拟传输入口，尚无当前未推版本远端执行结论。

建议下一步先做无模型交付前审计，形成具体可审阅结果，再另定推送/新CI。
若希望扩大模型验收，先研究样例/独立答案/预算与证据范围，不因三题已接受
就自动追加调用。当前阶段按限定结论收口，模型/推送仍暂停在未授权状态。

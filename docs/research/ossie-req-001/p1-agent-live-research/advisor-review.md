# 一轮只读反证审查及Lead裁定

触发：旧模拟scene参数校验可能污染真实自主调用结论；预算/归档与运行-评估分离涉及架构取舍。复用一个Advisor，无子代理/写文件/服务调用。请求Astra/high；有效backend/effort/usage/billing未知。

Advisor结论：候选可继续收敛，未发现提示词/独立答案实质矛盾；不代表真实入口、预算门禁或模型验收已实现。只读按原fixture核算facts3共同/2选中、states30/20/10且T/F/U12/1/17、history7链接正确；未运行测试或核验全部hash。

| 反证风险 | Lead核对与落实 |
| --- | --- |
| fixed request污染运行 | fact decodeFactCall、historyToolArgs比较预设scene，history运行在RPC前调用。保持旧合同，新薄运行路径只校验实际请求；隐藏任务范围/oracle事后判定，合法错scope真实到达MCP，禁止纠正或反馈oracle差异 |
| 1MiB预算对象错名 | 已改为native内层JSON；外层decoder允许8MiB，再次转义可能先触发128KiB出站。必须检完整serialized body，不声称1MiB保证续接 |
| 保护尚未实现 | 已注明agent_loop外层JSON.parse丢重复键，provider无2MiB有界读取；后续实施前置而非当前保证 |
| stop/pending/空引用不明确 | Lead机械+解释通过才下一场；task_failed与pending区分，均暂停后续；failed无证据可[]，不虚构引用 |
| 场景ID/源绑定 | prompts实际question明示scene ID；最终receipt绑定审阅后提示词和独立答案hash |
| 续接重放范围 | 保留store:true/预算/留存未知边界；公开投影重放不能重建provider私有续接状态 |

补充Lead反例：expense>80也会在本fixture选B/C，必须验证实际成本筛选口径，而非只比较成员/金额。这个反例仅评估侧，不能作为模型纠正提示。

后续无模型控制必须覆盖合法错scope确实dispatch、请求+结果协调篡改仍被oracle拒绝、外层重复键、转义后出站越界、入站流超限、incomplete无可见输出、首场失败/pending停止。无需改业务能力或旧模拟合同；本次未实施。信心中高，范围限研究静态核对与Lead离线答案检查。

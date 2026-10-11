# 六场景关系 Agent 无模型演练

[最终选择性receipt](final-offline.json)记录六个独立真实MCP上下文：直接关系、依赖关系、同问题下上游阈值20、空来源、宽DATE、只解释不执行。共17次模拟completion、11次工具调用；真实模型请求0，不读provider配置。脚本固定选择关系和compile→execute顺序，最终回答从实际工具返回字段构建。完整四列links由独立CSV筛选/手列oracle校验，不是distinct User人数，也不以count相同证明成员相同。该结果证明集成，不证明模型自主选择、自然语言理解或现实业务验收。

[固定场景、提示词和预算](../../../../scripts/relation_agent_contract.ts)把oracle留在审计侧，provider context只有系统/问题/真实发现与工具信息。最终回答必须引用当前场景的实际执行证据，并匹配语义对象、规则、依赖、完整身份、集合、范围和批次；compile-only没有成员/人数，返回not_executed，不能说不支持。局部evidence ID必须与scene共同引用，不能跨场景移用。

演练沿既有agentLoop、Responses公共字段解析器和PrivateArchive hash容器，但有独立relation协议/源码清单。始终注入模拟transport，无live分支；预算每循环最多8round/12calls、正文131072字节、输出token请求参数2048、循环120秒。最多六场景合计48completion/72tools。模拟usage的零是占位而非测量，输出token参数不能证明真实服务会在此预算完成；正文限制只覆盖当前请求，不是服务端历史累计token上限。discovery另有RPC timeout，无整体进程/发现期限保证；归档无时间戳，不能据重放证明原运行时长。

原始context/setup/事件/review在ignored `.local-agent-evidence/relation-agent/coordinated2`，目录0700/文件0600，hash封存不覆盖原件。公开仅派生receipt、源码/fixture/runtime/hash和失败日志，不是完整raw transcript。重放不启动进程/网络或模型，逐事件消费RPC请求/响应/initialized通知、provider公共投影、工具调用/输出、最终答案、语义审计与终态；验证源码清单、场景清单、引用、完整oracle及没有剩余事件。封存与源码hash只证明相对于清单的字节一致性，不证明provider身份或外部真实性。

20份显式标记synthetic的重新封存控制均拒绝：前18份包括schema/mode/source/terminal/scene、RPC缺失/重复/id/次序、工具id/参数、function output/public projection、成员/trace/引用、事件缺失/尾随。部分成员/引用控制会先在重复记录对应处失败，因此不能据此独立证明语义门禁。最后两份协调修改RPC/tool/provider/final/review中的对应JSON载体，保持前面对应检查一致，分别确认失败消息来自独立完整成员oracle与必须引用实际执行结果的语义审计。独立incomplete回复保留失败档案并拒绝complete replay，原始成功seal保持不变。

只读Advisor建议保留上述证据界限并补协调反例；Lead实现且验证了目标拒绝消息。首次协调变换误把JSON-RPC字符串2.0改成2，提前协议失败；已限制为对象/数组JSON carrier，原始日志和私有失败档案保留。第一轮18拒绝receipt和原档案保留，之后源码变化导致其当前-source重放不再适用，最终receipt单独绑定新档案。请求Astra/high，有效backend/effort/usage/billing未知。

五个新增语义测试包含全部六正例、count-only/User去重/整数类型/规则/trace/引用/多字段伪造、schema合法且工具/答案同步篡改仍被oracle拒绝、compile-only、同成员错声明和隐藏limit。最终全部121 Bun测试/544expects及类型/格式/架构通过；当前关系MCP14检查/13拒绝和旧每日五场景/18伪造拒绝回归通过。C++/扩展SHA未变，原生308/SQL26/982/旧A1–A8及观察MCP沿先前封存证据，不宣称本轮重跑它们。新CI命令仅本地增加，没有推送/远端CI/tidy结果。

下一实施转向多事实/不同粒度指标、复杂人群与观察行窗口；待答状态/历史业务问题、其余全部P1、最终兼容矩阵/tidy/交付历史审计继续保留。新关系真实模型入口和真实Agent验收没有得到本goal授权。没有推送、合并或发布，整个P1未完成。

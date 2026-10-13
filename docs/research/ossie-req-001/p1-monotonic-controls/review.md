# P1 单调计时合同与无模型修复

本轮完成取消、耗时记录和等待边界的本地修复，沿本地问题OSSIE-REQ-001推进。原Linux失败记录与三个真实样例的限定验收保持；这次没有调用真实模型、读取实际配置、推送或重跑远端CI。

## 计时合同

P1使用`ossie-p1-timing-v2`，共享cleanup使用`ossie-p1-cleanup-policy-v2`。每场景从服务启动前开始一个`performance.now()`单调计时器，发现、模型循环、工具调用、评估和会话收尾共用原额度。记录保留未取整的elapsed；取消的唯一判据为`elapsed >= budget`。先算`start+budget`再比较会产生不同的小数边界，因此不采用该算法，也不增加epsilon。

提前触发的计时器重新检查剩余量，只在额度耗尽时取消；晚触发时，回调入场和完成后检查仍拒绝到期结果。零额度不开始场景RPC或provider工作。内层只释放自己的定时器，不改变外层截止时间；provider/tool使用懒回调，在预算检查后才开始操作。同步请求构造抛错也不会遗留活动定时器。

这些合同是等待与结果接受规则，不保证操作系统准时调度、硬实时总耗时或停止所有底层工作。[High Resolution Time 3工作草案](https://www.w3.org/TR/hr-time-3/)与[Node performance.now参考](https://nodejs.org/api/perf_hooks.html#performancenow)提供单调时钟的依据，具体截止与记录规则由本项目明确约定。

共享`beforeDeadline(epoch, action)`保留调用接口，只在入场采样墙钟剩余量，后续等待转单调。六类既有profile的elapsed归档格式本轮不迁移；其来源清单新增实际依赖的helper，使用新来源各自重新回归，不能沿用旧档案证明新代码。

## cleanup 与终止重放

cleanup每步开始独立30秒单调等待窗口。正常close/work两步累计请求等待额度60秒；注入测试callback时三步90秒。顺序执行、首失败停止后续，成功、拒绝和超时均释放本步timer。超时只结束调用方等待；底层操作可能尚未结束，因此不能宣称60/90秒硬总耗时或进程已被杀死。

场景评估完成或prepare完成后，session.close仍处于场景额度内。若此时超时，轨迹可以是`evaluation → failure`或`prepared → failure`；前者为task_failed，后者为failed。重放核对阈值、当前阶段、失败文件/错误身份、摘要、计数、停止位置和尾部事件，只返回失败诊断，不将已记录评估升格为成功。

事件游标仅在类型匹配后推进；错误信息仅带固定事件类别、场景、游标与数值预算/elapsed，不复制归档自由文本。缺响应、非超时失败和来源/文件/摘要矛盾仍拒绝。

## 旧归档

原始字节、seal和source_hashes不改。当前入口拒绝旧来源及缺失、混搭的计时版本。历史重放须使用调用者可信Git来源中的匹配原源码、依赖和产物SHA；不自动执行档案携带的代码。

已从可信本地Git提交3d78592提取原源码，对原positive与deadline的原字节副本离线重放，分别返回complete、failed，网络与重放子进程均0。当前入口拒绝这两份旧来源，seal保持。旧记录说明旧版本行为，不能作为新69项来源的运行证据。历史四份私有manifest共1714文件逐项复核不变。

## 验证与局限

最终来源绑定的无模型验证结果见[验证清单](verification.json)和[证据收据](receipt.json)。本地194个Bun测试、956断言通过；P1离线64控制、模拟传输20控制和观察/事实/每日人群/关系/每日状态/历史关系六类回归通过。新控制覆盖小数与提前/迟到计时器、墙钟跳变、零额度、懒操作、timer所有权与清理，以及prepare/评估后收尾超时及矛盾记录。两条收尾轨迹是明确标注的协调衍生控制，不是实际慢关闭重现。

使用已有macOS ARM64扩展产物，SHA为`ec6e314d9f5fa2446df7e5b2e9cbcaa29823599d5745738eef4f843b8dfdf18d`，stock DuckDB1.5.6；native C++本轮未改、未重编译。工作树迁移留下的专属子模块路径曾阻塞观察预检，原失败已留存；核对归属后仅修复专属本地Git配置，未改gitlink或共享checkout，最终观察回归通过。

Advisor只读复核指出小数边界、定时器与来源依赖以及收尾超时重放缺口；Lead实施并运行上述控制。复核没有独立重跑所有验证，有效后台身份、用量和费用未知。

本工作包完成，整体交付仍需新提交的跨平台与tidy CI。原[4768236 CI失败](https://github.com/jacobbubu/duckdb-ossie/actions/runs/38367223932)保留：原Linux私有轨迹不可得，本地可达缺口及修复不能证明那次运行的具体根因。下一步先审阅本地差异和交付证据，再在授权后推送并检查对应新SHA的Linux/macOS及tidy；不追加模型调用。三个旧真实样例的业务数据、单次调用和版本局限仍见[整体验收](../p1-three-scene-live-closure/review.md)。

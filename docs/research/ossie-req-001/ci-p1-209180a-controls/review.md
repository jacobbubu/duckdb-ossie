# P1 分支交付与新提交 CI 结果

`209180a`已推送到`codex/ossie-execution`，远端实际head与本次验收提交一致。
[本次跨平台、产物与tidy运行](https://github.com/jacobbubu/duckdb-ossie/actions/runs/38195621160)
完成，17项任务全部成功，包括最终CI gate。本次没有失败或重跑；
[完整任务及证据摘要](final-state.json)保留执行和跳过测试的区别。

Linux AMD64、macOS ARM64、Windows MSVC/MinGW实际SQL测试均为30用例/1,079断言通过。
Linux ARM64和macOS AMD64的上游任务只构建，测试步骤跳过；三个Wasm目标也成功。
远端Project controls为138 Bun/723 expects，format和tidy通过。原七native runner的
本地证据保留；不将其声称为本次CI已运行。

下载本次Linux AMD64和macOS ARM64的实际二进制、两平台完整独立验收报告，
46份关键报告检查通过，P0 A1–A8、每日集合/状态、关系、多事实/窗口、历史
全接口及六MCP/六脚本无模型重放均绑定各自产物。产物SHA：

- Linux AMD64：`9ade682db9f3e100555dc4ec79e069a58391a2a8c6a07b0348038f3b9b548f63`。
- macOS ARM64：`98b0aeba1f97f2e61d8570be43c671e3270d9ffe36120b15d5e6908ab82dab3a`。

原始CI日志、任务/产物元数据、下载二进制和公开报告由私下manifest封存；
没有下载runner中的原始私有Agent档案，没有读取.env_test或调用模型。
GitHub Issues已禁用，沿[OSSIE-REQ-001](../../../ossie-execution-research.md)本地记录。
日志中预期拒绝测试的error字段不是CI失败。无模型演练仍只证明工具集成和
有序重放；不升级为模型自主验收。Registry检查不代表本次尚未发布代码。

当前推送与新CI检查工作完成。此交付记录另做本地提交，远端已验证tip保持209180a；
未合并main、发布、部署或调用真实模型。分页仍先调研，真实Agent新增P1验收另行授权。

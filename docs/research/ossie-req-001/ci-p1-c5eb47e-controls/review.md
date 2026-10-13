# c5eb47e 推送与跨平台 CI 收口

本次新CI已整体通过：17个job全部成功，包括GitHub Linux AMD64/macOS ARM64独立产物验收、实际tidy、项目质量与最终CI gate。两平台P1离线各66项、模拟传输各20项通过，报告与本次下载的扩展SHA一致。此前测试副本权限造成的Linux交付阻塞，在本次提交的验收范围内已解除。

沿本地问题OSSIE-REQ-001记录。用户授权推送当前分支并检查新提交跨平台及tidy，禁止真实模型。分支正常fast-forward从4f991db推进至`c5eb47e6f20ccc265ef84840097b3c6dcee6b9b1`，推送后远端完整SHA一致；没有强制推送、CI重跑、产品代码修改或真实模型调用。本收口记录仅本地保存，没有追加文档推送。

## 新运行及实际步骤

[主流水线38402095010](https://github.com/jacobbubu/duckdb-ossie/actions/runs/38402095010)由本次push触发，完整head SHA为c5eb47e，run_attempt为1。最终状态completed/success，不能用旧运行或本地ARM64结果替代此处的GitHub AMD64证据。

| 检查 | 结果 | 核验范围 |
| --- | --- | --- |
| 九种分发构建 | 通过 | Linux两架构、macOS两架构、Windows两种、Wasm三个目标 |
| 原生格式 / tidy | 通过 | tidy日志实际执行make tidy-check，匹配范围覆盖整个src/ |
| 项目质量 | 通过 | 格式/声明一致性、TypeScript、架构，194 Bun测试/956断言/0失败 |
| MCP round trip | 通过 | 使用本run构建产物执行独立MCP检查 |
| Linux AMD64独立验收 | 通过 | P1离线66项、模拟传输20项及后续观察MCP/无模型演练全部执行成功 |
| macOS ARM64独立验收 | 通过 | 同一完整验收入口，P1离线66项、模拟传输20项及后续观察全部通过 |
| CI gate | 通过 | 所有必需依赖成功后执行并通过 |

两平台P1报告均声明真实模型请求0；模拟传输报告还确认实际网络0、实际provider配置未读及env_test未读。CI中的live入口使用mock transport，不代表发生真实模型调用。

两平台公开quality-reports、实际扩展和原始job日志已保存在忽略的本地证据目录。artifact元数据绑定本run和完整head SHA，报告artifact_sha256与下载文件逐项一致。没有上传、读取或重新封存runner私有原始Agent轨迹；公开控制报告和hash不能证明真实provider行为。

## 权限修复的证据边界

66项完整离线报告包括显式0700/0600复制后验证原seal，根目录/内层目录/文件权限放宽时拒绝，以及异步修改run.json触发原精确字节/来源变化错误且不产生摘要。两平台都完成了这些控制，归档生产权限检查与错误正则保持严格。

原[4768236有序事件失败](../ci-p1-4768236-controls/review.md)、[4f991db副本权限失败](../ci-p1-4f991db-controls/review.md)与后续[本地确定性修复](../p1-copy-permissions-controls/review.md)分别保留。本次新运行通过不能反向确认原4768236的具体计时根因，也不抹去失败记录或迁移原三题限定真实验收的版本/数据边界。

本次运行较长，AMD64的Build Docker image步骤从13:06:46Z至14:06:11Z，最终success。此前API显示该步骤运行中，完整活动任务日志不可用，浏览器未登录也无法取得实时日志；没有据此判定失败或触发重跑。阶段耗时说明主要等待位置，不单独证明网络、CPU或缓存根因。

## 交付状态

本推送与检查工作包完成；新c5eb47e的跨平台/tidy CI收口成立。它证明这套固定fixture、严格权限/字节保护、无模型MCP回归与分发构建通过，不等同新真实Agent验收、任意数据/平台保证或整个OSSIE规范实现完成。

当前没有剩余CI阻塞。下一步先审阅本收口记录与既有计划的剩余交付事项；文档追加推送、合并/发布及新增模型调用仍按各自授权推进，不在本轮自动开展。

[最终状态](final-state.json)、[证据收据](receipt.json)、[验证及产物绑定](verification.json)。

# 4f991db 推送与跨平台 CI 验收

当前分支已推送至`4f991dbfdc31191a3f80d23ac459cb698fb8a900`。本次新CI终态失败：17个job中15个成功，Linux独立产物验收与最终CI gate失败。九种分发构建、实际tidy、格式、项目检查、MCP round trip及macOS独立验收均通过。当前仍不能收口跨平台交付。

沿已有本地问题OSSIE-REQ-001记录。用户授权推送当前分支及检查新提交CI，禁止真实模型。本次正常fast-forward推送，没有强制推送、重跑、产品代码修改或模型调用；本报告仅本地保存，没有追加推送。

## 本次运行

[主流水线38385250440](https://github.com/jacobbubu/duckdb-ossie/actions/runs/38385250440)由本次push触发，run_attempt为1，完整head SHA与远端分支一致。不能以此前或本地成功结果替代本次Linux结果。

| 检查 | 结果 | 核验范围 |
| --- | --- | --- |
| 九种分发构建 | 通过 | Linux两架构、macOS两架构、Windows两种、Wasm三个目标 |
| 原生格式 / tidy | 通过 | tidy实际执行make tidy-check，匹配范围涵盖整个src/ |
| 项目质量 | 通过 | 格式/生成声明一致性、TypeScript、架构，194 Bun测试/956断言/0失败 |
| MCP round trip | 通过 | 加载本run构建的扩展进行检查 |
| macOS ARM64独立验收 | 通过 | P1离线64项/模拟传输20项及后续观察检查均成功，真实模型请求0 |
| Linux AMD64独立验收 | 失败 | P1离线控制的末尾公开摘要篡改测试出现权限错误，未写出完整成功报告 |
| CI gate | 失败 | 正确拒绝存在失败依赖的运行 |

两平台实际扩展文件及本次artifact元数据已下载并绑定。macOS两份P1报告的artifact_sha256与下载文件一致。Linux没有P1离线/传输报告，后续P1模拟传输和观察检查被跳过，不计为通过。已上传的公开quality-reports和原始job日志保持；没有上传或读取runner私有原始轨迹。

## Linux错误与范围

本次错误不同于原4768236运行的`ordered event expected: simulation_response`。当前堆栈定位`verify_p1_agent_offline.ts:822`，进入末尾的公开摘要“来源在异步导出时变化”控制。此控制先用`fs.cpSync`复制positive档案，再启动export，并修改副本run.json；断言预期`archive byte hash mismatch`或`source snapshot changed`。

实际错误为`private evidence permissions must be 0700/0600`，目录模式493（0755），预期448（0700）。`verifyArchive`在检查这个副本目录时已拒绝，因而没有到达该测试想验证的字节/来源变化边界，错误正则不匹配导致测试失败。私有归档权限保护本身发挥了作用，不能为取绿放宽它或把权限拒绝算作来源变化控制通过。

日志与源码支持“测试复制副本不满足归档权限合同”的具体判断；没有独立Linux复制行为复现，不能据此泛称所有Bun复制行为或原始档案权限都有问题。当前执行已到达末尾控制，依源码执行顺序可推知此前控制返回，但Linux完整64项成功报告缺失，仍不宣称整包通过。旧Linux计时具体根因未知，当前macOS/新Linux推进也不能反向证明原运行根因。

## 下一步

先对复制测试副本的目录/文件权限做最小、确定性复现，区分原始档案与独立衍生副本；再修正测试夹具创建方式，使副本满足0700/0600后才进入来源变化测试。应分别验证权限不符仍拒绝、合法权限副本可校验，以及异步修改确实触发字节/来源拒绝且不写输出。保留原seal和两次失败，不把仅接受更多错误类型当修复。

本轮仅推送与检查，未实施上述修复、提高预算或自动重跑；追加修复后须新来源无模型验证，再以另行授权的新推送验证Linux/macOS与tidy。三个已限定接受的真实样例仍按原版本与数据边界保存，不追加模型调用。

[最终状态](final-state.json)、[证据与绑定](receipt.json)、[验证清单](verification.json)。

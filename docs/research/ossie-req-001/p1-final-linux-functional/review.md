# 当前时间能力与完整 P1 接口的 Linux 功能验收

当前实现已在独立 Ubuntu 24.04 ARM64 源目录重新构建，通过原生、SQL、实际
stock MCP 与六组无模型封存重放。[receipt](receipt.json)绑定 40 项成功阶段、
逐文件 source hash、保留的 Linux 二进制及私有日志/报告/原始封存记录。
新状态/历史代码的 Linux 功能缺口已补齐，原五项 P1 统一审计仍需完成。
这项结果不代表远端 CI 或自主模型验收。

Linux 产物 SHA256：
`71c1ccb9c27e57e837d40df6359e8851f8ff6ea5435867b1985cfff199e06692`。
七个原生 runner、30 SQL/1,079 断言、138 Bun/723 expects 通过。P0 A1–A8 与
观察/每日集合/状态、非递归关系、多事实人群/窗口、历史载入/links/compile/program
全部实际产物验证通过，六个独立 MCP 及六组有序封存无模型重放通过。历史 MCP
含完整 10,000 链接身份；历史七场景/20 模拟响应/13 工具调用、29 负控制通过。

沿用工具链镜像 `89b3d0f09972`，固定 DuckDB/ci-tools/fkYAML 提交及 Bun1.3.11。
独立 detached 378a8dc 源加唯一 population.cpp 质量补丁，与 Lead 3437cc8 的实现
源一致；receipt 给出逐文件 hash，不把文档提交差异当代码差异。旧 Linux checkout
和证据没有覆盖，新目录复制缓存后重新配置、构建；实际 616 步 Ninja 构建及新
native 目标成功，不以缓存存在代替当前证据。MCP/tpcds 缓存来自旧成功容器，
未悄悄改上游 MCP 版本。

没有挂载主工作目录、私有配置或原需求临时目录，没有读 .env_test。模型变量
显式取消、Bun 禁止 dotenv，未调用模型。3,301 份六组原始封存文件保留在独立
ignored 目录，542 个源/日志/报告/二进制由另一清单绑定。公共 receipt 只给摘要
和 hash，不公开原始会话。另有[16 编译单元 tidy](../p1-final-tidy-controls/review.md)
以及 macOS `622b4796` 的同范围回归，两平台各有实际产物与对应 hash。

下一必要为原第5节五项逐条审计、支持/拒绝边界和交付状态统一、未推送历史与
私有信息检查。goal active，未推送、合并或发布。

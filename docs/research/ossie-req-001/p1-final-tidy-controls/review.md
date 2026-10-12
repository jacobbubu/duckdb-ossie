# 新时间能力的最终 C++ tidy 与 macOS 回归

当前状态、历史和窗口代码已在 Ubuntu 24.04 ARM64 完成全部 16 个项目编译单元的
clang-tidy18 检查，使用固定 DuckDB 配置，最终 exit0、项目诊断0。
[扫描范围与结果](verification.json)和[同产物 receipt](receipt.json)分别记录质量
和 macOS 实际回归；当前 Linux 功能构建仍在运行，不能据 tidy 通过推断其成功。

首轮 105 个唯一诊断保留：104 个控制语句缺少花括号，一处只读循环复制字符串。
clang-tidy 在独立源码副本应用 210 个 fixits，Lead 用 clang-format11 整理，并核对
字符串字面量组不变；忽略新增花括号后，只剩一个 auto→const auto& 声明变化。
初次两种词法比较假设错误（相邻字符串因格式重排分拆、diff opcode 合并），
修正检查后通过；这两次检查误报不是生产回归，也不代替实际执行验证。

复用一个只读 Advisor 核对花括号归属、yyjson foreach 边界及 initializer-list
引用生命周期：未见 else/continue、捕获/发布/取消顺序变化；被引用字符串副本
在整个循环中存活，没有写入或保存引用。少一次分配可改变资源耗尽时机，未把
这个修复宣称为总体内存保证。请求 Astra/high；有效 backend/effort/usage/billing
未知。Lead 保留实现与测试责任。

新 macOS ARM64 产物 SHA 为
`622b4796ea1caa42c1f241c5087eee7edc830262157be39f866414581a8fcd7a`。
七原生 runner、30 SQL/1,079 断言、138 Bun/723 expects、类型/TS/C++ 格式/架构
通过；同一新产物的 P0、全部专用 SQL/解释/发现/stock 控制、六个 MCP 服务和
六组封存无模型重放全部复验通过。旧时间能力报告保留旧 SHA，未覆盖原件。

Linux 检查使用独立 detached 378a8dc 源加这一项明确源差异，并核对 population.cpp
与 Lead 当前文件 SHA 相同；35 个项目编译命令含重复目标，不冒充 35 个独立
编译单元。旧 checkout/证据保持，新源码用独立目录与已有工具链镜像，不挂载
主目录或私有配置。功能构建的终态和新产物仍需验证；其后原五项 P1 最终审计、
交付文档及待推历史私有检查保留。goal active，无真实模型或推送。

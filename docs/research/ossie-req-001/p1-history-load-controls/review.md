# 历史双源加载与原子发布切片

历史 profile 现支持 typed 双源绑定、完整捕获/索引与 `ossie_history_program()`，
但尚未支持历史 link 查询、compile 或 MCP，声明明确为 false。接口边界见
[加载契约](../../../population-history.md)，[加载验证](verification.json)及
[同产物 receipt](receipt.json)。本工作包完成不等于 P1 完成。

最终 macOS ARM64 产物 SHA256 为
`65321694903bb90173776c6e6ed9a272b20de96e9ff596f972c663c6f2fdcdfa`。
4 项控制/13 项拒绝包括实际角色/类型及原件与 index 数、空来源、有限 INT32_MIN、
两源各 10,000 行完整载入与各自 10,001 拒绝；无观察 User 的全历史冲突、重复 ID/
观察元组、空区间、错目标类型、内置值类型伪装成实体、未执行 ontology 规则、
未知算子、第二表丢失、实际端点类型错配等均拒绝。每次失败后完整旧 program
与之前一致，不隐藏范围外错误。输入总行数 20,000 与人口分母/最终 link 数分开。

原生捕获测试 428 检查、threads=4/1，新增两处取消均是原生 INTERRUPT，旧发布
状态保持。并发在首次捕获后用一个事务修改两表，本次父 hash 仍等于旧原件，
显式再载入才变化；prepared 的完整 metadata 与 prepare 当时原件精确相同，
两源删除后仍可读取并耗尽流，当前父不同。该证明仅针对 metadata/父引用，不能
外推为未实施的历史 link 正确或所有引用释放后无泄漏。

28 SQL/1,049 断言、138 Bun/723 expects、类型/TS 格式/C++ format11/架构通过。
同一最终共享产物上的调整范围 P0 A1–A8、观察、每日集合/状态、关系加载/对象、
多事实加载/对象复验通过。现有完整 MCP/无模型与其他独立 native 的新产物复验
尚待最终接口工作包；receipt 不将前一 SHA 的证据算到此 SHA。CI 加入历史加载
验证命令，但未推送或远端运行。Linux/tidy 和全五项 P1 统一验收保留。

失败原件保存：元数据 lambda 索引的双方括号编译失败；加载驱动错写统一错误码
预期（原契约按 constraint/identity/mapping/snapshot 分开）；新测试误用 QueryResult
的 GetValue 后误强转 Materialized，产生编译/INTERNAL 类型不匹配；改 Fetch 后
检查单行单列/NULL、精确原件、耗尽且无末尾错误，再用新查询。最早编译失败后的
旧 native/SQL 运行不作为最终通过证据。最终在新增 built-in EntityType 保护后
重编译、重跑并归档，前一选择性 receipt 与原件保留，未覆盖旧证据。

复用一个只读 Advisor 对两次 getter 失败及捕获/父生命周期做有限反证，Lead
按固定头文件/既有测试核实并修复。请求 Astra/high；实际 backend/effort/usage/
billing 未知。最终 297 个源/日志/报告由私有清单绑定。下一必要行动是完整历史
link/compile、严格请求与遗漏拒绝/字节预算控制，然后 MCP/无模型、新产物全回归
和跨环境/全 P1 审计。无真实模型、推送、合并或发布。

追加边界核对将历史旧入口拒绝放入 LegacyCurrent 所持的同一父快照；单独 Current 检查
存在并发发布间隙。双向历史/旧观察切换 gate 控制证明按已绑定父做支持/拒绝，
新产物重新执行旧回归。前一类型保护与选择性证据保留，当前 receipt 优先。

# 关系 MCP 的本地验收

独立 server 将已完成的关系 SQL 入口发布为两工具、两发现资源，旧三个服务未改。[实际 MCP 记录](gate-mcp.json)在固定 DuckDB1.5.6/duckdb_mcp6f9c4ba和同一4ba513f7扩展上通过14检查/13拒绝。六个独立会话覆盖阈值10/20、空、宽DATE、原始身份、10000links；每个会话同时测试直接/依赖关系的计划和完整集合。完整18/7 links与CSV独立对照，其余人工/生成完整集合对照；当前请求的规范关系、发现中的规则/依赖/谓词、根/目标身份及六hash均匹配。

两工具都拒绝未知关系、缺参数、数字或对象关系参数；未开放任意SQL/旧查询/每日集合/重载工具。未知关系保留原生INVALID_REQUEST，外部缺参/工具拒绝不伪装为项目错误。多余date/products/limit确实被上游忽略，仍完整返回18links；描述明确不给额外范围语义。native program 静态false不表示当前外部服务未注册；实际tools/list与surface分别验明。

宽DATE实际MCP包含最小/最大有限日期、纪元前与10000年。Advisor核对固定源码发现负无穷哨兵是-2147483647，Lead修正原decoder下界并补端点/相邻越界测试。Advisor也指出同成员关系会掩盖返回名称错配；已按请求解析声明并要求canonical name匹配，反例把qualified响应替换为同成员audited响应仍拒绝。plan/result共同伪造身份也被discovery校验拒绝。

结构完整性与成员真值分开：删一条link并同步改count仍通过结构decoder，但独立oracle不相同。JSON.parse不检出重复key，四条operator描述非空不证明计划语义，内部1MiB不等于外层报文或峰值内存。RPC initialized notification已记录，与请求/响应一起归档；这不是底层协议字节抓包或新Agent重放证明。

六个新增Bun测试覆盖原实际native envelopes、有限DATE/Gregorian/BC、partial/顺序/重复/目标/日期/UTF16/人数/范围/hash/多字段伪造、compile执行或成员混入、carrier/budget和请求/discovery错配。最终全部116测试/522expects通过，类型/格式/架构通过。旧每日/观察MCP和每日五场景/18重封伪造拒绝的无模型回归另存。原生代码/产物未变，沿128c198已封存的native308/SQL26/982/旧A1–A8与其余native证据，不宣称本轮重新运行那些检查。

保留关系发现字段名称误用、TS注解问题、错误carrier提取误用、跨不同ontology捕获的测试误配和一次测试替换范围失误的原始失败。只读Advisor建议由Lead核实并修正；请求Astra/high，有效backend/effort/usage/billing未知。新增跨平台CI命令尚未推送或远端执行，Linux tidy未验明。

下一必要步骤是新关系无模型Agent协议/证据和重放，继而继续多事实/混合粒度/复杂人群及窗口等原P1；状态/历史业务决策仍待答，不缩减目标。本工作包不代表整个P1完成。真实模型请求0，未推送、合并或发布。

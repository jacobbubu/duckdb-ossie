# 双事实无模型集成与离线验收

八个场景已完成实际MCP调用、注入Responses模拟、答案审计和严格顺序离线
重放。最终23次模拟completion、15次工具调用，30个重新封存的伪造控制全部
拒绝。原生扩展未变，仍使用9ddb20db。这个工作包证明有限脚本集成，完整P1、
真实模型自主性和生产业务验收仍未完成；没有模型请求、推送或远端CI运行。

[场景契约](../../../../scripts/fact_agent_contract.ts)覆盖两事实分别聚合后的
金额/COUNT、差集右侧UNKNOWN、空人群保留共同分母、整份来源窗口后展示、
空来源、BC/宽年/有限极值窗口、Unicode/大小写/NUL身份和compile-only。
参数由脚本明确指定，答案复制实际成功执行envelope并引用该调用。独立
BigInt/全身份oracle不进入provider上下文，用于核对所有成员、精确金额、
计数与筛选前共同分母、LAG/两观察行和及展示范围。仅解释场景不调用执行工具，
不返回结构化结果/成员/金额/计数，也不能写虚假的计算声明。

这套固定录制/答案契约不是对真实模型推理方法的要求。模拟reason/limitations
固定为有限集成声明，不能外推为任意真实模型文本审计。八场景都使用合成的
加性USD数据，不声称GAAP解释、现实缺失数据完整性或历史状态推断。

[最终receipt](final3.json)记录八场景与30项重封拒绝；[封存清单](receipt.json)
绑定最终源码、58份私有旁证文件和195个阶段/伪造/旧回归私有hash容器。
实际原件保存于忽略的`.local-agent-evidence/fact-offline-final3/run`及各控制
目录，权限沿共享0700/0600容器规则，专用旁证副本0400。封存前后逐文件hash
检查，重放未改变原件。早期23控制版本、后续30控制版本和各最终运行分别保留；
较早版本有较早source_hashes，不将它们说成当前代码的最终重放。

重放校验当前源码/fixture清单，发现中的core/ontology/mapping/manifest hash
另与对应实际文件字节交叉核对；每场景context/setup、source绑定、RPC id/顺序、
工具call/evidence id、Responses continuation和公开投影/规范completion必须
一致。所有事件消费完，无遗漏或额外terminal后事件，review/summary与重新
计算结论一致。元数据与事件JSON的decoded重复键拒绝，计划不冒充执行证据。
模拟响应model和三项zero usage必须符合模拟契约；这不是测得的token用量。

runtime要求固定DuckDB1.5.6/source id、duckdb_mcp 6f9c4ba及完整hash结构，
server hash绑定当前示例。实际验收调用者传入本次产物hash，与封存runtime
extension hash交叉核对；不传参数的纯重放只报告记录hash，不声称对应外部
产物。离线不启动客户端，不重新查运行中的CLI/MCP文件，记录hash不是执行
历史真实性认证。封存和一致性都不能单独证明provider真实性或实际计算真值。

30控制包括缺失/重复/乱序RPC、调用id/参数、错误continuation输出、公开投影
与completion矛盾、旧snapshot、错误引用、缺/多terminal、错误run/schema/mode/
source/scene清单；另协调修改所有重复载体中的金额、同数成员、空cohort共同
分母、先筛日期造成的窗口值和compile引用，必须到达对应独立oracle/引用门禁。
协调定义hash错误、模拟model/usage、缺runtime/错误产物hash、虚假compile-only
文字及局限也必须到达新门禁，不能用提前发生的无关错误冒充语义拒绝证据。
incomplete响应生成可核验的failed私有档案，但不能通过完整重放。

[RPC回归fixture](rpc-fixture.json)是从最终原件选择的八场景实际回复，绑定原
seal和每场景events hash，并以合成runtime路径替换私有路径。工具测试据此
重建当前source_hashes的模拟记录，明确不是原始执行重放或原始wire导出。
8项新增测试/36断言核对完整答案与同步错误金额/成员/分母/窗口、定义绑定、
compile-only、错误范围/失败回复/配对、重封重复metadata keys；重放时封锁
fetch、Node spawn/spawnSync和Bun.spawn，仍成功且四入口调用数均零。

138Bun/721断言、类型/格式/架构/diff通过。新增CI五个命令对应的本地实际运行
均通过：加载4/28、对象/窗口30/55、标准客户端五函数、MCP32/48及八场景/
30控制；旧每日五场景18控制和关系六场景20控制也通过。CI将同一下载产物
交给每项验收，保持linux_amd64/Ubuntu24.04与osx_arm64/macOS15两平台，关闭
dotenv/模型配置，仅上传quality-reports选择receipt，不上传私有原件。当前
远端未执行，因此不把本地成功称为跨平台或tidy成功；完整P1最终门禁保留。

只读Advisor指出定义hash只内部一致、模拟model/usage和runtime声明缺口，Lead
补齐并实际验证新反例；请求Astra/high，有效backend/effort/usage/billing未知，
Advisor未运行测试。当前工作包没有实际失败runner；早期较弱的通过记录保留，
最终结论只用当前严格入口。公开材料替换私有路径并选择receipt；完整RPC/
provider投影/上下文/答案继续保留于私有容器。

下一授权动作是整理并实际核对固定OSSIE_SQL_2026与各profile的函数/窗口兼容
矩阵，再继续原P1其余时间/历史能力和整体验收。状态/历史业务口径仍待答，
不能凭本工作包将它们移出原scope或宣称goal完成。待推历史私有审计与最终
跨平台/tidy仍必需；推送、真实模型、合并和发布不在当前授权内。

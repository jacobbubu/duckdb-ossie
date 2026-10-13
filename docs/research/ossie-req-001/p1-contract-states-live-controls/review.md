# 新版 states 一次真实验收：Lead限定通过

2026-10-13，运行基线70d7a6c。一次授权启动已结束：完整最终envelope逐字段
对应实际states查询；全部状态、全部比较、统计值与独立答案和逐fixture计算
一致。Lead核全捕获用户域、完整日历/相邻对、三值/无carry、正式规则和统计
单位后给出**本合成fixture限定通过**。用户接受尚未记录，原raw pending/seal
保持；本结论不宣称整体可靠率、真实业务总体或全部状态组合已经验收。

沿[OSSIE-REQ-001本地issue](../../../ossie-execution-research.md)和
[新版运行前审阅](../p1-revised-states-live-review/review.md)。用户仅授权states一次，
先脱敏核有效配置/固定产物，原预算新路径，结束停下审阅。没有重试、其他
场景、源码/提示词/额度更改或推送；facts/history的旧原件与限定接受保持。

## 完整答案与口径审阅

真实服务返回的工具选择为population_daily_states，外层概念为当前program的
资格概念；原题日期/产品/user_domain/transitions完全对应，未照抄假想概念。
工具提议参数与实际RPC一致，客户端没有纠正查询；完整最终结果与query实际
envelope相同，executed/complete为true，无分页/截断，不是compile证据。

完整states、comparisons逐成员/顺序与独立gold和CSV逐记录oracle核对，非仅
计数/schema一致。每个全捕获User均有所有请求日期，包括仅其他产品/范围外
有观察的用户；后者在完整日历中为UNKNOWN，不被筛掉。首尾日期包含，最后
缺失观察日保持UNKNOWN，没有沿用前一日TRUE；全部相邻对含未变化，没有
请求外前驱或只返回变化对。

正式规则取本冻结program的classification_value资格表达式，不从金额字段、
假想规则或历史版本推断。模型口径说明与程序相符：任一TRUE胜出，非空全
FALSE才FALSE，缺失/全NULL/FALSE+NULL为UNKNOWN；UNKNOWN不当现实不合格。
状态统计单位User/day，变化计User/相邻日对，标签不同（含UNKNOWN）就是
changed，不能替代去重变化用户数或观察源行数。全矩形和全部比较数量、
state_totals及变化/未变化计数都核对，源加载计数不当执行计算证据。

本题单产品范围中每User/day最多一个贡献观察，因此**没有直接演示同日
FALSE+NULL、TRUE+FALSE或TRUE+NULL混合归约**。解释契约审阅通过不等于这些
组合的真实模型样例已经覆盖；日期/产品/规则固定、资源极值与其他规则也未
在本次真实场景执行。旧无模型覆盖不冒充新增真实覆盖，不擅自加样本或重跑。

私有Lead衍生记录保存完整实际答案/参数/定义、逐项准则与样例限制；公开
[receipt](receipt.json)不复制状态/比较原数组、模型自由说明或provider信息。
一次成功不证明补语法是唯一因果，也不推导自主可靠率/现实完整性。

## 配置与真实调用证据

OPENAI_API_KEY/OPENAI_BASE_URL/OPENAI_MODEL标准变量有效、匹配此前选定目标/
模型，检查仅输出布尔，不存Key或公开内部目标。私有.env_test普通非符号链接、
Git忽略且核对期间未变；清继承三变量、禁自动dotenv并显式加载配置。固定CLI/
原生扩展/MCP SHA、68来源与新out/log在启动前核对。仅一次入口，stdout私有、
stderr为空，退出1为既有pending审阅合同，未按退出码重跑。

| 环节 | 记录 |
| --- | --- |
| 构造/客户端尝试/HTTP收到/白名单投影接受 | 各2，HTTP均200 |
| 工具意图/实际query/匹配回复/解码 | 各1 |
| 发现RPC | 7，两工具/四资源，无新增资源 |
| 最终答案 | 1，完整实际执行envelope及独立scope/值机械相符 |
| 重试/其他题/审阅期间新模型请求 | 均0 |

原系统/scope说明、原问题、完整语法/通用例和四资源与实际首请求对应；没有
将oracle或正确请求模板塞入模型。真实首/续body22,912/17,681 UTF-8字节，原
4请求/3工具/4096输出token、128KiB出站/2MiB入站、180秒及顺序清理合同不改，
没有provider或cleanup失败。请求/意图/RPC/解码/执行分别记账，发现和compile
不计实际业务query，HTTP成功不独立证明真实后端身份或账单。

白名单投影不是原provider wire，不保存Key/请求头/原响应或reasoning内容；
可见说明/参数和numeric usage留私有，usage不是费用证明。逐body hash只留
私有，公开只大小与opaque seal/来源绑定。[公开v2摘要](summary.json)保持
semantic_acceptance=not_proven，本Lead判定不冒充用户接受。后端身份、原wire、
实际费用/输入token/留存没有独立核验；本次未耗尽输出不证明其他请求也足够。

## 封存与剩余工作

阻断fetch的纯离线重放一致pending，不启动MCP或模型进程；原seal不变。
68来源以及上一实施991/审阅4私有文件hash保持，新运行和审阅另用清单绑定，
旧失败/接受不改写或重封。没有代码改动，不重复原176Bun/892断言/native/tidy/
远端CI，它们不代替本次真实调用和自由口径审阅。

本单题启动和Lead审阅包完成，下一待用户接受本限定结论再梳理三题整体状态
及剩余工作；不自动增加真实模型样例、混合组合场景或推送。facts/history已
fixture限定接受，states尚待用户接受，三个样例也不能证明整体自主可靠性。

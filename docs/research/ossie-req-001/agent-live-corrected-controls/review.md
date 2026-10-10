# 首次正确配置的真实 Agent 验收

三场景运行完成，两项数值门禁通过，能力边界正确返回 unsupported。Lead逐项核对口径与引用，未发现不一致；原始human_verdict保持pending，最终人工语义确认由用户完成。单次成功不证明重复可靠性或完整规范兼容。

| 场景 | 当前规则 | 记录数 | 总额 USD | 平均 USD | 模型请求 / 工具调用 |
| --- | --- | --- | --- | --- | --- |
| 基准 | ytd_gaap >= 10 | 37 | 1697.80 | 45.88648648648649 | 2 / 1 |
| 修改规则 | ytd_gaap >= 20 | 21 | 1277.54 | 60.8352380952381 | 2 / 1 |
| 活跃且付费 | 未定义 | 无数值 | 无数值 | 无数值 | 1 / 0 |

两个数值场景模型均自行选ValidUser、record_count/gaap_sum/mean_gaap/gaap_value_count，筛选仅包含2026-03-10与产品X，没有额外缩小范围。调用参数原样转发，未向模型发送oracle或预制业务参数。concepts资源分别声明User.ytd_gaap >= 10和>=20；ontology/manifest哈希不同，数据/content哈希相同，执行响应与各场景发现身份一致。第二场景回答明确使用新阈值，未沿用旧定义。

工具响应tool-1各有一行，record_count/gaap_sum/mean_gaap三个列分别支持回答；count是COUNT(user_id)记录数，不是COUNT(DISTINCT)独立用户数，非空GAAP数分别37/21。AVG非空值、SUM及USD说明与metrics资源对应，模型注明DOUBLE近似值且未额外舍入。concepts/fields/metrics/tool-1引用均实际存在。

第三场景明确说明ValidUser不等于活跃且付费，缺少两项业务定义/筛选条件；没有数值查询，count/total/average/unit均为null，没有把现有user_type自行解释为活跃/付费。其资源引用与发现内容一致。

Responses首次请求无previous_response_id，两个数值场景第二轮只续接本场景response ID与实际call_id对应function_call_output；第三场景无续接。原始出站body及公开入站投影保留，私有reasoning不归档；store:true依赖服务端保存状态。所有入站响应completed；未调用population_compile，所以编译哈希一致项不适用。

[evidence.json](evidence.json)包含实际usage、耗时、执行head/产物哈希和原始/脱敏文件SHA256。网关费用未知；公开记录脱敏了服务/模型，serialized_body不再代表原始精确字节，原件本地保留。401失败也保留，未覆盖或解释成成功。

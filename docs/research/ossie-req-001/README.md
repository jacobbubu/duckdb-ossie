# OSSIE-REQ-001 研究证据复现

本目录包含固定来源、研究定义草案和独立数值基准。扩展实现没有修改，也未加载或编译 ossie。本目录中的通过结果不代表 A1—A8 通过。

主要结论和执行 profile 见 [研究记录](../../ossie-execution-research.md)。原始需求和业务文档保持不变；来源与文件 SHA-256 见 [manifest](sources/manifest.json)。Apache Ossie 来源文件按固定提交保存，保留版权与许可；QwenPaw 来源记录使用本地固定提交和内容哈希。

## 环境

本次计算：Python 3.14，DuckDB wheel 1.5.6（source_id 069cc9f9b5），jsonschema 4.26.0。研究隔离环境位于 `/tmp/duckdb-ossie-req-001-venv`，不会更改仓库、source 项目或 bundled runtime 的依赖。重建时：

```sh
python3 -m venv /tmp/duckdb-ossie-req-001-venv
/tmp/duckdb-ossie-req-001-venv/bin/python -m pip install duckdb==1.5.6 jsonschema==4.26.0
```

在仓库根目录运行：

```sh
/tmp/duckdb-ossie-req-001-venv/bin/python docs/research/ossie-req-001/verify_gaap_baseline.py
/tmp/duckdb-ossie-req-001-venv/bin/python docs/research/ossie-req-001/verify_boundary_oracle.py
/tmp/duckdb-ossie-req-001-venv/bin/python docs/research/ossie-req-001/verify_definition_shapes.py
```

脚本分别生成 `baseline-results.json`、`boundary-results.json`、`definition-shape-results.json`。第一项核对所有保存来源的 SHA-256，执行固定生成器的 demo_rows 函数，不运行 main、dotenv、数据库初始化或服务注册。随后以独立 SQL 和独立 Python Decimal 循环核对原始 475 行、15 天结果、对象 ID 集合、全部地区/类型分组。SUM/COUNT 精确比较，AVG 容差 1e-10，展示均值单独舍入。日期范围为 [2026-03-01, 2026-04-01)，只有 15 天实际有数据。

CSV 是固定生成器的派生序列化；已存在而内容不同会拒绝覆盖。边界 CSV 独立于原始数据，使用人工可推导控制。对象 ID JSON 和结果 JSON 是计算输出，不是原始提取内容。

形状校验不联网：上游 main URI 注册到本地固定内容，未知引用失败。四份研究定义使用固定 schema，包含两个只修改 derived_by 阈值的 ontology；未校验其实际运行语义。

## 原始业务定义提取

读取 xlsx/docx 的 bundled Python：

```sh
/Users/rongshen/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 docs/research/ossie-req-001/extract_business_sources.py
```

此脚本只读原文件，验证 manifest 哈希，保存单元格原值/公式文本及 docx 段落/表格内容。已存在而内容不同的提取会拒绝覆盖。`business-source-extraction.json` 的 source_sha256 对应原文件，坐标与段落索引用于定位；没有渲染、修改、导入或发布这些文档。

## 许可

`sources/apache-ossie/` 为 Apache Ossie 固定提交的文档、schema、示例和只读研究所需源码，按 Apache-2.0 使用；该目录保存原 LICENSE 和 NOTICE。它们不是扩展二进制的一部分，未更新产品 vendor 快照。

`sources/qwenpaw-data/` 保留原 LICENSE，原始生成器和说明仅用于研究基准；xlsx/docx 原文件未复制，只记录来源哈希和提取内容。新生成的 CSV、研究脚本及定义草案属于本地研究产物。本次未推送或发布这些文件。

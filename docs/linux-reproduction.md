# Linux 跨环境复现

此工作包沿用 OSSIE-REQ-001 的两个有界 profile，在独立 Ubuntu 24.04 ARM64 容器里构建扩展并验证质量与实际结果。它不代替远端 Ubuntu AMD64 CI、发布或真实自主 Agent 验证。结果与失败记录见[跨环境控制记录](research/ossie-req-001/portability-control-results.json)。

使用独立递归 checkout，保留固定的 DuckDB/ci-tools/fkYAML 提交；不要复制 macOS 的 `build` 或 `node_modules`。本次容器只挂载独立 checkout 和输出目录，没有挂载原始 `/Users` 来源目录或原需求的 `/tmp` 路径。Bun 的 Linux 原生依赖、Linux 扩展与 stock CLI 均在该环境独立准备。

在 Ubuntu 24.04 ARM64 内准备引擎工具（与 TypeScript 项目工具分开）：

```sh
apt-get update
apt-get install -y build-essential cmake ninja-build git clang-tidy curl unzip python3-venv
python3 -m venv /opt/ossie-python
/opt/ossie-python/bin/pip install 'pybind11[global]==3.1.0' 'black==24.10.0' 'cmakelang==0.6.13' cxxheaderparser==2.0.1 pcpp==1.30
curl -fL --retry 3 https://github.com/oven-sh/bun/releases/download/bun-v1.3.11/bun-linux-aarch64.zip -o /tmp/bun.zip
unzip -q /tmp/bun.zip -d /opt/bun
curl -fL --retry 3 https://github.com/duckdb/duckdb/releases/download/v1.5.6/duckdb_cli-linux-arm64.zip -o /tmp/duckdb.zip
unzip -q /tmp/duckdb.zip -d /opt/duckdb
export PATH="/opt/ossie-python/bin:/opt/bun/bun-linux-aarch64:/opt/duckdb:$PATH"
```

在 checkout 根目录运行：

```sh
test "$(git -C duckdb rev-parse HEAD)" = "069cc9f9b5be802405797faecc284961b07c70ef"
export EXT_FLAGS="-DGIT_COMMIT_HASH=069cc9f9b5be802405797faecc284961b07c70ef"
bun install --frozen-lockfile
bun run typecheck
bun run format:check
bun run architecture
bun test test/tooling
GEN=ninja CMAKE_BUILD_PARALLEL_LEVEL=4 make release
GEN=ninja TIDY_THREADS=2 make tidy-check
GEN=ninja CMAKE_BUILD_PARALLEL_LEVEL=4 make test
bun scripts/run_p0_acceptance.ts build/release/extension/ossie/ossie.duckdb_extension /evidence/linux-acceptance
```

独立Git clone可能把`git log --format=%h`缩短为8位，原生capture测试仍严格要求stock引擎的10位source ID。先核对完整固定提交，再向CMake显式传入该真实提交，由上游截取10位；不放宽身份断言或伪造源码版本。

输出目录必须新建或为空。runner 先准备 stock CLI 的 `duckdb_mcp` 与 `tpcds` 扩展，再运行全部11个产物验证器、真实 MCP、独立进程复现与 CLI；首次安装需要网络和可写扩展缓存。失败停止并保留日志及 `failure.json`。单命令验收不自动运行 native tests 或 clang-tidy，须分别运行上面的命令。

来源审计使用仓库内的原始副本。新 locator 仅指定原清单中未保存的 workbook/docx 及原需求副本，绑定原清单的 SHA256；20份来源和1份原需求的预期内容哈希仍来自原清单。缺失、篡改、错误绑定、重复/未知映射、父目录穿越和越界 symlink 均拒绝。原始绝对路径是 provenance 标签，不是迁移环境中的读取路径。QwenPaw 数据副本沿用保存的 Apache-2.0 许可证；解析接受或哈希相等不证明文档显示和计算正确。

本地 ARM64 的 distro clang-tidy 与远端 Ubuntu 24.04 使用同类配置，但架构不同。C++ 格式门禁仍要求 pinned clang-format 11.0.1；不能用较新 formatter 的通过替代这个版本。macOS 与 Linux 的产物分别记录哈希，跨平台功能结果一致不要求二进制相同。

本次 ARM64 安装 clang-format 11.0.1 触发完整 LLVM Git 历史下载和源码构建，已主动终止该安装，保留安装/终止日志。格式门禁改在独立 Ubuntu 24.04 AMD64 容器使用已发布的11.0.1 wheel执行，checkout只读挂载；没有更换formatter版本。可在该AMD64容器内复现：

```sh
apt-get update
apt-get install -y python3-venv git
python3 -m venv /opt/format-python
/opt/format-python/bin/pip install black==24.10.0 cmakelang==0.6.13 clang-format==11.0.1
export PATH="/opt/format-python/bin:$PATH"
python3 duckdb/scripts/format.py --all --check --directories src test
```

ARM64环境的TypeScript/架构规则/Bun控制与AMD64环境的C++格式检查分别报告；不宣称在ARM64运行了完整`make quality-local`。本地AMD64 formatter检查也不等于远端AMD64构建或GitHub CI执行。

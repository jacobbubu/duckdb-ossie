#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq build-essential cmake ninja-build git clang-tidy curl unzip python3-venv
python3 -m venv /opt/ossie-python
/opt/ossie-python/bin/pip install 'pybind11[global]' 'black==24.10.0' 'cmakelang==0.6.13' cxxheaderparser pcpp
curl -fL --retry 3 https://github.com/oven-sh/bun/releases/download/bun-v1.3.11/bun-linux-aarch64.zip -o /tmp/bun.zip
unzip -q /tmp/bun.zip -d /opt/bun
ln -s /opt/bun/bun-linux-aarch64/bun /usr/local/bin/bun
curl -fL --retry 3 https://github.com/duckdb/duckdb/releases/download/v1.5.6/duckdb_cli-linux-arm64.zip -o /tmp/duckdb.zip
unzip -q /tmp/duckdb.zip -d /opt/duckdb
ln -s /opt/duckdb/duckdb /usr/local/bin/duckdb
cd /workspace
bun install --frozen-lockfile
uname -a
cat /etc/os-release
bun --version
duckdb -version
gcc --version
clang-tidy --version
cmake --version
git -C duckdb rev-parse HEAD

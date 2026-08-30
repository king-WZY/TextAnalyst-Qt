#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 TextAnalyst-Qt contributors
# 一键构建脚本（ARCHITECTURE.md §10.2）
# 使用：./build.sh [Debug|Release]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="${ROOT}/build/${BUILD_TYPE}"

# 生成器：优先 Ninja（若无则回退 Unix Makefiles）
GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  GENERATOR="Ninja"
fi

mkdir -p "${BUILD_DIR}"
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G "${GENERATOR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DCMAKE_INSTALL_PREFIX="/usr/local"
cmake --build "${BUILD_DIR}" -j "$(nproc)"
ctest --test-dir "${BUILD_DIR}" --output-on-failure
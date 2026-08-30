#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 TextAnalyst-Qt contributors
# =============================================================================
# check-core.sh: core 层验证脚本（无 cmake/Qt 环境的现行验证入口）
# 标准构建环境请使用 build.sh（CMake+Ninja，tests 全部走 ctest）。
# 用法：tools/check-core.sh [--asan]
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-g++}"
FLAGS="-std=c++17 -O2 -Wall -Wextra -Wpedantic -I${ROOT}/src/core"
if [[ "${1:-}" == "--asan" ]]; then
  FLAGS+=" -fsanitize=address,undefined -fno-omit-frame-pointer -g"
fi
OUT="${ROOT}/build/gcc-check"
mkdir -p "${OUT}"
BUFFER_SRC="${ROOT}/src/core/buffer/*.cpp"
ENGINE_SRC="${ROOT}/src/core/engine/*.cpp"

run() {  # $1=可执行名 $2+=源文件
  local name="$1"; shift
  # shellcheck disable=SC2086
  "${CXX}" ${FLAGS} -o "${OUT}/${name}" "$@"
  echo "=== ${name} ==="
  "${OUT}/${name}"
}

run tst_buffer   tests/unit/tst_buffer.cpp ${BUFFER_SRC}
run tst_encoding tests/unit/tst_encoding.cpp ${BUFFER_SRC}
run tst_marker   tests/unit/tst_marker.cpp ${BUFFER_SRC} ${ENGINE_SRC}
run tst_filter   tests/unit/tst_filter.cpp ${BUFFER_SRC} ${ENGINE_SRC}
run tst_search   tests/unit/tst_search.cpp ${BUFFER_SRC} ${ENGINE_SRC}

if [[ "${1:-}" != "--no-bench" ]] && command -v bench_1gb >/dev/null 2>&1; then :; fi
# 基准（可选，--bench <MiB>）
if [[ "${1:-}" == "--bench" ]]; then
  MIB="${2:-256}"
  "${CXX}" ${FLAGS} -o "${OUT}/bench_1gb" tests/performance/bench_1gb.cpp ${BUFFER_SRC} ${ENGINE_SRC}
  "${OUT}/bench_1gb" --size "${MIB}"
fi

echo "core 层全部测试通过 ✅"
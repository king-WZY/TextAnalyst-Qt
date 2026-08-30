#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 TextAnalyst-Qt contributors
# =============================================================================
# build-deb.sh: 构建 .deb 安装包（CPack DEB → dpkg-deb，无需 root）
# 用法：packaging/deb/build-deb.sh [Release|Debug]
# 产物：build/<type>/textanalyst-qt_<ver>_amd64.deb
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="${ROOT}/build/${BUILD_TYPE}"

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  GENERATOR="Ninja"
fi

echo "== 配置构建（${GENERATOR}, ${BUILD_TYPE}） =="
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G "${GENERATOR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo "== 编译 =="
cmake --build "${BUILD_DIR}" -j "$(nproc)"

echo "== 打包 .deb =="
(cd "${BUILD_DIR}" && cpack -G DEB)

DEB="$(ls -1 "${BUILD_DIR}"/textanalyst-qt_*.deb 2>/dev/null | sort | tail -1)"
if [ -z "${DEB}" ]; then
  echo "错误：未找到 .deb 产物" >&2
  exit 1
fi

echo
echo "== 包元数据（dpkg-deb --info） =="
dpkg-deb --info "${DEB}" | head -20
echo
echo "== 包内容（dpkg-deb --contents） =="
dpkg-deb --contents "${DEB}"
echo
echo "== 产物 =="
ls -lh "${DEB}"
echo
echo "安装：  sudo apt install ${DEB}"
echo "卸载：  sudo apt remove textanalyst-qt"
echo "验证：  dpkg -L textanalyst-qt"
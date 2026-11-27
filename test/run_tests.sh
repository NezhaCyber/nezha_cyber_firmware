#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/test/.build"
mkdir -p "${BUILD_DIR}"
trap 'rm -rf "${BUILD_DIR}"' EXIT

CXX="${CXX:-g++}"
CXXFLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror -I"${ROOT_DIR}/test" -I"${ROOT_DIR}/test/mocks" -I"${ROOT_DIR}/src")

echo "============================================================"
echo " Nezha Cyber (哪吒网络安全) — Host-Native Unit Test Runner"
echo " Compiler: $("${CXX}" --version | head -n 1)"
echo "============================================================"

# Build & run every C++17 test suite (both standard 16MB and compact 4MB CJK modes for i18n)
for src in "${ROOT_DIR}"/test/cpp/test_*.cc; do
    name="$(basename "${src}" .cc)"
    bin="${BUILD_DIR}/${name}"
    "${CXX}" "${CXXFLAGS[@]}" "${src}" -o "${bin}"
    "${bin}"
done

echo ""
echo "--- Extra check: i18n & CJK table under 4MB BRUCE_COMPACT_CJK_FONT=1 ---"
"${CXX}" "${CXXFLAGS[@]}" -DBRUCE_COMPACT_CJK_FONT=1 "${ROOT_DIR}/test/cpp/test_i18n_and_cjk.cc" -o "${BUILD_DIR}/test_i18n_4mb"
"${BUILD_DIR}/test_i18n_4mb"

echo ""
echo "=== Python Repository & Firmware Invariant Tests ==="
python3 -m unittest discover -s "${ROOT_DIR}/test/python" -p "test_*.py" -v

echo ""
echo "ALL HOST-NATIVE C++ AND PYTHON UNIT TESTS PASSED!"

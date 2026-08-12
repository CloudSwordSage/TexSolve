#!/bin/bash
# @Time    : 2026/2/6 17:48:01
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : eigen.sh
# @License : Apache-2.0
# @Desc    :

set -euo pipefail

PROJECT_ROOT="$1"
BUILD="$2"
HOST="$3"
CMAKE="$4"
CC="$5"
CXX="$6"
FC="$7"
ABI="$8"
CFLAGS="$9"
CXXFLAGS="${10}"
JOBS="${11}"

if [ $# -lt 11 ]; then
    echo "用法: $0 <project_root> <build> <host> <cmake> <cc> <cxx> <fc> <abi> <cflags> <cxxflags> <jobs>"
    exit 1
fi

SRC_DIR="$PROJECT_ROOT/tmp/sources/eigen"
PREFIX="$PROJECT_ROOT/third_party/eigen"

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

BUILD_DIR="$SRC_DIR/build"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==== 配置 Eigen ===="

export CC="$CC"
export CXX="$CXX"
export CFLAGS="$CFLAGS"
export CXXFLAGS="$CXXFLAGS"

CMAKE_FLAGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
    -DBUILD_TESTING=OFF
    -DEIGEN_BUILD_DOC=OFF
    -DEIGEN_TEST_CXX11=OFF
    -DEIGEN_TEST_CXX17=OFF
    -DEIGEN_TEST_CXX20=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

ABI="$ABI" CC="$CC" CFLAGS="$CFLAGS" CXX="$CXX" CXXFLAGS="$CXXFLAGS" "$CMAKE" .. -G "MSYS Makefiles" "${CMAKE_FLAGS[@]}"

echo "==== 构建 Eigen ===="

"$CMAKE" --build . --parallel "$JOBS"

echo "==== 安装 Eigen ===="

"$CMAKE" --install .

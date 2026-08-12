#!/bin/bash
# @Time    : 2026/2/6 19:23:01
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : openblas.sh
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
FFLAGS="${11}"
JOBS="${12}"

if [ $# -lt 12 ]; then
    echo "用法: $0 <project_root> <build> <host> <cmake> <cc> <cxx> <fc> <abi> <cflags> <cxxflags> <fflags> <jobs>"
    exit 1
fi

SRC_DIR="$PROJECT_ROOT/tmp/sources/openblas"
PREFIX="$PROJECT_ROOT/third_party/openblas"

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

BUILD_DIR="$SRC_DIR/build"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==== 配置 OpenBLAS ===="

CMAKE_FLAGS=(
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
  -DCMAKE_BUILD_TYPE=Release

  -DBUILD_STATIC_LIBS=ON
  -DBUILD_SHARED_LIBS=OFF

  -DBUILD_WITHOUT_LAPACK=OFF
  -DBUILD_WITHOUT_CBLAS=OFF
  -DBUILD_LAPACK_DEPRECATED=OFF
  -DBUILD_RELAPACK=OFF
  -DC_LAPACK=OFF

  -DDYNAMIC_ARCH=OFF
  -DDYNAMIC_OLDER=OFF

  -DNO_WARMUP=ON
  -DUSE_LOCKING=ON

  -DBUILD_TESTING=OFF
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)


ABI="$ABI" \
CC="$CC" \
CXX="$CXX" \
FC="$FC" \
CFLAGS="$CFLAGS" \
CXXFLAGS="$CXXFLAGS" \
FFLAGS="$FFLAGS" \
"$CMAKE" .. -G "MSYS Makefiles" "${CMAKE_FLAGS[@]}"

echo "==== 构建 OpenBLAS ===="

"$CMAKE" --build . --parallel "$JOBS"

echo "==== 安装 OpenBLAS ===="

"$CMAKE" --install .

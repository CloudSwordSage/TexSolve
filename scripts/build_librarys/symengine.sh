#!/bin/bash
# @Time    : 2026/2/7 13:52:06
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : symengine.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/symengine"
PREFIX="$PROJECT_ROOT/third_party/symengine"

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

GMP_PREFIX="$PROJECT_ROOT/third_party/gmp"
MPFR_PREFIX="$PROJECT_ROOT/third_party/mpfr"

GMP_INCLUDE_DIR="$GMP_PREFIX/include"
GMP_LIBRARY_DIR="$GMP_PREFIX/lib"
MPFR_INCLUDE_DIR="$MPFR_PREFIX/include"
MPFR_LIBRARY_DIR="$MPFR_PREFIX/lib"

BUILD_DIR="$SRC_DIR/build"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==== 配置 symengine ===="

CMAKE_FLAGS=(
  -DCMAKE_BUILD_TYPE=Release                 # Release 模式
  -DCMAKE_INSTALL_PREFIX="$PREFIX"          # 安装目录
  -DINTEGER_CLASS=gmp                        # 核心整数类型
  -DWITH_SYMENGINE_RCP=ON                   # 内部内存管理引用计数
  -DBUILD_TESTS=ON                           # 编译测试
  -DBUILD_BENCHMARKS=OFF                     # 基准可关
  -DBUILD_BENCHMARKS_GOOGLE=OFF              # Google benchmark 关
  -DBUILD_DOXYGEN=OFF                        # 文档可关
  -DWITH_OPENMP=OFF                          # 并行可关
  -DWITH_BOOST=OFF                           # 不依赖 boost
  -DWITH_MPFR=ON                             # 高精度浮点
  -DWITH_FLINT=OFF
  -DWITH_PIRANHA=OFF
  -DWITH_ARB=OFF
  -DWITH_SYMENGINE_ASSERT=OFF
  -DWITH_SYSTEM_CEREAL=OFF
  -DWITH_SYSTEM_FASTFLOAT=OFF

  # 指定 GMP 和 MPFR 路径
  -DGMP_INCLUDE_DIR="$GMP_INCLUDE_DIR"
  -DGMP_LIBRARY="$GMP_LIBRARY_DIR/libgmp.a"
  -DMPFR_INCLUDE_DIR="$MPFR_INCLUDE_DIR"
  -DMPFR_LIBRARY="$MPFR_LIBRARY_DIR/libmpfr.a"
)


ABI="$ABI" \
CC="$CC" \
CXX="$CXX" \
CFLAGS="$CFLAGS" \
CXXFLAGS="$CXXFLAGS" \
"$CMAKE" .. -G "MSYS Makefiles" "${CMAKE_FLAGS[@]}"

echo "==== 编译 symengine ===="
"$CMAKE" --build . --parallel "$JOBS"

echo "==== 安装 symengine ===="
"$CMAKE" --install .

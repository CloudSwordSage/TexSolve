#!/bin/bash
# @Time    : 2026/2/7 15:19:24
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : armadillo.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/armadillo"
PREFIX="$PROJECT_ROOT/third_party/armadillo"

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

OPENBLAS_PREFIX="$PROJECT_ROOT/third_party/openblas"
OPENBLAS_INCLUDE_DIR="$OPENBLAS_PREFIX/include/"
OPENBLAS_LIB_DIR="$OPENBLAS_PREFIX/lib/"
OPENBLAS_LIB_FILE="$OPENBLAS_LIB_DIR/libopenblas.a"

# 使用 FC 获取 Fortran 库路径
GFORTRAN_LIB_RAW=$(${FC} -print-file-name=libgfortran.a)

# 如果 .a 不存在，通配符搜索其他格式
if [ ! -f "$GFORTRAN_LIB_RAW" ]; then
  GFORTRAN_LIB_DIR=$(dirname "$GFORTRAN_LIB_RAW")
  GFORTRAN_LIB_RAW=$(ls "$GFORTRAN_LIB_DIR"/libgfortran.* 2>/dev/null | head -1)
fi

# 规范化路径
GFORTRAN_LIB=$(realpath "$GFORTRAN_LIB_RAW" 2>/dev/null || readlink -f "$GFORTRAN_LIB_RAW" 2>/dev/null || echo "$GFORTRAN_LIB_RAW")

# 同样处理 quadmath
QUADMATH_LIB_RAW=$(${FC} -print-file-name=libquadmath.a)

if [ ! -f "$QUADMATH_LIB_RAW" ]; then
  QUADMATH_LIB_DIR=$(dirname "$QUADMATH_LIB_RAW")
  QUADMATH_LIB_RAW=$(ls "$QUADMATH_LIB_DIR"/libquadmath.* 2>/dev/null | head -1)
fi

if [ -n "$QUADMATH_LIB_RAW" ] && [ -f "$QUADMATH_LIB_RAW" ]; then
  QUADMATH_LIB=$(realpath "$QUADMATH_LIB_RAW" 2>/dev/null || readlink -f "$QUADMATH_LIB_RAW" 2>/dev/null || echo "$QUADMATH_LIB_RAW")
else
  QUADMATH_LIB=""
fi

# 构建 Fortran 库列表
FORTRAN_LIBS="$GFORTRAN_LIB"
[ -n "$QUADMATH_LIB" ] && FORTRAN_LIBS="$FORTRAN_LIBS;$QUADMATH_LIB"

echo "🔍 检测到的 Fortran 库："
echo "  libgfortran: $GFORTRAN_LIB"
[ -n "$QUADMATH_LIB" ] && echo "  libquadmath: $QUADMATH_LIB"

LDFLAGS="$GFORTRAN_LIB"
[ -n "$QUADMATH_LIB" ] && LDFLAGS="$LDFLAGS $QUADMATH_LIB"

BUILD_DIR="$SRC_DIR/build"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==== 配置 armadillo ===="
CMAKE_FLAGS=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX="$PREFIX"

  # 核心选项
  -DBUILD_SHARED_LIBS=OFF
  -DARMA_USE_WRAPPER=OFF        # 避免 Armadillo 自带 wrapper 干扰
  -DARMA_USE_OPENBLAS=ON        # 强制使用 OpenBLAS
  -DBLAS_LIBRARIES="$OPENBLAS_LIB_FILE;$FORTRAN_LIBS"
  -DLAPACK_LIBRARIES="$OPENBLAS_LIB_FILE;$FORTRAN_LIBS"
  -DOpenBLAS_INCLUDE_DIR="$OPENBLAS_INCLUDE_DIR"

  # 不启用不需要的库
  -DARMA_USE_MKL=OFF
  -DARMA_USE_ATLAS=OFF
  -DARMA_USE_SUPERLU=OFF
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

ABI="$ABI" \
CC="$CC" \
CXX="$CXX" \
CFLAGS="$CFLAGS" \
CXXFLAGS="$CXXFLAGS" \
LDFLAGS="$LDFLAGS" \
"$CMAKE" .. -G "MSYS Makefiles" "${CMAKE_FLAGS[@]}"

echo "==== 编译 armadillo ===="
"$CMAKE" --build . --parallel "$JOBS"

echo "==== 安装 armadillo ===="
"$CMAKE" --install .

#!/bin/bash
# @Time    : 2026/2/7 16:17:20
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : sundials.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/sundials"
PREFIX="$PROJECT_ROOT/third_party/sundials"

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

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

BUILD_DIR="$SRC_DIR/build"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==== 配置 sundials ===="

CMAKE_FLAGS=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
  -DBUILD_SHARED_LIBS=OFF
  -DBUILD_STATIC_LIBS=ON
  -DBUILD_ARKODE=ON
  -DBUILD_CVODE=ON
  -DBUILD_CVODES=ON
  -DBUILD_IDA=ON
  -DBUILD_IDAS=ON
  -DBUILD_KINSOL=ON
  -DBUILD_TESTING=ON
  -DEXAMPLES_ENABLE_C=ON
  -DEXAMPLES_ENABLE_CXX=OFF
  -DEXAMPLES_INSTALL=ON
  -DENABLE_LAPACK=ON
  -DENABLE_MPI=OFF
  -DENABLE_OPENMP=OFF
  -DBLAS_LIBRARIES="$OPENBLAS_LIB_FILE"
  -DLAPACK_LIBRARIES="$OPENBLAS_LIB_FILE"
  -DBLAS_INCLUDE_DIRS="$OPENBLAS_INCLUDE_DIR"
  -DLAPACK_INCLUDE_DIRS="$OPENBLAS_INCLUDE_DIR"
  # 可选：如果上面不够，添加这些
  -DBLA_VENDOR=OpenBLAS
  -DOpenBLAS_DIR="$OPENBLAS_PREFIX"
  -DCMAKE_Fortran_FLAGS="$CFLAGS"
  -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS"
  -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS"
)

ABI="$ABI" \
CC="$CC" \
CXX="$CXX" \
CFLAGS="$CFLAGS" \
CXXFLAGS="$CXXFLAGS" \
"$CMAKE" .. -G "MSYS Makefiles" "${CMAKE_FLAGS[@]}"

echo "==== 编译 sundials ===="
"$CMAKE" --build . --parallel "$JOBS"

echo "==== 安装 sundials ===="
"$CMAKE" --install .

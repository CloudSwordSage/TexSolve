#!/bin/bash
# @Time    : 2026/2/7 14:18:30
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : ceres-solver.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/ceres"
PREFIX="$PROJECT_ROOT/third_party/ceres"

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

EIGEN_PREFIX="$PROJECT_ROOT/third_party/eigen"
EIGEN_INCLUDE_DIR="$EIGEN_PREFIX/include/"
EIGEN_CMAKE_DIR="$EIGEN_PREFIX/share/eigen3/cmake/"

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

BUILD_DIR="$SRC_DIR/build_dir"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==== 配置 ceres-solver ===="

CMAKE_FLAGS=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX="$PREFIX"

  # 核心构建选项
  -DBUILD_SHARED_LIBS=OFF
  -DBUILD_TESTING=ON
  -DBUILD_EXAMPLES=ON
  -DBUILD_BENCHMARKS=ON

  # 线性代数配置
  -DCUSTOM_BLAS=ON
  -DEIGENSPARSE=ON
  -DEIGENMETIS=OFF
  -DLAPACK=ON
  -DSCHUR_SPECIALIZATIONS=ON
  -DSUITESPARSE=OFF

  # OpenBLAS 路径
  -DBLAS_LIBRARIES="$OPENBLAS_LIB_FILE;$GFORTRAN_LIB;$QUADMATH_LIB"
  -DLAPACK_LIBRARIES="$OPENBLAS_LIB_FILE;$GFORTRAN_LIB;$QUADMATH_LIB"
  -DOpenBLAS_INCLUDE_DIR="$OPENBLAS_INCLUDE_DIR"

  # 日志与工具
  -DGFLAGS=OFF
  -DMINIGLOG=ON
  -DPROVIDE_UNINSTALL_TARGET=ON

  # CUDA 支持（自动判定）
  -DUSE_CUDA=OFF

  # Eigen3 路径
  -DEigen3_DIR="$EIGEN_CMAKE_DIR"

  # 不启用 sanitizer
  -DSANITIZERS=""
)

ABI="$ABI" \
CC="$CC" \
CXX="$CXX" \
CFLAGS="$CFLAGS" \
CXXFLAGS="$CXXFLAGS" \
LDFLAGS="$LDFLAGS" \
"$CMAKE" .. -G "MSYS Makefiles" "${CMAKE_FLAGS[@]}"

echo "==== 编译 ceres-solver ===="
"$CMAKE" --build . --parallel "$JOBS"

echo "==== 安装 ceres-solver ===="
"$CMAKE" --install .

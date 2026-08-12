#
# @Time    : 2026/2/5 15:43:58
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : mpfr.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/mpfr"
PREFIX="$PROJECT_ROOT/third_party/mpfr"

echo "当前目录: $SRC_DIR"
echo "安装路径: $PREFIX"

GMP_DIR="$PROJECT_ROOT/third_party/gmp"
GMP_INCLUDE="$GMP_DIR/include"
GMP_LIB="$GMP_DIR/lib"

# 检查 GMP 文件
if [ ! -f "$GMP_INCLUDE/gmp.h" ]; then
    echo "错误: GMP 头文件 $GMP_INCLUDE/gmp.h 不存在"
    exit 1
fi

if [ ! -f "$GMP_LIB/libgmp.a" ]; then
    echo "错误: GMP 静态库 $GMP_LIB/libgmp.a 不存在"
    exit 1
fi

cd "$SRC_DIR"
echo "切换目录到 $(pwd)"

# 配置参数
CONFIGURE_FLAGS=(
  --build="$BUILD"
  --host="$HOST"
  --prefix="$PREFIX"
  --libdir="$PREFIX/lib"
  --enable-static
  --disable-shared
  --with-pic
  --with-gmp-include="$GMP_INCLUDE"
  --with-gmp-lib="$GMP_LIB"
  --enable-assert=no
  --disable-thread-safe
  --enable-fast-install=yes
  --disable-dependency-tracking
  --enable-lto
)


echo "正在配置 MPFR..."
ABI="$ABI" CC="$CC" CFLAGS="$CFLAGS" ./configure "${CONFIGURE_FLAGS[@]}"

echo "开始编译 MPFR..."
make -j"$JOBS"

echo "安装 MPFR 到 $PREFIX..."
make install

echo "MPFR 安装完成"

#!/bin/bash
# @Time    : 2026/2/7 15:28:53
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : ginac.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/ginac"
PREFIX="$PROJECT_ROOT/third_party/ginac"

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

CLN_PREFIX="$PROJECT_ROOT/third_party/cln"
CLN_INCLUDE="$CLN_PREFIX/include"
CLN_LIB="$CLN_PREFIX/lib"
CLN_LIB_FILE="$CLN_LIB/libcln.a"

GMP_PREFIX="$PROJECT_ROOT/third_party/gmp"
GMP_INCLUDE="$GMP_PREFIX/include"
GMP_LIB="$GMP_PREFIX/lib"
GMP_LIB_FILE="$GMP_LIB/libgmp.a"

export CLN_CFLAGS="-I$CLN_INCLUDE"
export CLN_LIBS="$CLN_LIB_FILE $GMP_LIB_FILE"

echo "==== 清理旧构建 ===="
make distclean 2>/dev/null || true

echo "==== 配置 GiNaC (autotools) ===="
CC="$CC" \
CXX="$CXX" \
CFLAGS="$CFLAGS -I$CLN_INCLUDE -I$GMP_INCLUDE" \
CXXFLAGS="$CXXFLAGS -I$CLN_INCLUDE -I$GMP_INCLUDE" \
LDFLAGS="-L$CLN_LIB -L$GMP_LIB" \
LIBS="$CLN_LIB_FILE $GMP_LIB_FILE" \
./configure \
    --build="$BUILD" \
    --host="$HOST" \
    --prefix="$PREFIX" \
    --enable-shared=no \
    --enable-static=yes

echo "==== 编译 GiNaC ===="
make -j"$JOBS"

echo "==== 安装 GiNaC ===="
make install

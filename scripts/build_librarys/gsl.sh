#!/bin/bash
# @Time    : 2026/2/7 16:42:06
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : gsl.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/gsl"
PREFIX="$PROJECT_ROOT/third_party/gsl"

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

echo "==== 清理旧构建 ===="
make distclean 2>/dev/null || true

echo "==== 配置 GSL ===="
CONFIGURE_FLAGS=(
    --prefix="$PREFIX"
    --build="$BUILD"
    --host="$HOST"
    --enable-shared=no
    --enable-static=yes
    --disable-dependency-tracking
    --disable-silent-rules
)

CC="$CC" \
CXX="$CXX" \
CFLAGS="$CFLAGS" \
CXXFLAGS="$CXXFLAGS" \
./configure "${CONFIGURE_FLAGS[@]}"

echo "==== 编译 GSL ===="
make -j"$JOBS"

echo "==== 安装 GSL ===="
make install

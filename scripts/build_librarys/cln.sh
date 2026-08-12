#!/bin/bash
# @Time    : 2026/2/6 23:57:03
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : cln.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/cln"
PREFIX="$PROJECT_ROOT/third_party/cln"

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

CONFIGURE_FLAGS=(
  --build="$BUILD"
  --host="$HOST"
  --prefix="$PREFIX"
  --enable-static
  --disable-shared
  --with-gmp="$PROJECT_ROOT/third_party/gmp"
  --disable-dependency-tracking
)


CPPFLAGS="-I$PROJECT_ROOT/third_party/gmp/include" \
LDFLAGS="-L$PROJECT_ROOT/third_party/gmp/lib" \
ABI="$ABI" \
CC="$CC" \
CFLAGS="$CFLAGS" \
CXX="$CXX" \
CXXFLAGS="$CXXFLAGS" \
./configure "${CONFIGURE_FLAGS[@]}"


make -j"$JOBS"
make install

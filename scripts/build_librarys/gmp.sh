#!/bin/bash
# @Time    : 2026/2/5 13:42:15
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : gmp.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/gmp"
PREFIX="$PROJECT_ROOT/third_party/gmp"

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

CONFIGURE_FLAGS=(
  --build="$BUILD"
  --host="$HOST"
  --prefix="$PREFIX"
  --libdir="$PREFIX/lib"
  --disable-fat            # 不生成 fat 库
  --enable-assembly        # 汇编优化
  --enable-static          # 静态库
  --disable-shared         # 不生成动态库
  --with-pic               # 生成位置无关代码
  --enable-cxx             # C++ 接口
  --enable-fft             # 大整数 FFT 加速
  --enable-old-fft-full=no # 关闭老版 FFT
  --enable-nails=no        # 默认即可
  --enable-assert=no       # 关闭断言
  --enable-profiling=no    # 不生成 profiler 支持
  --enable-minithres=no    # 保持默认阈值
  --enable-fast-install=yes
)


ABI="$ABI" \
CC="$CC" \
CFLAGS="$CFLAGS" \
CXX="$CXX" \
CXXFLAGS="$CXXFLAGS" \
./configure "${CONFIGURE_FLAGS[@]}"

make -j"$JOBS"
make check
make install

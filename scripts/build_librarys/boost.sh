#!/bin/bash
# @Time    : 2026/2/7 17:21:59
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : boost.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/boost"
PREFIX="$PROJECT_ROOT/third_party/boost"
BUILD_DIR="$SRC_DIR/build"

cd "$SRC_DIR"

echo "==> Boost source: $SRC_DIR"
echo "==> Install prefix: $PREFIX"
echo "==> Build dir: $BUILD_DIR"

mkdir -p "$BUILD_DIR"
mkdir -p "$PREFIX"

# -------- toolset 映射 ----------
# b2 不认 gcc.exe / clang++.exe 这种全路径
TOOLSET="gcc"
if "$CXX" --version 2>/dev/null | grep -qi clang; then
    TOOLSET="clang"
fi

escape_sed() {
    printf '%s' "$1" | sed 's/[&|]/\\&/g'
}

# -------- 生成 user-config.jam ----------
CXX_REAL="$(command -v "$CXX" 2>/dev/null || which "$CXX" 2>/dev/null)"
if [ -z "$CXX_REAL" ]; then
    echo "错误: 找不到编译器 $CXX"
    exit 1
fi

echo "==> CXX real path: $CXX_REAL"

# 转换为 Windows 路径格式（使用正斜杠）
if command -v cygpath >/dev/null 2>&1; then
    CXX_FINAL_PATH="$(cygpath -m "$CXX_REAL")"
else
    # 如果不是 MSYS2 环境，尝试直接使用
    CXX_FINAL_PATH="$CXX_REAL"
fi

echo "==> CXX final path: $CXX_FINAL_PATH"

# 使用完整路径生成 jam 文件
cp "$PROJECT_ROOT/scripts/user-config.jam.in" "$BUILD_DIR/user-config.jam"
sed -i'' \
  -e "s|@TOOLSET@|$TOOLSET|g" \
  -e "s|@CXX@|$CXX_FINAL_PATH|g" \
  -e "s|@CXXFLAGS@|$(escape_sed "$CXXFLAGS")|g" \
  -e "s|@CFLAGS@|$(escape_sed "$CFLAGS")|g" \
  "$BUILD_DIR/user-config.jam"

USER_CONFIG="$BUILD_DIR/user-config.jam"

# -------- Boost libs ----------
BOOST_LIBS=(
    atomic chrono context coroutine date_time exception fiber
    filesystem graph iostreams locale log program_options
    regex serialization system test thread timer wave
)

WITH_LIBS=$(IFS=, ; echo "${BOOST_LIBS[*]}")

# -------- bootstrap ----------
if [ ! -f ./b2 ]; then
    echo "==> bootstrap boost"
    ./bootstrap.sh \
        --prefix="$PREFIX" \
        --with-libraries="$WITH_LIBS"
fi

# -------- build & install ----------
echo "==> build boost"

./b2 \
    --build-dir="$BUILD_DIR" \
    --prefix="$PREFIX" \
    --user-config="$USER_CONFIG" \
    toolset="$TOOLSET" \
    variant=release \
    threading=multi \
    link=static \
    runtime-link=static \
    address-model=64 \
    cxxstd=20 \
    install \
    -j"$JOBS"

echo "==> Boost installed to $PREFIX"

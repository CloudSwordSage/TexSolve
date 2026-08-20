#!/bin/bash
# @Time    : 2026/2/5 13:43:49
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : windows_build.sh
# @License : Apache-2.0
# @Desc    :

set -euo pipefail

if [[ -t 1 ]] \
    && [[ "${TERM:-dumb}" != "dumb" ]] \
    && command -v tput >/dev/null 2>&1; then

    BOTTOM_LINE=$(( $(tput lines) - 1 ))
    TERM_COLS=$(tput cols)

    init_status_bar() {
        # 设置滚动区域：0 到倒数第二行
        tput csr 0 $(( BOTTOM_LINE - 1 ))

        # 光标移到内容区最后一行
        tput cup $(( BOTTOM_LINE - 1 )) 0
    }

    update_status() {
        local msg="$1"

        tput sc
        tput cup "$BOTTOM_LINE" 0
        tput el
        tput rev
        printf "%-${TERM_COLS}s" "$msg"
        tput sgr0
        tput rc
    }

    cleanup() {
        # 恢复完整滚动区
        tput csr 0 $(( $(tput lines) - 1 ))
        tput cup "$BOTTOM_LINE" 0
        printf '\n'
    }
    trap cleanup EXIT
else
    init_status_bar() {
        :
    }

    update_status() {
        local msg="$1"
        echo "$msg"
    }

    cleanup() {
        :
    }

fi

init_status_bar
update_status "准备编译"

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
echo "项目根目录: $PROJECT_ROOT"

TARGET_SYS="ucrt64"
BUILD_TRIPLE="x86_64-w64-mingw32"
HOST_TRIPLE="x86_64-w64-mingw32"

CC=$(command -v gcc)
CXX=$(command -v g++)
CMAKE=$(command -v cmake)
FC=$(command -v gfortran)

if [ -z "$CC" ] || [ -z "$CXX" ] || [ -z "$CMAKE" ] || [ -z "$FC" ]; then
    echo "错误: 未找到 gcc 或 g++ 或 cmake 或 gfortran 命令"
    exit 1
fi

JOBS="${1:-$(nproc)}"

echo "并行编译线程数: $JOBS"

export CC CXX BUILD_TRIPLE HOST_TRIPLE TARGET_SYS CMAKE FC

# 构建 GMP 库
update_status "[1/13] 正在构建 GMP 库...   --- 包含必要的 check, 整体耗时较高"
"$SCRIPT_DIR/build_librarys/gmp.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"

# 构建 MPFR 库  --  依赖 gmp 库
update_status "[2/13] 正在构建 MPFR 库..."
"$SCRIPT_DIR/build_librarys/mpfr.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"

# 构建 eigen 库
update_status "[3/13] 正在构建 eigen 库..."
"$SCRIPT_DIR/build_librarys/eigen.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"

# 构建 openblas 库
update_status "[4/13] 正在构建 openblas 库...   --- 小文件巨多, 编译耗时较长"
"$SCRIPT_DIR/build_librarys/openblas.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "-O2 -pipe" \
  "$JOBS"

# 构建 cln 库  --  依赖 gmp 库
update_status "[5/13] 正在构建 cln 库..."
"$SCRIPT_DIR/build_librarys/cln.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"

# 构建 symengine 库  --  依赖 gmp 库和 mpfr 库
update_status "[6/13] 正在构建 symengine 库..."
"$SCRIPT_DIR/build_librarys/symengine.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"

# 构建 ceres-solver 库  --  依赖 eigen 库 和 openblas 库
update_status "[7/13] 正在构建 ceres-solver 库..."
"$SCRIPT_DIR/build_librarys/ceres-solver.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -include cstdint -std=gnu++17" \
  "$JOBS"

# 构建 armadillo 库  --  依赖 openblas 库
update_status "[8/13] 正在构建 armadillo 库..."
"$SCRIPT_DIR/build_librarys/armadillo.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"


# 构建 ginac 库  --  依赖 cln 库
update_status "[9/13] 正在构建 ginac 库..."
"$SCRIPT_DIR/build_librarys/ginac.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"

# 构建 sundials 库   --  依赖 openblas 库
update_status "[10/13] 正在构建 sundials 库..."
"$SCRIPT_DIR/build_librarys/sundials.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"

# 构建 gsl 库
update_status "[11/13] 正在构建 gsl 库..."
"$SCRIPT_DIR/build_librarys/gsl.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"

# 构建 nlopt 库
update_status "[12/13] 正在构建 nlopt 库..."
"$SCRIPT_DIR/build_librarys/nlopt.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"

# 构建 boost 库
update_status "[13/13] 正在构建 boost 库...   --- 存在 b2 配置, 总体耗时极长"
"$SCRIPT_DIR/build_librarys/boost.sh" \
  "$PROJECT_ROOT" \
  "$BUILD_TRIPLE" \
  "$HOST_TRIPLE" \
  "$CMAKE" \
  "$CC" \
  "$CXX" \
  "$FC" \
  "64" \
  "-O2 -pipe -std=gnu17" \
  "-O2 -pipe -std=gnu++17" \
  "$JOBS"

update_status "[13/13] 全部库构建完成"

#
# @Time    : 2026/2/7 17:03:39
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : nlopt.sh
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

SRC_DIR="$PROJECT_ROOT/tmp/sources/nlopt"
PREFIX="$PROJECT_ROOT/third_party/nlopt"

cd "$SRC_DIR"
echo "当前目录: $(pwd)"
echo "安装路径: $PREFIX"

BUILD_DIR="$SRC_DIR/build"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==== 配置 nlopt ===="

CMAKE_FLAGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
    -DBUILD_SHARED_LIBS=OFF         # 静态库
    -DNLOPT_CXX=ON                  # 开启 C++ API
    -DNLOPT_FORTRAN=OFF             # 关闭 Fortran
    -DNLOPT_GUILE=OFF               # 关闭 Guile
    -DNLOPT_PYTHON=OFF              # 关闭 Python
    -DNLOPT_JAVA=OFF                # 关闭 Java
    -DNLOPT_MATLAB=OFF              # 关闭 MATLAB
    -DNLOPT_OCTAVE=OFF              # 关闭 Octave
    -DNLOPT_SWIG=OFF                # 关闭 SWIG
    -DNLOPT_LUKSAN=ON               # 启用 Luksan 算法
    -DDISABLE_FP_CONTRACT=ON        # 禁止浮点合并
)

ABI="$ABI" \
CC="$CC" \
CFLAGS="$CFLAGS" \
CXX="$CXX" \
CXXFLAGS="$CXXFLAGS" \
"$CMAKE" .. -G "MSYS Makefiles" "${CMAKE_FLAGS[@]}"

echo "==== 构建 nlopt ===="

"$CMAKE" --build . --parallel "$JOBS"

echo "==== 安装 nlopt ===="

"$CMAKE" --install .

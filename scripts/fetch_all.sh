#!/bin/bash
# @Time    : 2026/2/4 20:19:52
# @Author  : 墨烟行(GitHub UserName: CloudSwordSage)
# @File    : fetch_all.sh
# @License : Apache-2.0
# @Desc    : 拉取依赖模块源码并进行校验

set -euo pipefail

USE_INSECURE_TLS=${USE_INSECURE_TLS:-0}
if [ "$USE_INSECURE_TLS" = "1" ]; then
    echo "正在使用不安全的 TLS 连接..."
fi

mkdir -p ./tmp/downloads
mkdir -p ./tmp/sources

# =========== 配置 ===========

# 高精度计算
GMP_VERSION="6.3.0"
GMP_URL="https://ftpmirror.gnu.org/gmp/gmp-${GMP_VERSION}.tar.xz"
GMP_OUT_NAME="gmp.tar.xz"
GMP_SHA256="a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898"

MPFR_VERSION="4.2.0"
MPFR_URL="https://www.mpfr.org/mpfr-${MPFR_VERSION}/mpfr-${MPFR_VERSION}.tar.xz"
MPFR_OUT_NAME="mpfr.tar.xz"
MPFR_SHA256="06a378df13501248c1b2db5aa977a2c8126ae849a9d9b7be2546fb4a9c26d993"

# 线性代数
EIGEN_VERSION="3.4.0"
EIGEN_URL="https://codeload.github.com/eigen-mirror/eigen/tar.gz/refs/tags/${EIGEN_VERSION}"
EIGEN_OUT_NAME="eigen.tar.gz"
EIGEN_SHA256="8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72"

ARMADILLO_VERSION="12.6.4"
ARMADILLO_URL="https://sourceforge.net/projects/arma/files/armadillo-${ARMADILLO_VERSION}.tar.xz"
ARMADILLO_OUT_NAME="armadillo.tar.xz"
ARMADILLO_SHA256="eb7f243ffc32f18324bc7fa978d0358637e7357ca7836bec55b4eb56e9749380"

OPENBLAS_VERSION="0.3.25"
OPENBLAS_URL="https://github.com/xianyi/OpenBLAS/archive/refs/tags/v${OPENBLAS_VERSION}.tar.gz"
OPENBLAS_OUT_NAME="openblas.tar.gz"
OPENBLAS_SHA256="4c25cb30c4bb23eddca05d7d0a85997b8db6144f5464ba7f8c09ce91e2f35543"

# 符号计算
SYMENGINE_VERSION="0.14.0"
SYMENGINE_URL="https://github.com/symengine/symengine/archive/refs/tags/v${SYMENGINE_VERSION}.tar.gz"
SYMENGINE_OUT_NAME="symengine.tar.gz"
SYMENGINE_SHA256="11c5f64e9eec998152437f288b8429ec001168277d55f3f5f1df78e3cf129707"

GINAC_VERSION="1.8.9"
GINAC_URL="https://mirrors.aliyun.com/macports/distfiles/GiNaC/ginac-${GINAC_VERSION}.tar.bz2"
GINAC_OUT_NAME="ginac.tar.bz2"
GINAC_SHA256="6cfd46cf4e373690e12d16b772d7aed0f5c433da8c7ecd2477f2e736483bb439"

CLN_VERSION="1.3.7"
CLN_URL="https://mirrors.aliyun.com/macports/distfiles/cln/cln-${CLN_VERSION}.tar.bz2"
CLN_OUT_NAME="cln.tar.bz2"
CLN_SHA256="7c7ed8474958337e4df5bb57ea5176ad0365004cbb98b621765bc4606a10d86b"

# 数值积分
GSL_VERSION="2.7.1"
GSL_URL="https://mirrors.aliyun.com/gnu/gsl/gsl-${GSL_VERSION}.tar.gz"
GSL_OUT_NAME="gsl.tar.gz"
GSL_SHA256="dcb0fbd43048832b757ff9942691a8dd70026d5da0ff85601e52687f6deeb34b"

BOOST_VERSION="1.86.0"
BOOST_URL="https://sourceforge.net/projects/boost/files/boost/${BOOST_VERSION}/boost_${BOOST_VERSION//./_}.tar.gz"
BOOST_OUT_NAME="boost.tar.gz"
BOOST_SHA256="2575e74ffc3ef1cd0babac2c1ee8bdb5782a0ee672b1912da40e5b4b591ca01f"

# 非线性优化
CERES_VERSION="2.2.0"
CERES_URL="https://github.com/ceres-solver/ceres-solver/archive/refs/tags/${CERES_VERSION}.tar.gz"
CERES_OUT_NAME="ceres.tar.gz"
CERES_SHA256="12efacfadbfdc1bbfa203c236e96f4d3c210bed96994288b3ff0c8e7c6f350d4"

NLOPT_VERSION="2.10.0"
NLOPT_URL="https://github.com/stevengj/nlopt/archive/refs/tags/v${NLOPT_VERSION}.tar.gz"
NLOPT_OUT_NAME="nlopt.tar.gz"
NLOPT_SHA256="506f83a9e778ad4f204446e99509cb2bdf5539de8beccc260a014bd560237be1"

# 微分方程
SUNDIALS_VERSION="7.5.0"
SUNDIALS_URL="https://github.com/LLNL/sundials/releases/download/v${SUNDIALS_VERSION}/sundials-${SUNDIALS_VERSION}.tar.gz"
SUNDIALS_OUT_NAME="sundials.tar.gz"
SUNDIALS_SHA256="089ac659507def738b7a65b574ffe3a900d38569e3323d9709ebed3e445adecc"

# =========== 拉取 ===========

URL=(
    $GMP_URL
    $MPFR_URL
    $EIGEN_URL
    $ARMADILLO_URL
    $OPENBLAS_URL
    $SYMENGINE_URL
    $GINAC_URL
    $CLN_URL
    $GSL_URL
    $BOOST_URL
    $CERES_URL
    $NLOPT_URL
    $SUNDIALS_URL
)
SHA256=(
    $GMP_SHA256
    $MPFR_SHA256
    $EIGEN_SHA256
    $ARMADILLO_SHA256
    $OPENBLAS_SHA256
    $SYMENGINE_SHA256
    $GINAC_SHA256
    $CLN_SHA256
    $GSL_SHA256
    $BOOST_SHA256
    $CERES_SHA256
    $NLOPT_SHA256
    $SUNDIALS_SHA256
)

OUT_NAME=(
    $GMP_OUT_NAME
    $MPFR_OUT_NAME
    $EIGEN_OUT_NAME
    $ARMADILLO_OUT_NAME
    $OPENBLAS_OUT_NAME
    $SYMENGINE_OUT_NAME
    $GINAC_OUT_NAME
    $CLN_OUT_NAME
    $GSL_OUT_NAME
    $BOOST_OUT_NAME
    $CERES_OUT_NAME
    $NLOPT_OUT_NAME
    $SUNDIALS_OUT_NAME
)

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
DOWNLOAD_DIR="$PROJECT_ROOT/tmp/downloads"
echo "项目根目录: $PROJECT_ROOT"
echo "下载目录: $DOWNLOAD_DIR"
mkdir -p "$DOWNLOAD_DIR"
cd "$DOWNLOAD_DIR"

for ((i = 0; i < ${#URL[@]}; i++)); do
    echo "正在处理 ${OUT_NAME[i]}..."

    if [[ -f "${OUT_NAME[i]}" ]]; then
        if [ -n "${SHA256[i]}" ]; then
            echo "${SHA256[i]}  ${OUT_NAME[i]}" | sha256sum -c --quiet
            if [ $? -eq 0 ]; then
                echo "  -> 文件已存在且 SHA256 匹配，跳过下载。"
                continue
            else
                echo "  -> 文件已存在，但 SHA256 不匹配，正在重新下载..."
            fi
        else
            echo "  -> 文件已存在，没有 SHA256 可供验证，跳过下载。"
            continue
        fi
    fi

    echo "正在获取 ${URL[i]}..."
    if [ "$USE_INSECURE_TLS" = "1" ]; then
        wget --no-check-certificate "${URL[i]}" -O "${OUT_NAME[i]}"
    else
        wget "${URL[i]}" -O "${OUT_NAME[i]}"
    fi

    if [ -n "${SHA256[i]}" ]; then
        echo "正在检查 ${OUT_NAME[i]}..."
        echo "${SHA256[i]}  ${OUT_NAME[i]}" | sha256sum -c --quiet || {
            echo "ERROR: ${OUT_NAME[i]} 的 SHA256 不匹配"
            exit 1
        }
    fi
done


# ========== 静默解压 ==========
for f in "${OUT_NAME[@]}"; do
    echo "解压缩 $f..."
    BASENAME="${f%.*}"
    # 处理二级扩展
    if [[ "$f" == *.tar.gz ]]; then
        BASENAME="${f%.tar.gz}"
        mkdir -p ../sources/"$BASENAME"
        tar -xzf "$f" -C ../sources/"$BASENAME" --strip-components=1
    elif [[ "$f" == *.tar.xz ]]; then
        BASENAME="${f%.tar.xz}"
        mkdir -p ../sources/"$BASENAME"
        tar -xJf "$f" -C ../sources/"$BASENAME" --strip-components=1
    elif [[ "$f" == *.tar.bz2 ]]; then
        BASENAME="${f%.tar.bz2}"
        mkdir -p ../sources/"$BASENAME"
        tar -xjf "$f" -C ../sources/"$BASENAME" --strip-components=1
    else
        echo "未知的存档格式：$f，正在跳过"
        continue
    fi
done

read -p "所有文件已解压。您想删除已下载的压缩文件吗？ [y/N]: " answer
if [[ "$answer" =~ ^[Yy]$ ]]; then
    rm -f "${OUT_NAME[@]}"
    echo "已下载的压缩文件已删除。"
else
    echo "已下载的压缩文件已保留。"
fi

#!/usr/bin/env bash
# EasyMath 一键构建 + 安装
#   用法: ./install.sh [安装前缀]      (默认 /opt/EasyMath)
set -euo pipefail

PREFIX="${1:-/opt/EasyMath}"
SRC="$(cd "$(dirname "$0")" && pwd)"

echo "==================== EasyMath 安装 ===================="
echo "源码目录: $SRC"
echo "安装前缀: $PREFIX"
echo

echo "---- 1/5 检查依赖 ----"
if command -v cmake >/dev/null 2>&1; then
    echo "  [有] cmake  $(cmake --version | head -1 | awk '{print $3}')"
else
    echo "  [缺] cmake  —— 请先安装: apt install cmake"; exit 1
fi
if command -v g++ >/dev/null 2>&1; then
    echo "  [有] g++    $(g++ --version | head -1 | awk '{print $NF}')"
elif command -v clang++ >/dev/null 2>&1; then
    echo "  [有] clang++"
else
    echo "  [缺] C++ 编译器 —— 请先安装: apt install g++"; exit 1
fi
# 可选依赖只提示, 不阻断
[ -f /usr/include/gmp.h ] || [ -f /usr/include/*/gmp.h ] 2>/dev/null || \
    echo "  [可选] 没有 GMP 开发包  -> 用自带大整数 (apt install libgmp-dev 可启用)"
[ -f /usr/include/mpfr.h ] || \
    echo "  [可选] 没有 MPFR/MPC    -> 高精度浮点退到 long double (apt install libmpfr-dev libmpc-dev)"
if command -v python3 >/dev/null 2>&1 && python3 -c "import sympy" >/dev/null 2>&1; then
    echo "  [有] python3 + sympy (外部符号引擎可用)"
else
    echo "  [可选] 没有 python3+sympy -> 求解使用内置引擎 (pip install sympy 可启用)"
fi
echo

echo "---- 2/5 配置 (CMake) ----"
cmake -S "$SRC" -B "$SRC/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
echo

echo "---- 3/5 编译 ----"
cmake --build "$SRC/build" -j"$( (nproc 2>/dev/null) || echo 4 )"
echo

echo "---- 4/5 安装 ----"
cmake --install "$SRC/build"
echo

echo "---- 5/5 自检 ----"
"$PREFIX/bin/EasyMath" --engine-info
"$PREFIX/bin/EasyMath" --eval "5! + 6^8" --hide all --show plain
echo
echo "安装完成: $PREFIX/bin/EasyMath"
echo "把它加进 PATH 即可直接使用:  export PATH=$PREFIX/bin:\$PATH"

#!/usr/bin/env bash
# 用 mingw-w64 交叉编译 Windows 版 (零依赖: 自带大整数/浮点后端)
# 需要: apt install mingw-w64
# 想要 Windows 上也用 GMP/MPFR: 用 MSYS2 (pacman -S mingw-w64-x86_64-gmp mingw-w64-x86_64-mpfr
# mingw-w64-x86_64-mpc) 或 vcpkg, 然后加 -DEASYMATH_USE_GMP -DEASYMATH_USE_MPFR 与对应 -l 库。
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
for tgt in x86_64-w64-mingw32 i686-w64-mingw32; do
  command -v "$tgt-g++" >/dev/null || { echo "跳过 $tgt (未安装)"; continue; }
  echo "== $tgt =="
  "$tgt-g++" -std=c++17 -O2 -Wall -Wextra -I "$SRC/src" \
      "$SRC"/src/*.cpp -static -o "$SRC/EasyMath-$tgt.exe"
done
echo "完成"

#!/usr/bin/env bash
# 全量: 单元/往返/端到端/差分/精度/打断/inflate/字体/字形/分组/CLI体检/标记法/大位数/模糊
# 用法: tests/run_all.sh [二进制] [--quick]
set -uo pipefail
BIN="${1:-./build/EasyMath}"
QUICK="${2:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0
run() {
  local name="$1"; shift
  echo "=============================================================="
  echo ">> $name"
  if "$@"; then echo "== $name 通过"; pass=$((pass+1));
  else echo "== $name 失败"; fail=$((fail+1)); fi
}

[ -x "$BIN" ] || { echo "找不到可执行文件: $BIN"; exit 2; }

echo "被测试的二进制: $(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
"$BIN" --version || true

# 1) 核心单元测试
g++ -std=c++17 -O2 -I src tests/test_core.cpp src/expr.cpp src/poly.cpp src/solve.cpp \
    src/rational.cpp src/bigint_gmp.cpp src/hpnum.cpp src/unicode.cpp src/i18n.cpp \
    src/interrupt.cpp src/engine.cpp -DEASYMATH_USE_GMP -DEASYMATH_USE_MPFR \
    -lgmpxx -lgmp -lmpfr -lmpc -o "$TMP/test_core" 2>"$TMP/core.log" \
  && run "核心单元测试" "$TMP/test_core" \
  || { echo "核心测试编译失败:"; tail -5 "$TMP/core.log"; fail=$((fail+1)); }

# 2) 打印-重解析往返
g++ -std=c++17 -O2 -I src tests/test_roundtrip.cpp src/expr.cpp src/rational.cpp \
    src/bigint_gmp.cpp src/unicode.cpp src/interrupt.cpp -DEASYMATH_USE_GMP -lgmpxx -lgmp \
    -o "$TMP/test_roundtrip" 2>"$TMP/rt.log" \
  && run "打印-重解析往返" "$TMP/test_roundtrip" \
  || { echo "往返测试编译失败:"; tail -5 "$TMP/rt.log"; fail=$((fail+1)); }

# 3) 端到端 CLI
run "端到端 CLI" ./tests/run_cli_tests.sh "$BIN"

# 4) 与独立预言机差分对拍
if [ "$QUICK" = "--quick" ]; then DCASES=120; else DCASES=200; fi
run "差分对拍 (${DCASES} 例)" python3 tests/difftest.py "$BIN" --cases="$DCASES"

# 5) 精度(逐位对照 mpmath)
if [ "$QUICK" = "--quick" ]; then
  run "精度 (20/30 位)" python3 tests/test_precision.py "$BIN" --quick
else
  run "精度 (20/30/50 位 + 80/120 位超精)" python3 tests/test_precision.py "$BIN" --digits=20,30,50
fi

# 6) 打断机制
run "打断机制" python3 tests/test_interrupt.py "$BIN"

# 7) inflate / zip 对拍(测试链接系统 zlib 只为造数据, 产品代码不依赖)
g++ -std=c++17 -O2 -I src tests/test_inflate.cpp src/inflate.cpp src/zipfile.cpp -lz \
    -o "$TMP/test_inflate" 2>"$TMP/inflate.log" \
  && run "inflate/ZIP 对拍" "$TMP/test_inflate" \
  || { echo "inflate 测试编译失败:"; tail -5 "$TMP/inflate.log"; fail=$((fail+1)); }

# 8) TrueType 字体解析
g++ -std=c++17 -O2 -I src tests/test_truetype.cpp src/truetype.cpp src/zipfile.cpp src/inflate.cpp \
    -o "$TMP/test_truetype" 2>"$TMP/tt.log" \
  && run "TrueType 解析" "$TMP/test_truetype" \
  || { echo "TrueType 测试编译失败:"; tail -5 "$TMP/tt.log"; fail=$((fail+1)); }

# 9) 字形(文字 -> 函数)
g++ -std=c++17 -O2 -I src tests/test_glyph.cpp src/glyph.cpp src/truetype.cpp src/zipfile.cpp \
    src/inflate.cpp -o "$TMP/test_glyph" 2>"$TMP/glyph.log" \
  && run "字形(文字→函数)" "$TMP/test_glyph" \
  || { echo "字形测试编译失败:"; tail -5 "$TMP/glyph.log"; fail=$((fail+1)); }

# 10) 分组/比例自洽性
run "分组/比例自洽性" python3 tests/test_grouping.py "$BIN" $QUICK

# 11) 广义 CLI/模式体检(不崩不挂不静默成功)
run "CLI 体检(边角输入/参数/配置/保存)" python3 tests/audit_cli.py "$BIN"

# 12) 超大小数位(>200): 逐位对拍 + 钳位承诺
run "超大小数位(>200)" python3 tests/test_bigdigits.py "$BIN"

# 13) 标记法/函数差分体检(独立期望值)
run "标记法体检(符号/函数)" python3 tests/audit_notation.py "$BIN"

# 14) 课程覆盖(小学 -> 高中/可符号求解)
run "课程覆盖(方程)" python3 tests/audit_equations.py "$BIN"

# 15) 模糊测试
if [ "$QUICK" = "--quick" ]; then FCASES=300; else FCASES=1003; fi
run "模糊测试 (${FCASES} 例)" python3 tests/fuzz_cli.py "$BIN" --cases="$FCASES"

echo "=============================================================="
echo "总计: 通过 $pass 套, 失败 $fail 套"
[ "$fail" -eq 0 ]

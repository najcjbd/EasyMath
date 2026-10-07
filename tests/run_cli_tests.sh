#!/usr/bin/env bash
# EasyMath 端到端 CLI 测试
set -u
BIN="${1:-./EasyMath}"
TMP="$(mktemp -d)"
PASS=0
FAIL=0
SKIP=0

# 探测本构建实际带哪些后端: 之后按能力跳过对应用例, 而不是误报失败。
# (同一份测试套件要能在"全内置"构建上跑: 那种构建没有 GMP/MPFR)
INFO=$("$BIN" --engine-info 2>&1)
BIGLINE=$(printf '%s\n' "$INFO" | grep -E "大整数后端|big integer backend")
HPLINE=$(printf '%s\n' "$INFO" | grep -E "高精度浮点|high-precision float")
SIGLINE=$(printf '%s\n' "$INFO" | grep -E "符号引擎|symbolic engine")
HAS_GMP=0; HAS_MPFR=0; HAS_SYMPY=0
case "$BIGLINE" in *GMP*) HAS_GMP=1 ;; esac
case "$HPLINE" in *MPFR*) HAS_MPFR=1 ;; esac
case "$SIGLINE" in *SymPy*) HAS_SYMPY=1 ;; esac
skip() { SKIP=$((SKIP+1)); echo "SKIP: $1"; }
need_mpfr() { [ "$HAS_MPFR" = 1 ] || { skip "$1 (本构建无 MPFR)"; return 1; }; return 0; }
need_hp()   { { [ "$HAS_MPFR" = 1 ] || [ "$HAS_SYMPY" = 1 ]; } || { skip "$1 (本构建无 MPFR 且无 SymPy)"; return 1; }; return 0; }
need_sympy(){ [ "$HAS_SYMPY" = 1 ] || { skip "$1 (本构建无 SymPy)"; return 1; }; return 0; }
need_gmp()  { [ "$HAS_GMP" = 1 ] || { skip "$1 (本构建无 GMP)"; return 1; }; return 0; }

ok()   { PASS=$((PASS+1)); }
bad()  { FAIL=$((FAIL+1)); echo "FAIL: $1"; }

contains() { # 名称 期望子串 实际
  case "$3" in *"$2"*) ok ;; *) bad "$1 (未找到 '$2')"; echo "--- 实际输出 ---"; echo "$3"; echo "---------------";; esac
}
notcontains() {
  case "$3" in *"$2"*) bad "$1 (不应出现 '$2')" ;; *) ok ;; esac
}
expect_exit() { # 名称 期望码 实际码
  if [ "$2" -eq "$3" ]; then ok; else bad "$1 (退出码 $3 != $2)"; fi
}

run() { "$BIN" "$@" 2>&1; }

# ---------- 基础 ----------
out=$(run --version); expect_exit "version 退出码" 0 $?
contains "version 内容" "EasyMath" "$out"

out=$(run --help); expect_exit "help 退出码" 0 $?
contains "help 内容" "--lagrange" "$out"
contains "help 内容2" "--solve" "$out"

# ---------- 模式 1: 拉格朗日 ----------
out=$(run --lagrange "x=1,y=3 x=2,y=5 x=3,y=9")
expect_exit "lagrange 退出码" 0 $?
contains "lagrange 多项式" "P(x) = x^2 - x + 3" "$out"
contains "lagrange 验算" "P(1) = 3" "$out"

out=$(run --lagrange "5x=1 5y=3 7x=2 7y=6")
contains "lagrange 轴标形式" "P(x) = 3x" "$out"

out=$(run --lagrange "(1,1) (2,4)")
contains "lagrange 括号点" "P(x) = 3x - 2" "$out"

out=$(run --lagrange "x=1,y=3 x=2,y=5 x=3,y=9" --hide note)
notcontains "lagrange --hide note" "该角度的正切" "$out"

# ---------- 模式 2: 解方程 ----------
out=$(run --engine=builtin --solve "x^4=5")
expect_exit "solve 退出码" 0 $?
n=$(printf '%s\n' "$out" | grep -c '^x_[0-9]')
if [ "$n" -eq 4 ]; then ok; else bad "solve x^4=5 根个数 ($n != 4)"; fi

out=$(run --engine=builtin --solve "x^2+1=0")
contains "solve 复根" "x_1 = -i" "$out"
contains "solve 复根2" "x_2 = i" "$out"

out=$(run --engine=builtin --solve "y=5x, y=6z, x=2z")
contains "solve 线性唯一解" "x = 0" "$out"

out=$(run --engine=builtin --solve "x+y=1, x+y=2")
contains "solve 无解" "无解" "$out"

out=$(run --engine=builtin --solve "y=6z, x=2z")
contains "solve 无穷解" "自由参数" "$out"

out=$(run --engine=builtin --solve "x+y=5, x*y=6")
contains "solve 非线性" "x = 3, y = 2" "$out"
contains "solve 非线性2" "x = 2, y = 3" "$out"

out=$(run --engine=builtin --solve 'y=5x$y=6z$x=2z')
contains "solve $ 分隔符" "x = 0" "$out"

out=$(run --engine=builtin --solve '\frac{x^2}{2}=2')
contains "solve LaTeX" "1/2x^2 - 2" "$out"

out=$(run --engine=builtin --solve "x⁴=5" --hide note)
contains "solve 上标输入" "x^4 - 5" "$out"

out=$(run --engine=builtin --solve "x^2=4, x=2")
contains "solve 交集" "x = 2" "$out"

# ---------- 模式 3: 求值 ----------
out=$(run --eval "5!")
contains "eval 阶乘" "5! = 120" "$out"

out=$(run --eval "6^8")
contains "eval 幂" "1679616" "$out"

out=$(run --eval "√6")
contains "eval 根号" "≈ 2.44948974" "$out"

out=$(run --eval "5x6")
contains "eval 隐式乘" "= 30" "$out"

out=$(run --eval "1/3")
contains "eval 循环小数" "0.(3)" "$out"
contains "eval 循环小数2" "≈ 0.33333333" "$out"

out=$(run --eval "6⁸")
contains "eval 上标" "1679616" "$out"

out=$(run --eval "5x6")
notcontains "eval 不把 x 当未知量" "未知量" "$out"

# ---------- 模式 4: 直线 ----------
out=$(run --line "45°")
contains "line 45" "y = x" "$out"

out=$(run --line "45度")
contains "line 45度" "y = x" "$out"

out=$(run --line "45dgree")
contains "line 45dgree" "y = x" "$out"

out=$(run --line "135")
contains "line 135" "y = -x" "$out"

out=$(run --line "-45")
contains "line -45" "y = -x" "$out"

out=$(run --line "30")
contains "line 30 精确值" "y = (√3/3)x" "$out"

out=$(run --line "90")
contains "line 90" "x = 0" "$out"

out=$(run --line "37")
contains "line 37 近似" "y ≈ 0.75355405x" "$out"

out=$(run --line "37" --decimals 4)
contains "line 37 小数位" "y ≈ 0.7536x" "$out"

# ---------- 外部引擎: SymPy ----------
if "$BIN" --engine-info >/dev/null 2>&1; then
  out=$(run --engine=sympy --solve "x^4=5")
  contains "sympy 精确根" "5^(1/4)" "$out"
  contains "sympy 复根" "5^(1/4)i" "$out"

  out=$(run --engine=builtin --solve "x^4=5")
  notcontains "builtin 与 sympy 形式不同" "5^(1/4)" "$out"

  out=$(run --engine=sympy --solve "sin(x)=0.5")
  contains "sympy 一般解集" "n ∈ ℤ" "$out"

  out=$(run --engine=sympy --solve "x^2+y^2=25, y=x")
  contains "sympy 精确多元解" "√(2)" "$out"

  out=$(run --engine=sympy --solve "x^4=5" --real)
  notcontains "sympy --real 过滤复根" "i" "$out"

  out=$(run --engine=sympy --solve "cos(x)=x")
  contains "sympy 无闭式解回退" "0.73908513" "$out"

  out=$(run --engine=sympy --lagrange "x=1,y=0 x=2,y=0 x=3,y=6")
  if need_sympy "sympy 因式分解"; then
    contains "sympy 因式分解" "因式分解" "$out"
  fi

  out=$(run --solve "2x+3=7")
  contains "默认 auto 引擎可用" "x" "$out"

  if need_hp "25 位任意精度"; then
    out=$(run --line "37" --decimals 25 --hide note,normalized)
    contains "mpmath 任意精度斜率" "0.7535540501027941570739564" "$out"

    out=$(run --eval "√6" --decimals 25 --hide note,normalized)
    contains "mpmath 任意精度根式" "2.4494897427831780981972841" "$out"
  fi

  # 内嵌桥脚本必须是合法 Python (防止嵌入/编辑出错)
  "$BIN" --dump-engine-script > "$TMP/engine.py" 2>/dev/null
  if python3 -c "import ast,sys; ast.parse(open(sys.argv[1]).read())" "$TMP/engine.py" 2>/dev/null; then ok; else bad "内嵌引擎脚本语法错误"; fi
  out=$("$TMP/engine.py" version 2>&1 | grep VERSION || python3 "$TMP/engine.py" version | grep VERSION)
  contains "导出的脚本可独立运行" "VERSION" "$out"
else
  echo "跳过 SymPy 段落 (外部引擎不可用)"
fi

# ---------- 配置往返 ----------
"$BIN" --dump-config > "$TMP/roundtrip.conf" 2>/dev/null
errout=$("$BIN" --no-config --config "$TMP/roundtrip.conf" --print-config 2>&1 >/dev/null)
if [ -z "$errout" ]; then ok; else bad "dump-config 生成的配置无法干净回读: $errout"; fi
out=$("$BIN" --no-config --config "$TMP/roundtrip.conf" --print-config 2>/dev/null)
contains "生成的配置含 engine" "engine = auto" "$out"
printf 'decimals = 12\n' > "$TMP/rt2.conf"
out=$("$BIN" --config "$TMP/rt2.conf" --print-config 2>/dev/null)
contains "自定义配置生效" "decimals = 12" "$out"
printf 'decimals = 12   # 行尾注释\n' > "$TMP/rt3.conf"
out=$("$BIN" --config "$TMP/rt3.conf" --print-config 2>/dev/null)
contains "行尾注释被忽略" "decimals = 12" "$out"

# ---------- 健壮性回归 (崩溃/挂死/精度 bug) ----------
# 超大点序号: 之前 std::stoi 抛异常 -> SIGABRT
run --lagrange "P99999999999999999999=(1,2)" >/dev/null 2>&1; expect_exit "超大点序号不崩溃" 3 $?
run --lagrange "x99999999999999999999=1 y99999999999999999999=2" >/dev/null 2>&1; expect_exit "超大坐标序号不崩溃" 3 $?
# 左深链/超深嵌套: 之前递归打印导致栈溢出
run --eval "1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1" >/dev/null 2>&1; expect_exit "正常长表达式仍可用" 0 $?
python3 -c "print('--eval'); print('('*5000+'1'+')'*5000)" >/dev/null
out=$(python3 -c "
import subprocess
s='('*5000+'1'+')'*5000
r=subprocess.run(['$BIN','--eval',s,'--hide','all'],capture_output=True)
print(r.returncode)" 2>/dev/null)
if [ "$out" = "3" ]; then ok; else bad "超深嵌套应干净报错(得到 $out)"; fi
out=$(python3 -c "
import subprocess
s='x'*5000
r=subprocess.run(['$BIN','--solve',s,'--hide','all'],capture_output=True)
print(r.returncode)" 2>/dev/null)
if [ "$out" = "3" ]; then ok; else bad "超长标识符应干净报错(得到 $out)"; fi
# 长循环节分母: 之前会长时间卡住
start=$(date +%s)
out=$(run --eval "1/6747528149883" --hide all --show decimal)
el=$(( $(date +%s) - start ))
if [ "$el" -le 5 ]; then ok; else bad "长循环节不应卡住(耗时 ${el}s)"; fi
contains "长循环节给出近似值" "e-13" "$out"
# 因子枚举爆炸: 之前 O(sqrt(4e18)) 会卡死
start=$(date +%s)
out=$(run --engine=builtin --solve "x^2=4000000000000000000" --hide all --show solution)
el=$(( $(date +%s) - start ))
if [ "$el" -le 10 ]; then ok; else bad "大系数求根不应卡住(耗时 ${el}s)"; fi
# 打印器括号: 2-(1+3) 不能打成 2-1+3 (求值也会跟着错)
out=$(run --eval "2-(1+3)" --hide all --show normalized,plain)
contains "减法右子树加括号" "(1 + 3)" "$out"
contains "减法右子树求值正确" "= -2" "$out"
out=$(run --eval "(2^2)^3" --hide all --show normalized)
contains "幂的底数加括号" "(2^2)^3" "$out"
# 精确无理根附带数值近似
out=$(run --engine=sympy --solve "x^2-2=0" --hide all --show solution)
contains "精确根附带近似值" "≈" "$out"
# 小数位: 200 以内是原算法; >200 走高精度特殊算法(拿不出就钳位并提示); 负数/非数才报错
out=$(run --eval "1/3" --decimals 500 --hide all --show decimal,approx)
expect_exit "decimals >200 应当接受(不再报错)" 0 $?
if printf '%s' "$out" | grep -qE "0\.[0-9]{500}"; then ok; else fail "decimals 500 要真给 500 位" "只得到较短输出"; fi
run --eval "1/2" --decimals -1 >/dev/null 2>&1; expect_exit "decimals 负数要报错" 2 $?
run --eval "1/2" --decimals abc >/dev/null 2>&1; expect_exit "decimals 非数字要报错" 2 $?

# ---------- 科学计数法 ----------
out=$(run --eval "exp(100)" --hide all --show approx)
contains "大数用科学计数法" "e+43" "$out"
out=$(run --eval "exp(-100)" --hide all --show approx)
contains "小数用科学计数法" "e-44" "$out"
out=$(run --eval "1/3" --hide all --show decimal)
contains "普通量级保持定点" "0.(3)" "$out"
out=$(run --eval "2^200" --hide all --show plain)
contains "精确整数不做科学计数法" "1606938044258990275541962092341162602522202993782792835301376" "$out"
notcontains "精确整数不退化" "e+60" "$out"
out=$(run --eval "1e12" --hide all --show plain)
contains "科学计数法输入可读回" "1000000000000" "$out"
out=$(run --eval "1.5e-3" --hide all --show plain)
contains "带指数小数输入" "3/2000" "$out"
out=$(run --eval "exp(50)" --scientific=always --hide all --show plain)
contains "--scientific=always" "e+21" "$out"
out=$(run --eval "exp(50)" --scientific=never --hide all --show plain)
notcontains "--scientific=never" "e+21" "$out"
out=$(run --eval "12345.6789" --sci-threshold=4 --hide all --show decimal)
contains "--sci-threshold 生效" "e+4" "$out"
# 负值不能被误判为科学计数法(MPFR 路径曾因 log10(负数)=NaN 而误判)
out=$(run --eval "sqrt(2)-2" --decimals 20 --hide all --show approx)
contains "负值保持定点" "-0.5857864376269049512" "$out"
notcontains "负值不用科学计数法" "e-1" "$out"
out=$(run --engine=builtin --solve "-6x^4-2x^3+x^2+3x-8=0" --decimals 10 --hide all --show solution)
contains "负实部根保持定点" "-0.8849728953" "$out"
out=$(run --eval "-exp(100)" --hide all --show approx)
contains "负的大值仍用科学计数法" "-2.68811714e+43" "$out"
# 内置后端受平台 long double 有效位限制(本机 33 位), MPFR 不受限
out=$(run --eval "sqrt(2)" --decimals 40 --hpfloat=builtin --hide all --show approx)
notcontains "内置后端不给出无意义位数" "420969807856" "$out"
if need_mpfr "MPFR 给出完整位数"; then
  out=$(run --eval "sqrt(2)" --decimals 40 --hide all --show approx)
  contains "MPFR 给出完整位数" "420969807856" "$out"
fi

# ---------- 数值后端: GMP / MPFR ----------
out=$(run --engine-info)
contains "engine-info 显示符号引擎" "SymPy" "$out"
[ "$HAS_GMP" = 1 ] && contains "engine-info 显示大整数后端" "GMP" "$out" || skip "engine-info 大整数后端 (无 GMP)"
[ "$HAS_MPFR" = 1 ] && contains "engine-info 显示高精度浮点" "MPFR" "$out" || skip "engine-info 高精度浮点 (无 MPFR)"

# 40 位小数: 超过 long double(arm64 上约 34 位)的有效位, 只有 MPFR 能给对
if need_mpfr "MPFR 40 位精度"; then
  out=$(run --eval "√2" --decimals 40)
  contains "MPFR 40 位精度" "420969807856" "$out"
fi

out=$(run --eval "√2" --decimals 40 --hpfloat=builtin)
notcontains "builtin 后端不做 40 位" "420969807856" "$out"

# GMP: 400 位整数的精确开方(自带后端会因 long double 溢出而放弃)
BIG=$(python3 -c "print(10**200)")
if need_gmp "GMP 大数精确开方"; then
  out=$(run --engine=builtin --solve "x^2=$BIG")
  contains "GMP 大数精确开方" "x_1 = -1" "$out"
  notcontains "GMP 大数开方为精确解" "≈" "$out"
fi

# MPFR 精化数值根(内置符号引擎 + 30 位小数)
if need_hp "数值根精化"; then
  out=$(run --engine=builtin --solve "x^4=5" --decimals 30)
  contains "MPFR 数值根精化" "1.495348781221220541911898994141" "$out"
fi

# 高精度配置项
if need_mpfr "precision 配置生效"; then
  out=$(run --eval "√2" --decimals 40 --precision 256)
  contains "precision 配置生效" "420969807856" "$out"
fi

# ---------- 错误处理 ----------
run --solve "x=1" --eval "1" >/dev/null 2>&1; expect_exit "模式冲突" 2 $?
run --frobnicate >/dev/null 2>&1; expect_exit "未知选项" 2 $?
run --lagrange "x=1,y=3" >/dev/null 2>&1; expect_exit "数据点不足" 3 $?
run --eval "1/0" >/dev/null 2>&1; expect_exit "除零" 1 $?
run --solve "x^2+1" >/dev/null 2>&1; expect_exit "无等号也可解" 0 $?

# ---------- 输出开关 ----------
out=$(run --eval "1+1" --hide all)
if [ -z "$out" ]; then ok; else bad "--hide all 应无输出"; fi

out=$(run --eval "1+1" --hide all --show plain)
contains "--show 覆盖" "1 + 1 = 2" "$out"

# ---------- 保存与命名 ----------
DATE="$(date +%Y-%-m-%-d)"
OUTD="$TMP/out1"; mkdir -p "$OUTD"
run --lagrange "x=1,y=3 x=2,y=5" --save --outdir "$OUTD" --hide all >/dev/null 2>&1
if [ -f "$OUTD/函数 $DATE 1.html" ]; then ok; else bad "默认命名 1.html (ls: $(ls "$OUTD"))"; fi
if [ -f "$OUTD/函数 $DATE 1.md" ]; then ok; else bad "默认命名 1.md"; fi
run --lagrange "x=1,y=3 x=2,y=5" --save --outdir "$OUTD" --hide all >/dev/null 2>&1
if [ -f "$OUTD/函数 $DATE 2.html" ]; then ok; else bad "默认命名 2.html"; fi
touch "$OUTD/函数 $DATE 5.html"
run --eval "1+1" --save --outdir "$OUTD" --hide all >/dev/null 2>&1
if [ -f "$OUTD/函数 $DATE 6.html" ]; then ok; else bad "默认命名取最大+1 (ls: $(ls "$OUTD"))"; fi

OUTD2="$TMP/out2"; mkdir -p "$OUTD2"
run --eval "1+1" --save --outdir "$OUTD2" --out "自定义" --hide all >/dev/null 2>&1
run --eval "2+2" --save --outdir "$OUTD2" --out "自定义" --hide all >/dev/null 2>&1
if [ -f "$OUTD2/自定义.html" ] && [ -f "$OUTD2/自定义(1).html" ]; then ok; else bad "重名加(1) (ls: $(ls "$OUTD2"))"; fi
out=$(run --eval "3+3" --save --outdir "$OUTD2" --out "自定义" --overwrite)
contains "覆盖提示" "覆盖" "$out"
grep -q "3 + 3 = 6" "$OUTD2/自定义.html" && ok || bad "覆盖后内容未更新"

# HTML 内容合法性
html="$OUTD2/自定义.html"
grep -q "<!DOCTYPE html>" "$html" && ok || bad "HTML doctype"
grep -q "markdown-source" "$html" && ok || bad "HTML 内嵌 markdown"
grep -q "</html>" "$html" && ok || bad "HTML 收尾"

# ---------- 交互模式 ----------
out=$(printf '1\nx=1,y=3 x=2,y=5\nn\n0\n' | "$BIN" 2>&1)
contains "交互 菜单" "请选择" "$out"
contains "交互 结果" "P(x) = 2x + 1" "$out"

out=$(printf 'x=1,y=3 x=2,y=5\nn\n0\n' | "$BIN" --lagrange 2>&1)
contains "缺参数补全交互" "P(x) = 2x + 1" "$out"

out=$(printf '6\nback\n0\n' | "$BIN" -i 2>&1)
contains "交互 配置菜单" "当前配置" "$out"
out=$(printf '5\n一(0,0)\n0\n' | "$BIN" -i 2>&1)
contains "交互 字形模式" "字形" "$out"

# ---------- 回归: 本轮修掉的问题 ----------
# 1) 分数/小数出现在指数、底数、除数位置时必须补括号(否则读出来是别的值)
out=$(run --eval "2^0.5" --hide banner,input,normalized,note,step,steps,tip,prompt,file)
contains "分数指数加括号" "2^(1/2)" "$out"
notcontains "分数指数不得写成 2^1/2" "2^1/2" "$out"
out=$(run --eval "1/0.5" --hide banner,input,normalized,note,step,steps,tip,prompt,file)
contains "分数除数加括号" "1/(1/2)" "$out"
out=$(run --eval "0.25^0.5" --hide banner,input,normalized,note,step,steps,tip,prompt,file)
contains "分数底数加括号" "(1/4)^(1/2)" "$out"

# 2) SymPy 桥: 嵌套根式要折成 √(...); 虚部不能丢 i; 方程组要附数值
if need_sympy "SymPy 桥输出形态"; then
  out=$(run --solve "x^2+y^2=5, y=x^2-1" --engine sympy --decimals 20 \
          --hide banner,input,normalized,note,step,steps,tip,prompt,file,verify)
  contains "嵌套根式折成 √" "√(" "$out"
  contains "虚部带 i" "i·√" "$out"
  contains "方程组附数值" "数值:" "$out"
  notcontains "不得出现裸 sqrt(" "sqrt(" "$out"
  out=$(run --solve "x^2+y^2=5, x*y=2" --decimals 20 \
          --hide banner,input,normalized,note,step,steps,tip,prompt,file,verify)
  notcontains "整数解不加冗余数值行" "数值:" "$out"
fi

# 3) 直线模式换算用高精度(不再用 21 位硬编码 pi)
# 弧度值打印在 normalized 通道, 这里不能把它隐藏掉
if need_hp "135° 弧度正确舍入"; then
  out=$(run --line "135°" --decimals 20 --hide banner,input,note,step,steps,tip,prompt,file,verify)
  contains "135° 弧度正确舍入" "2.35619449019234492885" "$out"
fi

# 3b) 缺少高精度后端时必须如实说明, 而不是悄悄少给位数或打印噪声位
if [ "$HAS_MPFR" = 0 ]; then
  printf 'hpfloat=mpfr\ndecimals=50\n' > "$TMP/nompfr.conf"
  out=$(run --eval "√6" --config "$TMP/nompfr.conf" --decimals 50 \
          --hide banner,input,normalized,note,step,steps,tip,prompt,file,verify 2>&1)
  contains "缺 MPFR 时提示降级" "警告" "$out"
  # 打印出的有效位数不应超过平台 long double 的可靠位数(否则后段是噪声)
  digits=$(printf '%s\n' "$out" | grep "≈" | head -1 | sed 's/.*≈//' | tr -d ' .\n' | wc -c)
  if [ "$digits" -le 35 ]; then ok; else bad "降级时位数未截断 (给了 $digits 位)"; fi
else
  skip "缺 MPFR 时提示降级 (本构建有 MPFR)"
fi

# 4) 规模闸门: 会爆炸的精确计算要快速报错, 而不是卡死
out=$(run --eval "(999999999^999)^1000" --hide banner,input,normalized,step,steps,tip,prompt,file,verify)
contains "规模超限明确报错" "超出精确求值上限" "$out"
out=$(run --eval "2^30000" --hide banner,input,normalized,note,step,steps,tip,prompt,file,verify)
contains "合理大整数仍可精确算" "79409035191329603241325178434927025139937" "$out"

# 4b) 规模闸门的"建议"要分情况, 不能在 SymPy 已开/已试过时还叫用户去开 SymPy
out=$(run --solve "999999999^999=x@x^1000=1" --engine builtin)
contains "规模闸门: 显式 builtin 时建议 sympy" "engine sympy" "$out"
if need_sympy "规模闸门: SymPy 已试过时不再建议"; then
  out=$(run --solve "999999999^999=x@x^1000=1" --engine-timeout 100)
  contains "规模闸门: SymPy 已试过" "SymPy 也已经试过" "$out"
  notcontains "规模闸门: 不再重复建议 sympy" "可改用 --engine sympy" "$out"
fi
out=$(run --solve "999999999^999=x@x^1000=1" --engine builtin --python /nonexistent)
contains "规模闸门: 无 SymPy 时提示安装" "没有可用的 SymPy" "$out"

# 5) 变量名与 SymPy 自带的函数同形时, 要当未知量而不是函数
if need_sympy "gamma 当未知量"; then
  out=$(run --solve "alpha+beta+gamma" --engine sympy \
          --hide banner,input,normalized,note,step,steps,tip,prompt,file,verify)
  contains "gamma 当未知量" "alpha = -beta - gamma" "$out"
  notcontains "不再回退内置引擎" "回退" "$out"
fi

# ---------- 关系约束: 不等式 / 非零 ----------
out=$(run --solve "x>y>0, xy=2" --hide banner,input,note,step,steps,tip,prompt,file,verify)
contains "约束: 不等式可化简" "约束: (0 < y)" "$out"
out=$(run --solve "a>b>c>0, a=b+c, 1/a+1/b=1/c" --hide banner,input,note,step,steps,tip,prompt,file,verify)
contains "约束: 链式不等式求解" "约束: 0 < c" "$out"
out=$(run --solve "abc≠0, ax^2+bx+c=0, bx^2+cx+a=0, cx^2+ax+b=0" --engine-timeout 60000 \
        --hide banner,input,note,step,steps,tip,prompt,file,verify)
contains "约束: 非零排除退化" "b ≠ 0" "$out"
notcontains "约束: 退化解被剔除" "a = 0, b = 0, c = 0" "$out"
out=$(run --solve "x>1, x=2" --engine builtin)
contains "约束: 缺 SymPy 时明确报错" "需要 SymPy" "$out"

# ---------- 解析器: 隐式乘法与幂的结合(曾经 ax^2 被当成 (a·x)^2) ----------
out=$(run --solve "ax^2=1" --hide all --show solution)
contains "隐式乘法+幂: ax^2 即 a·x²" "x^(-2)" "$out"
out=$(run --solve "a*x^2=1" --hide all --show solution)
contains "显式乘法+幂 结果一致" "x^(-2)" "$out"
out=$(run --solve "(ax)^2=1" --hide all --show solution)
contains "显式括号: (ax)^2 是整个乘积" "-1/x" "$out"
out=$(run --solve "2x^2=8" --hide all --show solution)
contains "数字系数+幂不受影响" "x_2 = 2" "$out"

# ---------- 只有关系式时的解集 / 周期不等式 ----------
out=$(run --solve "x≠2" --hide all --show solution)
contains "纯非零约束给出解集" "(-∞, 2) ∪ (2, +∞)" "$out"
out=$(run --solve "x>1, x<5" --hide all --show solution)
contains "不等式组解集" "(1, 5)" "$out"
out=$(run --solve "x^2≥4" --hide all --show solution)
contains "二次不等式开闭区间" "(-∞, -2] ∪ [2, +∞)" "$out"
out=$(run --solve "x≠y, xy=1" --hide all --show solution)
contains "非零+方程组合约束" "y + 1 ≠ 0" "$out"
out=$(run --solve "sin(x)>0" --hide all --show solution)
contains "周期不等式(正弦)" "2kπ < x < π + 2kπ" "$out"
out=$(run --solve "tan(x)>0" --hide all --show solution)
contains "周期不等式(正切含极点)" "kπ < x < π/2 + kπ" "$out"
out=$(run --solve "sin(x)>=0.5" --hide all --show solution)
contains "周期不等式(闭区间)" "≤ x ≤ 5π/6 + 2kπ" "$out"

# ---------- numericInequality 配置项(三档) ----------
out=$(run --set numericInequality=never --print-config)
contains "配置项: numericInequality 可设置" "numericInequality = never" "$out"
out=$(run --numeric-ineq always --print-config)
contains "配置项: --numeric-ineq 生效" "numericInequality = always" "$out"
out=$(run --solve "sin(x)>0, cos(x)>0" --hide all --show solution)
contains "auto(默认): 多条周期关系不化简" "自由参数" "$(run --solve "sin(x)>0, cos(x)>0" --hide banner,input,step,steps,tip,prompt,file,verify)"
out=$(run --solve "sin(x)>0, cos(x)>0" --numeric-ineq always --hide all --show solution)
contains "always: 多条周期关系给数值解" "2kπ < x < π/2 + 2kπ" "$out"
out=$(run --solve "sin(x)>0" --numeric-ineq never --hide all --show solution,note)
contains "never: 单条也不做数值" "不化简" "$out"

# ---------- 解方程的空格分隔写法 ----------
out=$(run --solve "y=5x y=6z x=2z" --hide all --show solution)
contains "空格分隔多个方程" "x = 0" "$out"
out=$(run --solve "a>b>c>0 a=b+c 1/a+1/b=1/c" --hide all --show solution)
contains "空格分隔(关系+等式)混合" "约束: 0 < c" "$out"
out=$(run --solve "abc≠0 ax²+bx+c=0 bx²+cx+a=0 cx²+ax+b=0" --hide all --show solution)
contains "空格分隔 + 上标² + 非零约束" "b ≠ 0" "$out"
out=$(run --solve "x^2 - 5 = 0" --hide all --show solution)
contains "等号两侧空格不误拆" "x_2 = √(5)" "$out"

# ---------- 解方程: 常量声明 与 派生量求值 ----------
out=$(run --solve "a*x^2+b*x+c=0" --const a,b,c --hide all --show solution)
contains "常量: 二次公式(只解 x)" "x = -b/(2a)" "$out"
contains "常量: 列出常量名" "a, b, c" "$(run --solve "a*x^2+b*x+c=0" --const a,b,c --hide all --show note)"
out=$(run --solve "abc=m, xy=n" --derive "abc, xy" --hide all --show solution)
contains "派生量: abc 归一行" "求值: a*b*c = m" "$out"
contains "派生量: xy 归一行" "求值: x*y = n" "$out"
out=$(run --solve "a>b>c>0, a=b+c, 1/a+1/b=1/c" --derive abc --hide all --show solution)
contains "派生量: abc 取决于自由参数" "a*b*c = c^3·(2 + √(5))" "$out"
out=$(run --solve "a+b=2" --const a,b --hide all --show note)
contains "全部为常量时明确说明" "没有可解的未知量" "$out"

# ---------- 分组(同取决于一个未确定量的坐一桌) 与 组内比例 ----------
out=$(run --solve "a>b>c>0, a=b+c, 1/a+1/b=1/c" --hide all --show solution,group)
contains "分组: a/b 同取决于 c" "分组(取决于 c): a, b" "$out"
contains "分组: 给出约掉公共参数的比值" "比例: a : b = (√(5) + 3)/2 : (1 + √(5))/2" "$out"
out=$(run --solve "abc=m, xy=n" --hide all --show solution,group)
contains "分组: a 这一桌" "分组(取决于 b, c, m): a" "$out"
contains "分组: n 这一桌" "分组(取决于 x, y): n" "$out"
out=$(run --solve "a=2t, b=3t, c=5t" --const t --hide all --show solution,group)
contains "分组: 常量参数也参与分组" "分组(取决于 t): a, b, c" "$out"
contains "分组: 三变量比例" "比例: a : b : c = 2 : 3 : 5" "$out"
out=$(run --solve "a=t, b=2t, c=7" --const t --hide all --show solution,group)
contains "分组: 确定值单独一桌" "分组(不依赖未确定量): c" "$out"
contains "分组: 纯参数组给比例" "比例: a : b = 1 : 2" "$out"
out=$(run --solve "x+y=3, x-y=1" --hide all --show solution,group)
contains "分组: 全为确定值时不啰嗦" "解 1: x = 2, y = 1" "$out"
notcontains "分组: 全为确定值时不显示分组" "分组" "$out"
out=$(run --solve "a=t^2, b=t, c=4t" --const t --hide all --show solution,group)
contains "比例: 非线性时提公因子" "比例(含未确定量): a : b : c = t : 1 : 4" "$out"
out=$(run --solve "a=m*k^2, b=m*k, c=3*m*k^2" --const m,k --hide all --show solution,group)
contains "比例: 多参数提公因子" "比例(含未确定量): a : b : c = k : 1 : 3k" "$out"
out=$(run --solve "a=m+n, b=m-n" --hide all --show solution,group)
contains "比例: 无可约因子时如实展示" "比例(含未确定量): a : b = m + n : m - n" "$out"
notcontains "比例: 非纯数值比不冒充分数比" "比例: a : b = m + n" "$out"
out=$(run --solve "a=2t+1, b=4t+2" --const t --hide all --show solution,group)
contains "比例: 提公因子后真变纯数值比" "比例: a : b = 1 : 2" "$out"
out=$(run --solve "p=q*r, s=q^2*r" --hide all --show solution,group)
contains "比例: 含根式也约到最简" "比例(含未确定量): p : q = r : 1" "$out"
out=$(run --solve "a=m*k^2, b=m*k, c=3*m*k^2" --const m,k --hide all --show solution,group)
contains "打印: 指数后的乘号要留着" "a = k^2·m" "$out"
out=$(run --solve "a=3u, b=3u, c=6u" --const u --hide all --show solution,group)
contains "比例: 纯数值比要约分" "比例: a : b : c = 1 : 1 : 2" "$out"
out=$(run --solve "a=x, b=x, c=x" --const x --hide all --show solution,group)
contains "比例: 同取一个未知量" "比例: a : b : c = 1 : 1 : 1" "$out"
out=$(run --solve "a=2t/3, b=4t/3" --hide all --show solution,group)
contains "比例: 分数比清分母" "比例: a : b = 1 : 2" "$out"

# ---------- 空输入 / --flag=值 写法(不能掉进交互菜单干等 stdin) ----------
expect_exit "空输入: solve 直接报错" 3 "$(run --solve "" >/dev/null 2>&1; echo $?)"
expect_exit "空输入: lagrange 直接报错" 3 "$(run --lagrange "" >/dev/null 2>&1; echo $?)"
expect_exit "空输入: eval 直接报错" 3 "$(run --eval "" >/dev/null 2>&1; echo $?)"
expect_exit "空输入: line 直接报错" 3 "$(run --line "" >/dev/null 2>&1; echo $?)"
expect_exit "空输入: --solve= 也直接报错" 3 "$(run --solve= >/dev/null 2>&1; echo $?)"
contains "空输入: solve 提示没有方程" "没有输入方程" "$(run --solve "")"
out=$(run --solve=x^2=4 --hide all --show solution,group)
contains "--flag=值 写法: 解方程可用" "x_2 = 2" "$out"
out=$(run --eval=2^10 --hide all --show latex)
contains "--flag=值 写法: 求值可用" "2^{10} = 1024" "$out"
out=$(run --lagrange="x=1,y=3 x=2,y=5" --hide all --show plain)
contains "--flag=值 写法: 拉格朗日可用" "y = 2x + 1" "$out"

# ---------- 字形模式(文字 -> 函数) ----------
FONT="${EASYMATH_TEST_FONT:-$HOME/Math/vivo_Sans.zip}"
if [ -f "$FONT" ]; then
  out=$(run --glyph "一" --hide all --show solution)
  contains "字形: 直线段的精确系数" "x(t) = 888t, y(t) = 85" "$out"
  contains "字形: 显式 y=f(x)" "显式: y = 0" "$out"
  contains "字形: 统计里有原始分段数" "字体自带分段 4" "$out"
  out=$(run --glyph "中" --at "(100,50)" --hide all --show note)
  contains "字形: 锚点坐标解析" "左下角坐标: (100, 50)" "$out"
  out=$(run --glyph "A" --at ":(0,0)" --hide all --show note)
  contains "字形: 冒号前缀=左右镜像" "已按要求左右镜像" "$out"
  out=$(run --glyph "一" --at "1 2" --hide all --show note)
  contains "字形: 坐标也接受 x x 写法" "左下角坐标: (1, 2)" "$out"
  out=$(run --glyph "一" --hide all --show solution --fit-tol 4)
  contains "字形: 容差可配" "函数" "$out"
  expect_exit "字形: 字体包不存在要报错" 3 "$(run --glyph "A" --font-pack /nonexistent-font-pack.zip >/dev/null 2>&1; echo $?)"
  contains "字形: 坏坐标要明确报错" "坐标" "$(run --glyph "A" --at "abc" 2>&1)"
  expect_exit "字形: 没有文字要报错" 3 "$(run --glyph "" >/dev/null 2>&1; echo $?)"
  out=$(run --glyph "手绘" --set 'strokes=0,0;100,80;200,0|0,200;400,200' --hide all --show note,solution)
  contains "字形: 手绘笔画(中心线)" "手绘: 笔画 2 条" "$out"
  contains "字形: 手绘出的直线也是精确系数" "x(t) = 400t, y(t) = 200" "$out"
  expect_exit "字形: 无效笔画要报错" 3 "$(run --glyph "手绘" --set 'strokes=1,2' >/dev/null 2>&1; echo $?)"
  out=$(run --glyph "手绘" --set 'strokes=' --hide all --show solution 2>&1)
  contains "字形: 空 strokes = 走文字模式" "字形" "$out"
  # 默认字体必须是用户提供的那套 vivo Sans(显式优先), 且用户可改
  out=$(run --font-list)
  contains "字体: 默认选中 vivo Sans 的 Regular" "vivoSans-Regular.ttf   <- 默认" "$out"
  notcontains "字体: 过滤掉 macOS 的 __MACOSX 垃圾项" "__MACOSX" "$out"
  out=$(run --glyph "A" --hide all --show note)
  contains "字形: 默认用 vivo Sans" "vivoSans-Regular.ttf" "$out"
  out=$(run --glyph "A" --font 1 --hide all --show note)
  contains "字形: 可按序号换字体" "vivoSans-Heavy.ttf" "$out"
  out=$(run --glyph "A" --font global --hide all --show note)
  contains "字形: 可按名字换字体(同家族优先 Regular)" "vivoSansGlobal-Regular.ttf" "$out"
  out=$(run --glyph "A" --font 999 2>&1)
  contains "字形: 序号越界要提示看清单" "--font-list" "$out"
  out=$(run --glyph "A" --font nosuchfont 2>&1)
  contains "字形: 名字找不到要提示看清单" "--font-list" "$out"
  expect_exit "字形: 序号越界退出码 3" 3 "$(run --glyph "A" --font 999 >/dev/null 2>&1; echo $?)"

  rm -rf "$TMP/glyphout"
  out=$(run --glyph "Ai" --at "(0,0)" --save --hide all --outdir "$TMP/glyphout")
  if ls "$TMP/glyphout"/*.svg >/dev/null 2>&1; then ok; else bad "字形: --save 要写出 .svg 预览"; fi
  if grep -q "<svg" "$TMP/glyphout"/*.html 2>/dev/null; then ok; else bad "字形: 保存的 HTML 里要内嵌预览"; fi
else
  skip "字形模式(没找到字体包 $FONT)"
fi

# ---------- 求值模式: 虚数单位 / 复数 / 常量精确形式 ----------
out=$(run --eval "i" --hide all --show plain,approx)
contains "求值: i 是虚数单位(不再是未知符号)" "i" "$out"
out=$(run --eval "2i" --hide all --show plain,approx)
contains "求值: 2i = 2·i(不再静默算成 2)" "2i" "$out"
notcontains "求值: 2i 不能变成 2*1" "2*1" "$out"
out=$(run --eval "i^2" --hide all --show plain,approx)
contains "求值: i^2 = -1" "= -1" "$out"
out=$(run --eval "sqrt(-1)" --hide all --show plain,approx)
contains "求值: sqrt(-1) = i" "= i" "$out"
out=$(run --eval "sqrt(-4)" --hide all --show plain,approx)
contains "求值: sqrt(-4) = 2i" "= 2i" "$out"
out=$(run --eval "exp(i*pi)" --hide all --show plain,approx)
contains "求值: exp(i*pi) = -1" "= -1" "$out"
out=$(run --eval "abs(3+4i)" --hide all --show plain,approx)
contains "求值: abs(3+4i) = 5" "= 5" "$out"
out=$(run --eval "pi/6" --hide all --show plain,approx)
contains "求值: 常量的精确形式" "π/6" "$out"
notcontains "求值: 不再说'未知量 pi'" "未知量 pi" "$out"
out=$(run --eval "e" --hide all --show plain,approx)
contains "求值: 自然常数显示成 e 不是 E" "e" "$out"
notcontains "求值: 不该显示 SymPy 的 E" "= E" "$out"
out=$(run --eval "pi/6+pi/3" --hide all --show plain,approx)
contains "求值: 常量合并成精确值" "π/2" "$out"
# 解方程模式下 i 仍然是普通未知量(不能被当成虚数单位抢走)
out=$(run --solve "i+1=3" --hide all --show solution)
contains "解方程: i 仍是可解的未知量" "i = 2" "$out"

# ---------- 绝对值 |…| 与取整括号 ⌊⌋/⌈⌉ ----------
out=$(run --eval "|-3|" --hide all --show plain)
contains "绝对值: |-3| = 3" "= 3" "$out"
notcontains "绝对值: 不能静默变成 -3" "-3 -3" "$out"
out=$(run --eval "|3-5|" --hide all --show plain)
contains "绝对值: 先算里面再取绝对值" "= 2" "$out"
out=$(run --eval "|-3|+|2|" --hide all --show plain)
contains "绝对值: 两个绝对值相加" "= 5" "$out"
out=$(run --eval "2*|-3|" --hide all --show plain)
contains "绝对值: 与乘法混用" "= 6" "$out"
out=$(run --eval "||-3||" --hide all --show plain)
contains "绝对值: 可嵌套" "= 3" "$out"
out=$(run --eval "|2^3-10|" --hide all --show plain)
contains "绝对值: 内含幂" "= 2" "$out"
out=$(run --eval "|1/3-0.5|" --hide all --show plain,decimal)
contains "绝对值: 分数也精确" "1/6" "$out"
out=$(run --solve "|x-1|>2" --hide all --show solution)
contains "绝对值: 不等式解集" "(3, +∞)" "$out"
out=$(run --solve "x=1|y=2" --hide all --show solution)
contains "绝对值: 落单的 | 仍当分隔符" "x = 1" "$out"
out=$(run --eval "|3" --hide all --show plain 2>&1)
contains "绝对值: 开头的 | 缺收尾要明确报错" "没有配对" "$out"
out=$(run --eval "1+1|2+2" --hide all --show plain)
contains "绝对值: 式子中间的 | 仍是分隔符" "4" "$out"
# 取整: 之前 ceil(正的非整数) 错成 floor
out=$(run --eval "ceil(1.5)" --hide all --show plain)
contains "取整: ceil(1.5) = 2" "= 2" "$out"
out=$(run --eval "ceil(-1.5)" --hide all --show plain)
contains "取整: ceil(-1.5) = -1" "= -1" "$out"
out=$(run --eval "floor(-1.5)" --hide all --show plain)
contains "取整: floor(-1.5) = -2" "= -2" "$out"
out=$(run --eval "⌈1.5⌉" --hide all --show plain)
contains "取整: ⌈⌉ 括号写法" "= 2" "$out"
out=$(run --eval "⌊1.5⌋" --hide all --show plain)
contains "取整: ⌊⌋ 括号写法" "= 1" "$out"

# ---------- 反三角/对数的精确形式 + 复数结果可见性 ----------
out=$(run --eval "asin(0.5)" --hide all --show plain)
contains "精确形式: asin(1/2) = π/6" "π/6" "$out"
out=$(run --eval "acos(0.5)" --hide all --show plain)
contains "精确形式: acos(1/2) = π/3" "π/3" "$out"
out=$(run --eval "atan(1)" --hide all --show plain)
contains "精确形式: atan(1) = π/4" "π/4" "$out"
out=$(run --eval "deg(180)" --hide all --show plain)
contains "deg(180) = π(不是解析失败)" "π" "$out"
out=$(run --eval "45°" --hide all --show plain)
contains "45° = π/4" "π/4" "$out"
out=$(run --eval "log(8,2)" --hide all --show approx)
contains "两参对数 log(8,2) = 3" "3" "$out"
out=$(run --eval "log(100,10)" --hide all --show approx)
contains "两参对数 log(100,10) = 2" "2" "$out"
# 复数的结果必须发在普通求值通道(以前发到 solution, --show plain 时整条看不见)
out=$(run --eval "i*i" --hide all --show plain)
contains "复数结果在 plain 通道可见" "-1" "$out"
out=$(run --eval "(1+i)^2" --hide all --show plain)
contains "复数幂精确形式" "2i" "$out"
out=$(run --eval "|3+4i|" --hide all --show plain)
contains "复数取模" "5" "$out"

# ---------- 真·解题步骤(--show step) ----------
out=$(run --solve "x^2-5x+6=0" --hide all --show step)
contains "步骤: 一元二次标准形式" "整理成标准形式" "$out"
contains "步骤: 判别式计算" "判别式: Δ = b^2 - 4ac = 1" "$out"
contains "步骤: 判别式符号判断" "Δ > 0: 两个不等实根" "$out"
contains "步骤: 求根公式" "求根公式" "$out"
contains "步骤: 因式分解" "因式分解" "$out"
contains "步骤: 两根" "两个根" "$out"
out=$(run --solve "2x+3=7" --hide all --show step)
contains "步骤: 移项" "移项" "$out"
contains "步骤: 系数化为 1" "两边同除以 2" "$out"
out=$(run --solve "x^2+2x+2=0" --hide all --show step)
contains "步骤: 复根情形" "共轭复根" "$out"
# 默认引擎是 SymPy, 步骤必须同样出现(历史上只有内置引擎才有)
out=$(run --solve "x^2-4=0" --hide all --show step)
contains "步骤: SymPy 路径也有步骤" "判别式" "$out"
out=$(run --engine builtin --solve "x^2-4=0" --hide all --show step)
contains "步骤: 内置引擎也有步骤" "判别式" "$out"
# 不能瞎编: 高次没有求根公式式步骤
out=$(run --solve "x^3-6x^2+11x-6=0" --hide all --show step)
notcontains "步骤: 高次不乱写求根公式" "判别式" "$out"

# ---------- 韦达定理 / 因式分解符号 ----------
out=$(run --solve "x^2-5x+6=0" --hide all --show step)
contains "步骤: 韦达定理两根和" "韦达定理" "$out"
contains "步骤: 韦达定理数值" "x₁ + x₂ = -b/a = 5" "$out"
contains "步骤: 韦达定理两根积" "x₁ · x₂ = c/a = 6" "$out"
out=$(run --solve "2x^2+3x-2=0" --hide all --show step)
contains "步骤: 负根的因式分解不带双负号" "(x + 2)" "$out"
notcontains "步骤: 不出现 x - -2" "x - -2" "$out"
out=$(run --solve "x^2+1=0" --hide all --show step)
contains "步骤: 复根也有韦达定理" "韦达定理" "$out"

# ---------- 超越方程的有理根精确化 ----------
out=$(run --solve "2^x=8" --hide all --show solution)
contains "有理根精确化: 2^x=8 -> x = 3" "x = 3" "$out"
notcontains "有理根精确化: 不再写成近似" "≈ 3" "$out"
out=$(run --solve "3^x=1/9" --hide all --show solution)
contains "有理根精确化: 3^x=1/9 -> x = -2" "x = -2" "$out"
out=$(run --solve "5^x=1/25" --hide all --show solution)
contains "有理根精确化: 5^x=1/25 -> x = -2" "x = -2" "$out"
out=$(run --solve "2^x=10" --hide all --show solution)
contains "无理根仍如实给近似(不能假装精确)" "≈" "$out"
notcontains "无理根不能写成等号精确值" "x = 3.32" "$out"
out=$(run --solve "sin(x)=0.5" --hide all --show solution)
contains "三角方程仍是精确解集" "n·π" "$out"

# ---------- 函数定义 f(x)=… (方程+函数类) ----------
out=$(run --solve "f(x)=x^2-3, f(x)=0" --hide all --show solution)
contains "函数定义: 求零点(正根)" "√(3)" "$out"
contains "函数定义: 求零点(负根)" "-√(3)" "$out"
out=$(run --solve "g(t)=2t+1, g(t)=7" --hide all --show solution)
contains "函数定义: 自定义函数名与变量" "t = 3" "$out"
out=$(run --solve "f(x)=x^2-3, f(x)=6" --hide all --show solution)
contains "函数定义: 求 f(x)=6" "x_2 = 3" "$out"
out=$(run --eval "f(x)=x^2-3, f(2)" --hide all --show plain)
contains "函数定义: 代入求值 f(2) = 1" "= 1" "$out"
out=$(run --solve "f(x)=x^2-3, f(x)=0, x>0" --hide all --show solution)
contains "函数定义: 配合约束" "√(3)" "$out"
out=$(run --solve "f(x)=x^2-3" --hide all 2>&1)
contains "函数定义: 只给定义要明确报错" "只给了函数定义" "$out"
out=$(run --eval "h(x,y)=x+y, h(1,2)" --hide all --show plain 2>&1)
contains "函数定义: 多参数(求值)" "= 3" "$out"

# ---------- 步骤: 线性方程组的消元过程 ----------
out=$(run --solve "x+y=5, x-y=1" --hide all --show step)
contains "步骤: 列出原方程组" "原方程组" "$out"
contains "步骤: 加减消元(行变换)" "消去 x" "$out"
contains "步骤: 消元后的方程" "-2y = -4" "$out"
contains "步骤: 回代 y" "回代得: y = 2" "$out"
contains "步骤: 回代 x" "回代得: x = 3" "$out"
out=$(run --solve "2x+3y=12, x-y=1" --hide all --show step)
contains "步骤: 系数不为 1 也消元" "消去 x" "$out"
contains "步骤: 三元/二元的解一致" "回代得: y = 2" "$out"
out=$(run --solve "x+y+z=6, x-y=1, x+y-z=2" --hide all --show step)
contains "步骤: 三元消元" "消去 x" "$out"
contains "步骤: 三元回代 z" "回代得: z = 2" "$out"
out=$(run --solve "x+y=1, 2x+2y=2" --hide all --show step,solution)
notcontains "步骤: 无穷多解不硬写消元过程" "回代得" "$out"
out=$(run --solve "x+y=1, x+y=2" --hide all --show step,solution)
notcontains "步骤: 无解不硬写消元过程" "回代得" "$out"

# ---------- 字体包: 中文目录名(GBK/无标志位 UTF-8) ----------
out=$(run --font-list)
contains "字体包: 中文目录名正常显示(不乱码)" "简体" "$out"
out=$(run --fonts)
contains "字体包: 机器清单也带真实名字" "简体" "$out"
out=$(run --glyph "A" --font "简体" --hide all --show note)
contains "字体包: 能按中文名选字体" "vivo Sans" "$out"
out=$(run --glyph "A" --font "SC" --hide all --show note)
contains "字体包: ASCII 名字匹配不受影响" "SC" "$out"
out=$(run --glyph "A" --hide all --show solution)
contains "字体包: 默认字体仍可正常还原" "字形" "$out"

# ---------- 复平面轨迹(z/w 当复变量) ----------
out=$(run --solve "|z-1|=2" --hide all --show solution)
contains "轨迹: 圆" "圆" "$out"
contains "轨迹: 圆心" "圆心 1" "$out"
contains "轨迹: 半径" "半径 2" "$out"
contains "轨迹: 直角坐标方程" "(x - 1)^2 + y^2 = 2^2" "$out"
out=$(run --solve "|z|=1" --hide all --show solution)
contains "轨迹: |z|=1 是圆" "圆心 0, 半径 1" "$out"
out=$(run --solve "|z-1|<2" --hide all --show solution)
contains "轨迹: < 是圆盘" "圆盘" "$out"
out=$(run --solve "|z-1|>2" --hide all --show solution)
contains "轨迹: > 是圆外" "外部" "$out"
out=$(run --solve "|z-1|<=2" --hide all --show solution)
contains "轨迹: <= 含边界" "含边界" "$out"
out=$(run --solve "|z-1|=|z-i|" --hide all --show solution)
contains "轨迹: 垂直平分线" "垂直平分线" "$out"
notcontains "轨迹: 复圆心不再出现 /1" "(1 + i)/1" "$out"
out=$(run --solve "|z-(1+i)|=2" --hide all --show solution)
contains "轨迹: 复圆心" "圆心 1 + i" "$out"
# 实数绝对值方程不能被当成轨迹(回归)
out=$(run --solve "|x-1|=2" --hide all --show solution)
contains "轨迹: 实数绝对值方程不受影响(根 3)" "3" "$out"
notcontains "轨迹: 实数绝对值方程不给圆心" "圆心" "$out"
out=$(run --solve "|2x-1|=5" --hide all --show solution)
notcontains "轨迹: 系数不为 1 也不误判" "圆心" "$out"

# ---------- 机器可读字体清单(安卓/测试用, 带 RAWHEX) ----------
out=$(run --font-entries)
contains "机器清单: 有 CHOSEN" "CHOSEN" "$out"
contains "机器清单: 有 PACK" "PACK" "$out"
if printf '%s' "$out" | awk -F'\t' '$1=="FONT" && NF>=5 && length($5)%2==0 && length($5)>0 {n++} END{exit !(n>0)}'; then
    ok
else
    fail "机器清单: 每个 FONT 行要带偶数长度的 RAWHEX" "字段不足或 hex 长度不是偶数"
fi
# RAWHEX 必须能解回 UTF-8 名字(与第 4 段一致)
if printf '%s' "$out" | awk -F'\t' '$1=="FONT" && NF>=5 {print $5}' | head -1 | \
   python3 -c "import sys,binascii; h=sys.stdin.read().strip(); b=binascii.unhexlify(h); s=b.decode('utf-8'); sys.exit(0 if 'vivo Sans' in s else 1)"; then
    ok
else
    fail "机器清单: RAWHEX 要能解回真实名字" "解不出来"
fi

# ---------- 可变字体(fvar): 轴与命名实例 ----------
out=$(run --font-variations --font SCVF)
contains "可变字体: 认得出是可变字体" "VARIABLE	1" "$out"
contains "可变字体: wght 轴" "AXIS	wght" "$out"
contains "可变字体: opsz 轴" "AXIS	opsz" "$out"
contains "可变字体: 轴的范围(最小值)" "	100	400	850" "$out"
contains "可变字体: 命名实例" "INSTANCE	1" "$out"
out=$(run --font-variations)
contains "可变字体: 非可变字体要明确说明" "VARIABLE	0" "$out"
notcontains "可变字体: 非可变字体不给轴" "AXIS" "$out"

# ---------- 可变字体: 轴/实例(--varied) ----------
out=$(run --varied 中 --font SCVF)
contains "可变字体: 默认轴为 400" "COORDS	wght=400	opsz=16" "$out"
contains "可变字体: 默认实例即静态轮廓" "点=24" "$out"
out=$(run --varied 中 --font SCVF --font-axis wght=850)
contains "可变字体: wght=850 生效" "wght=850" "$out"
contains "可变字体: 粗体包围盒更宽" "x[85,914]" "$out"
contains "可变字体: 点数不变(850)" "点=24" "$out"
out=$(run --varied 中 --font SCVF --font-axis wght=100)
contains "可变字体: 细体包围盒更窄" "x[126,876]" "$out"
contains "可变字体: 点数不变(100)" "点=24" "$out"
out=$(run --varied 中 --font SCVF --font-instance 1)
contains "可变字体: 按序号选实例" "wght=100" "$out"
out=$(run --varied 中 --font SCVF --font-axis wght=9999 2>&1)
contains "可变字体: 轴值超范围要报错" "超出范围" "$out"
out=$(run --varied 中 --font SCVF --font-axis badtag=1 2>&1)
contains "可变字体: 未知轴要报错" "没有这个轴" "$out"
out=$(run --varied 中 --font SCVF --font-instance 999 2>&1)
contains "可变字体: 实例不存在要报错" "字体实例不存在" "$out"
out=$(run --varied A)
contains "可变字体: 非可变字体降级" "VARIABLE	0" "$out"

# ---------- 可变字体: 接入轮廓(同一字符不同字重 -> 不同函数) ----------
out=$(run --glyph 中 --font SCVF --font-axis wght=100 --hide all --show solution,note)
contains "变体接通: 打印生效轴值" "变体: wght=100" "$out"
contains "变体接通: 细体第一段函数" "-334t + 723" "$out"
contains "变体接通: 段数不变(细)" "函数 24 个" "$out"
out=$(run --glyph 中 --font SCVF --font-axis wght=850 --hide all --show solution)
contains "变体接通: 粗体第一段函数" "-184t + 676" "$out"
notcontains "变体接通: 粗细生成的函数确实不同" "-334t" "$out"
contains "变体接通: 段数不变(粗)" "函数 24 个" "$out"
out=$(run --glyph 中 --font SCVF --font-instance 1 --hide all --show note)
contains "变体接通: 按实例序号也生效" "变体: wght=100" "$out"
out=$(run --glyph 中 --font SCVF --font-axis wght=9999 --hide all 2>&1)
contains "变体接通: 非法轴值要报错" "变体设置无效" "$out"
out=$(run --glyph A --font-axis wght=700 --hide all --show note)
contains "变体接通: 非可变字体给提示" "不是可变字体" "$out"

# ---------- A: GPOS 成对字距 ----------
out=$(run --kern AV)
contains "字距: 有 kern 特性" "HASKERN	1" "$out"
contains "字距: AV 为负(更紧)" "字距=-75" "$out"
out=$(run --kern Ta)
contains "字距: Ta 也为负" "字距=-50" "$out"
out=$(run --kern AA)
contains "字距: 无字距字对为 0" "字距=0" "$out"
out=$(run --kern A 2>&1)
contains "字距: 只给一个字要报错" "请给两个字" "$out"
out=$(run --glyph AV --hide all --show solution)
contains "字距: 排版仍能正常出函数" "轮廓1 段1" "$out"
# A/B 可观测对照: 开/关字距的第二个字形必须正好差 75(等于 GPOS 里 AV 的字距值)
run --glyph AV --hide all --show solution > /tmp/em_k1.txt 2>&1
run --glyph AV --no-kern --hide all --show solution > /tmp/em_k2.txt 2>&1
if python3 -c "
import re, sys
from fractions import Fraction
def c(f):
    return re.findall(r'x\(t\) = ([ -\d/]+)t \+ ([ -\d/]+)', open(f, encoding='utf-8').read())
A, B = c('/tmp/em_k1.txt'), c('/tmp/em_k2.txt')
if len(A) != len(B) or not A: sys.exit(1)
d = set()
for (a1,a0),(b1,b0) in zip(A,B):
    for x,y in ((a1,b1),(a0,b0)):
        if x != y: d.add(Fraction(x) - Fraction(y))
sys.exit(0 if d == {Fraction(-75)} else 1)
"; then
    ok
else
    fail "字距: 开关前后第二字必须正好差 75" "diff 不是 {-75}"
fi

# ---------- A: GSUB 连字(接入排版) ----------
out=$(run --glyph AC --hide all --show tip)
contains "连字: 生效并给出替换字形" "连字: AC -> 字形 29493" "$out"
out=$(run --glyph AC --no-liga --hide all --show tip)
notcontains "连字: 关闭后不替换" "连字: AC" "$out"
ka=$(run --glyph AC --hide all --show solution | grep -o "合计: 字形 [0-9]* 个, 函数 [0-9]* 个" | head -1)
kb=$(run --glyph AC --no-liga --hide all --show solution | grep -o "合计: 字形 [0-9]* 个, 函数 [0-9]* 个" | head -1)
if [ -n "$ka" ] && [ -n "$kb" ] && [ "$ka" != "$kb" ]; then
    ok
else
    fail "连字: 开关前后函数数必须不同" "[$ka] vs [$kb]"
fi
out=$(run --glyph AB --hide all --show tip)
notcontains "连字: 无连字字对不受影响" "连字: AB" "$out"

# ---------- B1: CFF(OTF) 结构读取(--cff-info) ----------
LOMA=/usr/share/fonts/opentype/tlwg/Loma.otf
if [ -f "$LOMA" ]; then
    out=$(run --cff-info "$LOMA")
    contains "CFF: 表偏移与长度" "off=6468	len=37144" "$out"
    contains "CFF: 版本与头长度" "版本=1.0	hdrSize=4" "$out"
    contains "CFF: Name INDEX 条数" "NAME	1" "$out"
    contains "CFF: TopDICT 条数" "TOPDICT	1" "$out"
    contains "CFF: String INDEX 条数" "STRING	156" "$out"
    contains "CFF: 全局子程序条数" "GSUBRS	0" "$out"
    contains "CFF: CharStrings 偏移" "CHARSTRINGS	off=2586" "$out"
    contains "CFF: CharStrings 条数" "条数=368" "$out"
    contains "CFF: Private 大小与偏移" "PRIVATE	size=43	off=33431" "$out"
    contains "CFF: 局部子程序偏移" "Subrs=43" "$out"
    contains "CFF: charset 偏移" "CHARSET	off=1851" "$out"
    out=$(run --cff-info /etc/hostname 2>&1)
    contains "CFF: 非字体文件要明确报错" "错误: " "$out"
    out=$(run --cff-info /nonexistent.ttf 2>&1)
    contains "CFF: 文件不存在要明确报错" "错误" "$out"
else
    echo "  (跳过: 系统没有 $LOMA)"
    ok
fi

# ---------- B2: Type2 charstring 解释(--cff-outline) ----------
if [ -f "$LOMA" ]; then
    out=$(run --cff-info "$LOMA" --cff-outline A)
    contains "CFF: Type2 端点个数" "端点=11" "$out"
    contains "CFF: Type2 包围盒" "包围盒 x[1,1364] y[0,1450]" "$out"
    contains "CFF: Type2 前 6 端点" "前6端点: (1364,0) (785,1450) (580,1450) (1,0) (206,0) (396,477)" "$out"
    contains "CFF: Type2 走了本地子程序" "子程序调用=" "$out"
    out=$(run --cff-info /etc/hostname --cff-outline A 2>&1)
    contains "CFF: 非字体做轮廓要报错" "错误: " "$out"
    out=$(run --cff-info "$LOMA" --cff-outline A)
    contains "CFF/B3: 轮廓能进拟合管线" "拟合: 轮廓=1	段数=10" "$out"
    contains "CFF/B3: 真的产出函数" "首个函数: x(x) = -579x + 1364, y(x) = 1450x" "$out"
    contains "CFF: 按字符走 cmap 查字形" "字符=A	gid=35" "$out"
    contains "CFF: OTF 输出函数行(第1段)" "轮廓1 段1: x(x) = -579x + 1364, y(x) = 1450x" "$out"
    contains "CFF: OTF 输出函数行(第2段)" "轮廓1 段2: x(x) = -205x + 785, y(x) = 1450" "$out"
    out=$(run --cff-info "$LOMA" --cff-outline 中 2>&1)
    contains "CFF: cmap 里没有的字符(中文)要报错" "错误: " "$out"
else
    echo "  (跳过: 系统没有 $LOMA)"
    ok
fi

# ---------- 高精度后端的位数守门(不打印二进制尾巴) ----------
out=$(run --eval pi --decimals 100 --hpfloat=builtin --hide all --show approx,note)
contains "高精度: 后端未给到数值时要说明" "只有 31 位是可靠的" "$out"
if printf '%s' "$out" | grep -qE "3\.1415926535897932384626[0-9]{10}"; then
    fail "高精度: 不该打印超过可靠位数的二进制尾巴" "输出里出现了 30+ 位的尾巴"
else
    ok
fi
out=$(run --eval pi --decimals 100 --hide all --show approx)
contains "高精度: 有 MPFR 时给满 100 位" "3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117068" "$out"

# ---------- B4: CFF 坏数据/边界 ----------
if [ -f "$LOMA" ]; then
    # 截断到 8KB: 表目录还在, 但 CFF 数据不全 -> 必须明确报错而不是崩
    head -c 8192 "$LOMA" > /tmp/em_trunc.otf
    out=$(run --cff-info /tmp/em_trunc.otf 2>&1)
    contains "CFF 边界: 截断文件要明确报错" "错误: " "$out"
    # 只保留前 100 字节: 表目录都不全
    head -c 100 "$LOMA" > /tmp/em_tiny.otf
    out=$(run --cff-info /tmp/em_tiny.otf 2>&1)
    contains "CFF 边界: 极短文件要明确报错" "错误: " "$out"
    # 完全不是字体: 随便一段文本
    printf 'hello world this is not a font at all' > /tmp/em_notfont.otf
    out=$(run --cff-info /tmp/em_notfont.otf 2>&1)
    contains "CFF 边界: 非字体文件要明确报错" "错误: " "$out"
    # 正常字体仍然可用(回归)
    out=$(run --cff-info "$LOMA")
    contains "CFF 边界: 正常字体仍可用" "CHARSTRINGS	off=2586" "$out"
    rm -f /tmp/em_trunc.otf /tmp/em_tiny.otf /tmp/em_notfont.otf
else
    echo "  (跳过: 系统没有 $LOMA)"; ok
fi

# ---------- B3: OTF 多字符走正常入口(--cff-text) ----------
if [ -f "$LOMA" ]; then
    out=$(run --cff-info "$LOMA" --cff-text AB)
    contains "OTF 文字: 报出字符与推进宽度(hmtx)" "字符 A (gid 35, 推进 1366)" "$out"
    contains "OTF 文字: 第一个字符的第一段函数" "段1: x(x) = -579x + 1364, y(x) = 1450x" "$out"
    contains "OTF 文字: 汇总(字符数/段数)" "字符 2 个" "$out"
    out=$(run --cff-info "$LOMA" --cff-text 中 2>&1)
    contains "OTF 文字: 缺字要如实报告" "缺字" "$out"
else
    echo "  (跳过: 系统没有 $LOMA)"; ok
fi

# ---------- B3: OTF 全 ASCII 冒烟(94 个可打印字符) ----------
if [ -f "$LOMA" ]; then
    S=$(python3 -c "print(''.join(chr(c) for c in range(33,127)))")
    out=$(run --cff-info "$LOMA" --cff-text "$S")
    contains "OTF 冒烟: 94 个字符全部还原" "字符 94 个" "$out"
    contains "OTF 冒烟: 段合计" "段合计 3775" "$out"
    if printf '%s' "$out" | grep -qE "缺字|取轮廓失败"; then
        fail "OTF 冒烟: 不该有缺字或取轮廓失败" "输出里出现了失败行"
    else
        ok
    fi
else
    echo "  (跳过: 系统没有 $LOMA)"; ok
fi

# ---------- 版本号一致性(桌面端 / APK / 打包脚本 必须同一个数) ----------
if [ -f android/app/build.gradle ]; then
  BINVER=$(run --version | grep -oE "[0-9]+\.[0-9]+\.[0-9]+" | head -1)
  GRADLEVER=$(grep -oE "versionName '[^']+'" android/app/build.gradle | head -1 | cut -d"'" -f2)
  SRCVER=$(grep -hoE 'kVersion = "[^"]+"' src/config.cpp src/cli.cpp 2>/dev/null | head -1 | cut -d'"' -f2)
  contains "版本: 桌面端与 APK 一致" "$GRADLEVER" "EasyMath $BINVER"
  contains "版本: 源码常量与 APK 一致" "$GRADLEVER" "$SRCVER"
  if grep -q "EasyMath 1.0.0" <<<"$BINVER"; then bad "版本: 不该还是 1.0.0(与 APK 漂移)"; else ok; fi
fi

# ---------- Ctrl+C 安全退出 ----------
FIFO="$TMP/fifo"; mkfifo "$FIFO"
"$BIN" -i < "$FIFO" > "$TMP/sigint.log" 2>&1 &
PID=$!
exec 3> "$FIFO"
sleep 0.6
kill -INT "$PID" 2>/dev/null
wait "$PID"; RC=$?
exec 3>&-
expect_exit "Ctrl+C 安全退出" 130 "$RC"

echo
echo "CLI 测试: 通过 $PASS 项, 失败 $FAIL 项, 跳过 $SKIP 项"
rm -rf "$TMP"
[ "$FAIL" -eq 0 ]

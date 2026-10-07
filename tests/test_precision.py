#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""EasyMath 精度测试: 逐位对照独立预言机(mpmath / Fraction / int)。

独立的含义: 参考值全部由 mpmath 以 >=200 位十进制重算, 不引用 EasyMath 的任何结果,
因此任何"少算了几位""末位错""悄悄退化成 double/long double"的问题都会暴露。

检查三件事:
  1) 够精密: 打印出的每个小数与参考值偏差 <= 半个末位, 且位数达到 --decimals 要求;
  2) 够精确: 整数/分数结果必须一位不差(用 Python 大整数与 Fraction 比对);
  3) 不遗漏: 多根/方程组的每一个根(实部+虚部)都必须有达到位数的数值。

用法: test_precision.py [二进制] [--digits=20,30,50] [--quick] [--only=组名]
"""
import re
import subprocess
import sys
from fractions import Fraction

try:
    import mpmath as mp
except ImportError:
    print("需要 mpmath: pip install mpmath")
    sys.exit(2)

mp.mp.dps = 220

BIN = "./EasyMath"
DIGITS = [20, 30, 50]
QUICK = False
ONLY = None
for a in sys.argv[1:]:
    if a.startswith("--digits="):
        DIGITS = [int(x) for x in a.split("=", 1)[1].split(",")]
    elif a == "--quick":
        QUICK = True
        DIGITS = [20, 30]
    elif a.startswith("--only="):
        ONLY = a.split("=", 1)[1]
    elif not a.startswith("-"):
        BIN = a

checks = 0
fails = []
skips = []

# 输出通道: 只看"结果类"行, 丢掉回显输入与规范化行(它们含输入里的数字)
ECHO_PREFIX = ("输入:", "Input:", "规范形式:", "Normalized:", "模式:", "Mode:", "语言",
               "Language:", "引擎", "Engine", "配置", "Config", "当前", "Current",
               # 方法/版本行里有 "SymPy 1.14.0" 这种版本号, 会被当成数值
               "方法:", "Method:", "版本:", "Version:", "后端:", "Backend:")

NUM = re.compile(r"(?<![\w.])(-?\d+\.\d+)(?:[eE]([+-]?\d+))?")
INT = re.compile(r"(?<![\d.])(\d{10,})(?![\d.])")


def run(args, timeout=600):
    return subprocess.run([BIN] + args, capture_output=True, text=True, timeout=timeout)


def result_lines(out):
    keep = []
    for ln in out.splitlines():
        s = ln.strip()
        if not s:
            continue
        if s.startswith(ECHO_PREFIX):
            continue
        keep.append(ln)
    return "\n".join(keep)


def binary_sign_before(text, start):
    """'a - 2.17i' 这种形态里, '-' 是独立运算符, 数字 token 本身不带符号。
    识别它并返回符号乘数, 使虚部能带上正确的正负。"""
    i = start - 1
    while i >= 0 and text[i] in " \t":
        i -= 1
    if i < 0 or text[i] not in "+-":
        return 1
    j = i - 1
    while j >= 0 and text[j] in " \t":
        j -= 1
    if j >= 0 and (text[j].isdigit() or text[j] in ")]i."):
        return -1 if text[i] == "-" else 1
    return 1


def numbers(text):
    """抽取所有小数 -> [(值, 显示位数, 是否截断, 指数)]"""
    got = []
    for m in NUM.finditer(text):
        s, e = m.group(1), m.group(2)
        exp = int(e) if e else 0
        val = mp.mpf(s) * (mp.power(10, exp) if exp else 1)
        if not s.startswith("-"):
            val *= binary_sign_before(text, m.start())
        ip, fp = s.split(".")
        shown = (len(ip.lstrip("-")) + len(fp) - 1) if e else len(fp)
        trunc = text[m.end():m.end() + 1] == "."
        got.append((val, shown, trunc, exp))
    return got


def exact_token_present(text, ref):
    """参考值为整数/简单分数时, 允许输出用精确形态表示(不做小数近似)。
    注意: 参考值常常是"浮点近似下的整数"(如 tan(135°) 算出来是 -0.9999...),
    这种也要认, 否则精确形态的输出会被误判为缺失。"""
    ref = mp.mpmathify(ref)
    near = mp.nint(ref)
    if abs(ref - near) <= mp.mpf("1e-25") * max(1, abs(ref)) and abs(near) < mp.mpf(10) ** 40:
        pat = r"(?<![\d.])%s(?!\d)" % re.escape(str(int(near)))
        return re.search(pat, text) is not None
    return False


def tol_of(shown, trunc, exp=0):
    """允许的偏差: 正确舍入 -> 半个末位; 循环小数截断 -> 一个末位"""
    return mp.power(10, exp - shown) * (mp.mpf("1.01") if trunc else mp.mpf("0.5000001"))


def expect(label, text, targets, digits):
    """targets: [(名字, 参考值, 是否要求位数达标)]"""
    global checks
    got = numbers(text)
    for name, ref, need_len in targets:
        ref = mp.mpmathify(ref)
        best = None
        for (val, shown, trunc, exp) in got:
            err = abs(val - ref)
            if err <= tol_of(shown, trunc, exp):
                checks += 1
                if best is None or shown > best[1]:
                    best = (val, shown, err, trunc, exp)
        if best is None:
            # 精确形态(整数/分数, 如 x_1 = 1、根号前的 1/2)没有小数:
            # 若参考值本身是有限小数/整数, 接受逐字匹配
            if exact_token_present(text, ref):
                checks += 1
                continue
            fails.append("%s: 输出里没有匹配 %s 的值(参考 %s)\n      输出: %s"
                         % (label, name, mp.nstr(ref, digits + 6), text.strip()[:300]))
            continue
        if need_len and not best[3] and best[2] != 0 and best[1] < digits - 2:
            fails.append("%s: %s 只给出 %d 位小数, 要求约 %d 位 (值 %s)"
                         % (label, name, best[1], digits, mp.nstr(best[0], 40)))
    # 反向检查: 输出里出现的每一个小数都必须能对上某个目标值。
    # 这一条专门抓"同一个量在别处又用低精度打印了一遍"这类不一致
    # (曾经斜率标签里的角度就是旧的 21 位 pi 算出来的)。
    for (val, shown, trunc, exp) in got:
        if any(abs(val - mp.mpmathify(r)) <= tol_of(shown, trunc, exp) for (_n, r, _f) in targets):
            continue
        fails.append("%s: 输出里有对不上的数值 %s (目标: %s)"
                     % (label, mp.nstr(val, min(shown + 2, 40)),
                        ", ".join(mp.nstr(mp.mpmathify(r), 25) for (_n, r, _f) in targets[:6])))


def expect_int(label, text, want):
    """精确整数: 输出里所有 >=10 位的整数必须都等于 want, 且 want 必须出现"""
    global checks
    got = [int(m.group(1)) for m in INT.finditer(text)]
    if not got:
        fails.append("%s: 输出里没有找到大整数(期望 %d 位)" % (label, len(str(want))))
        return
    bad = [v for v in got if v != want]
    if bad:
        fails.append("%s: 整数不对\n      期望 %s\n      得到 %s"
                     % (label, want, bad[0]))
    elif want not in got:
        fails.append("%s: 期望值未出现 (%d 位)" % (label, len(str(want))))
    else:
        checks += 1


def roots_of(coeffs):
    """独立预言机: mpmath 自带的 Durand-Kerner 多项式求根"""
    return mp.polyroots(coeffs, maxsteps=800, extraprec=600)


def root_targets(z, digits, need_len=True):
    z = mp.mpmathify(z)
    out = []
    for nm, part in (("实部", mp.re(z)), ("虚部", mp.im(z))):
        if abs(part) > mp.mpf(10) ** (-(digits + 1)):
            out.append((nm, part, need_len))
    return out


# ---------------------------------------------------------------- 组1: 求值(小数)
IRRATIONAL_EVAL = [
    ("√6", lambda: mp.sqrt(6)),
    ("√2", lambda: mp.sqrt(2)),
    ("√(1/3)", lambda: mp.sqrt(mp.mpf(1) / 3)),
    ("2^(1/7)", lambda: mp.power(2, mp.mpf(1) / 7)),
    ("2^0.5", lambda: mp.sqrt(2)),
    ("2^(2/3)", lambda: mp.power(2, mp.mpf(2) / 3)),
    ("π", lambda: mp.pi),
    ("π^2", lambda: mp.pi ** 2),
    ("e^3", lambda: mp.exp(3)),
    ("e^π", lambda: mp.exp(mp.pi)),
    ("ln(2)", lambda: mp.log(2)),
    ("log(2)", lambda: mp.log(2) / mp.log(10)),
    ("sin(1)", lambda: mp.sin(1)),
    ("cos(1)", lambda: mp.cos(1)),
    ("tan(1)", lambda: mp.tan(1)),
    ("(1+√5)/2", lambda: (1 + mp.sqrt(5)) / 2),
    ("√6+√2", lambda: mp.sqrt(6) + mp.sqrt(2)),
    ("-√2", lambda: -mp.sqrt(2)),
    ("1/7", lambda: mp.mpf(1) / 7),
    ("22/7", lambda: mp.mpf(22) / 7),
    ("1/3", lambda: mp.mpf(1) / 3),
    ("2/3", lambda: mp.mpf(2) / 3),
    ("1/0.5", lambda: mp.mpf(2)),
    ("0.1+0.2", lambda: mp.mpf(3) / 10),
    ("10/4", lambda: mp.mpf(5) / 2),
    ("2^-3", lambda: mp.mpf(1) / 8),
    ("1e-3", lambda: mp.mpf(1) / 1000),
]
# ------------------------------------------------------------- 组2: 精确大整数
EXACT_INT = [
    ("5!", 120),
    ("6^8", 1679616),
    ("2^100", 2 ** 100),
    ("2^200", 2 ** 200),
    ("2^1000", 2 ** 1000),
    ("20!", 1),
    ("100!", 1),
    ("3^500", 1),
    ("7^100", 1),
    ("√(2^200)", 1),
]
EXACT_INT = [(e, {"20!": __import__("math").factorial(20),
                  "100!": __import__("math").factorial(100),
                  "3^500": 3 ** 500,
                  "7^100": 7 ** 100,
                  "√(2^200)": 2 ** 100}[e] if e in ("20!", "100!", "3^500", "7^100", "√(2^200)") else v)
             for e, v in EXACT_INT]

# --------------------------------------------------------------- 组4: 解方程
def root_value_strings(text):
    """抽出 'x_1 = ...' 的右侧(去掉结尾的 (≈ ...) 说明)"""
    vals = []
    for ln in text.splitlines():
        m = re.search(r"^\s*x(?:_\d+)?\s*=\s*(.+?)\s*$", ln)
        if not m:
            continue
        v = re.split(r"\s+\(≈", m.group(1))[0]
        v = re.sub(r"\s*\(\s*\d+\s*重根?\s*\)", "", v).strip()
        vals.append(v)
    return vals


def check_exact_roots(label, text, want):
    """精确解: 根集合必须与期望完全一致(字符串级)"""
    global checks
    got = root_value_strings(text)
    if sorted(got) != sorted(want):
        fails.append("%s: 精确根集合不符\n      得到 %s\n      期望 %s" % (label, got, want))
    else:
        checks += 1


def sys_pairs(text):
    """抽出 '解 i: x = a, y = b' 的数对集合"""
    pairs = set()
    for ln in text.splitlines():
        m = re.match(r"\s*(?:解|Solution)\s*\d+:\s*(.+)$", ln)
        if not m:
            continue
        body = re.sub(r"\s*$", "", m.group(1))
        d = dict(re.findall(r"([a-zA-Z])\s*=\s*([^,]+)", body))
        if "x" in d and "y" in d:
            pairs.add((d["x"].strip(), d["y"].strip()))
    return pairs


def solve_cases():
    cs = [
        ("x^2=5", [mp.sqrt(5), -mp.sqrt(5)]),
        ("x^2+x+5=0", [mp.mpf(-1) / 2 + mp.sqrt(19) * 1j / 2,
                       mp.mpf(-1) / 2 - mp.sqrt(19) * 1j / 2]),
        ("x^3=2", None),
        ("x^4=16", [2, -2, 2j, -2j]),
        ("x^5-3x+1=0", None),
        ("x^7-x-1=0", None),
        ("x^6+x^3-1=0", None),
        ("x^2+y^2=5, y=x^2-1", None),
    ]
    out = []
    for eq, refs in cs:
        if refs is None:
            if eq == "x^3=2":
                r = mp.power(2, mp.mpf(1) / 3)
                refs = [r, r * mp.exp(2j * mp.pi / 3), r * mp.exp(-2j * mp.pi / 3)]
            elif eq == "x^5-3x+1=0":
                refs = roots_of([1, 0, 0, 0, -3, 1])
            elif eq == "x^7-x-1=0":
                refs = roots_of([1, 0, 0, 0, 0, 0, -1, -1])
            elif eq == "x^6+x^3-1=0":
                refs = roots_of([1, 0, 0, 1, 0, 0, -1])
            else:  # 方程组 x^2+y^2=5, y=x^2-1
                y1 = (mp.sqrt(17) - 1) / 2
                y2 = (-mp.sqrt(17) - 1) / 2
                x1 = mp.sqrt(y1 + 1)
                x2 = mp.sqrt(-(y2 + 1)) * 1j          # y+1<0 -> 纯虚
                refs = [x1, -x1, y1, x2, -x2, y2]
        out.append((eq, refs))
    return out


# --------------------------------------------------------------- 组5: 直线
def line_cases():
    """(输入, [(名字, 参考值, 是否要求位数)])"""
    def rad(deg):
        return mp.pi * deg / 180
    deg_cases = [("45°", 45), ("30°", 30), ("60°", 60), ("135°", 135), ("120°", 120),
                 ("150°", 150), ("-30°", -30), ("180°", 180), ("45", 45), ("1°", 1),
                 ("89°", 89), ("7°", 7), ("173°", 173)]
    out = []
    specials = {0, 45, 30, 60, 135, 120, 150, 180}
    for txt, deg in deg_cases:
        tg = [("弧度", rad(deg), True)]
        d = deg % 180
        if d not in specials:
            tg.append(("斜率", mp.tan(rad(deg)), True))
        out.append((txt, tg))
    # 弧度输入: 打印的是角度值, 不打印弧度小数
    out.append(("1rad", [("角度", mp.mpf(180) / mp.pi, True), ("斜率", mp.tan(1), True)]))
    out.append(("0.5rad", [("角度", mp.mpf(90) / mp.pi, True),
                           ("斜率", mp.tan(mp.mpf("0.5")), True)]))
    out.append(("2rad", [("角度", mp.mpf(360) / mp.pi, True), ("斜率", mp.tan(2), True)]))
    return out


# ------------------------------------------------------- 独立拉格朗日(分数精确)
def lagrange_coeffs(pts):
    """用 Fraction 独立算插值多项式系数: 返回 {次数: Fraction}"""
    n = len(pts)
    coef = {}
    for i, (xi, yi) in enumerate(pts):
        term = {0: Fraction(yi)}
        for j, (xj, _) in enumerate(pts):
            if j == i:
                continue
            nxt = {}
            for d, c in term.items():
                nxt[d + 1] = nxt.get(d + 1, Fraction(0)) + c
                nxt[d] = nxt.get(d, Fraction(0)) - c * Fraction(xj)
            term = nxt
        den = Fraction(1)
        for j, (xj, _) in enumerate(pts):
            if j != i:
                den *= Fraction(xi) - Fraction(xj)
        for d, c in term.items():
            coef[d] = coef.get(d, Fraction(0)) + c / den
    return {d: c for d, c in coef.items() if c != 0}


def parse_printed_poly(s):
    """解析 EasyMath 打印的 P(x) 多项式(纯文本形态, 如 '1/2x^2 - 1/2x + 1')"""
    s = s.replace(" ", "").replace("−", "-")
    if s in ("0", "-0", ""):
        return {}
    coef = {}
    for m in re.finditer(r"([+-]?)(\d+(?:/\d+)?)?(x(?:\^(\d+))?)?", s):
        sign, num, var, deg = m.group(1), m.group(2), m.group(3), m.group(4)
        if not num and not var:
            continue
        c = Fraction(num) if num else Fraction(1)
        if sign == "-":
            c = -c
        d = int(deg) if deg else (1 if var else 0)
        coef[d] = coef.get(d, Fraction(0)) + c
    return {d: c for d, c in coef.items() if c != 0}


def lagrange_printed_poly(text):
    """取最后一条 'P(x) = ...'(非 Σ 形式、非因式分解) 的右侧"""
    best = None
    for ln in text.splitlines():
        if "Σ" in ln or "因式分解" in ln or "简写" in ln:
            continue
        m = re.search(r"P\(x\)\s*=\s*(.+)$", ln)
        if m:
            best = m.group(1).strip()
    return best


def main():
    global checks
    if ONLY in (None, "eval"):
        for expr, ref in IRRATIONAL_EVAL:
            for d in DIGITS:
                r = run(["--eval", expr, "--decimals", str(d), "--engine-timeout", "60000"])
                text = result_lines(r.stdout + r.stderr)
                if r.returncode not in (0,):
                    fails.append("eval %s @%d 位: 退出码 %d\n      %s" % (expr, d, r.returncode, text[:200]))
                    continue
                expect("eval %s @%d" % (expr, d), text, [("值", ref(), True)], d)

    if ONLY in (None, "int"):
        for expr, want in EXACT_INT:
            r = run(["--eval", expr, "--decimals", "20"])
            text = result_lines(r.stdout + r.stderr)
            if r.returncode != 0:
                fails.append("精确整数 %s: 退出码 %d" % (expr, r.returncode))
                continue
            if want < 10 ** 10:
                expect("精确整数 %s" % expr, text, [("值", mp.mpf(want), False)], 20)
            else:
                expect_int("精确整数 %s" % expr, text, want)

    if ONLY in (None, "solve"):
        for eq, refs in solve_cases():
            for d in DIGITS:
                r = run(["--solve", eq, "--decimals", str(d), "--engine-timeout", "120000"])
                text = result_lines(r.stdout + r.stderr)
                if r.returncode != 0:
                    fails.append("solve %s @%d 位: 退出码 %d\n      %s" % (eq, d, r.returncode, text[:200]))
                    continue
                targets = []
                for z in refs:
                    targets += root_targets(z, d)
                # 多根的每个分量都必须有达标值; 重复分量去重以免噪声
                seen = set()
                uniq = []
                for nm, v, need in targets:
                    key = mp.nstr(v, 30)
                    if key in seen:
                        continue
                    seen.add(key)
                    uniq.append((nm, v, need))
                expect("solve %s @%d" % (eq, d), text, uniq, d)

    if ONLY in (None, "solve_exact"):
        for eq, want in [("x^3-6x^2+11x-6=0", ["1", "2", "3"]),
                         ("x^3-2x^2-5x+6=0", ["-2", "1", "3"]),
                         ("x^4=16", ["-2", "2", "-2i", "2i"]),
                         ("x^2=4/9", ["-2/3", "2/3"]),
                         ("x^2-2x+1=0", ["1"]),
                         ("x^3-3x^2+3x-1=0", ["1"])]:
            r = run(["--solve", eq, "--decimals", "20", "--engine-timeout", "60000"])
            text = result_lines(r.stdout + r.stderr)
            if r.returncode != 0:
                fails.append("solve_exact %s: 退出码 %d" % (eq, r.returncode))
                continue
            check_exact_roots("solve_exact %s" % eq, text, want)
        # 方程组精确解(整数对)
        r = run(["--solve", "x^2+y^2=5, x*y=2", "--decimals", "20", "--engine-timeout", "60000"])
        text = result_lines(r.stdout + r.stderr)
        got = sys_pairs(text)
        want_pairs = {("-2", "-1"), ("-1", "-2"), ("1", "2"), ("2", "1")}
        if got != want_pairs:
            fails.append("solve_exact 方程组: 解集不符\n      得到 %s\n      期望 %s" % (got, want_pairs))
        else:
            checks += 1

    if ONLY in (None, "line"):
        for ang, tg in line_cases():
            for d in DIGITS:
                r = run(["--line", ang, "--decimals", str(d)])
                text = result_lines(r.stdout + r.stderr)
                if r.returncode != 0:
                    fails.append("line %s @%d 位: 退出码 %d\n      %s" % (ang, d, r.returncode, text[:200]))
                    continue
                expect("line %s @%d" % (ang, d), text, tg, d)
        # 90° 是竖直直线, 不该给斜率数值
        r = run(["--line", "90°", "--decimals", "20"])
        text = result_lines(r.stdout + r.stderr)
        if "垂直" not in text and "perpendicular" not in text.lower() and "x = 0" not in text:
            fails.append("line 90°: 未识别为竖直直线\n      %s" % text[:200])
        else:
            checks += 1
        expect("line 90° 弧度", text, [("弧度", mp.pi / 2, True)], 20)

    if ONLY in (None, "lagrange"):
        cases = [
            ([(0, 1), (1, 2), (2, 5)], "x^2 + 1"),
            ([(1, 1), (2, 2), (4, 7)], "1/2x^2 - 1/2x + 1"),
            ([(0, 1), (1, 3), (2, 7)], "x^2 + x + 1"),
            ([(-2, 3), (-1, 5), (0, 7), (1, 9)], "2x + 7"),
            ([(0, 1), (1, 2), (3, 5), (4, 11)], None),
            ([(-3, -5), (-1, 4), (2, -3), (5, 8)], None),
            ([(1, 1), (2, 4), (3, 9), (4, 16), (5, 25)], "x^2"),
        ]
        for pts, want in cases:
            spec = " ".join("x=%d,y=%d" % p for p in pts)
            r = run(["--lagrange", spec, "--decimals", "20"])
            text = result_lines(r.stdout + r.stderr)
            if r.returncode != 0:
                fails.append("lagrange %s: 退出码 %d\n      %s" % (spec, r.returncode, text[:200]))
                continue
            printed = lagrange_printed_poly(text)
            if printed is None:
                fails.append("lagrange %s: 没找到 P(x) 展开式\n      %s" % (spec, text[:200]))
                continue
            got = parse_printed_poly(printed)
            want_c = lagrange_coeffs(pts)
            if got != want_c:
                fails.append("lagrange %s: 多项式系数不对\n      打印: %s -> %s\n      期望: %s"
                             % (spec, printed, got, want_c))
            else:
                checks += 1
            if want is not None and want.replace(" ", "") not in printed.replace(" ", ""):
                fails.append("lagrange %s: 展开式与期望形态不符 (期望含 %s, 得到 %s)" % (spec, want, printed))

    if ONLY in (None, "engine"):
        info = run(["--engine-info"]).stdout
        if "SymPy" not in info:
            skips.append("engine: 本机没有可用的 SymPy, 跳过双引擎一致性")
        else:
            for args in (["--eval", "√6"], ["--eval", "e^π"],
                         ["--solve", "x^3=2"], ["--solve", "x^5-3x+1=0"],
                         ["--line", "1rad"]):
                sets = {}
                for eng in ("builtin", "sympy"):
                    r = run(args + ["--decimals", "30", "--engine", eng, "--engine-timeout", "120000"])
                    text = result_lines(r.stdout + r.stderr)
                    if r.returncode != 0:
                        fails.append("engine %s %s: 退出码 %d" % (eng, " ".join(args), r.returncode))
                        sets = None
                        break
                    # 去重: 同一个值可能被打印不止一次(例如常量精确形式那行也带一个数值),
                    # 判据是"两个引擎给出的高精度值一致", 不是"行数相同"。
                    sets[eng] = sorted(set(mp.nstr(v, 30) for (v, L, _t, _e) in numbers(text)
                                           if L >= 28))
                if not sets:
                    continue
                # 判据: 两个引擎在请求位数内一致(允许 1 ulp)。
                # 不用字符串相等: 无 MPFR 的构建里 builtin 引擎只能算到 long double
                # 极限(~33 位), 30 位时末位可能差 1, 但这仍然是"一致"的正确答案。
                tol = mp.mpf(10) ** (-30)
                same = len(sets["builtin"]) == len(sets["sympy"]) and all(
                    abs(mp.mpf(a) - mp.mpf(b)) <= tol
                    for a, b in zip(sets["builtin"], sets["sympy"]))
                if not same:
                    fails.append("双引擎不一致 %s\n      builtin: %s\n      sympy  : %s"
                                 % (" ".join(args), sets["builtin"], sets["sympy"]))
                elif not sets["builtin"]:
                    fails.append("双引擎 %s: 都没有产出 30 位数值" % " ".join(args))
                else:
                    checks += 1

    if ONLY in (None, "round"):
        # 正确舍入: 必须 <= 0.5 ulp(即末位既不截断也不错位)
        vals = [("√6", mp.sqrt(6)), ("√2", mp.sqrt(2)), ("√(1/3)", mp.sqrt(mp.mpf(1) / 3)),
                ("2^(1/7)", mp.power(2, mp.mpf(1) / 7)), ("2^(2/3)", mp.power(2, mp.mpf(2) / 3)),
                ("π", mp.pi), ("π^2", mp.pi ** 2), ("e^3", mp.exp(3)), ("e^π", mp.exp(mp.pi)),
                ("ln(2)", mp.log(2)), ("log(2)", mp.log(2) / mp.log(10)),
                ("sin(1)", mp.sin(1)), ("cos(1)", mp.cos(1)), ("tan(1)", mp.tan(1)),
                ("(1+√5)/2", (1 + mp.sqrt(5)) / 2), ("√6+√2", mp.sqrt(6) + mp.sqrt(2)),
                ("22/7", mp.mpf(22) / 7), ("1/7", mp.mpf(1) / 7), ("2/3", mp.mpf(2) / 3),
                ("3π/4", mp.pi * 3 / 4), ("π/7", mp.pi / 7), ("√7", mp.sqrt(7)),
                ("e", mp.e), ("1/9999", mp.mpf(1) / 9999), ("355/113", mp.mpf(355) / 113)]
        for d in (8, 20, 30, 50):
            for expr, ref in vals:
                r = run(["--eval", expr, "--decimals", str(d)])
                text = result_lines(r.stdout + r.stderr)
                got = numbers(text)
                if not got:
                    fails.append("舍入 %s @%d: 没有小数输出" % (expr, d))
                    continue
                best = min(got, key=lambda t: abs(t[0] - ref))
                err, shown, trunc, exp = abs(best[0] - ref), best[1], best[2], best[3]
                ulp = tol_of(shown, trunc, exp)
                if err > ulp:
                    fails.append("舍入 %s @%d: 偏差 %.3f ulp > 0.5 (打印 %s, 正确 %s)"
                                 % (expr, d, float(err / mp.power(10, exp - shown)),
                                    mp.nstr(best[0], shown + 4), mp.nstr(ref, shown + 4)))
                else:
                    checks += 1

    if ONLY in (None, "conv"):
        # 度<->弧度的展示值(曾经用 21 位硬编码 pi, 末位会错)
        special_deg = {45, 135, 150, 180, 90}
        for ang, deg in [("135°", 135), ("150°", 150), ("45°", 45), ("1°", 1), ("89°", 89),
                         ("7°", 7), ("173°", 173), ("0.1°", mp.mpf("0.1"))]:
            deg = mp.mpmathify(deg)
            for d in (20, 40, 60):
                r = run(["--line", ang, "--decimals", str(d)])
                text = result_lines(r.stdout + r.stderr)
                # 输入的角度值本身也会打印(如 "0.1° = ... rad"), 是合法输出
                tg = [("角度输入", deg, False), ("弧度", mp.pi * deg / 180, True)]
                if int(deg) not in special_deg or deg != int(deg):
                    tg.append(("斜率", mp.tan(mp.pi * deg / 180), True))
                expect("换算 %s @%d" % (ang, d), text, tg, d)
        for ang, ref, ref_tan in [("1rad", mp.mpf(180) / mp.pi, mp.tan(1)),
                                  ("0.5rad", mp.mpf(90) / mp.pi, mp.tan(mp.mpf("0.5"))),
                                  ("2rad", mp.mpf(360) / mp.pi, mp.tan(2)),
                                  ("3rad", mp.mpf(540) / mp.pi, mp.tan(3))]:
            for d in (20, 40, 60):
                r = run(["--line", ang, "--decimals", str(d)])
                text = result_lines(r.stdout + r.stderr)
                expect("换算 %s @%d" % (ang, d), text,
                       [("角度", ref, True), ("斜率", ref_tan, True)], d)

    if ONLY in (None, "ultra"):
        # 超过 long double(本机 33 位)的位数, 证明没有退化
        for d in (80, 120):
            r = run(["--eval", "√6", "--decimals", str(d), "--precision", str(d * 4 + 40)])
            text = result_lines(r.stdout + r.stderr)
            expect("ultra √6 @%d 位" % d, text, [("值", mp.sqrt(6), True)], d)
            r = run(["--solve", "x^3=2", "--decimals", str(d), "--precision", str(d * 4 + 40),
                     "--engine-timeout", "300000"])
            text = result_lines(r.stdout + r.stderr)
            cube = mp.power(2, mp.mpf(1) / 3)
            # 三个根的全部实部/虚部(含负虚部)都要对上, 否则残留判定会误报
            tg = [("实根", cube, True), ("实部", -cube / 2, True),
                  ("虚部+", cube * mp.sqrt(3) / 2, True),
                  ("虚部-", -cube * mp.sqrt(3) / 2, True)]
            expect("ultra x^3=2 @%d 位" % d, text, tg, d)

    if ONLY in (None, "sci"):
        r = run(["--eval", "e^100", "--decimals", "30", "--scientific", "always"])
        text = result_lines(r.stdout + r.stderr)
        expect("科学计数法 e^100", text, [("值", mp.exp(100), True)], 30)
        r = run(["--eval", "e^-100", "--decimals", "30", "--scientific", "always"])
        text = result_lines(r.stdout + r.stderr)
        expect("科学计数法 e^-100", text, [("值", mp.exp(-100), True)], 30)

    print("精度测试: 检查 %d 项, 失败 %d 项" % (checks, len(fails)))
    for f in fails:
        print("  FAIL " + f)
    for s in skips:
        print("  SKIP " + s)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

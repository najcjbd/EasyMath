#!/usr/bin/env python3
"""EasyMath 差分测试: 用独立预言机(Python int/Fraction/SymPy)校验运算结果。

用法: difftest.py <二进制> [--cases N] [--no-sympy]
"""
import random
import re
import subprocess
import sys
from fractions import Fraction

BIN = sys.argv[1] if len(sys.argv) > 1 else "./EasyMath"
N = 200
USE_SYMPY = "--no-sympy" not in sys.argv
for a in sys.argv[2:]:
    if a.startswith("--cases="):
        N = int(a.split("=")[1])

fails = []
checks = 0

SUP = str.maketrans("⁰¹²³⁴⁵⁶⁷⁸⁹⁻", "0123456789-")


def conv(text):
    """Unicode 上标 -> ^n, 便于求值"""
    out, i = [], 0
    while i < len(text):
        if text[i] in "⁰¹²³⁴⁵⁶⁷⁸⁹⁻":
            j = i
            while j < len(text) and text[j] in "⁰¹²³⁴⁵⁶⁷⁸⁹⁻":
                j += 1
            out.append("^(" + text[i:j].translate(SUP) + ")")
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def run(args, timeout=30):
    r = subprocess.run([BIN] + args, capture_output=True, timeout=timeout)
    return r.returncode, r.stdout.decode("utf-8", "replace"), r.stderr.decode("utf-8", "replace")


def check(name, got, want):
    global checks
    checks += 1
    if got != want:
        fails.append("%s: got %r want %r" % (name, got, want))


def exact_fraction(out):
    """从输出中提取精确值(Fraction); 小数形式行优先"""
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("小数形式:"):
            body = line.split(":", 1)[1]
            head = body.split("=")[0].strip()
            try:
                return Fraction(head)
            except Exception:
                pass
    for line in out.splitlines():
        line = line.strip()
        if "≈" in line or "\\approx" in line:
            continue
        if line.startswith("P(") or line.startswith("y =") or line.startswith("解"):
            continue
        if " = " in line:
            val = conv(line.split(" = ")[-1].strip())
            try:
                return Fraction(val)
            except Exception:
                continue
    return None


def approx_float(out):
    for line in out.splitlines():
        if "≈" in line:
            tail = line.split("≈")[-1].strip().rstrip("...")
            try:
                return float(tail)
            except Exception:
                pass
    return None


def test_integers(rng):
    for _ in range(N // 3):
        a = rng.randint(-10 ** rng.randint(1, 40), 10 ** rng.randint(1, 40))
        b = rng.randint(-10 ** rng.randint(1, 20), 10 ** rng.randint(1, 20)) or 1
        for expr, want in [("%d+%d" % (a, b), Fraction(a + b)),
                           ("%d-%d" % (a, b), Fraction(a - b)),
                           ("%d*%d" % (a, b), Fraction(a * b)),
                           ("%d/%d" % (a, b), Fraction(a, b))]:
            rc, out, err = run(["--eval", expr, "--hide", "all", "--show", "plain,decimal"])
            if rc != 0:
                fails.append("int rc=%d %s: %s" % (rc, expr, err[:80]))
                continue
            got = exact_fraction(out)
            check("int %s" % expr, got, want)
        e = rng.randint(0, 30)
        expr = ("(%d)^%d" % (a, e)) if a < 0 else ("%d^%d" % (a, e))
        rc, out, err = run(["--eval", expr, "--hide", "all", "--show", "plain"])
        check("pow %s" % expr, exact_fraction(out), Fraction(a ** e))
        # 同时校验标准约定: -X^e == -(X^e)
        rc, out, err = run(["--eval", "-%d^%d" % (abs(a), e), "--hide", "all", "--show", "plain"])
        check("prec -%d^%d" % (abs(a), e), exact_fraction(out), Fraction(-(abs(a) ** e)))


def test_rationals(rng):
    for _ in range(N // 3):
        p1, q1 = rng.randint(-10 ** 6, 10 ** 6), rng.randint(1, 10 ** 6)
        p2, q2 = (rng.randint(-10 ** 6, 10 ** 6) or 1), rng.randint(1, 10 ** 6)
        f1, f2 = Fraction(p1, q1), Fraction(p2, q2)
        for op, fn in [("+", lambda x, y: x + y), ("-", lambda x, y: x - y),
                       ("*", lambda x, y: x * y), ("/", lambda x, y: x / y if y else None)]:
            want = fn(f1, f2)
            if want is None:
                continue
            expr = "(%d/%d)%s(%d/%d)" % (p1, q1, op, p2, q2)
            rc, out, err = run(["--eval", expr, "--hide", "all", "--show", "plain,decimal"])
            if rc != 0:
                fails.append("rat rc=%d %s: %s" % (rc, expr, err[:80]))
                continue
            check("rat %s" % expr, exact_fraction(out), want)
        # 精确小数展开(循环节) 与 Python 独立实现比对
        d = rng.randint(5, 30)
        rc, out, err = run(["--eval", "%d/%d" % (p1, q1), "--decimals", str(d), "--hide", "all",
                            "--show", "decimal"])
        line = [l for l in out.splitlines() if l.startswith("小数形式:")]
        if line:
            exact = line[0].split(":", 1)[1].split("=")[1].split("≈")[0].strip()
            want = decimal_of(f1, d)
            truncated = exact.endswith("...") or exact.endswith("...)")
            if truncated:
                # 循环节超过程序显示上限: 只比较循环节前 40 位
                body = exact.rstrip(".)").split(".", 1)[1] if "." in exact else exact
                got_cycle = body.split("(", 1)[1] if "(" in body else body
                if "(" in want:
                    pre, cyc = want.split(".", 1)[1].split("(", 1)
                    cyc = cyc.rstrip(")")
                    want_cycle = cyc * (len(got_cycle) // max(1, len(cyc)) + 2)
                    if "(" not in body:  # 程序没标出循环节, 需要算上前缀
                        want_cycle = pre + want_cycle
                else:
                    want_cycle = want.split(".", 1)[1]
                n = min(len(got_cycle), len(want_cycle), 40)
                check("decimal(截断) %d/%d" % (p1, q1), got_cycle[:n], want_cycle[:n])
            else:
                check("decimal %d/%d" % (p1, q1), exact, want)


def decimal_of(fr, digits):
    num, den = fr.numerator, fr.denominator
    sign = "-" if num < 0 else ""
    num = abs(num)
    ip, rem = divmod(num, den)
    if rem == 0:
        return sign + str(ip)
    seen, fp = {}, ""
    while rem and len(fp) <= 600:
        if rem in seen:
            start = seen[rem]
            return "%s%d.%s(%s)" % (sign, ip, fp[:start], fp[start:])
        seen[rem] = len(fp)
        rem *= 10
        d, rem = divmod(rem, den)
        fp += str(d)
    return "%s%d.%s..." % (sign, ip, fp[:40])


def test_lagrange(rng):
    for _ in range(max(4, N // 12)):
        n = rng.randint(2, 5)
        xs = rng.sample(range(-6, 7), n)
        ys = [rng.randint(-9, 9) for _ in range(n)]
        inp = " ".join("x%d=%d y%d=%d" % (i + 1, xs[i], i + 1, ys[i]) for i in range(n))
        rc, out, err = run(["--lagrange", inp, "--engine=builtin", "--hide", "all",
                            "--show", "polynomial"])
        if rc != 0:
            fails.append("lagrange rc=%d %s: %s" % (rc, inp, err[:80]))
            continue
        coeffs = lagrange_coeffs(xs, ys)
        lines = [l for l in out.splitlines() if "P(x)" in l]
        if not lines:
            fails.append("lagrange 无输出: %s" % inp)
            continue
        line = conv(lines[0])
        for x, y in zip(xs, ys):
            try:
                got = poly_eval(parse_poly(line), x)
            except Exception as exc:
                fails.append("lagrange 解析失败 %r: %s" % (line, exc))
                break
            if got != Fraction(y):
                fails.append("lagrange 求值 %s 在 x=%d 得 %s, 应为 %d" % (line, x, got, y))
                break
        # 独立实现也自检一次
        for x, y in zip(xs, ys):
            if poly_eval(coeffs, x) != Fraction(y):
                fails.append("oracle lagrange 自检失败 %s" % inp)


def parse_poly(line):
    """'P(x) = 3/4x^2 - 2x + 1' -> {次数: 系数}"""
    if "=" in line:
        line = line.split("=", 1)[1]
    line = line.replace(" ", "").replace("-", "+-")
    coeffs = {}
    for term in line.split("+"):
        if not term:
            continue
        m = re.match(r"^(-?\d+(?:/\d+)?|-)?x(?:\^(\d+))?$", term)
        if m:
            head = m.group(1)
            deg = int(m.group(2)) if m.group(2) else 1
            head = head or ""
            if head in ("", "+"):
                c = Fraction(1)
            elif head == "-":
                c = Fraction(-1)
            else:
                c = Fraction(head)
        else:
            c, deg = Fraction(term), 0
        coeffs[deg] = coeffs.get(deg, Fraction(0)) + c
    return coeffs


def poly_eval(coeffs, x):
    if isinstance(coeffs, dict):
        return sum(c * Fraction(x) ** d for d, c in coeffs.items())
    acc = Fraction(0)
    for c in reversed(coeffs):
        acc = acc * x + c
    return acc


def lagrange_coeffs(xs, ys):
    n = len(xs)
    coeffs = [Fraction(0)] * n
    for i in range(n):
        denom, basis = Fraction(1), [Fraction(1)]
        for j in range(n):
            if i == j:
                continue
            denom *= (xs[i] - xs[j])
            new = [Fraction(0)] * (len(basis) + 1)
            for k, c in enumerate(basis):
                new[k + 1] += c
                new[k] -= c * xs[j]
            basis = new
        fac = Fraction(ys[i]) / denom
        for k, c in enumerate(basis):
            coeffs[k] += c * fac
    while len(coeffs) > 1 and coeffs[-1] == 0:
        coeffs.pop()
    return coeffs


def test_solver_vs_sympy(rng):
    if not USE_SYMPY:
        return
    try:
        import sympy  # noqa: F401
    except Exception:
        print("  (跳过 SymPy 对拍: 未安装)")
        return
    for _ in range(max(4, N // 15)):
        deg = rng.randint(1, 6)
        coeffs = [rng.randint(-9, 9) for _ in range(deg + 1)]
        while coeffs[-1] == 0:
            coeffs[-1] = rng.randint(1, 9)
        expr = poly_str(coeffs) + "=0"
        outs = {}
        for eng in ("builtin", "sympy"):
            rc, out, err = run(["--engine=" + eng, "--solve", expr, "--hide", "all",
                                "--show", "solution", "--decimals", "10"])
            if rc != 0:
                fails.append("solve rc=%d engine=%s %s: %s" % (rc, eng, expr, err[:80]))
                outs = None
                break
            outs[eng] = roots_numeric(out)
        if not outs:
            continue
        r1, r2 = outs["builtin"], outs["sympy"]
        if len(r1) != len(r2):
            fails.append("solve 根数不同 %s: builtin=%d sympy=%d" % (expr, len(r1), len(r2)))
            continue
        for a, b in zip(sorted(r1), sorted(r2)):
            if abs(a[0] - b[0]) > 1e-6 or abs(a[1] - b[1]) > 1e-6:
                fails.append("solve 根不同 %s: %s vs %s" % (expr, a, b))
                break


def poly_str(coeffs):
    terms = []
    for i in range(len(coeffs) - 1, -1, -1):
        c = coeffs[i]
        if c == 0:
            continue
        if i == 0:
            terms.append(str(c))
        else:
            v = "x" if i == 1 else "x^%d" % i
            terms.append(v if c == 1 else ("-" + v if c == -1 else "%d*%s" % (c, v)))
    return " + ".join(terms).replace("+ -", "- ")


def roots_numeric(out):
    """解析解的输出; 精确根式用 sympy 求数值, 保证两个引擎可比"""
    res = []
    try:
        import sympy as sp
    except Exception:
        sp = None
    for line in out.splitlines():
        line = line.strip()
        m = re.match(r"^x(?:_\d+)?\s*[=≈]\s*(.+)$", line)
        if not m:
            continue
        val = m.group(1)
        mm = list(re.finditer(r"≈\s*(-?[\d.]+(?:e[-+]?\d+)?)(?:\s*([-+]\s*[\d.]+)\s*i)?", val))
        if mm:
            g = mm[-1]
            im = g.group(2).replace(" ", "") if g.group(2) else "0"
            res.append((round(float(g.group(1)), 6), round(float(im), 6)))
            continue
        if sp is not None:
            try:
                txt = val.replace("√(", "sqrt(").replace("^", "**").replace("i", "I")
                txt = txt.replace("π", "pi")
                txt = re.sub(r"(\d)([a-zA-Z(])", r"\1*\2", txt)
                txt = re.sub(r"\)([a-zA-Z(])", r")*\1", txt)
                z = complex(sp.N(sp.sympify(txt), 20))
                res.append((round(z.real, 6), round(z.imag, 6)))
                continue
            except Exception:
                pass
        try:
            res.append((round(float(val), 6), 0.0))
        except ValueError:
            pass
    return res


def main():
    rng = random.Random(4242)
    tests = [("整数", test_integers), ("有理数", test_rationals), ("拉格朗日", test_lagrange),
             ("解方程对拍", test_solver_vs_sympy)]
    for name, fn in tests:
        before = len(fails)
        try:
            fn(rng)
        except Exception as exc:
            fails.append("%s 抛异常: %s: %s" % (name, type(exc).__name__, exc))
        print("  %s: %s" % (name, "OK" if len(fails) == before else "%d 个问题" % (len(fails) - before)))
    print("\n差分测试: %d 项检查, %d 个问题" % (checks, len(fails)))
    for f in fails[:20]:
        print("  FAIL " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

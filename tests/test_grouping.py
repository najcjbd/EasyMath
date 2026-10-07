#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""分组 / 比例 自洽性测试。

不信工具自己说"分组对了" —— 而是把工具打印出来的解重新解析回 SymPy, 然后:
  1) 代回原方程 / 原不等式, 独立验证解本身是对的;
  2) 重新算一遍"每个解依赖哪些还没有确定数值的量", 与打印的分组行逐条比对;
  3) 对每条比例行验证 解_i / 比例_i 全部相等(比例行确实成立),
     并检查标着"纯数值比"的行里真的不含未确定量、标着"含未确定量"的行确实含。
这样分组逻辑、比例化简、以及被分组的那批解三者互相印证, 任一环节错都会被抓到。

用法: python3 tests/test_grouping.py [二进制] [--quick]
"""
import re
import subprocess
import sys

try:
    import sympy as sp
except ImportError:
    print("分组/比例测试: 跳过 (未安装 sympy)")
    sys.exit(0)

BIN = sys.argv[1] if len(sys.argv) > 1 else "./EasyMath"
QUICK = "--quick" in sys.argv
TIMEOUT = 900

# 系统, 常量, 期望分组条数, 期望"纯数值比"条数, 期望比例总条数
CASES = [
    ("a>b>c>0, a=b+c, 1/a+1/b=1/c", "", 1, 1, 1),
    ("abc=m, xy=n", "", 2, 0, 0),
    ("a=2t, b=3t, c=5t", "t", 1, 1, 1),
    ("a=t, b=2t, c=7", "t", 2, 1, 1),
    ("a=t^2, b=t, c=4t", "t", 1, 0, 1),
    ("a=m*k^2, b=m*k, c=3*m*k^2", "m,k", 1, 0, 1),
    ("a=m+n, b=m-n", "", 1, 0, 1),
    ("x+y=3, x-y=1", "", 0, 0, 0),
]
if not QUICK:
    CASES += [
        ("p=q*r, s=q^2*r", "", 2, 0, 2),
        ("u=3v, w=9v", "v", 1, 1, 1),
        ("a=2t+1, b=4t+2", "t", 1, 1, 1),   # 提公因子后是纯数值比 1:2
        ("a=3u, b=3u, c=6u", "u", 1, 1, 1),  # 纯数值比要约分: 3:3:6 -> 1:1:2
        ("a=x, b=x, c=x", "x", 1, 1, 1),     # 同取一个未知量 -> 1:1:1
        ("a=2t/3, b=4t/3", "t", 1, 1, 1),    # 分数比清分母 -> 1:2
    ]

SINGLE = set("abcdefghijklmnopqrstuvwxyz")
FUNCS = {"sqrt", "cbrt", "sin", "cos", "tan", "log", "ln", "exp", "abs",
         "pi", "oo", "i", "asin", "acos", "atan", "sinh", "cosh", "tanh"}

PASS = 0
FAIL = 0


def ok(_name=""):
    global PASS
    PASS += 1


def bad(name, detail=""):
    global FAIL
    FAIL += 1
    print("FAIL: " + name)
    if detail:
        for ln in str(detail).splitlines():
            print("      " + ln)


def expect(name, cond, detail=""):
    if cond:
        ok(name)
    else:
        bad(name, detail)


def _split_letters(t):
    """多字母连写 = 隐式乘积(abc -> a*b*c), 函数名/常数名除外。"""
    def rep(m):
        w = m.group(0)
        if w in FUNCS or not all(ch in SINGLE for ch in w):
            return w
        return "*".join(w) if len(w) > 1 else w
    return re.sub(r"[A-Za-z_][A-Za-z_0-9]*", rep, t)


def to_sym(t):
    """把工具打印的纯文本表达式解析回 SymPy。"""
    t = t.strip()
    t = t.replace("\u221a(", "sqrt(").replace("\u221a", "sqrt")
    t = t.replace("\u221b(", "cbrt(")
    t = t.replace("^", "**").replace("\u00b7", "*").replace("\u00d7", "*")
    t = t.replace("\u2212", "-").replace("\u03c0", "pi").replace("\u221e", "oo")
    t = t.replace("\u2260", "!=").replace("\u2264", "<=").replace("\u2265", ">=")
    # 数值系数隐式: 2t -> 2*t (但 2^t 不动)
    t = re.sub(r"(?<![A-Za-z_0-9)])(\d+)\s*(?=[A-Za-z(])", r"\1*", t)
    t = _split_letters(t)
    return sp.sympify(t, rational=True)


def run_case(system, consts):
    args = [BIN, "--solve", system]
    if consts:
        args += ["--const", consts]
    args += ["--hide=banner,input,step,steps,tip,prompt,file,verify,normalized",
             "--show", "solution,group"]
    p = subprocess.run(args, capture_output=True, text=True, timeout=TIMEOUT)
    return p.stdout + p.stderr


EQ_SPLIT = re.compile(r",\s*(?=[A-Za-z_][A-Za-z_0-9]*\s*=)")
SKIP_PREFIX = ("\u7ea6\u675f", "\u5e38\u91cf", "\u5206\u7ec4", "\u6bd4\u4f8b",
               "\u6c42\u503c", "\u5b58\u5728\u81ea\u7531\u53c2\u6570", "\u5176\u4e2d")


def parse_solutions(out):
    """返回 [(标签, {变量: sympy 值}), ...]"""
    sols = []
    for raw in out.splitlines():
        line = raw.strip()
        if not line or "=" not in line:
            continue
        if line.startswith(SKIP_PREFIX):
            continue
        body = re.sub(r"^\u89e3\s*\d+\s*:\s*", "", line)
        parts = EQ_SPLIT.split(body)
        if not all(re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*\s*=\s*.+", p.strip()) for p in parts):
            continue
        try:
            vals = {}
            for p in parts:
                name, rhs = p.split("=", 1)
                vals[sp.Symbol(name.strip())] = to_sym(rhs)
        except Exception as e:  # noqa: BLE001
            bad("解析解行", "%s -> %s" % (line, e))
            continue
        sols.append((line, vals))
    return sols


def parse_groups(out):
    """{变量: frozenset(依赖)} 以及分组条数"""
    dep = {}
    n = 0
    for raw in out.splitlines():
        # 多解时每行前面会标"解 N "(单解不标)
        m = re.match(r"\s*(?:\u89e3\s*\d+\s+)?\u5206\u7ec4\(([^)]*)\):\s*(.+)$", raw)
        if not m:
            continue
        n += 1
        key_txt = m.group(1)
        if key_txt.startswith("\u53d6\u51b3\u4e8e"):
            # 取决于 b, c, m
            names = [s.strip() for s in key_txt[len("\u53d6\u51b3\u4e8e"):].split(",") if s.strip()]
            key = frozenset(sp.Symbol(s) for s in names)
        else:
            key = frozenset()
        for v in [s.strip() for s in m.group(2).split(",") if s.strip()]:
            dep[sp.Symbol(v)] = key
    return dep, n


def parse_ratios(out):
    """[(是否纯数值, [变量], [比例值]), ...]"""
    rats = []
    for raw in out.splitlines():
        s = raw.strip()
        m = re.match(r"\s*(?:\u89e3\s*\d+\s+)?\u6bd4\u4f8b"
                     r"(\uff08\u542b\u672a\u786e\u5b9a\u91cf\uff09|\(含未确定量\))?:\s*(.+)$", s)
        if not m:
            continue
        numeric = m.group(1) is None
        body = m.group(2)
        if " = " not in body:
            continue
        lhs, rhs = body.split(" = ", 1)
        names = [sp.Symbol(x.strip()) for x in lhs.split(":") if x.strip()]
        vals = [to_sym(x) for x in rhs.split(" : ")]
        rats.append((numeric, names, vals))
    return rats


def parse_input_relations(system):
    """把输入里的等式/不等式拆成 (lhs-rhs 表达式, 关系符) 或链式关系。"""
    items = []
    for chunk in re.split(r"[,;\n]", system):
        chunk = chunk.strip()
        if not chunk:
            continue
        if re.search(r"[\u2260<>\u2264\u2265]", chunk) and "=" not in chunk.replace("==", ""):
            # 纯不等式(可能链式): a>b>c>0
            toks = re.split(r"(?<=[^<>\u2260\u2264\u2265])(?=[<>\u2260\u2264\u2265])", chunk)
            parts = []
            cur = ""
            for tk in toks:
                if re.fullmatch(r"[\u2260<>\u2264\u2265]+", tk):
                    parts.append(cur)
                    parts.append(tk)
                    cur = ""
                else:
                    cur += tk
            parts.append(cur)
            for i in range(1, len(parts) - 1, 2):
                items.append((to_sym(parts[i - 1]), parts[i], to_sym(parts[i + 1])))
        elif "=" in chunk:
            lhs, rhs = chunk.split("=", 1)
            items.append((to_sym(lhs) - to_sym(rhs), "==", sp.Integer(0)))
        elif re.search(r"[\u2260<>\u2264\u2265]", chunk):
            for op in ["\u2264", "\u2265", "\u2260", "<", ">"]:
                if op in chunk:
                    lhs, rhs = chunk.split(op, 1)
                    items.append((to_sym(lhs), op, to_sym(rhs)))
                    break
    return items


def check_case(system, consts, want_groups, want_numeric, want_ratio):
    out = run_case(system, consts)
    sols = parse_solutions(out)
    if not sols:
        bad("用例 %s: 没解析出解" % system, out)
        return
    dep, ngroups = parse_groups(out)
    rats = parse_ratios(out)

    expect("用例 %s: 分组条数" % system, ngroups == want_groups,
           "期望 %d 条, 实际 %d 条\n%s" % (want_groups, ngroups, out))
    expect("用例 %s: 纯数值比条数" % system,
           sum(1 for r in rats if r[0]) == want_numeric,
           "期望 %d 条, 实际 %d 条\n%s" % (want_numeric, sum(1 for r in rats if r[0]), out))
    expect("用例 %s: 比例条数" % system, len(rats) == want_ratio,
           "期望 %d 条, 实际 %d 条\n%s" % (want_ratio, len(rats), out))

    rels = parse_input_relations(system)
    # 自由量的取值(用于验证不等式): 都给正数, 兼顾 a>b>c>0 这类
    freesyms = sorted({s for _lbl, v in sols for val in v.values()
                       for s in val.free_symbols}, key=lambda s: s.name)
    subs = {s: sp.Rational(i + 2, i + 3) for i, s in enumerate(freesyms)}

    for label, vals in sols:
        solved = set(vals)
        # (1) 代回原方程/不等式
        for lhs, op, rhs in rels:
            try:
                expr = (lhs - rhs).subs(vals).subs(subs)
                expr = sp.nsimplify(sp.simplify(expr))
            except Exception as e:  # noqa: BLE001
                bad("用例 %s: 代回失败" % system, "%s -> %s" % (label, e))
                continue
            if op == "==":
                expect("用例 %s: 解满足方程" % system,
                       sp.simplify(expr) == 0, "%s 代入后 = %s" % (label, expr))
            elif op == "\u2260":
                expect("用例 %s: 解满足非零" % system, expr != 0,
                       "%s 代入后 = %s" % (label, expr))
            else:
                num = complex(sp.N(expr))
                good = {"<": num.imag == 0 and num.real < 0,
                        ">": num.imag == 0 and num.real > 0,
                        "\u2264": num.imag == 0 and num.real <= 0,
                        "\u2265": num.imag == 0 and num.real >= 0}[op]
                expect("用例 %s: 解满足不等式" % system, good,
                       "%s 代入后 %s (%s)" % (label, expr, num))
        # (2) 分组 = 重新算的依赖集合(工具没打印分组行 = 没有自由依赖, 已在 (4) 检查)
        for v, val in (vals.items() if ngroups else []):
            want = frozenset((val.free_symbols or set()) - solved)
            got = dep.get(v)
            expect("用例 %s: %s 的依赖" % (system, v), got == want,
                   "打印 %s, 实际依赖 %s\n%s" % (got, want, out))
        # (3) 比例行成立: 解_i / 比例_i 全都相等
        for numeric, names, rvals in rats:
            if not (set(names) <= solved):
                continue
            if len(rvals) != len(names):
                bad("用例 %s: 比例项数不匹配" % system, "%s vs %s" % (names, rvals))
                continue
            factors = []
            for nm, rv in zip(names, rvals):
                q = sp.simplify(sp.together(vals[nm] / rv))
                factors.append(sp.nsimplify(sp.simplify(q)))
            same = all(sp.simplify(f - factors[0]) == 0 for f in factors)
            expect("用例 %s: 比例 %s 成立" % (system, names), same,
                   "解 %s, 比例 %s, 系数 %s" % ([vals[n] for n in names], rvals, factors))
            if numeric:
                expect("用例 %s: 纯数值比不含未确定量" % system,
                       all(not rv.free_symbols for rv in rvals),
                       "%s" % (rvals,))
                key = dep.get(names[0], frozenset())
                expect("用例 %s: 纯数值比只依赖一个自由量" % system, len(key) <= 1,
                       "依赖 %s" % (key,))
            else:
                expect("用例 %s: 含未确定量的比确实含未确定量" % system,
                       all(rv.free_symbols for rv in rvals) or any(
                           dep.get(n, frozenset()) for n in names),
                       "%s" % (rvals,))
    # (4) 没有自由依赖时不该打印分组行
    if want_groups == 0:
        expect("用例 %s: 无自由量时不分组" % system, not ngroups, out)


def main():
    for system, consts, wg, wn, wr in CASES:
        try:
            check_case(system, consts, wg, wn, wr)
        except subprocess.TimeoutExpired:
            bad("用例 %s: 超时" % system)
        except Exception as e:  # noqa: BLE001
            bad("用例 %s: 异常" % system, repr(e))
    print("\n分组/比例测试: 通过 %d 项, 失败 %d 项" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())

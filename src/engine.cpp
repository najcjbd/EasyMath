// EasyMath - 外部符号引擎 (SymPy) 桥实现
// POSIX: fork/exec + pipe/poll; Windows: CreateProcess + 匿名管道。
// 两种实现都把脚本从 stdin 送入, 方程走 argv, 用制表符分隔的记录协议读回结果。
#include "engine.hpp"

#include "expr.hpp"
#include "i18n.hpp"
#include "interrupt.hpp"
#include "poly.hpp"
#include "unicode.hpp"

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 // Windows 7+, GetTickCount64 需要
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <map>
#include <sstream>

namespace em {

static const char *kEngineScript = R"EASYMATH_PY(
#!/usr/bin/env python3
# EasyMath <-> SymPy 引擎桥 (由 EasyMath 通过 stdin 传入)
# 调用: python3 - <op> [--domain=real|complex] [--digits=N] <args...>
# 输出: TAB 分隔记录, 以 END 结束
import sys
import warnings

# SymPy 内部会对某些输入发弃用告警; 这些不是我们的错误, 会污染用户在终端/日志里看到的内容。
warnings.filterwarnings("ignore")
try:
    from sympy.utilities.exceptions import SymPyDeprecationWarning
    warnings.simplefilter("ignore", SymPyDeprecationWarning)
except Exception:
    pass

SEP = "\t"
# 不等式的数值解集策略(可由 --ineqnum= 覆盖, 见 Config::numericInequality)
INEQ_NUM = "auto"
# 解方程: 视为常量的字母 / 由解反推的表达式(派生量)
CONSTS = []
DERIVES = []


def emit(*fields):
    sys.stdout.write(SEP.join("" if f is None else str(f) for f in fields) + "\n")


def fail(msg):
    emit("STATUS", "error")
    emit("ERROR", str(msg).replace("\n", " ").replace("\t", " "))
    emit("END")
    sys.exit(0)


SCI_MODE = "never"
SCI_THR = 12


def _fixed(q, digits):
    """精确四舍五入到 digits 位小数; 过大/过小时按策略用科学计数法"""
    import mpmath
    if q == 0:
        return "0"
    if SCI_MODE == "always":
        return _sci(q, digits)
    if SCI_MODE == "auto":
        try:
            if abs(mpmath.log10(abs(q))) >= SCI_THR:
                return _sci(q, digits)
        except Exception:
            pass
    neg = q < 0
    n = int(mpmath.nint(abs(q) * (mpmath.mpf(10) ** digits)))
    d = str(n)
    if digits > 0:
        if len(d) <= digits:
            d = "0" * (digits + 1 - len(d)) + d
        out = d[:-digits] + "." + d[-digits:]
        out = out.rstrip("0").rstrip(".")
        if out == "" or out == "-":
            out = "0"
    else:
        out = d
    res = ("-" if neg else "") + out
    if res.lstrip("-").strip("0.") == "" and SCI_MODE == "auto":
        return _sci(q, digits)
    return res


def _sci(q, digits):
    """科学计数法: 尾数保留 digits 位小数 (digits+1 位有效数字)"""
    import mpmath
    txt = mpmath.nstr(q, digits + 1, strip_zeros=True)
    if "e" in txt:
        mant, ex = txt.split("e", 1)
        if mant.endswith(".0"):
            mant = mant[:-2]
        txt = mant + "e" + ex
    return txt


def numeric_of(sp, r, digits):
    import mpmath
    mpmath.mp.dps = max(60, digits + 35)
    num = sp.N(r, digits + 25)
    re_f = mpmath.mpf(str(sp.N(sp.re(num), digits + 25)))
    im_f = mpmath.mpf(str(sp.N(sp.im(num), digits + 25)))
    eps = mpmath.mpf(10) ** (-(digits + 3))
    if abs(im_f) <= eps * max(1, abs(re_f)):
        return _fixed(re_f, digits)
    if abs(re_f) <= eps * max(1, abs(im_f)):
        return ("-" if im_f < 0 else "") + _fixed(abs(im_f), digits) + "i"
    return (_fixed(re_f, digits) + (" - " if im_f < 0 else " + ") + _fixed(abs(im_f), digits) + "i")


def _surdize(txt, name, sym):
    """把 name(...) 折成符号形式; 用括号配对而不是正则, 以支持嵌套
    (例如 sqrt(1/2 - sqrt(17)/2) -> \u221a(1/2 - \u221a(17)/2))"""
    key = name + "("
    while True:
        i = txt.find(key)
        if i < 0:
            return txt
        depth = 0
        end = -1
        for k in range(i + len(name), len(txt)):
            if txt[k] == "(":
                depth += 1
            elif txt[k] == ")":
                depth -= 1
                if depth == 0:
                    end = k
                    break
        if end < 0:
            return txt  # 括号不配对, 原样保留
        inner = _surdize(txt[i + len(key):end], name, sym)
        txt = txt[:i] + sym + "(" + inner + ")" + txt[end + 1:]


def _norm_ratio(vals, solved_set):
    """把比例化成最简: 轮流除以其中一个值, 取"算子数更少且不新增含参分母"的那个。

    -r·√(s/r) : -√(s/r)  ->  r : 1
    m + n : m - n        ->  保持(归一化会引入含参分母, 不划算)
    返回 None 表示没有更好的形式。
    """
    import sympy as sp

    def _ops(c):
        return sum(sp.count_ops(sp.together(x)) for x in c)

    def _dens(c):
        n = 0
        for x in c:
            d = sp.together(x).as_numer_denom()[1]
            if (getattr(d, "free_symbols", set()) or set()) - solved_set:
                n += 1
        return n

    best, best_ops, best_d = vals, _ops(vals), _dens(vals)
    for g in vals:
        if g == 0:
            continue
        try:
            alt = [sp.simplify(x / g) for x in vals]
        except Exception:
            continue
        o, d = _ops(alt), _dens(alt)
        if o < best_ops and d <= best_d:
            best, best_ops, best_d = alt, o, d
    return None if best is vals else best


def pretty(txt):
    import re
    txt = txt.replace("**", "^").replace("I", "i")
    # 数字系数隐式化: 2*t -> 2t, 但指数后面的乘号必须留着(k^2*m 不能变成 k^2m,
    # 那读起来像 k^(2m)); 所以要求这一串数字前面既不是 ^ 也不是数字。
    txt = re.sub(r"(?<!\^)(?<!\d)(\d+)\*([a-zA-Z(])", r"\1\2", txt)
    txt = re.sub(r"\)\*([a-zA-Z(])", r")\1", txt)
    txt = txt.replace("*", "\u00b7")
    txt = _surdize(txt, "sqrt", "\u221a")
    txt = _surdize(txt, "cbrt", "\u221b")
    txt = re.sub(r"(?<![A-Za-z])pi(?![A-Za-z])", "\u03c0", txt)
    return txt


def _num_txt(sp, v):
    """端点/数值的纯文本(-∞ / +∞ / 根式等)"""
    if v == -sp.oo:
        return "-∞"
    if v == sp.oo:
        return "+∞"
    return pretty(sp.sstr(v)).replace("sqrt", "√")


def interval_text(sp, s):
    """区间 / 并集 / 交集 / 补集 -> 可读文本, 如 (-∞, 2) ∪ (2, +∞)"""
    if isinstance(s, sp.Interval):
        lb = "(" if s.left_open else "["
        rb = ")" if s.right_open else "]"
        return "%s%s, %s%s" % (lb, _num_txt(sp, s.start), _num_txt(sp, s.end), rb)
    if isinstance(s, sp.Union):
        return " ∪ ".join(interval_text(sp, a) for a in s.args)
    if isinstance(s, sp.Intersection):
        return " ∩ ".join(interval_text(sp, a) for a in s.args)
    if isinstance(s, sp.Complement):
        base = interval_text(sp, s.args[0])
        parts = []
        for a in s.args[1:]:
            if isinstance(a, sp.FiniteSet):
                parts.append("{" + ", ".join(_num_txt(sp, x) for x in a) + "}")
            else:
                parts.append(interval_text(sp, a))
        return base + " \ " + " \ ".join(parts)
    if isinstance(s, sp.FiniteSet):
        return "{" + ", ".join(_num_txt(sp, x) for x in s) + "}"
    if s == sp.S.Reals:
        return "ℝ"
    if s is sp.S.EmptySet:
        return "∅"
    return pretty(sp.sstr(s))


def set_plain(sp, ss):
    """把解集渲染成可读纯文本"""
    def render(s):
        if isinstance(s, (sp.Interval, sp.Complement)):
            return interval_text(sp, s)
        if isinstance(s, sp.Union):
            # 区间并集用区间写法; 其它(如解族)沿用 "或"
            if all(isinstance(a, sp.Interval) for a in s.args):
                return interval_text(sp, s)
            return " 或 ".join(render(a) for a in s.args)
        if isinstance(s, sp.ImageSet):
            try:
                var = s.lamda.variables[0]
                expr = s.lamda.expr
                if s.base_set == sp.S.Integers:
                    txt = pretty(sp.sstr(expr)).replace(str(var), "n")
                    return "x = %s   (n ∈ ℤ)" % txt
            except Exception:
                pass
        if isinstance(s, sp.FiniteSet):
            return ", ".join("x = " + pretty(sp.sstr(a)) for a in s)
        return pretty(sp.sstr(s))
    return render(ss)


def exact_form(sp, val):
    """精确式的规范形式。
    SymPy 的 sstr/latex 会把 sqrt(负数) 原样写出(如 -sqrt(1/2 - sqrt(17)/2)),
    看着像实数其实带虚部; 对已确定非实数的数值用 expand_complex 显式写出 i。
    含自由符号的表达式不动(is_real 可能为 None, 强行展开会引入 re()/im())。
    注意: sp 是本模块的局部名(由 main 传入), 不能引用全局同名变量。"""
    try:
        if val.is_number and val.is_real is False:
            return sp.expand_complex(val)
    except Exception:
        return val
    return val


def emit_root(sp, r, mult, digits):
    try:
        is_complex = (r.is_real is False) or bool(r.has(sp.I))
    except Exception:
        is_complex = False
    exact = not r.atoms(sp.Float)
    rp = exact_form(sp, r)
    emit("SOL", pretty(sp.sstr(rp)), sp.latex(rp), numeric_of(sp, r, digits),
         "1" if exact else "0", "1" if is_complex else "0", mult)


def env_dict(sp):
    names = ["sqrt", "cbrt", "exp", "ln", "log", "sin", "cos", "tan", "cot", "sec", "csc",
             "asin", "acos", "atan", "sinh", "cosh", "tanh", "floor", "ceiling", "sign",
             "Max", "Min", "gcd", "lcm", "Abs", "factorial", "Rational", "pi"]
    d = {}
    for n in names:
        if hasattr(sp, n):
            d[n] = getattr(sp, n)
    d.setdefault("Rational", sp.Rational)
    d["pi"] = sp.pi
    d["E"] = sp.E
    return d


def main():
    try:
        import sympy as sp
    except Exception as exc:
        fail("sympy 不可用: %s" % exc)
    args = sys.argv[1:]
    if not args:
        fail("缺少操作")
    op, rest = args[0], args[1:]
    domain, digits, pos, rels = "real", 8, [], []
    global INEQ_NUM, CONSTS, DERIVES
    INEQ_NUM = "auto"
    CONSTS, DERIVES = [], []
    for a in rest:
        if a.startswith("--domain="):
            domain = a.split("=", 1)[1]
        elif a.startswith("--rel="):
            rels.append(a.split("=", 1)[1])
        elif a.startswith("--ineqnum="):
            INEQ_NUM = a.split("=", 1)[1]
        elif a.startswith("--const="):
            CONSTS.append(a.split("=", 1)[1])
        elif a.startswith("--derive="):
            DERIVES.append([a.split("=", 1)[1], a.split("=", 1)[1]])
        elif a.startswith("--derivename="):
            nm = a.split("=", 1)[1]
            if DERIVES:
                DERIVES[-1][1] = nm
            else:
                DERIVES.append([nm, nm])
        elif a.startswith("--digits="):
            digits = int(a.split("=", 1)[1])
        elif a.startswith("--sci="):
            global SCI_MODE
            SCI_MODE = a.split("=", 1)[1]
        elif a.startswith("--scithr="):
            global SCI_THR
            SCI_THR = int(a.split("=", 1)[1])
        else:
            pos.append(a)
    try:
        if op == "solve":
            do_solve(sp, pos, domain, digits, rels)
        elif op == "factor":
            do_factor(sp, pos)
        elif op == "eval":
            do_eval(sp, pos, digits)
        elif op == "version":
            emit("STATUS", "ok")
            emit("VERSION", sp.__version__)
        else:
            fail("未知操作 %s" % op)
    except Exception as exc:
        fail("%s: %s" % (type(exc).__name__, exc))
    emit("END")


def do_factor(sp, pos):
    if len(pos) != 2:
        fail("factor 需要 <表达式> <变量>")
    e = sp.sympify(pos[0], locals=env_dict(sp))
    v = sp.Symbol(pos[1])
    f = sp.factor(e, v)
    emit("STATUS", "ok")
    emit("FACTOR", pretty(sp.sstr(f)), sp.latex(f))
    emit("END")
    sys.exit(0)


def do_eval(sp, pos, digits):
    env = env_dict(sp)
    for p in pos:
        e = sp.sympify(p, locals=env)
        # 精确形式(复数/常数都能给, 例如 I / 2*I / -1 / pi/6) + 数值
        try:
            e_exact = sp.simplify(e)
            emit("EXACT", pretty(sp.sstr(e_exact)), sp.latex(e_exact))
        except Exception:
            pass
        v = sp.N(e, digits + 15)
        emit("VAL", numeric_of(sp, v, digits))
    emit("STATUS", "ok")


def solve_locals(sp, pos, real=False):
    """把方程里出现的"未知标识符"显式声明成 Symbol。
    否则 sympify 会撞上 SymPy 自带的同名对象: 例如变量名叫 gamma 时,
    sympify("gamma") 得到的是伽马函数类而不是符号, 于是整个求解直接失败回退。
    real=True 时声明为实变量 —— 不等式只在实数域上有意义, 否则 SymPy 无法化简。"""
    import re
    env = env_dict(sp)
    known = set(env.keys())
    for pat in pos:
        for name in re.findall(r"[A-Za-z_][A-Za-z_0-9]*", pat):
            if name in known or name == "I":
                continue
            if name not in env:
                env[name] = sp.Symbol(name, real=True) if real else sp.Symbol(name)
    return env


# ---------------- 关系约束(不等式 / 非零) ----------------
REL_OPS = [">=", "<=", "!=", ">", "<"]


def is_relation(txt):
    t = txt.replace("\u2265", ">=").replace("\u2264", "<=").replace("\u2260", "!=")
    return any(op in t for op in REL_OPS)


def split_chain(rel):
    """把 a>b>c>0 拆成 a>b, b>c, c>0; 并把 ≥ ≤ ≠ 归一化成 ASCII 运算符。"""
    t = rel.replace("\u2265", ">=").replace("\u2264", "<=").replace("\u2260", "!=")
    parts, ops, cur, i = [], [], "", 0
    while i < len(t):
        hit = None
        for op in REL_OPS:
            if t.startswith(op, i):
                hit = op
                break
        if hit:
            parts.append(cur)
            ops.append(hit)
            cur = ""
            i += len(hit)
        else:
            cur += t[i]
            i += 1
    parts.append(cur)
    if not ops:
        return []
    return ["%s%s%s" % (parts[k].strip(), ops[k], parts[k + 1].strip()) for k in range(len(ops))]


def split_one(rel):
    """a>=b -> ("a", ">=", "b"); 运算符长优先"""
    for op in REL_OPS:
        p = rel.find(op)
        if p >= 0:
            return rel[:p].strip(), op, rel[p + len(op):].strip()
    return None, None, None


def relation_obj(sp, text, env):
    """把关系文本变成 SymPy 关系对象。
    必须显式构造: Python 的 != / == 作用在 SymPy 对象上返回的是 bool,
    所以 sympify("a*b*c!=0") 得到的是 True 而不是 Ne(a*b*c, 0)。"""
    lhs, op, rhs = split_one(text)
    if op is None:
        raise ValueError("不是关系式: " + text)
    L = sp.sympify(lhs, locals=env)
    R = sp.sympify(rhs, locals=env)
    if op == "!=":
        return sp.Ne(L, R)
    if op == ">=":
        return sp.Ge(L, R)
    if op == "<=":
        return sp.Le(L, R)
    if op == ">":
        return sp.Gt(L, R)
    if op == "<":
        return sp.Lt(L, R)
    raise ValueError("未知关系运算符: " + op)


def same_solution(sp, a, b, syms):
    """两个解是否等价(写法可能不同, 如 c(√5+3)/2 与 c/2+√5c/2+c)"""
    for v in syms:
        va = a.get(v, v) if isinstance(a, dict) else a
        vb = b.get(v, v) if isinstance(b, dict) else b
        if va == vb:
            continue
        try:
            if sp.simplify(va - vb) != 0:
                return False
        except Exception:
            if str(va) != str(vb):
                return False
    return True


def dedupe_solutions(sp, sols, syms):
    out = []
    for s in sols:
        if not any(same_solution(sp, s, t, syms) for t in out):
            out.append(s)
    return out


def _pi_multiple(sp, v):
    """把数值端点认成 π 的有理倍(如 3.14159 -> π, 0.5236 -> π/6); 认不出返回 None"""
    try:
        import mpmath
        q = sp.nsimplify(mpmath.mpf(v) / mpmath.pi, [sp.pi], rational=True, tolerance=1e-10)
        if q.is_rational:
            return q
    except Exception:
        pass
    return None


def _render_endpoint(sp, v, digits):
    q = _pi_multiple(sp, v)
    if q is not None:
        if q == 0:
            return "0"
        num, den = q.p, q.q
        if den == 1:
            return ("π" if num == 1 else ("-π" if num == -1 else "%dπ" % num))
        return ("π/%d" % den) if num == 1 else ("-%s" % ("π/%d" % den) if num == -1
                                               else "%dπ/%d" % (num, den))
    return _fixed(mpmath.mpf(v), digits)


def common_period(sp, fs, v):
    """多个周期函数的公共周期: 各周期都是 π 的有理倍时求最小公倍数, 否则 None"""
    ps = []
    for f in fs:
        try:
            P = sp.periodicity(f, v)
        except Exception:
            return None
        if P is None or not getattr(P, "is_number", False) or P == 0 or P == sp.oo:
            return None
        try:
            q = sp.nsimplify(P / sp.pi, rational=True, tolerance=1e-12)
        except Exception:
            return None
        if not q.is_rational:
            return None
        ps.append(sp.Rational(q))
    if not ps:
        return None
    num, den = 1, 1
    for q in ps:
        num = sp.ilcm(num, q.p)
        den = sp.igcd(den, q.q)
    return sp.Rational(num, den) * sp.pi


def numeric_periodic_set(sp, rel_objs, v, digits):
    """含周期函数的关系(一条或多条) -> 数值区间族。
    做法: 精确公共周期 + 一个周期内所有零点/极点的数值定位 + 逐段判定条件是否成立。
    返回 (intervals, note); intervals = [(a, b, left_closed, right_closed, period)]"""
    items = []
    for r in rel_objs:
        if isinstance(r, sp.Ne):
            items.append((sp.simplify(r.lhs - r.rhs), "ne"))
        elif isinstance(r, sp.Rel) and hasattr(r, "lhs"):
            f = sp.simplify(r.lhs - r.rhs)
            kind = ("ge" if isinstance(r, sp.Ge) else "gt" if isinstance(r, sp.Gt)
                    else "le" if isinstance(r, sp.Le) else "lt")
            items.append((f, kind))
        else:
            return None, ""
    if not items:
        return None, ""
    P = common_period(sp, [it[0] for it in items], v)
    if P is None:
        return None, ""
    import mpmath
    mpmath.mp.dps = max(40, digits + 25)
    funcs = []
    for f, _kind in items:
        try:
            funcs.append(sp.lambdify(v, f, "mpmath"))
        except Exception:
            return None, ""

    def raw(i, t):
        try:
            val = mpmath.mpf(funcs[i](t))
            return val if mpmath.isfinite(val) else None
        except Exception:
            return None

    def holds(t):
        for i, (f, kind) in enumerate(items):
            val = raw(i, t)
            if val is None:
                return False
            if kind == "gt" and not (val > 0):
                return False
            if kind == "ge" and not (val >= 0):
                return False
            if kind == "lt" and not (val < 0):
                return False
            if kind == "le" and not (val <= 0):
                return False
            if kind == "ne" and not (val != 0):
                return False
        return True

    Pv = mpmath.mpf(str(sp.N(P, 40)))
    N = 4000
    samples = []
    for k in range(N):
        t = Pv * k / N
        samples.append([raw(i, t) for i in range(len(items))])
    def bisect(i, a, b):
        fa = raw(i, a)
        lo, hi = a, b
        for _ in range(100):
            m = (lo + hi) / 2
            fm = raw(i, m)
            if fm is None or fa is None:
                return m, True
            if (fa > 0) != (fm > 0):
                hi = m
            else:
                lo, fa = m, fm
        return (lo + hi) / 2, False
    cuts = []
    for k in range(N):
        t1, t2 = Pv * k / N, Pv * (k + 1) / N
        for i in range(len(items)):
            v1, v2 = samples[k][i], samples[(k + 1) % N][i]
            if v1 is None or v2 is None:
                cuts.append((t2 if v2 is None else t1, True))
                continue
            if v1 == 0:
                cuts.append((t1, False))
            elif (v1 > 0) != (v2 > 0):
                c, ispole = bisect(i, t1, t2)
                cuts.append((c, ispole))
    if not cuts:
        if holds(Pv / 2):
            return [(mpmath.mpf(0), Pv, False, False, Pv)], ""
        return [], ""
    cuts.sort(key=lambda c: c[0])
    merged = []
    for c, ispole in cuts:
        if merged and abs(c - merged[-1][0]) < Pv * 1e-6:
            merged[-1] = (merged[-1][0], merged[-1][1] and ispole)
        else:
            merged.append((c, ispole))
    cuts = merged
    all_closed = all(kind in ("ge", "le") for _f, kind in items)
    out = []
    for i in range(len(cuts)):
        a, apole = cuts[i]
        b, bpole = cuts[(i + 1) % len(cuts)]
        if i == len(cuts) - 1:
            b = b + Pv
        if b <= a:
            continue
        if not holds((a + b) / 2):
            continue
        out.append((a, b, all_closed and not apole, all_closed and not bpole, Pv))
    note = ("数值参考: 周期为精确值, 区间端点由数值求根得到(最多 %d 位); "
            "已在一个周期内抽样验证, 但极窄区间仍可能被漏掉" % digits)
    if len(items) > 1:
        note += "; 该结果由 %d 条关系联立数值求得, 漏解风险高于单条情形" % len(items)
    return out, note


def periodic_render(sp, var, intervals, digits):
    """区间族 -> (纯文本, LaTeX): 如 '2kπ < x < π + 2kπ   (k ∈ ℤ)'"""
    if not intervals:
        return "", ""
    P_txt = _render_endpoint(sp, intervals[0][4], digits)
    if P_txt.endswith("π"):
        coef = P_txt[:-1]
        kterm = (coef + "kπ") if coef not in ("", "1") else "kπ"
    else:
        kterm = "k·" + P_txt
    kterm_l = kterm.replace("π", "\pi").replace("·", "\cdot ")
    pts, lts = [], []
    for (a, b, lc, rc, _P) in intervals:
        A = _render_endpoint(sp, a, digits)
        B = _render_endpoint(sp, b, digits)
        left = kterm if A == "0" else A.replace("π", "π") + " + " + kterm
        right = B + " + " + kterm
        pts.append("%s %s %s %s %s" % (left, "≤" if lc else "<", var, "≤" if rc else "<", right))
        Al = A.replace("π", "\pi").replace("·", "\cdot ")
        Bl = B.replace("π", "\pi").replace("·", "\cdot ")
        leftl = kterm_l if A == "0" else Al + " + " + kterm_l
        lts.append("%s %s %s %s %s" % (leftl, "\le" if lc else "<", var,
                                       "\le" if rc else "<", Bl + " + " + kterm_l))
    return " 或 ".join(pts) + "   (k ∈ ℤ)", " \vee ".join(lts) + "\;(k \in \mathbb{Z})"


def ne_conditions(sp, e):
    """Ne(e,0) -> 更易读的一组条件: 分子各因子 ≠ 0, 以及分母 ≠ 0"""
    outs = []
    try:
        num, den = sp.fraction(sp.cancel(e))

        def add_factors(expr):
            for f in sp.Mul.make_args(sp.factor(expr)):
                if f.is_number:
                    continue
                if f.is_Pow and f.exp.is_integer and f.exp.is_positive:
                    outs.append(sp.Ne(f.base, 0))   # (x+1)^3 ≠ 0 -> x+1 ≠ 0
                else:
                    outs.append(sp.Ne(f, 0))

        add_factors(num)
        if den != 1:
            add_factors(den)
    except Exception:
        outs = []
    if not outs:
        outs.append(sp.Ne(e, 0))
    return outs


def rel_text(sp, rel):
    """关系式 -> 纯文本(Ne 用 ≠, 因为 sstr 会打成 Ne(...))"""
    if isinstance(rel, sp.Ne):
        return "%s ≠ %s" % (pretty(sp.sstr(rel.lhs)), pretty(sp.sstr(rel.rhs)))
    return pretty(sp.sstr(rel))


def rel_latex(sp, rel):
    return sp.latex(rel)


def cond_render(sp, rels):
    """一组关系 -> (纯文本, LaTeX)"""
    txt = " \u2227 ".join(rel_text(sp, r) for r in rels)
    lat = " \\wedge ".join(rel_latex(sp, r) for r in rels)
    return txt, lat


def apply_constraints(sp, sols, rels_raw, env, syms):
    """把关系约束代入解族: 矛盾的分支丢掉, 其余给出化简后的约束文本。
    返回 (kept, notes, dropped); kept = [(sol, cond_plain, cond_latex), ...]"""
    notes, dropped = [], 0
    rels = []
    for raw in rels_raw:
        for one in (split_chain(raw) or [raw]):
            try:
                rels.append(relation_obj(sp, one, env))
            except Exception as exc:
                notes.append("无法解析关系 %s: %s" % (one, exc))
    if not rels:
        return None, notes, 0
    # 周期性/超越函数的不等式: 实测 SymPy 会漏解(sin(x)>0 只给 (0,pi), 丢掉周期性),
    # 所以只原样保留约束并明确标注, 绝不做"看起来化简了"的处理。
    periodic = False
    for rel in rels:
        atoms = getattr(rel, "atoms", None)
        if atoms is None:
            continue
        for fn in atoms(sp.Function):
            if isinstance(fn, (sp.sin, sp.cos, sp.tan, sp.cot, sp.sec, sp.csc,
                               sp.asin, sp.acos, sp.atan, sp.sinh, sp.cosh, sp.tanh)):
                periodic = True
    if periodic:
        notes.append("含周期/超越函数的不等式未做化简(这类只能给不保证完整的结果, 已原样保留)")

    kept = []
    for sol in sols:
        sub = {}
        if isinstance(sol, dict):
            for v in syms:
                val = sol.get(v)
                if val is not None:
                    sub[v] = val
        ne_list, ineq_list, dead = [], [], False
        for rel in rels:
            try:
                r2 = sp.simplify(rel.subs(sub)) if sub else sp.simplify(rel)
            except Exception:
                r2 = rel
            if isinstance(r2, sp.Ne):
                e = sp.simplify(r2.lhs - r2.rhs)
                if e.is_zero:
                    dead = True
                    break
                if not e.free_symbols:
                    continue
                ne_list.append(e)
                continue
            if r2 is sp.true or r2 is True:
                continue
            if r2 is sp.false or r2 is False:
                dead = True
                break
            if not isinstance(r2, sp.Rel):
                continue
            e = sp.simplify(r2.lhs - r2.rhs)
            if not e.free_symbols:
                continue
            ineq_list.append((e, r2.func))
        if dead:
            dropped += 1
            continue
        conds = []
        if ineq_list and not periodic:
            free = set()
            for e, _op in ineq_list:
                free |= (e.free_symbols & set(syms))
            if len(free) == 1:
                sym = list(free)[0]
                try:
                    # 先各自化成解集(区间并集)再求交: 比直接 reduce 关系式干净得多
                    # (直接 reduce 会留下 (0<y) & ((y<-√2) | ((0<y)&(y<√2))) 这种冗余形式)
                    sets = [sp.solve_univariate_inequality(op(e, 0), sym, relational=False)
                            for e, op in ineq_list]
                    inter = sp.Intersection(*sets).simplify()
                    if inter is sp.S.EmptySet:
                        dropped += 1
                        continue
                    if inter != sp.S.Reals:
                        conds.append(inter.as_relational(sym))
                except Exception:
                    try:
                        red = sp.reduce_inequalities([op(e, 0) for e, op in ineq_list], sym)
                        if red is sp.false or red == sp.S.EmptySet:
                            dropped += 1
                            continue
                        if red is not sp.true and red != sp.S.Reals:
                            conds.append(red)
                    except Exception:
                        conds.extend([op(e, 0) for e, op in ineq_list])
                        notes.append("不等式未能完全化简, 已原样保留")
            else:
                conds.extend([op(e, 0) for e, op in ineq_list])
                notes.append("多元非线性约束未做化简(已原样保留, 可用数值代入验证)")
        elif ineq_list:
            conds.extend([op(e, 0) for e, op in ineq_list])
        for e in ne_list:
            conds.extend(ne_conditions(sp, e))
        if conds:
            p, l = cond_render(sp, conds)
            kept.append((sol, p, l))
        else:
            kept.append((sol, "", ""))
    if dropped and not kept:
        notes.append("这些解在给定约束下都不成立")
    return kept, notes, dropped


def do_solve(sp, pos, domain, digits, rels=None):
    if not pos and not rels:
        fail("没有方程")
    env = solve_locals(sp, pos + (rels or []) + [d[0] for d in DERIVES], real=bool(rels))
    exprs = [sp.sympify(p, locals=env) for p in pos]
    syms = sorted(set().union(*[e.free_symbols for e in exprs]), key=lambda s: s.name)
    # 声明为常量的字母: 从求解变量里剔除(它们仍是符号, 只是不当未知量)
    const_syms = set()
    for name in CONSTS:
        v = env.get(name)
        if isinstance(v, sp.Symbol):
            const_syms.add(v)
    if const_syms:
        syms = [s for s in syms if s not in const_syms]
        emit("CONST", ", ".join(sorted(s.name for s in const_syms)))
    emit("METHOD", "sympy")
    for s in syms:
        emit("VAR", s.name)

    # ---- 只给关系式、没有等式: 解集就是这些关系的交集(以前会错报"任意值都满足") ----
    if not exprs:
        rel_objs = []
        for raw in rels:
            for one in (split_chain(raw) or [raw]):
                try:
                    rel_objs.append(relation_obj(sp, one, env))
                except Exception as exc:
                    emit("STATUS", "ok")
                    emit("NOTE", "无法解析关系 %s: %s" % (one, exc))
        rsyms = set()
        for r in rel_objs:
            rsyms |= getattr(r, "free_symbols", set())
        rsyms &= {v for v in env.values() if isinstance(v, sp.Symbol)}
        syms2 = sorted(rsyms, key=lambda s: s.name)
        for s in syms2:
            emit("VAR", s.name)
        # 周期性/超越函数: 拒绝给"看起来精确"的解集(实测 SymPy 会漏掉周期性)
        periodic = False
        for r in rel_objs:
            atoms = getattr(r, "atoms", None)
            if atoms is None:
                continue
            for fn in atoms(sp.Function):
                if isinstance(fn, (sp.sin, sp.cos, sp.tan, sp.cot, sp.sec, sp.csc)):
                    periodic = True
        if periodic:
            # 单条关系 + 单变量: 用"精确周期 + 一个周期内的数值根"给出区间族(标注为数值参考)
            handled = False
            allow = (INEQ_NUM == "always") or (INEQ_NUM == "auto" and len(rel_objs) == 1)
            if allow and INEQ_NUM != "never" and len(syms2) == 1:
                try:
                    ivs, note = numeric_periodic_set(sp, rel_objs, syms2[0], digits)
                except Exception:
                    ivs, note = None, ""
                if ivs is not None:
                    p_txt, l_txt = periodic_render(sp, syms2[0], ivs, digits)
                    emit("STATUS", "ok")
                    if not ivs:
                        emit("KIND", "none")
                    else:
                        if note:
                            emit("NOTE", note)
                        emit("KIND", "set")
                        emit("SET", p_txt, l_txt)
                    handled = True
            if handled:
                return
            emit("STATUS", "ok")
            emit("NOTE", "含周期函数: 这类不等式求解器只能给不保证完整的结果, 因此不化简; 约束原样列出")
            if syms2:
                emit("KIND", "system")
                for s in syms2:
                    emit("SYS", s.name, s.name, s.name, "1")
                p_txt, l_txt = cond_render(sp, rel_objs)
                emit("COND", "1", p_txt, l_txt)
            else:
                emit("KIND", "none")
            return
        if len(syms2) == 1:
            v = syms2[0]
            sets = []
            ok = True
            for r in rel_objs:
                try:
                    if isinstance(r, sp.Ne):
                        other = r.rhs if r.lhs == v else r.lhs
                        sets.append(sp.Complement(sp.S.Reals, sp.FiniteSet(other)))
                    else:
                        sets.append(sp.solve_univariate_inequality(r, v, relational=False))
                except Exception:
                    ok = False
            if ok and sets:
                inter = sp.Intersection(*sets).simplify()
                emit("STATUS", "ok")
                if inter is sp.S.EmptySet:
                    emit("KIND", "none")
                elif inter == sp.S.Reals:
                    emit("KIND", "all")
                else:
                    emit("KIND", "set")
                    emit("SET", set_plain(sp, inter), sp.latex(inter))
                return
            emit("STATUS", "ok")
            p_txt, l_txt = cond_render(sp, rel_objs)
            emit("NOTE", "未能化简为区间, 约束原样列出")
            emit("KIND", "set")
            emit("SET", p_txt, l_txt)
            return
        emit("STATUS", "ok")
        if not syms2:
            # 纯常量关系
            truth = True
            for r in rel_objs:
                try:
                    truth = truth and bool(r)
                except Exception:
                    truth = False
            emit("KIND", "all" if truth else "none")
            return
        # 多变量关系: 无法化简为区间, 原样列出(并明确说明)
        p_txt, l_txt = cond_render(sp, rel_objs)
        emit("NOTE", "多元关系无法化简为区间, 已原样列出(可用数值代入验证)")
        emit("KIND", "set")
        emit("SET", p_txt, l_txt)
        return

    if not syms:
        if const_syms:
            emit("STATUS", "ok")
            emit("NOTE", "全部符号都被声明为常量, 没有可解的未知量")
            emit("KIND", "all")
            return
        zero = all(sp.simplify(e) == 0 for e in exprs)
        emit("STATUS", "ok")
        emit("KIND", "all" if zero else "none")
        return

    if len(syms) == 1 and not rels and not const_syms:
        v = syms[0]
        polys, allpoly = [], True
        for e in exprs:
            try:
                polys.append(sp.Poly(e, v))
            except Exception:
                allpoly = False
                break
        if allpoly:
            g = polys[0]
            for p in polys[1:]:
                g = sp.gcd(g, p)
            if g.is_zero:
                emit("STATUS", "ok"); emit("KIND", "all"); return
            if g.degree() <= 0:
                emit("STATUS", "ok"); emit("KIND", "none"); return
            emit("POLY", sp.sstr(g.as_expr()).replace("**", "^"), sp.latex(g.as_expr()), str(g.degree()))
            rts = sp.roots(g)
            emit("STATUS", "ok"); emit("KIND", "single")
            if rts:
                def key_of(kv):
                    r = kv[0]
                    try:
                        return (1 if r.has(sp.I) else 0, float(sp.N(sp.re(r), 20)),
                                float(sp.N(sp.im(r), 20)))
                    except Exception:
                        return (2, 0.0, 0.0)
                for r, m in sorted(rts.items(), key=key_of):
                    emit_root(sp, r, m, digits)
            else:
                emit("NOTE", "该多项式无有理根/根式解, 使用数值求根")
                for r in g.nroots(n=digits + 10, maxsteps=300):
                    emit_root(sp, sp.nsimplify(r, rational=False), 1, digits)
            return
        f = sp.simplify(exprs[0]) if len(exprs) == 1 else None
        if f is None:
            sol = sp.solve(exprs, v, dict=False)
            if isinstance(sol, list) and sol:
                emit("STATUS", "ok"); emit("KIND", "single")
                for r in sol:
                    emit_root(sp, r, 1, digits)
                return
        ss = sp.solveset(f, v, domain=(sp.S.Reals if domain == "real" else sp.S.Complexes))
        emit("STATUS", "ok")
        if ss is sp.S.EmptySet:
            emit("KIND", "none")
        elif ss == sp.S.Reals or ss == sp.S.Complexes:
            emit("KIND", "all")
        elif isinstance(ss, sp.FiniteSet):
            emit("KIND", "single")
            for r in ss:
                emit_root(sp, r, 1, digits)
        elif isinstance(ss, sp.ConditionSet):
            emit("KIND", "fallback")
        else:
            emit("KIND", "set")
            emit("SET", set_plain(sp, ss), sp.latex(ss))
        return

    sols = None
    try:
        sols = sp.solve(exprs, syms, dict=True)
    except Exception:
        sols = None
    # 多项式方程组的 sp.solve 常常漏解(即使有解也只给一部分), 有约束时再并上
    # nonlinsolve 的分支 —— 每个分支各自带约束, 宁可多给也不漏。
    if rels and sols:
        try:
            ns = sp.nonlinsolve(exprs, syms)
            for t in ns:
                sols.append(dict(zip(syms, t)))
        except Exception:
            pass
    if sols:
        sols = dedupe_solutions(sp, sols, syms)
    if not sols:
        try:
            ns = sp.nonlinsolve(exprs, syms)
            sols = [dict(zip(syms, t)) for t in ns] if ns is not None else []
        except Exception:
            sols = []
    emit("STATUS", "ok")
    if not sols:
        emit("KIND", "none")
        return
    if all(isinstance(s, dict) and not s for s in sols):
        emit("KIND", "all")
        return
    free = set()
    for sol in sols:
        if isinstance(sol, dict):
            for val in sol.values():
                if isinstance(val, sp.Basic):
                    free |= (val.free_symbols & set(syms))
    # 有约束时的额外 VAR(关系里出现但等式里没有的变量)
    cond_map = {}
    if rels:
        kept, cnotes, dropped = apply_constraints(sp, sols, rels, env, syms)
        for n in cnotes:
            emit("NOTE", n)
        if kept is None:
            kept = [(s, "", "") for s in sols]
        if not kept:
            emit("KIND", "none")
            return
        uniq = []
        for (s, p, l) in kept:
            if not any(same_solution(sp, s, t[0], syms) for t in uniq):
                uniq.append((s, p, l))
        kept = uniq
        sols = [k[0] for k in kept]
        for idx, (s, p, l) in enumerate(kept, 1):
            if p:
                cond_map[tuple(str(s.get(v)) for v in syms)] = (p, l)
    emit("KIND", "system")
    if free:
        emit("NOTE", "存在自由参数: " + ", ".join(sorted(s.name for s in free))
             + ("(取值范围见下方约束)" if rels else ""))
    seen = set()
    sol_index = 0
    for sol in sols:
        if not isinstance(sol, dict):
            continue
        key = tuple(str(sol.get(v)) for v in syms)
        if key in seen:
            continue
        seen.add(key)
        sol_index += 1
        for v in syms:
            val = sol.get(v)
            if val is None or val == v:
                emit("SYS", v.name, v.name, v.name, "1")
            else:
                approx = ""
                try:
                    if val.is_number:
                        approx = numeric_of(sp, val, digits)
                except Exception:
                    approx = ""
                v2 = exact_form(sp, val)
                emit("SYS", v.name, "%s = %s" % (v.name, pretty(sp.sstr(v2))),
                     "%s = %s" % (v.name, sp.latex(v2)), "0", approx)
        if key in cond_map:
            emit("COND", str(sol_index), cond_map[key][0], cond_map[key][1])
        # ---- 分组: 按"该解依赖哪些没有确定值的未知量"分组(同组即"坐一桌") ----
        solved_vars = [v for v in syms if sol.get(v) is not None]
        if len(solved_vars) >= 2:
            solved_set = set(solved_vars)
            free_unknowns = [v for v in syms if sol.get(v) is None]

            def dep_of(val):
                # 值里出现的、没有确定数值的符号: 未解未知量 + 用户声明的常量参数
                return set(getattr(val, "free_symbols", set()) or set()) - solved_set
            groups = []
            for v in solved_vars:
                val = sol.get(v)
                deps = dep_of(val)
                key = tuple(sorted(s.name for s in deps))
                hit = None
                for g in groups:
                    if g[0] == key:
                        hit = g
                        break
                if hit is None:
                    groups.append([key, [v]])
                else:
                    hit[1].append(v)
            # 一个自由未知量都不依赖(都是确定数值)时不需要分组, 免得刷屏
            if not any(k for k, _vs in groups):
                groups = []
            for key, vs in groups:
                name_list = ", ".join(v.name for v in vs)
                dep_txt = ("取决于 " + ", ".join(key)) if key else "不依赖未确定量"
                emit("GRP", str(sol_index), "分组(" + dep_txt + "): " + name_list)
                if len(vs) < 2:
                    continue
                vals = [sol[v] for v in vs]
                cand = None
                if len(key) == 1:
                    # 公共自由参数约掉: a=c(√5+3)/2, b=c(1+√5)/2 -> (√5+3)/2 : (1+√5)/2
                    try:
                        pv = [u for u in dep_of(vals[0]) if u.name == key[0]][0]
                        div = [sp.simplify(x / pv) for x in vals]
                        if all(not dep_of(d) for d in div):   # 约掉参数后应是纯数值
                            cand = div
                    except Exception:
                        cand = None
                if cand is None:
                    # 多参数/非线性时至少提出公因子: t^2 : t : 4t -> t : 1 : 4
                    try:
                        ks = [u for u in dep_of(vals[0]) if u.name in key]
                        if ks and all(sp.sympify(x).is_polynomial(*ks) for x in vals):
                            g = sp.gcd_list([sp.sympify(x) for x in vals])
                            if g != 0 and g != 1:
                                cand = [sp.cancel(x / g) for x in vals]
                    except Exception:
                        cand = None
                if cand is None:
                    cand = _norm_ratio(vals, solved_set)   # 含根式时也能约一约
                if cand is None:
                    cand = vals
                # 约完之后可能已经变成纯数值: a=2t+1,b=4t+2 -> 1 : 2
                numeric_ratio = all(not dep_of(x) for x in cand)
                if numeric_ratio:
                    # 纯数值比还要约分: 3 : 3 : 6 -> 1 : 1 : 2
                    try:
                        import math
                        rs = [sp.Rational(x) for x in cand]
                        L = 1
                        for r in rs:
                            L = L * r.q // math.gcd(L, r.q)
                        ints = [int(r * L) for r in rs]
                        G = 0
                        for n in ints:
                            G = math.gcd(G, abs(n))
                        if G > 1:
                            cand = [sp.Rational(n // G, 1) for n in ints]
                    except Exception:
                        pass
                def _nice(z):
                    try:
                        z = sp.together(sp.simplify(z))   # (√5+3)/2 而不是 √5/2+3/2
                    except Exception:
                        pass
                    return exact_form(sp, z)
                txt = " : ".join(pretty(sp.sstr(_nice(x))) for x in cand)
                lat = " : ".join(sp.latex(_nice(x)) for x in cand)
                emit("RATIO", str(sol_index), " : ".join(v.name for v in vs), txt, lat,
                     "1" if numeric_ratio else "0")

        if DERIVES:
            sub2 = {}
            for v in syms:
                if sol.get(v) is not None:
                    sub2[v] = sol.get(v)
            for pair in DERIVES:
                dtxt, dname = pair[0], pair[1]
                try:
                    e = sp.sympify(dtxt, locals=env)
                    val = sp.simplify(e.subs(sub2)) if sub2 else sp.simplify(e)
                    # 代入后毫无进展(还是原样)时, 尝试用方程把它换成更简形式:
                    #   方程 L=R 可写成 e = (e - (L-R)), 例如 xy=n 时 e=x*y -> n
                    if sp.simplify(val - e) == 0:
                        best = val
                        best_n = len(getattr(val, "free_symbols", set()) or [])
                        for eq in exprs:
                            try:
                                cand = sp.simplify(e - eq)
                            except Exception:
                                continue
                            if cand == val or cand.has(sp.zoo):
                                continue
                            cn = len(getattr(cand, "free_symbols", set()) or [])
                            if cn < best_n:
                                best, best_n = cand, cn
                        val = best
                    v2 = exact_form(sp, val)
                    emit("DERIVE", str(sol_index), dname, pretty(sp.sstr(v2)), sp.latex(v2))
                except Exception as exc:
                    emit("NOTE", "派生量 %s 计算失败: %s" % (dname, exc))


main()
)EASYMATH_PY";

const char *sympyEngineScript() { return kEngineScript; }

static const char *sciName(SciMode m) {
    switch (m) {
        case SciMode::Always: return "always";
        case SciMode::Never: return "never";
        default: return "auto";
    }
}


// ============================ 子进程执行 ============================

#if !defined(_WIN32)
static long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif

#if defined(_WIN32)
// ================= Windows: CreateProcess + 匿名管道 =================
// 不用 _popen/cmd.exe, 因此没有 shell 转义问题; 脚本仍从 stdin 送入, 方程走 argv。

static std::wstring utf8ToWide(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

// Windows 命令行参数引用规则(反斜杠+引号)
static std::wstring quoteWinArg(const std::string &a) {
    std::wstring w = utf8ToWide(a);
    bool need = w.empty();
    for (wchar_t c : w)
        if (c == L' ' || c == L'\t' || c == L'"') need = true;
    if (!need) return w;
    std::wstring r = L"\"";
    int backslashes = 0;
    for (wchar_t c : w) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            r.append(static_cast<std::size_t>(backslashes) * 2 + 1, L'\\');
            r += L'"';
            backslashes = 0;
            continue;
        }
        r.append(static_cast<std::size_t>(backslashes), L'\\');
        backslashes = 0;
        r += c;
    }
    r.append(static_cast<std::size_t>(backslashes) * 2, L'\\');
    r += L'"';
    return r;
}

static bool runPythonPosix(const std::string &python, const std::vector<std::string> &args,
                           const std::string &script, int timeoutMs, std::string &out,
                           std::string &err) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) {
        err = L("无法创建管道", "CreatePipe failed");
        return false;
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = quoteWinArg(python) + L" -";
    for (const auto &a : args) cmd += L" " + quoteWinArg(a);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = outW;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
        err = "CreateProcess failed (error " + std::to_string(GetLastError()) + ")";
        CloseHandle(inR);
        CloseHandle(inW);
        CloseHandle(outR);
        CloseHandle(outW);
        return false;
    }
    CloseHandle(inR);
    CloseHandle(outW);

    std::size_t off = 0;
    while (off < script.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(script.size() - off, 65536));
        DWORD written = 0;
        if (!WriteFile(inW, script.data() + off, chunk, &written, nullptr) || written == 0) break;
        off += written;
    }
    CloseHandle(inW);

    std::string data;
    char buf[8192];
    bool timedOut = false;
    ULONGLONG deadline =
        static_cast<ULONGLONG>(GetTickCount()) + static_cast<ULONGLONG>(timeoutMs > 0 ? timeoutMs : 15000);
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(outR, nullptr, 0, nullptr, &avail, nullptr)) break; // 子进程已退出
        if (avail == 0) {
            DWORD w = WaitForSingleObject(pi.hProcess, 20);
            if (w == WAIT_OBJECT_0) {
                DWORD got = 0;
                while (ReadFile(outR, buf, sizeof(buf), &got, nullptr) && got > 0)
                    data.append(buf, got);
                break;
            }
            // GetTickCount 是 32 位毫秒(约 49 天回绕), 用无符号差值比较保证正确
            if (static_cast<long long>(static_cast<ULONGLONG>(GetTickCount()) - deadline) >= 0) {
                timedOut = true;
                break;
            }
            continue;
        }
        DWORD got = 0;
        if (!ReadFile(outR, buf, sizeof(buf), &got, nullptr) || got == 0) break;
        data.append(buf, got);
    }
    if (timedOut) TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(outR);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (timedOut) {
        err = L("外部引擎超时", "external engine timed out");
        return false;
    }
    if (code != 0) {
        std::string first = data.substr(0, data.find('\n'));
        err = "python exit " + std::to_string(code) + (first.empty() ? "" : (": " + first));
        return false;
    }
    out = data;
    return true;
}

#else // ---------------- POSIX: fork/exec + pipe/poll ----------------

static bool runPythonPosix(const std::string &python, const std::vector<std::string> &args,
                           const std::string &script, int timeoutMs, std::string &out,
                           std::string &err) {
    int inPipe[2] = {-1, -1};
    int outPipe[2] = {-1, -1};
    if (::pipe(inPipe) != 0 || ::pipe(outPipe) != 0) {
        err = L("无法创建管道", "pipe() failed");
        return false;
    }
    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(inPipe[0]); ::close(inPipe[1]);
        ::close(outPipe[0]); ::close(outPipe[1]);
        err = L("fork 失败", "fork() failed");
        return false;
    }
    if (pid == 0) {
        ::dup2(inPipe[0], 0);
        ::dup2(outPipe[1], 1);
        ::close(inPipe[0]); ::close(inPipe[1]);
        ::close(outPipe[0]); ::close(outPipe[1]);
        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(python.c_str()));
        argv.push_back(const_cast<char *>("-"));
        for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);
        ::execvp(python.c_str(), argv.data());
        _exit(127);
    }
    ::close(inPipe[0]);
    ::close(outPipe[1]);

    std::size_t off = 0;
    while (off < script.size()) {
        ssize_t w = ::write(inPipe[1], script.data() + off, script.size() - off);
        if (w <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += static_cast<std::size_t>(w);
    }
    ::close(inPipe[1]);

    std::string data;
    char buf[8192];
    long long deadline = nowMs() + (timeoutMs > 0 ? timeoutMs : 15000);
    bool timedOut = false;
    for (;;) {
        if (interruptRequested()) {
            ::kill(pid, SIGKILL); // 用户中断: 立即结束外部引擎
            break;
        }
        long long remain = deadline - nowMs();
        if (remain <= 0) {
            timedOut = true;
            break;
        }
        struct pollfd pfd;
        pfd.fd = outPipe[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = ::poll(&pfd, 1, static_cast<int>(remain));
        if (pr == 0) {
            timedOut = true;
            break;
        }
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        ssize_t r = ::read(outPipe[0], buf, sizeof(buf));
        if (r <= 0) break;
        data.append(buf, static_cast<std::size_t>(r));
    }
    ::close(outPipe[0]);
    if (timedOut) ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (timedOut) {
        err = L("外部引擎超时", "external engine timed out");
        return false;
    }
    if (!WIFEXITED(status)) {
        err = L("外部引擎异常退出", "external engine crashed");
        return false;
    }
    int code = WEXITSTATUS(status);
    if (code != 0) {
        std::string first = data.substr(0, data.find('\n'));
        err = "python3 exit " + std::to_string(code) + (first.empty() ? "" : (": " + first));
        return false;
    }
    out = data;
    return true;
}

#endif // _WIN32

// 统一入口: 优先用宿主注入的执行器(Android/Chaquopy), 否则按平台分发
static PythonRunner g_pythonRunner = nullptr;

// 执行器代数: 桌面端用 fork/exec, Android 用 Chaquopy 进程内执行器,
// 同一个 python 路径在两种执行器下的探测结果完全不同, 所以缓存键要带代数。
static unsigned long &pythonRunnerGeneration() {
    static unsigned long g = 0;
    return g;
}

void setPythonRunner(PythonRunner runner) {
    g_pythonRunner = runner;
    ++pythonRunnerGeneration(); // 执行器变了, 之前的探测结论作废
}
PythonRunner currentPythonRunner() { return g_pythonRunner; }

static bool runPython(const std::string &python, const std::vector<std::string> &args,
                      const std::string &script, int timeoutMs, std::string &out, std::string &err) {
    if (interruptRequested()) {
        err = L("计算已被中断", "interrupted by user");
        return false;
    }
    if (g_pythonRunner) return g_pythonRunner(script, args, out, err);
    return runPythonPosix(python, args, script, timeoutMs, out, err);
}

// ============================ 记录解析 ============================

struct Rec {
    std::string key;
    std::vector<std::string> f;
};

static std::vector<Rec> parseRecords(const std::string &data) {
    std::vector<Rec> out;
    std::istringstream in(data);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        Rec r;
        std::size_t start = 0;
        bool first = true;
        while (true) {
            std::size_t tab = line.find('\t', start);
            std::string piece = (tab == std::string::npos) ? line.substr(start) : line.substr(start, tab - start);
            if (first) {
                r.key = piece;
                first = false;
            } else {
                r.f.push_back(piece);
            }
            if (tab == std::string::npos) break;
            start = tab + 1;
        }
        if (r.key == "END") break;
        out.push_back(r);
    }
    return out;
}

// ============================ 探测 ============================

// 缓存键里带上代数(见 pythonRunnerGeneration 的说明)。
// 曾经只用 python 路径做键: 执行器没注册时探测失败会被永久记住 →
// 整个进程都不再用 SymPy(真机表现: 引擎徽标一直"内置", 精确根式/高精度全失效)。
EngineInfo detectSympyEngine(const std::string &python) {
    static std::map<std::string, EngineInfo> cache;
    std::string key = python + "#" + std::to_string(pythonRunnerGeneration());
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    EngineInfo info;
    info.probed = true;
    info.python = python;
    std::string out, err;
    std::vector<std::string> args{"version"};
    // 探测超时要给足: 机器繁忙时 import sympy 可能超过 10 秒, 一旦误判为"不可用"
    // 就会静默退回内置引擎(结果从精确根式变成数值近似), 所以宁可多等。
    if (!runPython(python, args, sympyEngineScript(), 30000, out, err)) {
        info.available = false;
        info.error = err;
        // 进程内执行器在场时不缓存失败: 可能只是首次启动 Python 的临时问题,
        // 缓存下来会把"暂时失败"变成"永久不可用"。
        if (!g_pythonRunner) cache[key] = info;
        return info;
    }
    auto recs = parseRecords(out);
    bool ok = false;
    for (const auto &r : recs) {
        if (r.key == "STATUS" && !r.f.empty() && r.f[0] == "ok") ok = true;
        if (r.key == "VERSION" && !r.f.empty()) info.version = r.f[0];
        if (r.key == "ERROR" && !r.f.empty()) info.error = r.f[0];
    }
    info.available = ok;
    cache[key] = info;
    return info;
}

bool sympyUsable(const std::string &python) { return detectSympyEngine(python).available; }

// ============================ 求解 ============================

// 关系运算符(已由 normalize_math 归一成 ASCII 形式), 长的优先
static const char *kRelOps[] = {">=", "<=", "!=", ">", "<"};

static bool splitRelation(const std::string &s, std::string &lhs, std::string &op,
                          std::string &rhs) {
    for (const char *cand : kRelOps) {
        std::size_t p = s.find(cand);
        if (p != std::string::npos) {
            lhs = s.substr(0, p);
            op = cand;
            rhs = s.substr(p + std::strlen(cand));
            return !lhs.empty() && !rhs.empty();
        }
    }
    return false;
}

// a>b>c>0 -> {a>b, b>c, c>0}
static void splitRelationChain(const std::string &s, std::vector<std::string> &out) {
    std::vector<std::string> parts;
    std::vector<std::string> ops;
    std::string cur;
    std::size_t i = 0;
    while (i < s.size()) {
        const char *hit = nullptr;
        for (const char *cand : kRelOps) {
            if (s.compare(i, std::strlen(cand), cand) == 0) { hit = cand; break; }
        }
        if (hit) {
            parts.push_back(cur);
            ops.push_back(hit);
            cur.clear();
            i += std::strlen(hit);
        } else {
            cur += s[i++];
        }
    }
    parts.push_back(cur);
    if (ops.empty()) {
        out.push_back(s);
        return;
    }
    for (std::size_t k = 0; k < ops.size(); ++k) {
        std::string a = parts[k], b = parts[k + 1];
        auto trim = [](std::string t) {
            std::size_t s0 = t.find_first_not_of(" \t");
            std::size_t e0 = t.find_last_not_of(" \t");
            return (s0 == std::string::npos) ? std::string() : t.substr(s0, e0 - s0 + 1);
        };
        out.push_back(trim(a) + ops[k] + trim(b));
    }
}

// 逗号(也支持中文逗号/分号/空白)分隔的列表 -> 去空项
std::vector<std::string> splitCommaList(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        std::size_t a = cur.find_first_not_of(" \t");
        std::size_t b = cur.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
        cur.clear();
    };
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == ',' || c == ';' || c == '\n' || c == '\r') { flush(); continue; }
        if (c == '\xef' && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xbc &&
            static_cast<unsigned char>(s[i + 2]) == 0x8c) { // 中文逗号
            flush();
            i += 2;
            continue;
        }
        cur += c;
    }
    flush();
    return out;
}

bool textHasRelation(const std::string &text) {
    // normalize_math 已把 ≥ ≤ ≠ 归一成 >= <= !=, 所以只需看 ASCII 形式
    return text.find('>') != std::string::npos || text.find('<') != std::string::npos ||
           text.find("!=") != std::string::npos;
}

static bool buildSympyExprs(const std::vector<std::string> &eqs, std::vector<std::string> &out,
                            std::vector<std::string> &outRels, std::string &err) {
    std::set<std::string> declared;
    for (const auto &e : eqs) {
        auto n = scanDeclaredNames(normalize_math(e));
        declared.insert(n.begin(), n.end());
    }
    for (const auto &raw : eqs) {
        std::string s = normalize_math(raw);
        if (s.empty()) continue;
        // 含关系运算符的条目(如 a>b>c>0、abc≠0)当约束交给 SymPy。
        // 注意: 两边仍要过我们的解析器 —— 否则 SymPy 会把 abc 当成一个名字叫"abc"的变量,
        // 而正确语义是 a·b·c(隐式乘法), 这点由 declared 名字集决定。
        if (textHasRelation(s)) {
            std::vector<std::string> chain;
            splitRelationChain(s, chain);
            bool anyBad = false;
            for (const auto &one : chain) {
                std::string lhs, op, rhs;
                if (!splitRelation(one, lhs, op, rhs)) { err = "关系式无法解析: " + one; return false; }
                ParseOptions po2;
                po2.numeric_only = false;
                po2.declared = declared;
                std::string perr2;
                NodePtr L2 = parseExpression(lhs, po2, perr2);
                if (!L2) { err = perr2; return false; }
                NodePtr R2 = parseExpression(rhs, po2, perr2);
                if (!R2) { err = perr2; return false; }
                outRels.push_back(astSympy(L2) + op + astSympy(R2));
                anyBad = false;
            }
            (void)anyBad;
            continue;
        }
        std::string lhs, rhs;
        bool hasEq = splitEquation(s, lhs, rhs);
        ParseOptions po;
        po.numeric_only = false;
        po.declared = declared;
        std::string perr;
        NodePtr L, R;
        if (hasEq) {
            L = parseExpression(lhs, po, perr);
            if (!L) { err = perr; return false; }
            R = parseExpression(rhs, po, perr);
            if (!R) { err = perr; return false; }
        } else {
            L = parseExpression(rhs, po, perr);
            if (!L) { err = perr; return false; }
            R = Node::num_(Rational(0));
        }
        out.push_back(astSympy(Node::op(NT::Sub, L, R)));
    }
    return !out.empty() || !outRels.empty();
}

bool solveWithSympy(const std::vector<std::string> &eqs, const SolveOptions &opt,
                    const std::string &python, int timeoutMs, SolveResult &out, std::string &err) {
    std::vector<std::string> exprs, rels;
    if (!buildSympyExprs(eqs, exprs, rels, err)) return false;
    std::vector<std::string> args;
    args.push_back("solve");
    args.push_back("--digits=" + std::to_string(opt.decimals));
    args.push_back(std::string("--sci=") + sciName(opt.fmt.sci));
    args.push_back("--scithr=" + std::to_string(opt.fmt.sciThreshold));
    args.push_back(std::string("--domain=") + ((opt.realOnly || !opt.complexAllowed) ? "real" : "complex"));
    for (const auto &e : exprs) args.push_back(e);
    for (const auto &r : rels) args.push_back("--rel=" + r);
    if (!rels.empty()) args.push_back("--ineqnum=" + opt.numericInequality);
    // 常量名逐个传(桥里只需知道名字)
    for (const auto &c : splitCommaList(opt.constants)) args.push_back("--const=" + c);
    // 派生量必须先用我们的解析器转成 SymPy 串: 否则 abc 会被 SymPy 当成一个叫"abc"的变量,
    // 而正确语义是 a·b·c(隐式乘法, 由 declared 名字集决定)
    if (!opt.derive.empty()) {
        std::set<std::string> declared;
        for (const auto &e : eqs) {
            auto n = scanDeclaredNames(normalize_math(e));
            declared.insert(n.begin(), n.end());
        }
        for (const auto &c : splitCommaList(opt.constants)) declared.insert(c);
        for (const auto &d : splitCommaList(opt.derive)) {
            ParseOptions po;
            po.numeric_only = false;
            po.declared = declared;
            std::string perr;
            NodePtr n = parseExpression(normalize_math(d), po, perr);
            if (!n) {
                err = L("派生量无法解析: ", "cannot parse derived expression: ") + d + " (" + perr + ")";
                return false;
            }
            args.push_back("--derive=" + astSympy(n));
            args.push_back("--derivename=" + astPlain(n));
        }
    }
    std::string data;
    if (!runPython(python, args, sympyEngineScript(), timeoutMs, data, err)) return false;
    auto recs = parseRecords(data);

    std::string kind;
    bool statusOk = false;
    for (const auto &r : recs) {
        if (r.key == "STATUS") statusOk = (!r.f.empty() && r.f[0] == "ok");
        else if (r.key == "ERROR") err = r.f.empty() ? "" : r.f[0];
        else if (r.key == "KIND") kind = r.f.empty() ? "" : r.f[0];
    }
    if (!statusOk) return false;
    if (kind == "fallback") {
        err = L("SymPy 无闭式解, 回退到内置数值方法", "SymPy has no closed form; falling back to numeric");
        return false;
    }

    out = SolveResult();
    out.ok = true;
    out.method = "SymPy " + detectSympyEngine(python).version + L(" (外部引擎)", " (external engine)");

    // 变量表
    for (const auto &r : recs) {
        if (r.key == "VAR" && !r.f.empty()) out.vars.push_back(r.f[0]);
        else if (r.key == "NOTE" && !r.f.empty()) out.notes.push_back(r.f[0]);
        else if (r.key == "SET" && r.f.size() >= 2) {
            out.hasGeneralSet = true;
            out.generalSetPlain = r.f[0];
            out.generalSetLatex = r.f[1];
        } else if (r.key == "POLY" && r.f.size() >= 2) {
            out.hasPolyStrings = true;
            out.polyPlain = r.f[0];
            out.polyLatex = r.f[1];
            out.polyVar = out.vars.empty() ? "x" : out.vars[0];
        }
    }

    if (kind == "all") {
        out.identity = true;
        return true;
    }
    if (kind == "none") {
        out.none = true;
        return true;
    }
    if (kind == "set") {
        out.kind = SolveResult::SingleVar;
        out.var = out.vars.empty() ? "x" : out.vars[0];
        return true;
    }
    if (kind == "single") {
        out.kind = SolveResult::SingleVar;
        out.var = out.vars.empty() ? "x" : out.vars[0];
        for (const auto &r : recs) {
            if (r.key != "SOL" || r.f.size() < 6) continue;
            RootOut ro;
            ro.plain = r.f[0];
            ro.latex = r.f[1];
            ro.approxPlain = r.f[2];
            ro.exact = (r.f[3] == "1");
            ro.isComplex = (r.f[4] == "1");
            ro.mult = std::max(1, std::atoi(r.f[5].c_str()));
            ro.numeric = std::complex<long double>(0, 0);
            // 非纯数字的精确根(根式/复数)值得附上数值近似, 否则用户看不出大小
            bool pureNumber = !ro.plain.empty() &&
                              ro.plain.find_first_not_of("0123456789.-+/ ") == std::string::npos;
            ro.haveApprox = !pureNumber && !ro.approxPlain.empty();
            out.roots.push_back(ro);
        }
        return true;
    }
    if (kind == "system") {
        out.kind = SolveResult::NonlinearSystem;
        // 常量与派生量记录
        for (const auto &r : recs) {
            if (r.key == "CONST" && !r.f.empty()) out.constantsList = r.f[0];
        }
        std::vector<std::vector<std::pair<std::string, std::string>>> deriv;
        for (const auto &r : recs) {
            if (r.key != "DERIVE" || r.f.size() < 4) continue;
            int didx = std::atoi(r.f[0].c_str());
            if (didx < 1) continue;
            if (static_cast<std::size_t>(didx) > deriv.size()) deriv.resize(didx);
            // 记录格式: DERIVE <解序号> <表达式> <纯文本值> <LaTeX值>
            deriv[didx - 1].push_back({r.f[1] + " = " + r.f[2], r.f[1] + " = " + r.f[3]});
        }
        // 分组与比例记录: GRP <解序号> <说明文本> / RATIO <解序号> <变量列表> <纯文本> <LaTeX>
        std::vector<std::vector<std::string>> grp;
        std::vector<std::vector<std::pair<std::string, std::pair<std::string, std::string>>>> rat;
        std::vector<std::vector<bool>> ratNum;
        for (const auto &r : recs) {
            if (r.key == "GRP" && r.f.size() >= 2) {
                int gi = std::atoi(r.f[0].c_str());
                if (gi < 1) continue;
                if (static_cast<std::size_t>(gi) > grp.size()) grp.resize(gi);
                grp[gi - 1].push_back(r.f[1]);
            } else if (r.key == "RATIO" && r.f.size() >= 4) {
                int ri = std::atoi(r.f[0].c_str());
                if (ri < 1) continue;
                if (static_cast<std::size_t>(ri) > rat.size()) { rat.resize(ri); ratNum.resize(ri); }
                if (ratNum.size() < rat.size()) ratNum.resize(rat.size());
                rat[ri - 1].push_back({r.f[1], {r.f[2], r.f[3]}});
                ratNum[ri - 1].push_back(r.f.size() >= 5 && r.f[4] == "1");
            }
        }
        // 约束记录: COND <解序号> <纯文本> <LaTeX>
        std::vector<std::pair<std::string, std::string>> conds;
        for (const auto &r : recs) {
            if (r.key != "COND" || r.f.size() < 3) continue;
            int cidx = std::atoi(r.f[0].c_str());
            if (cidx < 1) continue;
            if (static_cast<int>(conds.size()) < cidx) conds.resize(cidx);
            conds[cidx - 1] = {r.f[1], r.f[2]};
        }
        // 按变量重复分组, 每组是一个解
        // 每个解: {纯文本, LaTeX, 数值近似}
        struct SysPiece { std::string plain, latex, approx; };
        std::vector<SysPiece> cur;
        std::set<std::string> seen;
        std::vector<std::string> freeVars;
        auto isPlainNumber = [](const std::string &t) {
            if (t.empty()) return false;
            for (char ch : t)
                if (!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' ||
                      ch == '+' || ch == '/' || ch == ' ' || ch == 'i' || ch == 'I'))
                    return false;
            return true;
        };
        auto flush = [&]() {
            if (cur.empty()) return;
            std::string p, l, a;
            for (const auto &kv : cur) {
                if (!p.empty()) { p += ", "; l += ",\\ "; }
                p += kv.plain;
                l += kv.latex;
                if (!kv.approx.empty()) { // 只收需要近似的变量, 纯数字解不留空壳
                    if (!a.empty()) a += ", ";
                    a += kv.approx;
                }
            }
            out.solutionPlain.push_back(p);
            out.solutionLatex.push_back(l);
            out.solutionApprox.push_back(a);
            cur.clear();
            seen.clear();
        };
        for (const auto &r : recs) {
            if (r.key != "SYS" || r.f.size() < 4) continue;
            if (seen.count(r.f[0])) flush();
            seen.insert(r.f[0]);
            bool isFree = (r.f[3] == "1");
            if (isFree) {
                if (std::find(freeVars.begin(), freeVars.end(), r.f[0]) == freeVars.end())
                    freeVars.push_back(r.f[0]);
                continue;
            }
            // 近似值: 只有"不是一眼能看懂的数"才附上(与单变量路径同规则)
            std::string approx;
            if (r.f.size() >= 5 && !r.f[4].empty()) {
                std::string valTxt = r.f[1];
                std::size_t eq = valTxt.find('=');
                if (eq != std::string::npos) valTxt = valTxt.substr(eq + 1);
                std::size_t nb = valTxt.find_first_not_of(" \t");
                valTxt = (nb == std::string::npos) ? std::string() : valTxt.substr(nb);
                if (!isPlainNumber(valTxt)) approx = r.f[0] + " \u2248 " + r.f[4];
            }
            cur.push_back({r.f[1], r.f[2], approx});
        }
        flush();
        if (!freeVars.empty()) {
            out.infinite = true;
            out.params = freeVars;
        }
        out.hasRelational = !rels.empty();
        out.groupLines.assign(out.solutionPlain.size(), {});
        out.ratioLabels.assign(out.solutionPlain.size(), {});
        out.ratioPlain.assign(out.solutionPlain.size(), {});
        out.ratioLatex.assign(out.solutionPlain.size(), {});
        out.ratioNumeric.assign(out.solutionPlain.size(), {});
        for (std::size_t i = 0; i < grp.size() && i < out.solutionPlain.size(); ++i) {
            out.groupLines[i] = grp[i];
        }
        for (std::size_t i = 0; i < rat.size() && i < out.solutionPlain.size(); ++i) {
            for (std::size_t k = 0; k < rat[i].size(); ++k) {
                out.ratioLabels[i].push_back(rat[i][k].first);
                out.ratioPlain[i].push_back(rat[i][k].second.first);
                out.ratioLatex[i].push_back(rat[i][k].second.second);
                out.ratioNumeric[i].push_back(i < ratNum.size() && k < ratNum[i].size() && ratNum[i][k]);
            }
        }
        out.derivedPlain.assign(out.solutionPlain.size(), {});
        out.derivedLatex.assign(out.solutionPlain.size(), {});
        for (std::size_t i = 0; i < deriv.size() && i < out.solutionPlain.size(); ++i) {
            for (const auto &kv : deriv[i]) {
                out.derivedPlain[i].push_back(kv.first);
                out.derivedLatex[i].push_back(kv.second);
            }
        }
        out.solutionCondPlain.assign(out.solutionPlain.size(), std::string());
        out.solutionCondLatex.assign(out.solutionPlain.size(), std::string());
        for (std::size_t i = 0; i < conds.size() && i < out.solutionPlain.size(); ++i) {
            out.solutionCondPlain[i] = conds[i].first;
            out.solutionCondLatex[i] = conds[i].second;
        }
        if (out.solutionPlain.empty() && !out.infinite) out.none = true;
        return true;
    }
    err = L("外部引擎返回了未知结果", "unknown result from external engine");
    return false;
}

bool evalWithSympy(const std::vector<std::string> &exprSyms, int digits, const std::string &python,
                   int timeoutMs, std::vector<std::string> &vals, std::string &err, SciMode sci,
                   int sciThreshold) {
    if (exprSyms.empty()) return false;
    std::vector<std::string> args;
    args.push_back("eval");
    args.push_back("--digits=" + std::to_string(digits));
    args.push_back(std::string("--sci=") + sciName(sci));
    args.push_back("--scithr=" + std::to_string(sciThreshold));
    for (const auto &e : exprSyms) args.push_back(e);
    std::string data;
    if (!runPython(python, args, sympyEngineScript(), timeoutMs, data, err)) return false;
    auto recs = parseRecords(data);
    bool ok = false;
    for (const auto &r : recs) {
        if (r.key == "STATUS") ok = (!r.f.empty() && r.f[0] == "ok");
        else if (r.key == "VAL" && !r.f.empty()) vals.push_back(r.f[0]);
        else if (r.key == "ERROR" && !r.f.empty()) err = r.f[0];
    }
    return ok && !vals.empty();
}

bool evalExactAndNumericWithSympy(const std::vector<std::string> &exprSyms, int digits,
                                  const std::string &python, int timeoutMs,
                                  std::vector<std::string> &exactPlain,
                                  std::vector<std::string> &exactLatex,
                                  std::vector<std::string> &vals, std::string &err, SciMode sci,
                                  int sciThreshold) {
    if (exprSyms.empty()) return false;
    std::vector<std::string> args;
    args.push_back("eval");
    args.push_back("--digits=" + std::to_string(digits));
    args.push_back(std::string("--sci=") + sciName(sci));
    args.push_back("--scithr=" + std::to_string(sciThreshold));
    for (const auto &e : exprSyms) args.push_back(e);
    std::string data;
    if (!runPython(python, args, sympyEngineScript(), timeoutMs, data, err)) return false;
    auto recs = parseRecords(data);
    bool ok = false;
    for (const auto &r : recs) {
        if (r.key == "STATUS") ok = (!r.f.empty() && r.f[0] == "ok");
        else if (r.key == "EXACT" && r.f.size() >= 2) {
            exactPlain.push_back(r.f[0]);
            exactLatex.push_back(r.f[1]);
        } else if (r.key == "VAL" && !r.f.empty()) vals.push_back(r.f[0]);
        else if (r.key == "ERROR" && !r.f.empty()) err = r.f[0];
    }
    return ok;
}

bool factorWithSympy(const std::string &polySympy, const std::string &var, const std::string &python,
                     int timeoutMs, std::string &plain, std::string &latex, std::string &err) {
    std::vector<std::string> args{"factor", polySympy, var};
    std::string data;
    if (!runPython(python, args, sympyEngineScript(), timeoutMs, data, err)) return false;
    auto recs = parseRecords(data);
    for (const auto &r : recs) {
        if (r.key == "FACTOR" && r.f.size() >= 2) {
            plain = r.f[0];
            latex = r.f[1];
            return true;
        }
        if (r.key == "ERROR" && !r.f.empty()) err = r.f[0];
    }
    return false;
}

} // namespace em

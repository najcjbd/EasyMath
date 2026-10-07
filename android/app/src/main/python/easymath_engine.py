# 自动生成, 请勿手改: 来源 src/engine.cpp 内嵌桥脚本 (sha256:4f397127dc77c707)

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



if __name__ == "__main__":
    main()


def run_main(joined_args):
    """Chaquopy 入口: 参数用 \x1f 连接, 返回捕获的输出文本"""
    import io
    import contextlib
    argv = [a for a in joined_args.split("\x1f") if a != ""]
    buf = io.StringIO()
    old = sys.argv
    sys.argv = ["-"] + argv
    try:
        with contextlib.redirect_stdout(buf):
            main()
    finally:
        sys.argv = old
    return buf.getvalue()

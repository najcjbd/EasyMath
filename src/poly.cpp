// EasyMath - 有理系数多项式实现
#include "poly.hpp"

#include "interrupt.hpp"

#include <algorithm>
#include <cmath>

namespace em {

void Poly::trim() {
    while (!c.empty() && c.back().isZero()) c.pop_back();
}

void Poly::trimZero() { trim(); }

Rational Poly::eval(const Rational &x) const {
    Rational r(0);
    for (std::size_t i = c.size(); i-- > 0;) r = r * x + c[i];
    return r;
}

long double Poly::evalApprox(long double x) const {
    long double r = 0;
    for (std::size_t i = c.size(); i-- > 0;) r = r * x + c[i].toLongDouble();
    return r;
}

std::vector<Rational> Poly::evalAll(const std::vector<Rational> &xs) const {
    std::vector<Rational> out;
    out.reserve(xs.size());
    for (const auto &x : xs) out.push_back(eval(x));
    return out;
}

Poly Poly::operator+(const Poly &o) const {
    std::vector<Rational> r(std::max(c.size(), o.c.size()), Rational(0));
    for (std::size_t i = 0; i < r.size(); ++i) r[i] = coeff(static_cast<int>(i)) + o.coeff(static_cast<int>(i));
    return Poly(r);
}

Poly Poly::operator-(const Poly &o) const {
    std::vector<Rational> r(std::max(c.size(), o.c.size()), Rational(0));
    for (std::size_t i = 0; i < r.size(); ++i) r[i] = coeff(static_cast<int>(i)) - o.coeff(static_cast<int>(i));
    return Poly(r);
}

Poly Poly::operator*(const Poly &o) const {
    if (isZero() || o.isZero()) return Poly();
    std::vector<Rational> r(c.size() + o.c.size() - 1, Rational(0));
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (c[i].isZero()) continue;
        for (std::size_t j = 0; j < o.c.size(); ++j) r[i + j] += c[i] * o.c[j];
    }
    return Poly(r);
}

Poly Poly::operator-() const { return scale(Rational(-1)); }

Poly Poly::scale(const Rational &k) const {
    if (k.isZero()) return Poly();
    std::vector<Rational> r = c;
    for (auto &v : r) v = v * k;
    return Poly(r);
}

Poly Poly::pow(int e) const {
    Poly result(std::vector<Rational>{Rational(1)});
    Poly base = *this;
    int n = e;
    while (n > 0) {
        if (n & 1) result = result * base;
        n >>= 1;
        if (n) base = base * base;
    }
    return result;
}

void Poly::divmod(const Poly &b, Poly &q, Poly &r) const {
    if (b.isZero()) {
        q = Poly();
        r = Poly();
        return;
    }
    r = *this;
    q = Poly();
    int db = b.degree();
    Rational lb = b.leading();
    while (!r.isZero() && r.degree() >= db) {
        int dq = r.degree() - db;
        Rational factor = r.leading() / lb;
        std::vector<Rational> qt(static_cast<std::size_t>(dq) + 1, Rational(0));
        qt[static_cast<std::size_t>(dq)] = factor;
        Poly t(qt);
        q = q + t;
        r = r - t * b;
    }
    q.trim();
    r.trim();
}

Poly Poly::derivative() const {
    if (c.size() <= 1) return Poly();
    std::vector<Rational> r(c.size() - 1);
    for (std::size_t i = 1; i < c.size(); ++i) r[i - 1] = c[i] * Rational(static_cast<long long>(i));
    return Poly(r);
}

Poly Poly::monic() const {
    if (isZero()) return Poly();
    return scale(leading().reciprocal());
}

Poly Poly::multiplyLinear(const Rational &root) const {
    Poly lin(std::vector<Rational>{-root, Rational(1)}); // x - root
    return (*this) * lin;
}

Poly Poly::compose(const Poly &inner) const {
    Poly r;
    for (std::size_t i = c.size(); i-- > 0;) r = r * inner + Poly(c[i]);
    return r;
}

Poly Poly::toIntegerCoefficients(BigInt &content) const {
    BigInt l = BigInt(1);
    for (const auto &v : c) l = BigInt::lcm(l, v.den());
    std::vector<Rational> r;
    r.reserve(c.size());
    BigInt g(0);
    for (const auto &v : c) {
        Rational t = v * Rational(l);
        r.push_back(t);
        g = BigInt::gcd(g, t.num());
    }
    if (g.isZero()) g = BigInt(1);
    content = g;
    for (auto &v : r) v = v / Rational(g);
    return Poly(r);
}

// ============================ 打印 ============================

std::string superscriptNumber(long long n) {
    static const char *sup[] = {"\u2070", "\u00b9", "\u00b2", "\u00b3", "\u2074",
                                "\u2075", "\u2076", "\u2077", "\u2078", "\u2079"};
    if (n == 0) return sup[0];
    bool neg = n < 0;
    unsigned long long v = neg ? static_cast<unsigned long long>(-n) : static_cast<unsigned long long>(n);
    std::string s;
    while (v) {
        s = std::string(sup[v % 10]) + s;
        v /= 10;
    }
    if (neg) s = "\u207b" + s;
    return s;
}

static std::string coefTimesVar(const Rational &coef, int deg, const std::string &var,
                                const std::string &vpow) {
    // 返回 "3/4x^2" 这种形式(不含符号)
    bool unit = (coef.isOne());
    bool negUnit = (coef == Rational(-1));
    std::string vs = (deg == 0) ? "" : ((deg == 1) ? var : var + vpow);
    if (deg == 0) return coef.abs().str();
    if (unit) return vs;
    if (negUnit) return vs;
    return coef.abs().str() + vs;
}

static std::string coefTimesVarLatex(const Rational &coef, int deg, const std::string &var) {
    if (deg == 0) return coef.abs().latex();
    std::string vs = (deg == 1) ? var : var + "^{" + std::to_string(deg) + "}";
    if (coef.isOne() || coef == Rational(-1)) return vs;
    return coef.abs().latex() + vs;
}

std::string Poly::toPlain(const std::string &var) const {
    if (isZero()) return "0";
    std::string out;
    for (int i = degree(); i >= 0; --i) {
        const Rational &a = c[static_cast<std::size_t>(i)];
        if (a.isZero()) continue;
        bool first = out.empty();
        std::string term = coefTimesVar(a, i, var, "^" + std::to_string(i));
        if (first) out += (a.isNeg() ? "-" : "") + term;
        else out += (a.isNeg() ? " - " : " + ") + term;
    }
    return out;
}

std::string Poly::toPretty(const std::string &var) const {
    if (isZero()) return "0";
    std::string out;
    for (int i = degree(); i >= 0; --i) {
        const Rational &a = c[static_cast<std::size_t>(i)];
        if (a.isZero()) continue;
        bool first = out.empty();
        std::string term = coefTimesVar(a, i, var, superscriptNumber(i));
        if (first) out += (a.isNeg() ? "-" : "") + term;
        else out += (a.isNeg() ? " - " : " + ") + term;
    }
    return out;
}

std::string Poly::toLatex(const std::string &var) const {
    if (isZero()) return "0";
    std::string out;
    for (int i = degree(); i >= 0; --i) {
        const Rational &a = c[static_cast<std::size_t>(i)];
        if (a.isZero()) continue;
        bool first = out.empty();
        std::string term = coefTimesVarLatex(a, i, var);
        if (first) out += (a.isNeg() ? "-" : "") + term;
        else out += (a.isNeg() ? " - " : " + ") + term;
    }
    return out;
}

std::string Poly::toSympy(const std::string &var) const {
    if (isZero()) return "0";
    auto term = [&](const Rational &a, int i, bool first) {
        std::string coef;
        if (a.isInteger()) coef = a.num().str();
        else coef = "Rational(" + a.num().str() + ", " + a.den().str() + ")";
        std::string body;
        if (i == 0) {
            body = coef;
        } else {
            std::string pw = (i == 1) ? var : var + "**" + std::to_string(i);
            bool unit = a.abs() == Rational(1);
            body = unit ? pw : ("(" + coef + ")*" + pw);
        }
        if (first) return (a.isNeg() && i == 0) ? body : ((a.isNeg() && i > 0) ? "-" + body : body);
        return (a.isNeg() ? " - " : " + ") + body;
    };
    std::string out;
    for (int i = degree(); i >= 0; --i) {
        if (c[static_cast<std::size_t>(i)].isZero()) continue;
        std::string raw = term(c[static_cast<std::size_t>(i)], i, out.empty());
        if (out.empty() && !raw.empty() && raw[0] == '-') out += raw;
        else if (out.empty()) out += raw;
        else if (raw.size() >= 3 && raw.compare(0, 3, " - ") == 0) out += raw;
        else out += " + " + raw;
    }
    return out;
}

std::string polyTermPlain(const Rational &coef, int deg, const std::string &var, bool first) {
    std::string term = coefTimesVar(coef, deg, var, "^" + std::to_string(deg));
    if (first) return (coef.isNeg() ? "-" : "") + term;
    return (coef.isNeg() ? " - " : " + ") + term;
}

// ============================ AST -> Poly ============================

bool astToPoly(const NodePtr &n, const std::string &var, Poly &out, std::string &err) {
    switch (n->t) {
        case NT::Num:
            out = Poly(n->num);
            return true;
        case NT::Var: {
            bool ok = false;
            constantValue(n->name, ok);
            if (n->name == var) {
                out = Poly::X();
                return true;
            }
            if (ok) {
                err = "常量 " + n->name + " 使方程无法化为多项式";
                return false;
            }
            err = "出现了额外未知量 " + n->name + " (单变量模式只允许 " + var + ")";
            return false;
        }
        case NT::Neg: {
            Poly a;
            if (!astToPoly(n->kids[0], var, a, err)) return false;
            out = -a;
            return true;
        }
        case NT::Add:
        case NT::Sub: {
            Poly a, b;
            if (!astToPoly(n->kids[0], var, a, err)) return false;
            if (!astToPoly(n->kids[1], var, b, err)) return false;
            out = (n->t == NT::Add) ? a + b : a - b;
            return true;
        }
        case NT::Mul: {
            Poly a, b;
            if (!astToPoly(n->kids[0], var, a, err)) return false;
            if (!astToPoly(n->kids[1], var, b, err)) return false;
            out = a * b;
            return true;
        }
        case NT::Div: {
            Poly a, b;
            if (!astToPoly(n->kids[0], var, a, err)) return false;
            if (!astToPoly(n->kids[1], var, b, err)) return false;
            if (b.isZero()) {
                err = "除以零";
                return false;
            }
            if (b.degree() > 0) {
                err = "分母含未知量, 不是多项式方程";
                return false;
            }
            out = a.scale(b.constantTerm().reciprocal());
            return true;
        }
        case NT::Pow: {
            Poly a, b;
            if (!astToPoly(n->kids[0], var, a, err)) return false;
            if (!astToPoly(n->kids[1], var, b, err)) return false;
            if (b.degree() > 0) {
                err = "指数含未知量, 不是多项式方程";
                return false;
            }
            Rational e = b.constantTerm();
            if (!e.isInteger()) {
                err = "非整数指数, 不是多项式方程";
                return false;
            }
            long long ei = 0;
            if (!e.num().fitsLongLong(ei)) {
                err = "指数过大";
                return false;
            }
            if (ei < 0) {
                if (a.degree() > 0) {
                    err = "未知量的负指数, 不是多项式方程";
                    return false;
                }
                if (a.isZero()) {
                    err = "0 的负数次幂";
                    return false;
                }
                out = Poly(a.constantTerm().pow(ei));
                return true;
            }
            if (ei > 1000) {
                err = "指数过大";
                return false;
            }
            out = a.pow(static_cast<int>(ei));
            return true;
        }
        case NT::Percent: {
            Poly a;
            if (!astToPoly(n->kids[0], var, a, err)) return false;
            out = a.scale(Rational(1, 100));
            return true;
        }
        case NT::Fact: {
            Rational v;
            std::string e;
            Surd s;
            if (!evalExact(n->kids[0], {}, s, e) || !s.isRational()) {
                err = "阶乘的参数必须是常数";
                return false;
            }
            v = s.coef;
            long long k = 0;
            if (!v.isInteger() || !v.num().fitsLongLong(k) || k < 0 || k > 5000) {
                err = "阶乘的参数必须是非负整数";
                return false;
            }
            BigInt f;
            if (!factorialExact(k, f)) {
                err = "阶乘过大";
                return false;
            }
            out = Poly(Rational(f));
            return true;
        }
        case NT::Call: {
            // 允许 sqrt(4) 这样的常数调用
            Surd s;
            std::string e;
            if (evalExact(n, {}, s, e) && s.isRational()) {
                out = Poly(s.coef);
                return true;
            }
            err = "含函数 " + n->name + " 的方程不是多项式方程";
            return false;
        }
    }
    err = "无法转换为多项式";
    return false;
}

// ============================ 拉格朗日插值 ============================

bool lagrangePolynomial(const std::vector<std::pair<Rational, Rational>> &pts, Poly &out,
                        std::string &err) {
    if (pts.empty()) {
        err = "没有数据点";
        return false;
    }
    std::size_t nPts = pts.size();
    out = Poly();
    for (std::size_t i = 0; i < nPts; ++i) {
        // 点数很多时这里是 O(n^3)(每个 i 都要乘 n 次线性因子), 必须可打断
        if (interruptRequested()) {
            err = "计算已被中断";
            return false;
        }
        Rational denom(1);
        Poly basis(std::vector<Rational>{Rational(1)});
        for (std::size_t j = 0; j < nPts; ++j) {
            if (i == j) continue;
            // 内层也查(比外层细得多): 系数会随点数增长到上万位,
            // 单次有理数运算本身无法打断, 只能把检查间隔压到最小
            if ((j & 0xF) == 0 && interruptRequested()) {
                err = "计算已被中断";
                return false;
            }
            denom = denom * (pts[i].first - pts[j].first);
            basis = basis.multiplyLinear(pts[j].first);
        }
        if (denom.isZero()) {
            err = "存在相同的 x 值, 无法唯一确定函数";
            return false;
        }
        out = out + basis.scale(pts[i].second / denom);
    }
    return true;
}

} // namespace em

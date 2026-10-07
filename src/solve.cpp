// EasyMath - 方程求解实现
#include "solve.hpp"

#include "hpnum.hpp"
#include "interrupt.hpp"
#include "i18n.hpp"
#include "unicode.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace em {

// ============================ AlgNum ============================

AlgNum AlgNum::fromRational(const Rational &r) {
    AlgNum a;
    a.a = r;
    a.b = Surd(Rational(0));
    a.approx = std::complex<long double>(r.toLongDouble(), 0);
    return a;
}

AlgNum AlgNum::fromSurd(const Surd &s) {
    AlgNum a;
    a.a = Rational(0);
    a.b = s;
    a.approx = std::complex<long double>(s.approx(), 0);
    if (s.isRational()) {
        a.a = s.coef;
        a.b = Surd(Rational(0));
    }
    return a;
}

static std::string imagFactor(const Surd &s) {
    if (s.coef.isZero()) return "";
    std::string body = surdPlainNice(s);
    bool neg = !body.empty() && body[0] == '-';
    std::string mag = neg ? body.substr(1) : body;
    if (mag == "1") mag.clear();
    std::string core = (mag.find('/') != std::string::npos) ? ("(" + mag + ")") : mag;
    return (neg ? "-" : "") + core + "i";
}

std::string algNumPlain(const AlgNum &v) {
    std::string real;
    if (!v.b.coef.isZero()) {
        if (v.a.isZero()) real = surdPlainNice(v.b);
        else real = v.a.str() + (v.b.coef.isNeg() ? " - " + surdPlainNice(-v.b) : " + " + surdPlainNice(v.b));
    } else {
        real = v.a.str();
    }
    if (v.bi_.coef.isZero()) return real;
    std::string im = imagFactor(v.bi_);
    bool realZero = v.a.isZero() && v.b.coef.isZero();
    if (realZero) return im;
    if (!im.empty() && im[0] == '-') return real + " - " + im.substr(1);
    return real + " + " + im;
}

std::string algNumLatex(const AlgNum &v) {
    std::string real;
    if (!v.b.coef.isZero()) {
        if (v.a.isZero()) real = surdLatexNice(v.b);
        else
            real = v.a.latex() + (v.b.coef.isNeg() ? " - " + surdLatexNice(-v.b) : " + " + surdLatexNice(v.b));
    } else {
        real = v.a.latex();
    }
    if (v.bi_.coef.isZero()) return real;
    std::string body = surdLatexNice(v.bi_);
    bool neg = !body.empty() && body[0] == '-';
    std::string mag = neg ? body.substr(1) : body;
    std::string core;
    if (mag == "1") core = "i";
    else if (mag.rfind("\\frac", 0) == 0) core = "\\left(" + mag + "\\right)i";
    else core = mag + "i";
    if (v.a.isZero() && v.b.coef.isZero()) return (neg ? "-" : "") + core;
    if (neg) return real + " - " + core;
    return real + " + " + core;
}

std::string complexPlain(const std::complex<long double> &z, const NumberFormat &nf) {
    long double re = z.real(), im = z.imag();
    long double scale = std::max(1.0L, std::max(std::fabs(re), std::fabs(im)));
    if (std::fabs(im) <= 1e-12L * scale) return formatNumber(re, nf);
    std::string rs = formatNumber(re, nf);
    NumberFormat an = nf;
    an.trimZeros = true;
    std::string is = formatNumber(std::fabs(im), an);
    if (std::fabs(re) <= 1e-12L * scale) return (im < 0 ? "-" : "") + is + "i";
    return rs + (im < 0 ? " - " : " + ") + is + "i";
}

std::string complexLatex(const std::complex<long double> &z, const NumberFormat &nf) {
    return complexPlain(z, nf);
}

std::string complexPlain(const std::complex<long double> &z, int decimals) {
    NumberFormat nf;
    nf.digits = decimals;
    nf.sci = SciMode::Never;
    return complexPlain(z, nf);
}

std::string complexLatex(const std::complex<long double> &z, int decimals) {
    return complexPlain(z, decimals);
}

// ============================ 输入切分 ============================

std::vector<std::string> splitItems(const std::string &s, char32_t extraSep, bool splitOnSpace) {
    std::vector<std::string> out;
    auto cps = utf8_decode(s);
    std::string cur;
    int depth = 0;
    // '|' 既是文档里的默认分隔符, 又是绝对值的定界符:
    // 从左到右两两配对 —— 成对的 | 当"绝对值内容"(不拆), 落单的仍当分隔符。
    // 例: |-3| 是一个整体; |x|=1|y=2 拆成 "|x|=1" 与 "y=2"; x=1|y=2 拆成两项。
    std::vector<char> barPaired(cps.size(), 0);
    {
        std::size_t openBar = std::string::npos;
        for (std::size_t i = 0; i < cps.size(); ++i) {
            if (cps[i] != '|') continue;
            if (openBar == std::string::npos) openBar = i;
            else {
                barPaired[openBar] = 1;
                barPaired[i] = 1;
                openBar = std::string::npos;
            }
        }
    }
    for (std::size_t i = 0; i < cps.size(); ++i) {
        cp_t c = cps[i];
        if (c == '(' || c == '[' || c == '{') ++depth;
        if (c == ')' || c == ']' || c == '}') --depth;
        // 行首(或紧跟分隔符)的落单 | 更可能是"要写绝对值但没配尾":
        // 这种情况下把它当开括号(让解析器报"没配对"), 而不是当分隔符静默吃掉。
        if (c == '|' && !barPaired[i] && cur.find_first_not_of(" \t") == std::string::npos &&
            depth <= 0)
            barPaired[i] = 1;
        bool sep = false;
        if (depth <= 0) {
            cp_t prev = (i > 0) ? cps[i - 1] : 0;
            cp_t next = (i + 1 < cps.size()) ? cps[i + 1] : 0;
            if (is_space_cp(c)) sep = splitOnSpace;
            else if (extraSep != 0 && c == extraSep) sep = true;
            else if (c == '|' && barPaired[i]) sep = false;  // 成对的竖线: 绝对值
            else if (is_default_separator(c, prev, next)) sep = true;
        }
        if (sep) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur += utf8_encode(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ============================ Node 折叠工具 ============================

namespace {

bool asConst(const NodePtr &n, Rational &r) {
    if (!n) return false;
    if (n->t == NT::Num) {
        r = n->num;
        return true;
    }
    Surd s;
    std::string e;
    if (evalExact(n, {}, s, e) && s.isRational()) {
        r = s.coef;
        return true;
    }
    return false;
}

NodePtr mkNum(const Rational &r) { return Node::num_(r); }

NodePtr mkNeg(const NodePtr &a) {
    Rational r;
    if (asConst(a, r)) return mkNum(-r);
    if (a->t == NT::Neg) return a->kids[0];
    if (a->t == NT::Sub) return Node::op(NT::Sub, a->kids[1], a->kids[0]);
    return Node::neg(a);
}

NodePtr mkAdd(const NodePtr &a, const NodePtr &b) {
    Rational x, y;
    if (asConst(a, x) && asConst(b, y)) return mkNum(x + y);
    if (asConst(a, x) && x.isZero()) return b;
    if (asConst(b, y) && y.isZero()) return a;
    return Node::op(NT::Add, a, b);
}

NodePtr mkSub(const NodePtr &a, const NodePtr &b) {
    Rational x, y;
    if (asConst(a, x) && asConst(b, y)) return mkNum(x - y);
    if (asConst(b, y) && y.isZero()) return a;
    if (asConst(a, x) && x.isZero()) return mkNeg(b);
    return Node::op(NT::Sub, a, b);
}

NodePtr mkMul(const NodePtr &a, const NodePtr &b) {
    Rational x, y;
    if (asConst(a, x) && asConst(b, y)) return mkNum(x * y);
    if (asConst(a, x)) {
        if (x.isZero()) return mkNum(Rational(0));
        if (x.isOne()) return b;
    }
    if (asConst(b, y)) {
        if (y.isZero()) return mkNum(Rational(0));
        if (y.isOne()) return a;
    }
    return Node::op(NT::Mul, a, b);
}

NodePtr mkDiv(const NodePtr &a, const NodePtr &b) {
    Rational x, y;
    if (asConst(a, x) && asConst(b, y) && !y.isZero()) return mkNum(x / y);
    if (asConst(b, y) && y.isOne()) return a;
    return Node::op(NT::Div, a, b);
}

bool containsVar(const NodePtr &n, const std::string &v) { return collectVars(n).count(v) > 0; }

// 把 a + b 形式的代数数转回表达式 (用于回代)
NodePtr algNumToNode(const AlgNum &v) {
    NodePtr n = mkNum(v.a);
    if (!v.b.coef.isZero()) {
        NodePtr s = Node::call("sqrt", {mkNum(Rational(v.b.rad))});
        n = mkAdd(n, mkMul(mkNum(v.b.coef), s));
    }
    return n;
}

// f = A*v + B, A 与 B 都不含 v
bool linearCoeffs(const NodePtr &n, const std::string &v, NodePtr &A, NodePtr &B) {
    switch (n->t) {
        case NT::Num:
            A = mkNum(Rational(0));
            B = n;
            return true;
        case NT::Var:
            if (n->name == v) {
                A = mkNum(Rational(1));
                B = mkNum(Rational(0));
            } else {
                A = mkNum(Rational(0));
                B = n;
            }
            return true;
        case NT::Neg: {
            NodePtr a, b;
            if (!linearCoeffs(n->kids[0], v, a, b)) return false;
            A = mkNeg(a);
            B = mkNeg(b);
            return true;
        }
        case NT::Add:
        case NT::Sub: {
            NodePtr a1, b1, a2, b2;
            if (!linearCoeffs(n->kids[0], v, a1, b1)) return false;
            if (!linearCoeffs(n->kids[1], v, a2, b2)) return false;
            A = (n->t == NT::Add) ? mkAdd(a1, a2) : mkSub(a1, a2);
            B = (n->t == NT::Add) ? mkAdd(b1, b2) : mkSub(b1, b2);
            return true;
        }
        case NT::Mul: {
            NodePtr a1, b1, a2, b2;
            if (!linearCoeffs(n->kids[0], v, a1, b1)) return false;
            if (!linearCoeffs(n->kids[1], v, a2, b2)) return false;
            Rational ca, cb;
            bool za = asConst(a1, ca) && ca.isZero();
            bool zb = asConst(a2, cb) && cb.isZero();
            if (!za && !zb) return false; // v*v 型
            if (zb) {
                A = mkMul(a1, b2);
                B = mkMul(b1, b2);
            } else {
                A = mkMul(a2, b1);
                B = mkMul(b2, b1);
            }
            return true;
        }
        case NT::Div: {
            NodePtr a1, b1, a2, b2;
            if (!linearCoeffs(n->kids[0], v, a1, b1)) return false;
            if (!linearCoeffs(n->kids[1], v, a2, b2)) return false;
            Rational cb;
            if (!asConst(a2, cb) || !cb.isZero()) return false;
            A = mkDiv(a1, b2);
            B = mkDiv(b1, b2);
            return true;
        }
        case NT::Pow: {
            if (!containsVar(n, v)) {
                A = mkNum(Rational(0));
                B = n;
                return true;
            }
            NodePtr a1, b1;
            if (!linearCoeffs(n->kids[1], v, a1, b1)) return false;
            Rational e;
            if (!asConst(n->kids[1], e)) return false;
            if (e.isZero()) {
                A = mkNum(Rational(0));
                B = mkNum(Rational(1));
                return true;
            }
            if (!(e == Rational(1))) return false;
            return linearCoeffs(n->kids[0], v, A, B);
        }
        default:
            break;
    }
    // 其它情况: 只要不含 v 就当作常数
    if (!containsVar(n, v)) {
        A = mkNum(Rational(0));
        B = n;
        return true;
    }
    return false;
}

} // namespace

// ============================ 数值求根 ============================

static std::complex<long double> horner(const std::vector<std::complex<long double>> &a,
                                 const std::complex<long double> &z) {
    std::complex<long double> r(0, 0);
    for (std::size_t i = a.size(); i-- > 0;) r = r * z + a[i];
    return r;
}

std::vector<std::complex<long double>> durandKerner(const std::vector<std::complex<long double>> &coeffs,
                                                    int maxIter, long double tol) {
    int n = static_cast<int>(coeffs.size()) - 1;
    std::vector<std::complex<long double>> roots;
    if (n <= 0) return roots;
    std::complex<long double> lead = coeffs.back();
    if (std::abs(lead) == 0) return roots;
    std::vector<std::complex<long double>> a(coeffs.size());
    for (std::size_t i = 0; i < coeffs.size(); ++i) a[i] = coeffs[i] / lead;
    long double radius = 0;
    for (int i = 0; i < n; ++i) radius = std::max(radius, std::abs(a[i]));
    radius = 1.0L + radius;
    const long double PI = 3.141592653589793238462643383279502884197L;
    roots.resize(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k)
        roots[static_cast<std::size_t>(k)] =
            std::polar(radius, static_cast<long double>(2 * PI * k / n) + 0.4L);
    for (int iter = 0; iter < maxIter; ++iter) {
        if (interruptRequested()) break;
        long double maxDelta = 0;
        for (int i = 0; i < n; ++i) {
            std::complex<long double> num = horner(a, roots[static_cast<std::size_t>(i)]);
            std::complex<long double> den(1, 0);
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                den *= (roots[static_cast<std::size_t>(i)] - roots[static_cast<std::size_t>(j)]);
            }
            if (std::abs(den) < 1e-30L) continue;
            std::complex<long double> delta = num / den;
            roots[static_cast<std::size_t>(i)] -= delta;
            maxDelta = std::max(maxDelta, std::abs(delta));
        }
        if (maxDelta < tol) break;
    }
    // 牛顿抛光
    std::vector<std::complex<long double>> dd(coeffs.size() > 1 ? coeffs.size() - 1 : 0);
    for (std::size_t i = 1; i < coeffs.size(); ++i) dd[i - 1] = coeffs[i] * static_cast<long double>(i);
    if (!dd.empty()) {
        for (auto &r : roots) {
            if (interruptRequested()) break;
            for (int k = 0; k < 20; ++k) {
                std::complex<long double> f = horner(coeffs, r);
                std::complex<long double> d = horner(dd, r);
                if (std::abs(d) < 1e-30L) break;
                std::complex<long double> step = f / d;
                r -= step;
                if (std::abs(step) < 1e-24L) break;
            }
        }
    }
    return roots;
}

namespace {

bool snapComplex(const Poly &p, const std::vector<std::complex<long double>> &cf,
                 const std::complex<long double> &z, AlgNum &out, bool &isComplex) {
    long double scale = 1.0L;
    for (const auto &c : cf) scale = std::max(scale, std::abs(c));
    long double tolIm = 1e-9L * (1 + std::fabs(z.real()));
    if (std::fabs(z.imag()) <= tolIm) {
        // 实根: 用精确有理求值判定
        Rational re = Rational::fromDouble(z.real(), 1000000);
        if (p.eval(re).isZero()) {
            out = AlgNum::fromRational(re);
            isComplex = false;
            return true;
        }
        return false;
    }
    // 复根: 只接受分母较小的有理实部/虚部, 且残差极小
    Rational re = Rational::fromDouble(z.real(), 1000);
    Rational im = Rational::fromDouble(z.imag(), 1000);
    std::complex<long double> cand(re.toLongDouble(), im.toLongDouble());
    if (std::abs(horner(cf, cand)) < 1e-10L * scale) {
        out.a = re;
        out.b = Surd(Rational(0));
        out.bi_ = Surd(im);
        out.approx = cand;
        isComplex = true;
        return true;
    }
    return false;
}

// ============================ 多项式求根 ============================

std::vector<BigInt> smallDivisors(const BigInt &n) {
    std::vector<BigInt> out;
    BigInt m = n.abs();
    if (m.isZero()) return out;
    // 无 GMP 时因式分解枚举代价高; 有 GMP 可放宽到 30 位
    if (m.decimalDigits() > (BigInt::usesGmp() ? 30u : 12u)) return out;
    long long v = 0;
    if (!m.fitsLongLong(v)) return out;
    // 试除法是 O(sqrt(v)): 对 10^18 量级需要 10^9 次循环, 必须设上界
    if (v > 1000000000000LL) return out; // 1e12 -> 最多 1e6 次
    for (long long i = 1; i * i <= v; ++i) {
        if ((i & 0xFFFF) == 0 && interruptRequested()) break;
        if (v % i == 0) {
            out.push_back(BigInt(i));
            if (i != v / i) out.push_back(BigInt(v / i));
        }
    }
    return out;
}

static void pushRationalRoot(const Rational &r, int mult, std::vector<RootOut> &out,
                            const NumberFormat &nf = NumberFormat()) {
    RootOut ro;
    ro.exact = true;
    ro.isComplex = false;
    ro.mult = mult;
    ro.value = AlgNum::fromRational(r);
    ro.numeric = std::complex<long double>(r.toLongDouble(), 0);
    ro.plain = r.str();
    ro.latex = r.latex();
    ro.approxPlain = formatNumber(r.toLongDouble(), nf);
    ro.haveApprox = false; // 有理根不需要近似值
    out.push_back(ro);
}

static bool findRationalRoot(const Poly &p, Rational &root) {
    Poly ip = p;
    BigInt content;
    ip = ip.toIntegerCoefficients(content);
    if (ip.isZero()) return false;
    Rational a0 = ip.constantTerm();
    Rational an = ip.leading();
    if (an.isZero()) return false;
    if (a0.isZero()) {
        root = Rational(0);
        return true;
    }
    BigInt p0 = a0.num().abs(), q0 = an.num().abs();
    std::vector<BigInt> ps = smallDivisors(p0);
    std::vector<BigInt> qs = smallDivisors(q0);
    if (ps.empty() || qs.empty()) return false;
    for (const auto &pp : ps) {
        if (interruptRequested()) return false;
        for (const auto &qq : qs) {
            if (interruptRequested()) return false;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                Rational cand(sgn < 0 ? -pp : pp, qq);
                if (p.eval(cand).isZero()) {
                    root = cand;
                    return true;
                }
            }
        }
    }
    return false;
}

static void solveQuadratic(const Poly &p, std::vector<RootOut> &out, const SolveOptions &opt) {
    Rational a = p.coeff(2), b = p.coeff(1), c = p.coeff(0);
    Rational D = b * b - Rational(4) * a * c;
    Rational base = -b / (Rational(2) * a);
    if (D.isZero()) {
        pushRationalRoot(base, 2, out, opt.fmt);
        return;
    }
    if (D.isNeg()) {
        if (opt.realOnly) return;
        Surd s;
        Surd::sqrtOf(-D, s);
        AlgNum v;
        v.a = base;
        v.b = Surd(Rational(0));
        v.bi_ = Surd(s.coef / (Rational(2) * a), s.rad);
        v.approx = std::complex<long double>(base.toLongDouble(), v.bi_.approx());
        RootOut r1;
        r1.exact = true;
        r1.isComplex = true;
        r1.mult = 1;
        r1.value = v;
        r1.numeric = v.approx;
        r1.plain = algNumPlain(v);
        r1.latex = algNumLatex(v);
        r1.approxPlain = complexPlain(v.approx, opt.fmt);
        r1.haveApprox = true;
        out.push_back(r1);
        AlgNum v2 = v;
        v2.bi_ = -v.bi_;
        v2.approx = std::conj(v.approx);
        RootOut r2 = r1;
        r2.value = v2;
        r2.numeric = v2.approx;
        r2.plain = algNumPlain(v2);
        r2.latex = algNumLatex(v2);
        r2.approxPlain = complexPlain(v2.approx, opt.fmt);
        out.push_back(r2);
        return;
    }
    Surd s;
    Surd::sqrtOf(D, s);
    Rational coef = s.coef / (Rational(2) * a);
    AlgNum v1, v2;
    v1.a = base;
    v1.b = Surd(coef, s.rad);
    v2.a = base;
    v2.b = Surd(-coef, s.rad);
    if (v1.b.isRational()) {
        v1.a = base + v1.b.coef;
        v1.b = Surd(Rational(0));
    }
    if (v2.b.isRational()) {
        v2.a = base + v2.b.coef;
        v2.b = Surd(Rational(0));
    }
    v1.approx = std::complex<long double>(v1.a.toLongDouble() + v1.b.approx(), 0);
    v2.approx = std::complex<long double>(v2.a.toLongDouble() + v2.b.approx(), 0);
    for (const AlgNum &v : {v1, v2}) {
        RootOut r;
        r.exact = true;
        r.isComplex = false;
        r.mult = 1;
        r.value = v;
        r.numeric = v.approx;
        r.plain = algNumPlain(v);
        r.latex = algNumLatex(v);
        r.approxPlain = formatNumber(v.approx.real(), opt.fmt);
        r.haveApprox = !v.b.isZero(); // 无理根才附近似值
        out.push_back(r);
    }
}

static void addNumericRoots(const Poly &p, std::vector<RootOut> &out, const SolveOptions &opt) {
    std::vector<std::complex<long double>> cf;
    for (const auto &c : p.c) cf.push_back(std::complex<long double>(c.toLongDouble(), 0));
    if (cf.size() < 2) return;
    auto rs = durandKerner(cf);
    std::sort(rs.begin(), rs.end(), [](const std::complex<long double> &a,
                                       const std::complex<long double> &b) {
        if (std::fabs(a.real() - b.real()) > 1e-9L) return a.real() < b.real();
        return a.imag() < b.imag();
    });
    std::vector<int> used(rs.size(), 0);
    for (std::size_t i = 0; i < rs.size(); ++i) {
        if (used[i]) continue;
        std::vector<std::complex<long double>> cluster{rs[i]};
        used[i] = 1;
        for (std::size_t j = i + 1; j < rs.size(); ++j) {
            if (used[j]) continue;
            if (std::abs(rs[j] - rs[i]) < 1e-6L * (1 + std::abs(rs[i]))) {
                cluster.push_back(rs[j]);
                used[j] = 1;
            }
        }
        std::complex<long double> z(0, 0);
        for (auto &c : cluster) z += c;
        z /= static_cast<long double>(cluster.size());
        RootOut r;
        r.mult = static_cast<int>(cluster.size());
        r.numeric = z;
        AlgNum v;
        bool isComplex = false;
        if (snapComplex(p, cf, z, v, isComplex)) {
            r.exact = true;
            r.isComplex = isComplex;
            r.value = v;
            r.plain = algNumPlain(v);
            r.latex = algNumLatex(v);
            r.haveApprox = true;
        } else {
            r.exact = false;
            r.isComplex = std::fabs(z.imag()) > 1e-9L * (1 + std::fabs(z.real()));
            r.plain = complexPlain(z, opt.fmt);
            r.latex = complexLatex(z, opt.fmt);
        }
        r.approxPlain = complexPlain(z, opt.fmt);
        out.push_back(r);
    }
}

static void solvePolyRecursive(const Poly &p, std::vector<RootOut> &out, const SolveOptions &opt,
                               int depth) {
    if (p.isZero() || p.degree() <= 0 || depth > 64) return;
    if (p.degree() == 1) {
        Rational r = -p.coeff(0) / p.coeff(1);
        pushRationalRoot(r, 1, out, opt.fmt);
        return;
    }
    if (p.degree() == 2) {
        solveQuadratic(p, out, opt);
        return;
    }
    Rational r;
    if (findRationalRoot(p, r)) {
        Poly q = p;
        int mult = 0;
        for (;;) {
            Poly rem, quo;
            q.divmod(Poly(std::vector<Rational>{-r, Rational(1)}), quo, rem);
            if (!rem.isZero()) break;
            q = quo;
            ++mult;
        }
        pushRationalRoot(r, mult, out, opt.fmt);
        solvePolyRecursive(q, out, opt, depth + 1);
        return;
    }
    if (p.degree() > opt.maxNumericDegree) {
        if (opt.notes) {
            opt.notes->push_back("多项式次数 " + std::to_string(p.degree()) + " 超过数值求根上限 " +
                                 std::to_string(opt.maxNumericDegree) + ", 只给出了精确/有理根");
        }
        return;
    }
    std::size_t before = out.size();
    addNumericRoots(p, out, opt);
    if (opt.realOnly && out.size() > before) {
        std::vector<RootOut> keep(out.begin(), out.begin() + static_cast<long>(before));
        for (std::size_t i = before; i < out.size(); ++i)
            if (!out[i].isComplex) keep.push_back(out[i]);
        out = keep;
    }
}

} // namespace

std::vector<RootOut> rootsOfPolynomial(const Poly &pIn, const SolveOptions &opt) {
    std::vector<RootOut> out;
    Poly p = pIn;
    (void)0;
    while (!p.isZero() && p.coeff(0).isZero() && p.degree() > 0) {
        pushRationalRoot(Rational(0), 1, out, opt.fmt);
        std::vector<Rational> c(p.c.begin() + 1, p.c.end());
        p = Poly(c);
    }
    solvePolyRecursive(p, out, opt, 0);
    // 高精度精化(MPFR/MPC 牛顿法)
    if (opt.highPrecision && !out.empty()) {
        bool anyApprox = false;
        for (const auto &r : out)
            if (!r.exact) anyApprox = true;
        if (anyApprox) hpPolishRoots(p, out, opt.hpPrecBits, opt.decimals, opt.fmt.sci, opt.fmt.sciThreshold);
    }
    // 排序: 实根按大小, 复根在后
    std::stable_sort(out.begin(), out.end(), [](const RootOut &a, const RootOut &b) {
        if (a.isComplex != b.isComplex) return !a.isComplex;
        if (a.isComplex) {
            if (std::fabs(a.numeric.real() - b.numeric.real()) > 1e-9L)
                return a.numeric.real() < b.numeric.real();
            return a.numeric.imag() < b.numeric.imag();
        }
        return a.numeric.real() < b.numeric.real();
    });
    return out;
}

namespace {

// ============================ 多项式 gcd ============================

// 规模闸门触发时的建议: 按"SymPy 是否可用/是否已试过"给, 避免让用户白试一遍。
static std::string gcdSizeAdvice(const SolveOptions &opt) {
    if (!opt.sympyUsable)
        return L("; 本机没有可用的 SymPy(装 python3 + sympy 后可用 --engine sympy 处理这类输入)",
                 "; no usable SymPy here (install python3+sympy to use --engine sympy)");
    if (opt.sympyTriedAndFailed)
        return L("; SymPy 也已经试过但未能求解这个方程组, 请缩小系数规模或改写方程",
                 "; SymPy was already tried and could not solve it; reduce the coefficients");
    return L("; 可改用 --engine sympy(用 Python 的大整数, 不受这里的规模预算限制)",
             "; try --engine sympy (it uses Python big integers, not bound by this budget)");
}

// 精确求公因式的规模预算。
// 欧几里得算法每一步都会把系数"乘大", 最终系数位数大致是
// max(a系数位数*b次数, b系数位数*a次数)。超过预算就放弃:
// 否则会造出几百万位的整数, 单次 GMP 运算要几分钟, 期间 Ctrl+C 完全无响应
// (实测: gcd(x-999999999^999, x^1000-1) 会算出 999999999^999000, 约 900 万位)。
// 同样按后端缩放(见 expr.cpp 里 maxExactPowDigits 的说明)
static long long maxGcdDigits() {
    return std::strcmp(BigInt::backendName(), "GMP") == 0 ? 200000 : 30000;
}

static long long maxCoefDigits(const Poly &p) {
    long long d = 1;
    int deg = p.degree();
    for (int i = 0; i <= deg; ++i) {
        const Rational &c = p.coeff(i);
        long long cd = static_cast<long long>(c.num().decimalDigits());
        if (!c.isInteger()) cd += static_cast<long long>(c.den().decimalDigits());
        if (cd > d) d = cd;
    }
    return d;
}

static long long gcdDigitEstimate(const Poly &a, const Poly &b) {
    if (a.isZero() || b.isZero()) return 0;
    long long ad = maxCoefDigits(a), bd = maxCoefDigits(b);
    long long est1 = ad * static_cast<long long>(b.degree());
    long long est2 = bd * static_cast<long long>(a.degree());
    return est1 > est2 ? est1 : est2;
}

static Poly polyGcd(Poly a, Poly b, bool *tooBig = nullptr) {
    if (tooBig) *tooBig = false;
    if (gcdDigitEstimate(a, b) > maxGcdDigits()) {
        if (tooBig) *tooBig = true;
        return Poly(); // 调用方必须先检查 tooBig
    }
    while (!b.isZero()) {
        if (interruptRequested()) break;
        Poly q, r;
        a.divmod(b, q, r);
        a = b;
        b = r.isZero() ? r : r.monic();
        if (gcdDigitEstimate(a, b) > maxGcdDigits()) { // 中途膨胀也要收手
            if (tooBig) *tooBig = true;
            return Poly();
        }
    }
    return a.isZero() ? a : a.monic();
}

// ============================ 求解主流程 ============================

namespace {

struct EqInfo {
    std::string original;
    NodePtr lhs;
    NodePtr rhs;
    NodePtr f; // lhs - rhs
};

bool parseEq(const std::string &sRaw, const std::set<std::string> &declared, EqInfo &info,
             std::string &err) {
    std::string s = normalize_math(sRaw);
    std::string lhs, rhs;
    bool hasEq = splitEquation(s, lhs, rhs);
    NodePtr L, R;
    if (hasEq) {
        if (lhs.empty() || rhs.empty()) {
            err = "等式 '" + s + "' 缺少一边";
            return false;
        }
        L = parseExpression(lhs, ParseOptions{false, declared, true}, err);
        if (!L) return false;
        R = parseExpression(rhs, ParseOptions{false, declared, true}, err);
        if (!R) return false;
    } else {
        L = parseExpression(rhs, ParseOptions{false, declared, true}, err);
        if (!L) return false;
        R = Node::num_(Rational(0));
    }
    info.original = sRaw;
    info.lhs = L;
    info.rhs = R;
    info.f = mkSub(L, R);
    return true;
}

void solveLinearSystem(const std::vector<std::vector<Rational>> &A,
                       const std::vector<Rational> &b, std::vector<std::string> &plain,
                       std::vector<std::string> &latex, bool &infinite, bool &none,
                       std::vector<std::string> &params, std::vector<std::string> &vars) {
    std::size_t m = A.size();
    std::size_t n = vars.size();
    std::vector<std::vector<Rational>> M(m, std::vector<Rational>(n + 1));
    for (std::size_t i = 0; i < m; ++i) {
        for (std::size_t j = 0; j < n; ++j) M[i][j] = A[i][j];
        M[i][n] = b[i];
    }
    std::vector<int> pivotCol;
    std::size_t row = 0;
    for (std::size_t col = 0; col < n && row < m; ++col) {
        std::size_t sel = row;
        while (sel < m && M[sel][col].isZero()) ++sel;
        if (sel == m) continue;
        std::swap(M[sel], M[row]);
        Rational piv = M[row][col];
        for (std::size_t j = col; j <= n; ++j) M[row][j] = M[row][j] / piv;
        for (std::size_t i = 0; i < m; ++i) {
            if (i == row || M[i][col].isZero()) continue;
            Rational factor = M[i][col];
            for (std::size_t j = col; j <= n; ++j) M[i][j] = M[i][j] - factor * M[row][j];
        }
        pivotCol.push_back(static_cast<int>(col));
        ++row;
    }
    for (std::size_t i = row; i < m; ++i) {
        bool allZero = true;
        for (std::size_t j = 0; j < n; ++j)
            if (!M[i][j].isZero()) allZero = false;
        if (allZero && !M[i][n].isZero()) {
            none = true;
            return;
        }
    }
    std::set<int> pivots(pivotCol.begin(), pivotCol.end());
    std::vector<int> freeCols;
    for (std::size_t j = 0; j < n; ++j)
        if (!pivots.count(static_cast<int>(j))) freeCols.push_back(static_cast<int>(j));
    if (freeCols.empty()) {
        for (std::size_t k = 0; k < pivotCol.size(); ++k) {
            const Rational &v = M[k][n];
            plain.push_back(vars[static_cast<std::size_t>(pivotCol[k])] + " = " + v.str());
            latex.push_back(vars[static_cast<std::size_t>(pivotCol[k])] + " = " + v.latex());
        }
        return;
    }
    infinite = true;
    const char *names[] = {"t", "u", "w", "s", "r", "q", "p"};
    for (std::size_t k = 0; k < freeCols.size(); ++k)
        params.push_back(k < 7 ? names[k] : ("t" + std::to_string(k + 1)));
    std::vector<std::string> exprs(n), exprsLatex(n);
    for (std::size_t j = 0; j < n; ++j) {
        exprs[j] = "0";
        exprsLatex[j] = "0";
    }
    for (std::size_t k = 0; k < pivotCol.size(); ++k) {
        Rational cst = M[k][n];
        std::string s = cst.isZero() ? "" : cst.str();
        std::string sl = cst.isZero() ? "" : cst.latex();
        for (std::size_t fi = 0; fi < freeCols.size(); ++fi) {
            Rational coef = -M[k][static_cast<std::size_t>(freeCols[fi])];
            if (coef.isZero()) continue;
            std::string mag = coef.abs().isOne() ? std::string() : coef.abs().str();
            std::string magL = coef.abs().isOne() ? std::string() : coef.abs().latex();
            if (s.empty()) {
                s = (coef.isNeg() ? "-" : "") + mag + params[fi];
                sl = (coef.isNeg() ? "-" : "") + magL + params[fi];
            } else {
                s += (coef.isNeg() ? " - " : " + ") + mag + params[fi];
                sl += (coef.isNeg() ? " - " : " + ") + magL + params[fi];
            }
        }
        if (s.empty()) s = "0";
        if (sl.empty()) sl = "0";
        exprs[static_cast<std::size_t>(pivotCol[k])] = s;
        exprsLatex[static_cast<std::size_t>(pivotCol[k])] = sl;
    }
    for (std::size_t fi = 0; fi < freeCols.size(); ++fi) {
        exprs[static_cast<std::size_t>(freeCols[fi])] = params[fi];
        exprsLatex[static_cast<std::size_t>(freeCols[fi])] = params[fi];
    }
    for (std::size_t j = 0; j < n; ++j) {
        plain.push_back(vars[j] + " = " + exprs[j]);
        latex.push_back(vars[j] + " = " + exprsLatex[j]);
    }
}

} // namespace

} // namespace

// 真·解题步骤: 按中学课堂过程给出一元一次 / 一元二次的推导过程。
// 和"哪个引擎求解"无关 —— 只要结果里有单变量多项式就生成, 所以默认的 SymPy 引擎也有步骤。
// 线性方程组的课堂步骤: 加减消元(高斯消元)逐行变换 + 回代。只在唯一解时给。
static void appendLinearSteps(SolveResult &res, const std::vector<std::vector<Rational>> &A0,
                              const std::vector<Rational> &b0) {
    if (!res.steps.empty()) return;
    const std::size_t n = A0.size();
    if (n == 0 || n > 4 || A0[0].size() != n || res.vars.size() != n) return;
    const auto &V = res.vars;
    auto rowEq = [&](const std::vector<Rational> &row, const Rational &rhs) {
        std::string out;
        for (std::size_t j = 0; j < n; ++j) {
            const Rational &c = row[j];
            if (c.isZero()) continue;
            Rational a = c.isNeg() ? -c : c;
            std::string cs = (a == Rational(1)) ? std::string() : a.str();
            std::string t = cs + V[j];
            if (out.empty()) out = (c.isNeg() ? "-" : "") + t;
            else out += (c.isNeg() ? " - " : " + ") + t;
        }
        if (out.empty()) out = "0";
        return out + " = " + rhs.str();
    };
    res.steps.push_back(L("原方程组: ", "system: "));
    for (std::size_t i = 0; i < n; ++i) res.steps.push_back("  " + rowEq(A0[i], b0[i]));
    std::vector<std::vector<Rational>> A = A0;
    std::vector<Rational> b = b0;
    auto num = [](std::size_t k) { return "第" + std::to_string(k + 1) + "式"; };
    for (std::size_t k = 0; k < n; ++k) {
        if (A[k][k].isZero()) return;   // 需要换行/无唯一解: 步骤交给求解器结论, 这里不硬写
        for (std::size_t i = k + 1; i < n; ++i) {
            if (A[i][k].isZero()) continue;
            Rational m = A[i][k] / A[k][k];
            for (std::size_t j = 0; j < n; ++j) A[i][j] = A[i][j] - m * A[k][j];
            b[i] = b[i] - m * b[k];
            res.steps.push_back(L("消去 ", "eliminate ") + V[k] + ": " + num(i) + " - (" + m.str() +
                                 ")×" + num(k) + "  ->  " + rowEq(A[i], b[i]));
        }
    }
    std::vector<Rational> sol(n);
    for (std::size_t kk = n; kk-- > 0;) {
        Rational s = b[kk];
        for (std::size_t j = kk + 1; j < n; ++j) s = s - A[kk][j] * sol[j];
        if (A[kk][kk].isZero()) return;
        sol[kk] = s / A[kk][kk];
        res.steps.push_back(L("回代得: ", "back-substitute: ") + V[kk] + " = " + sol[kk].str());
    }
}

// 从原始方程文本重建线性方程组并给消元步骤 —— 供 modes 在"两条引擎路径合流处"调用,
// 这样默认的 SymPy 引擎也能看到课堂步骤。
bool appendLinearStepsFromText(const std::vector<std::string> &eqs, SolveResult &res) {
    if (!res.steps.empty() || res.vars.size() < 2 || res.vars.size() > 4) return false;
    std::set<std::string> declared;
    for (const auto &e : eqs) {
        auto n = scanDeclaredNames(normalize_math(e));
        declared.insert(n.begin(), n.end());
    }
    std::vector<EqInfo> infos;
    for (const auto &e : eqs) {
        EqInfo info;
        std::string err;
        if (!parseEq(e, declared, info, err)) return false;
        infos.push_back(info);
    }
    const std::size_t n = res.vars.size();
    if (infos.size() != n) return false;
    std::vector<std::vector<Rational>> A;
    std::vector<Rational> b;
    for (const auto &i : infos) {
        std::vector<Rational> row(n, Rational(0));
        std::map<std::string, NodePtr> zeroSub;
        for (const auto &v : res.vars) zeroSub[v] = mkNum(Rational(0));
        NodePtr f0;
        substitute(i.f, zeroSub, f0);
        Rational c0;
        if (!asConst(simplifyConst(f0), c0)) return false;
        b.push_back(-c0);
        for (std::size_t j = 0; j < n; ++j) {
            std::map<std::string, NodePtr> sub = zeroSub;
            sub[res.vars[j]] = mkNum(Rational(1));
            NodePtr fj;
            substitute(i.f, sub, fj);
            Rational cj;
            if (!asConst(simplifyConst(fj), cj)) return false;
            row[j] = cj - c0;
        }
        A.push_back(row);
    }
    appendLinearSteps(res, A, b);
    return !res.steps.empty();
}

// ==================== 复平面轨迹 ====================
// 把 z / w 视作复变量(不是实未知量):
//   |z - c| = r   -> 圆(圆心 c, 半径 r);  < r -> 圆盘;  > r -> 圆外;  <=/>= 同理
//   |z - a| = |z - b| -> 线段 ab 的垂直平分线;  < -> 靠近 a 的一侧(半平面)
// 其它情形一律返回 false, 由原来的实数求解流程处理(所以 |x-1|=2 不受影响)。
namespace {
// 找一个不在括号内的顶层关系运算符
bool splitRelation(const std::string &s, std::string &lhs, std::string &op, std::string &rhs) {
    int depth = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if (depth == 0) {
            if (c == '=' || c == '<' || c == '>') {
                std::string o(1, c);
                std::size_t j = i + 1;
                if (j < s.size() && (s[j] == '=' || s[j] == '<' || s[j] == '>')) {
                    o += s[j];
                    ++j;
                }
                lhs = s.substr(0, i);
                rhs = s.substr(j);
                op = o;
                return true;
            }
            // 全角 ≤ ≥ 已在 normalize 里转成 <= >=
        }
    }
    return false;
}
bool isAbsCall(const NodePtr &n, NodePtr &inner) {
    if (n && n->t == NT::Call && n->name == "abs" && n->kids.size() == 1) {
        inner = n->kids[0];
        return true;
    }
    return false;
}
} // namespace

bool complexLocusFromText(const std::string &itemRaw, SolveResult &res) {
    std::string item = normalize_math(itemRaw);
    std::string lhsT, op, rhsT;
    if (!splitRelation(item, lhsT, op, rhsT)) return false;
    // 文本里可能是 |x| 竖线, 也可能已归一化成 abs( —— 两种都认
    auto hasAbsText = [](const std::string &t) {
        return t.find("abs(") != std::string::npos || t.find('|') != std::string::npos;
    };
    if (!hasAbsText(lhsT) && !hasAbsText(rhsT)) return false;
    // 复变量必须是 z 或 w(避免把 |x-1|=2 这类实数方程解释成轨迹)
    auto pickVar = [&](const std::string &t) -> std::string {
        for (std::size_t i = 0; i < t.size(); ++i) {
            char c = t[i];
            if (!std::isalpha(static_cast<unsigned char>(c))) continue;
            if (i > 0 && (std::isalnum(static_cast<unsigned char>(t[i - 1])) || t[i - 1] == '_')) continue;
            std::size_t j = i;
            std::string w;
            while (j < t.size() && (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '_')) w += t[j++];
            if (w.size() != 1) { i = j - 1; continue; }
            if (isFunctionName(w) || isGreekVarName(w)) { i = j - 1; continue; }
            if (w == "z" || w == "w") return w;
        }
        return std::string();
    };
    std::string v = pickVar(item);
    if (v.empty()) return false;

    ParseOptions po;
    std::string perr;
    NodePtr ln = parseExpression(lhsT, po, perr);
    NodePtr rn = parseExpression(rhsT, po, perr);
    if (!ln || !rn) return false;
    NodePtr li, ri;
    bool lAbs = isAbsCall(ln, li), rAbs = isAbsCall(rn, ri);
    if (!lAbs && !rAbs) return false;
    if (rAbs && !lAbs) { std::swap(ln, rn); std::swap(li, ri); lAbs = true; rAbs = false; }
    // 关系方向: 若我们交换了两边, 比较符要反过来
    bool swapped = false;
    if (rAbs && !lAbs) swapped = true;   // 不会发生(上面已保证)
    (void)swapped;

    // 圆心: 结构化成字符串(这样 1+i 这种复圆心也支持), 同时尽量给出有理的首项系数
    auto centerOf = [&](const NodePtr &linear, std::string &cs, Rational *aOut) {
        NodePtr A, B;
        if (!linearCoeffs(linear, v, A, B)) return false;
        Rational a;
        bool aRat = asConst(simplifyConst(A), a);
        if (aRat && a.isZero()) return false;
        NodePtr center;
        if (aRat && a == Rational(1)) center = simplifyConst(mkNeg(B));
        else if (aRat && a == Rational(-1)) center = simplifyConst(B);
        else center = simplifyConst(Node::op(NT::Div, mkNeg(B), A));
        cs = astPlain(center);
        if (aOut && aRat) *aOut = a;
        return true;
    };

    auto relWord = [&](const std::string &o) {
        if (o == "=") return std::string("=");
        if (o == "<") return std::string("<");
        if (o == ">") return std::string(">");
        if (o == "<=") return std::string("<=");
        if (o == ">=") return std::string(">=");
        return std::string("=");
    };
    const std::string rel = relWord(op);

    // ---- 情形 A: |E| REL 常数 ----
    if (!rAbs) {
        Rational rv;
        if (collectVars(rn).empty() && asConst(simplifyConst(rn), rv)) {
            std::string cen;
            Rational a(1);
            if (!centerOf(li, cen, &a)) return false;   // 圆心(结构化文本, 支持 1+i 这种复圆心)
            Rational radius = rv.isNeg() ? -rv : rv;
            if (!a.isZero() && !(a == Rational(1) || a == Rational(-1)))
                radius = radius / (a.isNeg() ? -a : a);
            std::string desc;
            if (rel == "=")
                desc = L("轨迹: 圆 — 圆心 ", "locus: circle — center ") + cen + L(", 半径 ", ", radius ") + radius.str();
            else if (rel == "<" || rel == "<=")
                desc = L("轨迹: 圆的内部(圆盘) — 圆心 ", "locus: disk — center ") + cen + L(", 半径 ", ", radius ") + radius.str() +
                       (rel == "<=" ? L("(含边界)", " (closed)") : L("(不含边界)", " (open)"));
            else
                desc = L("轨迹: 圆的外部 — 圆心 ", "locus: outside the circle — center ") + cen + L(", 半径 ", ", radius ") + radius.str();
            res.solutionPlain.push_back(desc);
            res.solutionLatex.push_back(desc);
            if (rel == "=" && cen.find('i') == std::string::npos) {
                // 直角坐标方程只在实圆心时给(复圆心写成 (x-c)^2 会误导)
                res.solutionPlain.push_back(L("直角坐标: (x - ", "cartesian: (x - ") + cen + L(")^2 + y^2 = ", ")^2 + y^2 = ") +
                                            radius.str() + "^2");
                res.solutionLatex.push_back(L("(x - ", "(x - ") + cen + ")^2 + y^2 = " + radius.str() + "^2");
            }
            res.ok = true;
            res.method = L("复平面轨迹", "complex locus");
            res.notes.push_back(L("把 ", "treating ") + v + L(" 当作复变量 z = x + yi(所以这是轨迹, 不是两个实根)",
                                                             " as a complex variable z = x + yi"));
            return true;
        }
    }
    // ---- 情形 B: |E1| REL |E2| ----
    if (rAbs) {
        std::string c1, c2;
        if (!centerOf(li, c1, nullptr) || !centerOf(ri, c2, nullptr)) return false;
        std::string desc;
        if (rel == "=")
            desc = L("轨迹: 到 ", "locus: perpendicular bisector of ") + c1 + L(" 与 ", " and ") + c2 +
                   L(" 距离相等的点的集合(线段垂直平分线)", " (points equidistant from the two centers)");
        else if (rel == "<" || rel == "<=")
            desc = L("轨迹: 到 ", "locus: points closer to ") + c1 + L(" 比到 ", " than to ") + c2 +
                   L(" 更近的一侧(半平面)", " (a half-plane)");
        else
            desc = L("轨迹: 到 ", "locus: points closer to ") + c2 + L(" 比到 ", " than to ") + c1 +
                   L(" 更近的一侧(半平面)", " (a half-plane)");
        res.solutionPlain.push_back(desc);
        res.solutionLatex.push_back(desc);
        res.ok = true;
        res.method = L("复平面轨迹", "complex locus");
        res.notes.push_back(L("把 ", "treating ") + v + L(" 当作复变量 z = x + yi", " as a complex variable z = x + yi"));
        return true;
    }
    return false;
}

void appendPolySteps(SolveResult &res, const Poly &g) {
    if (!res.steps.empty() || g.degree() < 1 || res.vars.size() != 1) return;
    const std::string &var = res.var;
    auto fmt = [](const Rational &r) { return r.str(); };
    auto term = [&](const Rational &cf, int k) {
        if (cf.isZero()) return std::string();
        bool neg = cf.isNeg();
        Rational a = neg ? -cf : cf;
        std::string out;
        if (k == 0) out = fmt(a);
        else {
            std::string cfs = (a == Rational(1)) ? std::string() : fmt(a);
            out = cfs + var;
            if (k > 1) out += "^" + std::to_string(k);
        }
        return (neg ? std::string("-") : std::string()) + out;
    };
    auto plusTerm = [&](const Rational &cf, int k) {
        if (cf.isZero()) return std::string();
        return std::string(cf.isNeg() ? " - " : " + ") + term(cf.isNeg() ? -cf : cf, k);
    };
    const int d = g.degree();
    if (d == 1) {
        Rational a = g.coeff(1), b = g.coeff(0);
        res.steps.push_back(L("一元一次方程", "linear equation"));
        res.steps.push_back(L("整理成标准形式: ", "standard form: ") + term(a, 1) + plusTerm(b, 0) + " = 0");
        res.steps.push_back(L("移项(常数移到右边): ", "move the constant: ") + term(a, 1) + " = " + fmt(-b));
        res.steps.push_back(L("两边同除以 ", "divide both sides by ") + fmt(a) + ": " + var + " = " + fmt(-b / a));
    } else if (d == 2) {
        Rational a = g.coeff(2), b = g.coeff(1), c = g.coeff(0);
        Rational disc = b * b - Rational(4) * a * c;
        res.steps.push_back(L("一元二次方程", "quadratic equation"));
        res.steps.push_back(L("整理成标准形式: ", "standard form: ") + term(a, 2) + plusTerm(b, 1) +
                             plusTerm(c, 0) + " = 0");
        res.steps.push_back(L("判别式: Δ = b^2 - 4ac = ", "discriminant Δ = b^2 - 4ac = ") + fmt(disc));
        if (disc.isNeg())
            res.steps.push_back(L("Δ < 0: 一对共轭复根", "Δ < 0: two complex conjugate roots"));
        else if (disc.isZero())
            res.steps.push_back(L("Δ = 0: 两个相等实根(重根)", "Δ = 0: one repeated real root"));
        else
            res.steps.push_back(L("Δ > 0: 两个不等实根", "Δ > 0: two distinct real roots"));
        res.steps.push_back(L("求根公式: x = (-b ± √Δ) / (2a) = (", "quadratic formula: x = (-b ± √Δ)/(2a) = (") +
                             fmt(-b) + " ± √(" + fmt(disc) + ")) / " + fmt(Rational(2) * a));
        // 韦达定理(根与系数关系): 课堂上必考, 直接给出来
        res.steps.push_back(L("韦达定理: ", "Vieta: ") + var + "₁ + " + var + "₂ = -b/a = " +
                             fmt(-b / a) + ",  " + var + "₁ · " + var + "₂ = c/a = " + fmt(c / a));
        if (!disc.isNeg()) {
            Surd sq;
            if (Surd::sqrtOf(disc, sq) && sq.isRational()) {
                Rational r = sq.coef;
                Rational x1 = (-b + r) / (Rational(2) * a);
                Rational x2 = (-b - r) / (Rational(2) * a);
                auto factorOf = [&](const Rational &r) {
                    // (x - r); r 为负时写成 (x + |r|), 避免出现 "x - -2"
                    if (r.isNeg()) return "(" + var + " + " + fmt(-r) + ")";
                    if (r.isZero()) return "(" + var + ")";
                    return "(" + var + " - " + fmt(r) + ")";
                };
                res.steps.push_back(L("因式分解: ", "factored: ") + fmt(a) + factorOf(x1) + factorOf(x2) + " = 0");
                res.steps.push_back(L("两个根: ", "roots: ") + var + "₁ = " + fmt(x1) + ", " + var +
                                     "₂ = " + fmt(x2));
            }
        }
    }
}

SolveResult solveEquations(const std::vector<std::string> &eqs, const SolveOptions &opt) {
    SolveResult res;
    if (eqs.empty()) {
        res.err = "没有输入方程";
        return res;
    }
    std::set<std::string> declared;
    for (const auto &e : eqs) {
        auto n = scanDeclaredNames(normalize_math(e));
        declared.insert(n.begin(), n.end());
    }
    // 函数定义(f(x)=…)先登记, 后续方程里的 f(…) 内联展开 —— 这样"函数类"也能当方程解
    FuncDefs fdefs;
    std::vector<std::string> defNotes;
    std::vector<std::string> expanded;
    for (const auto &e : eqs) {
        std::string dname, dbody;
        std::vector<std::string> dparams;
        if (extractFuncDef(e, dname, dparams, dbody) && fdefs.defs.find(dname) == fdefs.defs.end()) {
            std::string sig = dname + "(";
            for (std::size_t k = 0; k < dparams.size(); ++k) sig += (k ? ", " : "") + dparams[k];
            sig += ")";
            // 先把定义体里已经登记的函数也展开(允许 f 用 g 定义)
            bool usedInner = false;
            std::string body2 = inlineFuncDefs(dbody, fdefs, usedInner);
            fdefs.defs[dname] = FuncDefs::Def{dparams, body2};
            defNotes.push_back("函数定义: " + sig + " = " + dbody);
            continue;
        }
        bool used = false;
        expanded.push_back(inlineFuncDefs(e, fdefs, used));
    }
    if (!defNotes.empty()) {
        for (const auto &n : defNotes) res.notes.push_back(n);
    }
    if (expanded.empty()) {
        res.err = "只给了函数定义, 没有要求解的方程(例如 f(x)=0)";
        return res;
    }
    std::vector<EqInfo> infos;
    for (std::size_t ei = 0; ei < expanded.size(); ++ei) {
        const std::string &e = expanded[ei];
        std::string trimmed = e;
        while (!trimmed.empty() && is_space_cp(utf8_decode(trimmed)[0])) trimmed.erase(0, 1);
        if (trimmed.empty()) continue;
        EqInfo info;
        std::string err;
        if (!parseEq(e, declared, info, err)) {
            res.err = err;
            return res;
        }
        info.original = trimmed;   // 展示用用户原话(而不是内联后的长表达式)
        infos.push_back(info);
    }
    if (infos.empty()) {
        res.err = "没有有效的方程";
        return res;
    }
    std::set<std::string> varSet;
    for (const auto &i : infos) {
        auto v = collectVars(i.f);
        varSet.insert(v.begin(), v.end());
    }
    res.vars.assign(varSet.begin(), varSet.end());

    // ---------- 无未知量: 判断恒等式 ----------
    if (res.vars.empty()) {
        res.kind = SolveResult::ConstantEq;
        bool allZero = true;
        long double scale = 1;
        for (const auto &i : infos) {
            long double v = 0;
            std::string err;
            if (!evalApprox(i.f, {}, v, err)) {
                res.err = err;
                return res;
            }
            scale = std::max(scale, std::fabs(v));
            if (std::fabs(v) > 1e-12L * scale) allZero = false;
        }
        res.ok = true;
        res.method = "常量判定";
        if (allZero) {
            res.identity = true;
            res.notes.push_back("等式恒成立, 任意值都满足");
        } else {
            res.none = true;
            res.notes.push_back("等式矛盾, 无解");
        }
        return res;
    }

    // ---------- 单变量 ----------
    if (res.vars.size() == 1) {
        res.kind = SolveResult::SingleVar;
        res.var = res.vars[0];
        std::vector<Poly> polys;
        bool allPoly = true;
        std::string firstErr;
        for (const auto &i : infos) {
            Poly p;
            std::string err;
            if (astToPoly(i.f, res.var, p, err)) polys.push_back(p);
            else {
                allPoly = false;
                if (firstErr.empty()) firstErr = err;
            }
        }
        if (allPoly) {
            res.method = "多项式(精确)";
            Poly g = polys[0];
            for (std::size_t k = 1; k < polys.size(); ++k) {
                if (g.isZero()) break;
                bool tooBig = false;
                g = polyGcd(g, polys[k], &tooBig);
                if (tooBig) {
                    res.err = L("多项式系数规模过大(精确求公因式会膨胀到数百万位数字), 已放弃精确求解",
                                "coefficients too large for exact gcd") + gcdSizeAdvice(opt);
                    return res;
                }
            }
            if (g.isZero()) {
                res.identity = true;
                res.notes.push_back("所有方程恒成立, 任意值都满足");
                res.ok = true;
                return res;
            }
            if (g.degree() == 0) {
                res.none = true;
                res.notes.push_back("方程无解");
                res.ok = true;
                return res;
            }
            res.poly = g;
            res.hasPoly = true;
            appendPolySteps(res, g);
            {
                SolveOptions o2 = opt;
                o2.notes = &res.notes;
                res.roots = rootsOfPolynomial(g, o2);
            }
        } else {
            res.method = "数值扫描";
            res.notes.push_back("方程含非多项式成分, 使用数值方法: " + firstErr);
            for (const auto &i : infos) {
                std::vector<RootOut> all;
                long double lo = opt.scanLo, hi = opt.scanHi;
                int samples = std::max(100, opt.scanSamples);
                long double h = (hi - lo) / samples;
                long double prevX = lo;
                long double prevV = 0;
                bool havePrev = false;
                for (int k = 0; k <= samples; ++k) {
                    if ((k & 0x3FF) == 0 && interruptRequested()) break;
                    long double x = lo + h * k;
                    long double v = 0;
                    std::string err;
                    if (!evalApprox(i.f, {{res.var, x}}, v, err)) {
                        havePrev = false;
                        continue;
                    }
                    if (havePrev) {
                        if ((prevV < 0 && v > 0) || (prevV > 0 && v < 0) || v == 0) {
                            long double a = prevX, bb = x;
                            for (int it = 0; it < 200; ++it) {
                                if (interruptRequested()) break;
                                long double mid = (a + bb) / 2;
                                long double vm = 0;
                                evalApprox(i.f, {{res.var, mid}}, vm, err);
                                if ((prevV < 0 && vm < 0) || (prevV > 0 && vm > 0)) a = mid;
                                else bb = mid;
                            }
                            RootOut r;
                            r.exact = false;
                            r.isComplex = false;
                            r.numeric = std::complex<long double>((a + bb) / 2, 0);
                            r.plain = formatNumber((a + bb) / 2, opt.fmt);
                            r.latex = r.plain;
                            r.approxPlain = r.plain;
                            // 数值根若"正好"是有理数, 回代做**精确**验证: 成立就按精确值给。
                            // 例: 2^x=8 的数值根是 3 -> 输出 x = 3 而不是 x ≈ 3。
                            // 判据是代入后精确等于 0(不是靠误差), 所以不会把无理根误报成精确。
                            {
                                long double xnum = (a + bb) / 2;
                                Rational cand = Rational::fromDouble(xnum, 1000000);
                                bool allExact = true;
                                for (const auto &ii : infos) {
                                    Surd rv;
                                    std::string e2;
                                    if (!evalExact(ii.f, {{res.var, cand}}, rv, e2) || !rv.isZero()) {
                                        allExact = false;
                                        break;
                                    }
                                }
                                if (allExact) {
                                    r.exact = true;
                                    r.plain = cand.str();
                                    r.latex = cand.str();
                                    r.approxPlain = formatNumber(xnum, opt.fmt);
                                }
                            }
                            all.push_back(r);
                            prevV = v;
                            prevX = x;
                            havePrev = true;
                            continue;
                        }
                    }
                    prevX = x;
                    prevV = v;
                    havePrev = true;
                }
                if (all.empty()) {
                    res.none = true;
                    res.notes.push_back("在区间 [" + formatLongDouble(lo, 0) + ", " +
                                        formatLongDouble(hi, 0) + "] 内未找到实根");
                    res.ok = true;
                    return res;
                }
                res.roots = all;
            }
        }
        // 附加说明
        int exactCount = 0;
        for (const auto &r : res.roots)
            if (r.exact) ++exactCount;
        if (exactCount < static_cast<int>(res.roots.size()))
            res.notes.push_back("部分根为数值近似(已给出 " + std::to_string(opt.decimals) + " 位小数)");
        res.ok = true;
        return res;
    }

    // ---------- 多变量: 先试线性 ----------
    bool allLinear = true;
    std::size_t n = res.vars.size();
    std::vector<std::vector<Rational>> A;
    std::vector<Rational> b;
    for (const auto &i : infos) {
        for (const auto &v : res.vars) {
            NodePtr Ac, Bc;
            if (!linearCoeffs(i.f, v, Ac, Bc) || !collectVars(Ac).empty()) {
                allLinear = false;
                break;
            }
        }
        if (!allLinear) break;
        std::vector<Rational> row(n, Rational(0));
        // 常数项
        std::map<std::string, NodePtr> zeroSub;
        for (const auto &v : res.vars) zeroSub[v] = mkNum(Rational(0));
        NodePtr f0;
        substitute(i.f, zeroSub, f0);
        Rational c0;
        if (!asConst(simplifyConst(f0), c0)) {
            allLinear = false;
            break;
        }
        b.push_back(-c0);
        for (std::size_t j = 0; j < n; ++j) {
            std::map<std::string, NodePtr> sub = zeroSub;
            sub[res.vars[j]] = mkNum(Rational(1));
            NodePtr fj;
            substitute(i.f, sub, fj);
            Rational cj;
            if (!asConst(simplifyConst(fj), cj)) {
                allLinear = false;
                break;
            }
            row[j] = cj - c0;
        }
        if (!allLinear) break;
        A.push_back(row);
    }
    if (allLinear) {
        res.kind = SolveResult::LinearSystem;
        res.method = "线性方程组(精确)";
        solveLinearSystem(A, b, res.solutionPlain, res.solutionLatex, res.infinite, res.none,
                          res.params, res.vars);
        res.ok = true;
        if (!res.none && !res.infinite) appendLinearSteps(res, A, b);   // 课堂步骤: 消元 + 回代
        if (res.none) res.notes.push_back("方程组不相容, 无解");
        else if (res.infinite)
            res.notes.push_back("存在自由参数, 有无穷多解");
        else
            res.notes.push_back("存在唯一解");
        return res;
    }

    // ---------- 非线性: 代入消元 ----------
    res.kind = SolveResult::NonlinearSystem;
    std::vector<NodePtr> f;
    for (const auto &i : infos) f.push_back(i.f);
    std::set<std::string> varsLeft = varSet;
    std::vector<std::pair<std::string, NodePtr>> solvedOrder;
    std::string err;
    while (!varsLeft.empty()) {
        if (interruptRequested()) {
            res.err = L("计算已被中断", "interrupted by user");
            return res;
        }
        if (f.empty()) {
            // 剩余变量是自由的(例如只给了关系式 y=x^4)
            res.method = "代入消元";
            res.infinite = true;
            for (const auto &v : varsLeft) res.params.push_back(v);
            for (auto &pr : solvedOrder) {
                res.solutionPlain.push_back(pr.first + " = " + astPlain(pr.second));
                res.solutionLatex.push_back(pr.first + " = " + astLatex(pr.second));
            }
            if (solvedOrder.empty()) {
                for (const auto &v : varsLeft) {
                    res.solutionPlain.push_back(v + L(" 为自由参数", " is free"));
                    res.solutionLatex.push_back(v + L("\\text{ 为自由参数}", "\\text{ is free}"));
                }
            } else {
                std::string names;
                for (const auto &v : varsLeft) {
                    if (!names.empty()) names += ", ";
                    names += v;
                }
                res.notes.push_back(L("自由变量: ", "free variables: ") + names);
            }
            res.notes.push_back(L("方程不足以确定唯一解, 已给出变量之间的关系",
                                  "not enough equations for a unique solution; relations given"));
            res.ok = true;
            return res;
        }
        if (varsLeft.size() == 1) {
            std::string v = *varsLeft.begin();
            std::vector<Poly> ps;
            bool ok = true;
            for (auto &e : f) {
                Poly p;
                std::string pe;
                if (!astToPoly(e, v, p, pe)) {
                    ok = false;
                    err = pe;
                    break;
                }
                ps.push_back(p);
            }
            if (!ok) {
                res.err = "非线性方程组无法解析求解: " + err;
                return res;
            }
            Poly g = ps[0];
            for (std::size_t k = 1; k < ps.size(); ++k) {
                if (g.isZero()) break;
                bool tooBig = false;
                g = polyGcd(g, ps[k], &tooBig);
                if (tooBig) {
                    res.err = L("多项式系数规模过大(精确求公因式会膨胀到数百万位数字), 已放弃精确求解",
                                "coefficients too large for exact gcd") + gcdSizeAdvice(opt);
                    return res;
                }
            }
            if (g.isZero()) {
                res.identity = true;
                res.notes.push_back("方程恒成立");
                res.ok = true;
                return res;
            }
            if (g.degree() <= 0) {
                res.none = true;
                res.notes.push_back("无解");
                res.ok = true;
                return res;
            }
            SolveOptions o2 = opt;
            o2.notes = &res.notes;
            auto roots = rootsOfPolynomial(g, o2);
            res.method = "代入消元 + 多项式求根";
            int idx = 1;
            for (const auto &r : roots) {
                std::map<std::string, NodePtr> sub;
                if (r.exact && !r.isComplex) {
                    sub[v] = algNumToNode(r.value);
                } else {
                    Rational rv = Rational::fromDouble(r.numeric.real(), 1000000);
                    sub[v] = mkNum(rv);
                }
                std::string label = "解 " + std::to_string(idx++) + ": ";
                std::string line = v + " = " + r.plain;
                std::string lineL = v + " = " + r.latex;
                bool numericOnly = !(r.exact && !r.isComplex);
                for (auto it = solvedOrder.rbegin(); it != solvedOrder.rend(); ++it) {
                    NodePtr expr;
                    substitute(it->second, sub, expr);
                    expr = simplifyConst(expr);
                    Surd ev;
                    std::string ee;
                    long double val = 0;
                    if (evalExact(expr, {}, ev, ee)) {
                        line = it->first + " = " + surdPlainNice(ev) + ", " + line;
                        lineL = it->first + " = " + surdLatexNice(ev) + ", " + lineL;
                        sub[it->first] = expr;
                    } else if (evalApprox(expr, {}, val, err)) {
                        std::string vs = formatNumber(val, opt.fmt);
                        line = it->first + " = " + vs + ", " + line;
                        lineL = it->first + " = " + vs + ", " + lineL;
                        sub[it->first] = mkNum(Rational::fromDouble(val, 1000000));
                        numericOnly = true;
                    } else {
                        line = it->first + " = " + astPlain(expr) + ", " + line;
                        lineL = it->first + " = " + astLatex(expr) + ", " + lineL;
                        sub[it->first] = expr;
                    }
                }
                res.solutionPlain.push_back(line);
                res.solutionLatex.push_back(lineL);
                if (numericOnly && !r.isComplex)
                    res.notes.push_back("该解为数值近似");
            }
            res.ok = true;
            if (res.solutionPlain.empty()) {
                res.none = true;
                res.notes.push_back("无实解或无法求解");
            }
            return res;
        }
        // 找一条对某变量线性的方程
        bool found = false;
        for (auto &e : f) {
            for (const auto &v : varsLeft) {
                NodePtr Acoef, Bcoef;
                if (!linearCoeffs(e, v, Acoef, Bcoef)) continue;
                Rational ca;
                bool isZeroA = asConst(Acoef, ca) && ca.isZero();
                if (isZeroA) continue;
                if (containsVar(Acoef, v)) continue;
                if (asConst(Acoef, ca) && ca.isZero()) continue;
                NodePtr expr = mkDiv(mkNeg(Bcoef), Acoef);
                // 代入其它方程
                std::vector<NodePtr> nf;
                for (auto &e2 : f) {
                    if (e2 == e) continue;
                    NodePtr sub2;
                    substitute(e2, {{v, expr}}, sub2);
                    nf.push_back(simplifyConst(sub2));
                }
                // 代入已解表达式
                for (auto &pr : solvedOrder) {
                    NodePtr sub2;
                    substitute(pr.second, {{v, expr}}, sub2);
                    pr.second = simplifyConst(sub2);
                }
                solvedOrder.push_back({v, expr});
                f = nf;
                varsLeft.erase(v);
                found = true;
                break;
            }
            if (found) break;
        }
        if (!found) {
            res.err = "该非线性方程组超出解析求解能力(无法找到可代入的线性变量)";
            return res;
        }
    }
    // 所有变量都被表达式替代: 检查剩余方程
    res.method = "代入消元";
    bool okAll = true;
    for (auto &e : f) {
        long double v = 0;
        if (!evalApprox(e, {}, v, err) || std::fabs(v) > 1e-9L) okAll = false;
    }
    if (!okAll) {
        res.none = true;
        res.notes.push_back("方程组不相容, 无解");
        res.ok = true;
        return res;
    }
    res.identity = true;
    res.ok = true;
    res.notes.push_back("方程恒成立(解含自由参数)");
    for (auto &pr : solvedOrder)
        res.solutionPlain.push_back(pr.first + " = " + astPlain(pr.second));
    for (auto &pr : solvedOrder)
        res.solutionLatex.push_back(pr.first + " = " + astLatex(pr.second));
    // 兜底: 不管哪个引擎解的, 只要结果里有单变量多项式就补上课堂步骤
    if (res.steps.empty() && res.hasPoly) appendPolySteps(res, res.poly);
    return res;
}

} // namespace em

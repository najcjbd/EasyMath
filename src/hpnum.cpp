// EasyMath - MPFR/MPC 高精度实现
#include "hpnum.hpp"

#include "i18n.hpp"
#include "interrupt.hpp"

#include <cmath>
#include <cstring>

#ifdef EASYMATH_USE_MPFR
#include <mpc.h>
#include <mpfr.h>
#endif

namespace em {

long hpPrecBitsForDigits(int digits) {
    if (digits < 8) digits = 8;
    long bits = static_cast<long>(static_cast<double>(digits) * 3.321928094887362 + 40.0);
    if (bits < 64) bits = 64;
    return bits;
}

#ifdef EASYMATH_USE_MPFR

bool hpAvailable() { return true; }
const char *hpBackendName() { return "MPFR"; }
std::string hpVersion() { return std::string(mpfr_get_version()) + " / MPC " + mpc_get_version(); }

// ---------- 定点格式化(不经过 double) ----------
static std::string formatMpfr(const mpfr_t x, int digits, SciMode sci = SciMode::Never,
                              int sciThreshold = 12) {
    if (mpfr_nan_p(x)) return "nan";
    if (mpfr_inf_p(x)) return mpfr_sgn(x) < 0 ? "-inf" : "inf";
    char buf[4096];
    mpfr_snprintf(buf, sizeof(buf), "%.*Rf", digits, x);
    std::string s = buf;
    if (digits > 0 && s.find('.') != std::string::npos) {
        std::size_t e = s.find_last_not_of('0');
        if (e != std::string::npos) {
            if (s[e] == '.') --e;
            s = s.substr(0, e + 1);
        }
    }
    bool nonZero = !mpfr_zero_p(x);
    bool allZero = true;
    for (char c : s)
        if (c >= '1' && c <= '9') allZero = false;
    if (allZero && s.size() >= 2 && s[0] == '-' && s[1] == '0') s = s.substr(1);
    bool wantSci = false;
    if (nonZero) {
        if (sci == SciMode::Always) wantSci = true;
        else if (sci == SciMode::Auto) {
            if (allZero) wantSci = true; // 定点会显示成 0
        }
    }
    if (nonZero && sci == SciMode::Auto) {
        // 注意: 必须先取绝对值再 log10 —— 对负数直接 log10 会得到 NaN,
        // 而 mpfr_cmp(NaN, thr) 返回 0, ">= 0" 会误判为真(所有负数都变科学计数法)。
        mpfr_t lg, thr;
        mpfr_init2(lg, 64);
        mpfr_init2(thr, 64);
        mpfr_abs(lg, x, MPFR_RNDN);
        mpfr_log10(lg, lg, MPFR_RNDN);
        mpfr_set_ui(thr, static_cast<unsigned long>(sciThreshold > 0 ? sciThreshold : 12), MPFR_RNDN);
        if (mpfr_cmp(lg, thr) >= 0) wantSci = true; // 过大
        mpfr_set_si(thr, -(digits + 1), MPFR_RNDN);
        if (mpfr_cmp(lg, thr) < 0) wantSci = true; // 过小(定点会显示成 0)
        mpfr_clear(lg);
        mpfr_clear(thr);
    }
    if (nonZero && wantSci) {
        char sbuf[256];
        mpfr_snprintf(sbuf, sizeof(sbuf), "%.*Re", digits > 0 ? digits : 1, x);
        s = sbuf;
        std::size_t e = s.find('e');
        if (e != std::string::npos) {
            std::string mant = s.substr(0, e), ex = s.substr(e + 1);
            if (mant.find('.') != std::string::npos) {
                std::size_t last = mant.find_last_not_of('0');
                if (last != std::string::npos && mant[last] == '.') --last;
                mant = mant.substr(0, last + 1);
            }
            if (ex.size() > 2 && (ex[0] == '-' || ex[0] == '+')) {
                std::size_t z = 1;
                while (z + 1 < ex.size() && ex[z] == '0') ++z;
                ex = ex.substr(0, 1) + ex.substr(z);
            }
            s = mant + "e" + ex;
        }
    }
    return s;
}

static std::string mpcToPlain(const mpc_t z, int digits, SciMode sci, int sciThreshold) {
    mpfr_t re, im, eps, scale;
    mpfr_inits2(mpfr_get_prec(mpc_realref(z)), re, im, eps, scale, (mpfr_ptr)0);
    mpfr_set(re, mpc_realref(z), MPFR_RNDN);
    mpfr_set(im, mpc_imagref(z), MPFR_RNDN);
    mpfr_abs(scale, re, MPFR_RNDN);
    mpfr_t ai;
    mpfr_init2(ai, mpfr_get_prec(re));
    mpfr_abs(ai, im, MPFR_RNDN);
    if (mpfr_cmp(ai, scale) > 0) mpfr_set(scale, ai, MPFR_RNDN);
    if (mpfr_cmp_ui(scale, 1) < 0) mpfr_set_ui(scale, 1, MPFR_RNDN);
    mpfr_set_ui(eps, 10, MPFR_RNDN);
    mpfr_pow_si(eps, eps, -(digits + 3), MPFR_RNDN);
    mpfr_mul(eps, eps, scale, MPFR_RNDN);
    std::string out;
    if (mpfr_cmpabs(im, eps) <= 0) {
        out = formatMpfr(re, digits, sci, sciThreshold);
    } else {
        mpfr_t aim;
        mpfr_init2(aim, mpfr_get_prec(im));
        mpfr_abs(aim, im, MPFR_RNDN);
        if (mpfr_cmpabs(re, eps) <= 0) {
            out = (mpfr_sgn(im) < 0 ? "-" : "") + formatMpfr(aim, digits, sci, sciThreshold) + "i";
        } else {
            out = formatMpfr(re, digits, sci, sciThreshold) + (mpfr_sgn(im) < 0 ? " - " : " + ") +
                  formatMpfr(aim, digits, sci, sciThreshold) + "i";
        }
        mpfr_clear(aim);
    }
    mpfr_clear(ai);
    mpfr_clears(re, im, eps, scale, (mpfr_ptr)0);
    return out;
}

// ---------- 实数求值 ----------
namespace {

constexpr int MAX_DEPTH = 200;

bool evalR(const NodePtr &n, long prec, mpfr_t out, std::string &err, int depth);

bool childR(const NodePtr &n, std::size_t i, long prec, mpfr_t out, std::string &err, int depth) {
    if (i >= n->kids.size()) {
        err = L("参数不足", "missing argument");
        return false;
    }
    return evalR(n->kids[i], prec, out, err, depth + 1);
}

bool evalFunc(const NodePtr &n, long prec, mpfr_t out, std::string &err, int depth) {
    const std::string &f = n->name;
    mpfr_t a, b;
    mpfr_init2(a, prec);
    mpfr_init2(b, prec);
    auto done = [&](bool ok) {
        mpfr_clear(a);
        mpfr_clear(b);
        return ok;
    };
    auto need = [&](std::size_t k) {
        if (n->kids.size() != k) {
            err = f + L(" 需要 ", " needs ") + std::to_string(k) + L(" 个参数", " argument(s)");
            return false;
        }
        return true;
    };
    const mpfr_rnd_t R = MPFR_RNDN;
    if (f == "sqrt") {
        if (!need(1) || !childR(n, 0, prec, a, err, depth)) return done(false);
        if (mpfr_sgn(a) < 0) {
            err = L("负数开平方需要复数支持", "sqrt of a negative number needs complex support");
            return done(false);
        }
        mpfr_sqrt(out, a, R);
        return done(true);
    }
    if (f == "cbrt") {
        if (!need(1) || !childR(n, 0, prec, a, err, depth)) return done(false);
        mpfr_cbrt(out, a, R);
        return done(true);
    }
    if (f == "root4") {
        if (!need(1) || !childR(n, 0, prec, a, err, depth)) return done(false);
        if (mpfr_sgn(a) < 0) {
            err = L("负数开偶次方需要复数支持", "even root of a negative number needs complex support");
            return done(false);
        }
        mpfr_sqrt(a, a, R);
        mpfr_sqrt(out, a, R);
        return done(true);
    }
    if (f == "abs" || f == "exp" || f == "ln" || f == "log" || f == "log10" || f == "log2" ||
        f == "sin" || f == "cos" || f == "tan" || f == "cot" || f == "sec" || f == "csc" ||
        f == "asin" || f == "acos" || f == "atan" || f == "sinh" || f == "cosh" || f == "tanh" ||
        f == "floor" || f == "ceil" || f == "round" || f == "sign") {
        if (!need(1) || !childR(n, 0, prec, a, err, depth)) return done(false);
        if (f == "abs") mpfr_abs(out, a, R);
        else if (f == "exp") mpfr_exp(out, a, R);
        else if (f == "ln") {
            if (mpfr_sgn(a) <= 0) {
                err = L("ln 的定义域为正数", "ln domain is positive numbers");
                return done(false);
            }
            mpfr_log(out, a, R);
        } else if (f == "log" || f == "log10") {
            if (mpfr_sgn(a) <= 0) {
                err = L("log 的定义域为正数", "log domain is positive numbers");
                return done(false);
            }
            mpfr_log10(out, a, R);
        } else if (f == "log2") {
            if (mpfr_sgn(a) <= 0) {
                err = L("log2 的定义域为正数", "log2 domain is positive numbers");
                return done(false);
            }
            mpfr_log2(out, a, R);
        } else if (f == "sin") mpfr_sin(out, a, R);
        else if (f == "cos") mpfr_cos(out, a, R);
        else if (f == "tan") mpfr_tan(out, a, R);
        else if (f == "cot") mpfr_cot(out, a, R);
        else if (f == "sec") mpfr_sec(out, a, R);
        else if (f == "csc") mpfr_csc(out, a, R);
        else if (f == "asin") mpfr_asin(out, a, R);
        else if (f == "acos") mpfr_acos(out, a, R);
        else if (f == "atan") mpfr_atan(out, a, R);
        else if (f == "sinh") mpfr_sinh(out, a, R);
        else if (f == "cosh") mpfr_cosh(out, a, R);
        else if (f == "tanh") mpfr_tanh(out, a, R);
        else if (f == "floor") mpfr_floor(out, a);
        else if (f == "ceil") mpfr_ceil(out, a);
        else if (f == "round") mpfr_round(out, a);
        else if (f == "sign") mpfr_set_si(out, mpfr_sgn(a), R);
        if (mpfr_nan_p(out)) {
            err = f + L(" 在此参数下无定义", " is undefined here");
            return done(false);
        }
        return done(true);
    }
    if (f == "deg") {
        if (!need(1) || !childR(n, 0, prec, a, err, depth)) return done(false);
        mpfr_const_pi(b, R);
        mpfr_mul(out, a, b, R);
        mpfr_div_ui(out, out, 180, R);
        return done(true);
    }
    if (f == "max" || f == "min") {
        if (n->kids.empty()) {
            err = f + L(" 需要参数", " needs arguments");
            return done(false);
        }
        if (!childR(n, 0, prec, a, err, depth)) return done(false);
        for (std::size_t i = 1; i < n->kids.size(); ++i) {
            if (!childR(n, i, prec, b, err, depth)) return done(false);
            int c = mpfr_cmp(b, a);
            if ((f == "max" && c > 0) || (f == "min" && c < 0)) mpfr_set(a, b, R);
        }
        mpfr_set(out, a, R);
        return done(true);
    }
    if (f == "sum" || f == "prod") {
        if (n->kids.empty()) {
            err = f + L(" 需要参数", " needs arguments");
            return done(false);
        }
        mpfr_set_ui(a, (f == "sum") ? 0 : 1, R);
        for (std::size_t i = 0; i < n->kids.size(); ++i) {
            if (!childR(n, i, prec, b, err, depth)) return done(false);
            if (f == "sum") mpfr_add(a, a, b, R);
            else mpfr_mul(a, a, b, R);
        }
        mpfr_set(out, a, R);
        return done(true);
    }
    if (f == "gcd" || f == "lcm") {
        if (n->kids.size() < 2) {
            err = f + L(" 需要至少 2 个参数", " needs at least 2 arguments");
            return done(false);
        }
        mpz_t acc, cur;
        mpz_init(acc);
        mpz_init(cur);
        for (std::size_t i = 0; i < n->kids.size(); ++i) {
            if (!childR(n, i, prec, a, err, depth)) {
                mpz_clears(acc, cur, (mpz_ptr)0);
                return done(false);
            }
            mpfr_get_z(cur, a, MPFR_RNDZ);
            if (i == 0) mpz_set(acc, cur);
            else if (f == "gcd") mpz_gcd(acc, acc, cur);
            else mpz_lcm(acc, acc, cur);
        }
        mpfr_set_z(out, acc, R);
        mpz_clears(acc, cur, (mpz_ptr)0);
        return done(true);
    }
    err = L("高精度模式下不支持函数 ", "unsupported function in high-precision mode: ") + f;
    return done(false);
}

bool evalR(const NodePtr &n, long prec, mpfr_t out, std::string &err, int depth) {
    if (!n) {
        err = L("空表达式", "empty expression");
        return false;
    }
    if (depth > MAX_DEPTH) {
        err = L("表达式嵌套过深", "expression too deep");
        return false;
    }
    const mpfr_rnd_t R = MPFR_RNDN;
    switch (n->t) {
        case NT::Num: {
            std::string s = n->num.num().str();
            mpfr_set_str(out, s.c_str(), 10, R);
            std::string d = n->num.den().str();
            if (d != "1") {
                mpfr_t q;
                mpfr_init2(q, prec);
                mpfr_set_str(q, d.c_str(), 10, R);
                mpfr_div(out, out, q, R);
                mpfr_clear(q);
            }
            return true;
        }
        case NT::Var: {
            if (n->name == "pi") {
                mpfr_const_pi(out, R);
                return true;
            }
            if (n->name == "e") {
                mpfr_set_ui(out, 1, R);
                mpfr_exp(out, out, R);
                return true;
            }
            if (n->name == "tau") {
                mpfr_const_pi(out, R);
                mpfr_mul_ui(out, out, 2, R);
                return true;
            }
            err = L("高精度模式下未知量 ", "unknown in high-precision mode: ") + n->name;
            return false;
        }
        case NT::Neg:
            if (!evalR(n->kids[0], prec, out, err, depth + 1)) return false;
            mpfr_neg(out, out, R);
            return true;
        case NT::Add:
        case NT::Sub: {
            mpfr_t a, b;
            mpfr_init2(a, prec);
            mpfr_init2(b, prec);
            if (!evalR(n->kids[0], prec, a, err, depth + 1) ||
                !evalR(n->kids[1], prec, b, err, depth + 1)) {
                mpfr_clears(a, b, (mpfr_ptr)0);
                return false;
            }
            if (n->t == NT::Add) mpfr_add(out, a, b, R);
            else mpfr_sub(out, a, b, R);
            mpfr_clears(a, b, (mpfr_ptr)0);
            return true;
        }
        case NT::Mul:
        case NT::Div: {
            mpfr_t a, b;
            mpfr_init2(a, prec);
            mpfr_init2(b, prec);
            if (!evalR(n->kids[0], prec, a, err, depth + 1) ||
                !evalR(n->kids[1], prec, b, err, depth + 1)) {
                mpfr_clears(a, b, (mpfr_ptr)0);
                return false;
            }
            if (n->t == NT::Mul) {
                mpfr_mul(out, a, b, R);
            } else {
                if (mpfr_zero_p(b)) {
                    err = L("除以零", "division by zero");
                    mpfr_clears(a, b, (mpfr_ptr)0);
                    return false;
                }
                mpfr_div(out, a, b, R);
            }
            mpfr_clears(a, b, (mpfr_ptr)0);
            return true;
        }
        case NT::Pow: {
            mpfr_t a, b;
            mpfr_init2(a, prec);
            mpfr_init2(b, prec);
            if (!evalR(n->kids[0], prec, a, err, depth + 1) ||
                !evalR(n->kids[1], prec, b, err, depth + 1)) {
                mpfr_clears(a, b, (mpfr_ptr)0);
                return false;
            }
            if (mpfr_sgn(a) < 0 && !mpfr_integer_p(b)) {
                err = L("负数的非整数次幂需要复数支持", "negative base with fractional exponent needs complex");
                mpfr_clears(a, b, (mpfr_ptr)0);
                return false;
            }
            mpfr_pow(out, a, b, R);
            mpfr_clears(a, b, (mpfr_ptr)0);
            return true;
        }
        case NT::Fact: {
            mpfr_t a;
            mpfr_init2(a, prec);
            if (!evalR(n->kids[0], prec, a, err, depth + 1)) {
                mpfr_clear(a);
                return false;
            }
            if (mpfr_sgn(a) >= 0 && mpfr_integer_p(a) && mpfr_cmp_ui(a, 100000) <= 0) {
                mpfr_fac_ui(out, mpfr_get_ui(a, MPFR_RNDZ), R);
            } else {
                mpfr_add_ui(a, a, 1, R);
                mpfr_gamma(out, a, R);
            }
            mpfr_clear(a);
            return true;
        }
        case NT::Percent:
            if (!evalR(n->kids[0], prec, out, err, depth + 1)) return false;
            mpfr_div_ui(out, out, 100, R);
            return true;
        case NT::Call:
            return evalFunc(n, prec, out, err, depth);
    }
    err = L("不支持的表达式", "unsupported expression");
    return false;
}

// ---------- MPC 多项式求值 ----------
void polyEvalMpc(const Poly &p, const mpc_t z, long prec, mpc_t out) {
    mpc_set_ui(out, 0, MPC_RNDNN);
    mpc_t t;
    mpc_init2(t, prec);
    for (std::size_t i = p.c.size(); i-- > 0;) {
        mpc_mul(out, out, z, MPC_RNDNN);
        const Rational &c = p.c[i];
        mpfr_set_str(mpc_realref(t), c.num().str().c_str(), 10, MPFR_RNDN);
        mpfr_set_str(mpc_imagref(t), "0", 10, MPFR_RNDN);
        if (c.den() != BigInt(1)) {
            mpfr_t d;
            mpfr_init2(d, prec);
            mpfr_set_str(d, c.den().str().c_str(), 10, MPFR_RNDN);
            mpfr_div(mpc_realref(t), mpc_realref(t), d, MPFR_RNDN);
            mpfr_clear(d);
        }
        mpc_add(out, out, t, MPC_RNDNN);
    }
    mpc_clear(t);
}

} // namespace

bool hpEvalToString(const NodePtr &n, long prec, int digits, std::string &out, std::string &err,
                    SciMode sci, int sciThreshold) {
    if (prec < 64) prec = 64;
    mpfr_t v;
    mpfr_init2(v, prec);
    bool ok = evalR(n, prec, v, err, 0);
    if (ok) out = formatMpfr(v, digits, sci, sciThreshold);
    mpfr_clear(v);
    return ok;
}

bool hpTan(const NodePtr &angle, bool degrees, long prec, int digits, std::string &out,
           std::string &err, SciMode sci, int sciThreshold) {
    if (prec < 64) prec = 64;
    mpfr_t v;
    mpfr_init2(v, prec);
    if (!evalR(angle, prec, v, err, 0)) {
        mpfr_clear(v);
        return false;
    }
    if (degrees) {
        mpfr_t pi;
        mpfr_init2(pi, prec);
        mpfr_const_pi(pi, MPFR_RNDN);
        mpfr_mul(v, v, pi, MPFR_RNDN);
        mpfr_div_ui(v, v, 180, MPFR_RNDN);
        mpfr_clear(pi);
    }
    mpfr_t c;
    mpfr_init2(c, prec);
    mpfr_cos(c, v, MPFR_RNDN);
    if (mpfr_zero_p(c)) {
        err = L("该角度的正切无定义", "tangent undefined at this angle");
        mpfr_clears(v, c, (mpfr_ptr)0);
        return false;
    }
    mpfr_t t;
    mpfr_init2(t, prec);
    mpfr_tan(t, v, MPFR_RNDN);
    out = formatMpfr(t, digits, sci, sciThreshold);
    mpfr_clears(v, c, t, (mpfr_ptr)0);
    return true;
}

bool hpPolishOne(const Poly &p, long double re, long double im, long prec, long double *outRe,
                 long double *outIm, std::string *plainOut, std::string *approxOut, int digits,
                 SciMode sci, int sciThreshold) {
    if (p.degree() < 1) return false;
    if (prec < 64) prec = 64;
    Poly d = p.derivative();
    mpc_t z, f, df, step;
    mpc_init2(z, prec);
    mpc_init2(f, prec);
    mpc_init2(df, prec);
    mpc_init2(step, prec);
    mpfr_set_d(mpc_realref(z), re, MPFR_RNDN);
    mpfr_set_d(mpc_imagref(z), im, MPFR_RNDN);
    mpfr_t tol, absz, tmp;
    mpfr_init2(tol, prec);
    mpfr_init2(absz, prec);
    mpfr_init2(tmp, prec);
    mpfr_set_ui(tol, 1, MPFR_RNDN);
    mpfr_div_2ui(tol, tol, static_cast<unsigned long>(prec - 8), MPFR_RNDN);
    bool converged = false;
    for (int iter = 0; iter < 200; ++iter) {
        if (interruptRequested()) break;
        polyEvalMpc(p, z, prec, f);
        polyEvalMpc(d, z, prec, df);
        mpc_abs(absz, df, MPFR_RNDN);
        if (mpfr_zero_p(absz)) break;
        mpc_div(step, f, df, MPC_RNDNN);
        mpc_sub(z, z, step, MPC_RNDNN);
        mpc_abs(tmp, step, MPFR_RNDN);
        mpc_abs(absz, z, MPFR_RNDN);
        if (mpfr_cmp_ui(absz, 1) < 0) mpfr_set_ui(absz, 1, MPFR_RNDN);
        mpfr_mul(absz, absz, tol, MPFR_RNDN);
        if (mpfr_cmp(tmp, absz) <= 0) {
            converged = true;
            break;
        }
    }
    if (outRe) *outRe = mpfr_get_ld(mpc_realref(z), MPFR_RNDN);
    if (outIm) *outIm = mpfr_get_ld(mpc_imagref(z), MPFR_RNDN);
    std::string pl;
    if (plainOut || approxOut) pl = mpcToPlain(z, digits, sci, sciThreshold);
    if (approxOut) *approxOut = pl;
    if (plainOut) {
        bool realish = (pl.find('i') == std::string::npos);
        *plainOut = realish ? pl : pl;
    }
    mpfr_clears(tol, absz, tmp, (mpfr_ptr)0);
    mpc_clear(z);
    mpc_clear(f);
    mpc_clear(df);
    mpc_clear(step);
    return converged;
}

bool hpPolishRoots(const Poly &p, std::vector<RootOut> &roots, long prec, int digits, SciMode sci,
                   int sciThreshold) {
    if (p.degree() < 1) return false;
    bool any = false;
    for (auto &r : roots) {
        if (r.exact) continue;
        std::string approx;
        long double re = r.numeric.real(), im = r.numeric.imag();
        if (hpPolishOne(p, re, im, prec, &re, &im, nullptr, &approx, digits, sci, sciThreshold)) {
            r.numeric = std::complex<long double>(re, im);
            r.approxPlain = approx;
            r.plain = approx;
            r.latex = approx;
            any = true;
        }
    }
    return any;
}

#else // 未启用 MPFR

bool hpAvailable() { return false; }
const char *hpBackendName() { return "builtin(long double)"; }
std::string hpVersion() { return ""; }

bool hpEvalToString(const NodePtr &, long, int, std::string &, std::string &, SciMode, int) {
    return false;
}
bool hpTan(const NodePtr &, bool, long, int, std::string &, std::string &, SciMode, int) {
    return false;
}
bool hpPolishRoots(const Poly &, std::vector<RootOut> &, long, int, SciMode, int) { return false; }
bool hpPolishOne(const Poly &, long double, long double, long, long double *, long double *,
                 std::string *, std::string *, int, SciMode, int) {
    return false;
}

#endif

} // namespace em

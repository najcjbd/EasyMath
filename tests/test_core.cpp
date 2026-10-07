// EasyMath - 核心单元测试
#include "bigint.hpp"
#include "expr.hpp"
#include "i18n.hpp"
#include "poly.hpp"
#include "rational.hpp"
#include "solve.hpp"
#include "unicode.hpp"

#include <algorithm>
#include <iostream>
#include <random>
#include <string>

using namespace em;

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) {                                                                     \
            ++g_pass;                                                                   \
        } else {                                                                        \
            ++g_fail;                                                                   \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                       \
    do {                                                                                     \
        auto va = (a);                                                                       \
        auto vb = (b);                                                                       \
        if (va == vb) {                                                                      \
            ++g_pass;                                                                        \
        } else {                                                                             \
            ++g_fail;                                                                        \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << " == " << #b \
                      << "  (" << va << " vs " << vb << ")\n";                               \
        }                                                                                    \
    } while (0)

static std::string i128str(__int128 v) {
    if (v == 0) return "0";
    bool neg = v < 0;
    if (neg) v = -v;
    std::string s;
    while (v) {
        s += static_cast<char>('0' + static_cast<int>(v % 10));
        v /= 10;
    }
    if (neg) s += '-';
    std::reverse(s.begin(), s.end());
    return s;
}

static void testBigInt() {
    CHECK_EQ(BigInt(12).str(), std::string("12"));
    CHECK_EQ((BigInt(2) + BigInt(3)).str(), std::string("5"));
    CHECK_EQ((BigInt(2) - BigInt(3)).str(), std::string("-1"));
    CHECK_EQ((BigInt(-2) * BigInt(3)).str(), std::string("-6"));
    CHECK_EQ((BigInt(1000000000LL) * BigInt(1000000000LL)).str(), std::string("1000000000000000000"));
    CHECK_EQ((BigInt(100) / BigInt(7)).str(), std::string("14"));
    CHECK_EQ((BigInt(100) % BigInt(7)).str(), std::string("2"));
    CHECK_EQ(BigInt::gcd(BigInt(462), BigInt(1071)).str(), std::string("21"));
    CHECK_EQ(BigInt::pow(BigInt(2), 100).str(),
             std::string("1267650600228229401496703205376"));
    BigInt r;
    CHECK(BigInt::sqrtExact(BigInt(144), r) && r.str() == "12");
    CHECK(!BigInt::sqrtExact(BigInt(145), r));
    CHECK(BigInt::nthRootExact(BigInt(1000), 3, r) && r.str() == "10");
    CHECK(!BigInt::nthRootExact(BigInt(1001), 3, r));
    CHECK_EQ(BigInt::fromString("-000123").str(), std::string("-123"));
    bool ok = false;
    BigInt::fromString("abc", &ok);
    CHECK(!ok);

    std::mt19937_64 rng(20240918);
    int bad = 0;
    for (int i = 0; i < 20000; ++i) {
        long long a = static_cast<long long>(rng() % (1ULL << 40)) - (1LL << 39);
        long long b = static_cast<long long>(rng() % (1ULL << 40)) - (1LL << 39);
        if (b == 0) b = 1;
        BigInt A(a), B(b);
        if ((A + B).str() != i128str((__int128)a + b)) ++bad;
        if ((A - B).str() != i128str((__int128)a - b)) ++bad;
        if ((A * B).str() != i128str((__int128)a * (__int128)b)) ++bad;
        if ((A / B).str() != i128str((__int128)a / b)) ++bad;
        if ((A % B).str() != i128str((__int128)a % b)) ++bad;
    }
    CHECK_EQ(bad, 0);
}

static void testRational() {
    Rational a(1, 3), b(1, 6);
    CHECK_EQ((a + b).str(), std::string("1/2"));
    CHECK_EQ((a - b).str(), std::string("1/6"));
    CHECK_EQ((a * b).str(), std::string("1/18"));
    CHECK_EQ((a / b).str(), std::string("2"));
    CHECK_EQ(Rational(-2, -4).str(), std::string("1/2"));
    CHECK_EQ(Rational(2, 4).str(), std::string("1/2"));
    CHECK_EQ(Rational(3).pow(3).str(), std::string("27"));
    CHECK_EQ(Rational(2, 3).pow(-1).str(), std::string("3/2"));
    CHECK_EQ(Rational(1, 3).toExactDecimal(), std::string("0.(3)"));
    CHECK_EQ(Rational(7, 8).toExactDecimal(), std::string("0.875"));
    CHECK_EQ(Rational(1, 7).toExactDecimal(), std::string("0.(142857)"));
    CHECK_EQ(Rational(1, 3).toDecimal(4), std::string("0.3333..."));
    CHECK_EQ(Rational(2, 3).toDecimal(4), std::string("0.6666..."));
    CHECK_EQ(Rational(1, 8).toDecimal(2), std::string("0.13")); // 四舍五入
    CHECK_EQ(Rational(-1, 6).toDecimal(5), std::string("-0.16666..."));
    CHECK_EQ(Rational(3, 4).latex(), std::string("\\frac{3}{4}"));
    CHECK(Rational(1, 3) < Rational(1, 2));
    CHECK_EQ(formatLongDouble(2.44948974278L, 8), std::string("2.44948974"));
    BigInt c, rr;
    simplifySurd(BigInt(72), c, rr);
    CHECK(c.str() == "6" && rr.str() == "2");
    simplifySurd(BigInt(50), c, rr);
    CHECK(c.str() == "5" && rr.str() == "2");
}

static void testUnicode() {
    CHECK_EQ(normalize_math("x\u2074=n"), std::string("x^(4)=n"));
    CHECK_EQ(normalize_math("x\u2081=n1"), std::string("x_1=n1"));
    CHECK_EQ(normalize_math("2\u00d73"), std::string("2*3"));
    CHECK_EQ(normalize_math("\u221a6"), std::string("sqrt6"));
    CHECK_EQ(normalize_math("\u6839\u53f76"), std::string("sqrt6"));
    CHECK_EQ(normalize_math("45\u5ea6"), std::string("45\u00b0"));
    CHECK_EQ(normalize_math("45dgree"), std::string("45\u00b0"));
    CHECK_EQ(normalize_math("45degrees"), std::string("45\u00b0"));
    CHECK_EQ(normalize_math("\\frac{1}{2}"), std::string("((1)/(2))"));
    CHECK_EQ(normalize_math("\\sqrt{6}"), std::string("sqrt(6)"));
    CHECK_EQ(normalize_math("\\sqrt[3]{8}"), std::string("((8)^(1/(3)))"));
    CHECK_EQ(normalize_math("x^{4}"), std::string("x^(4)"));
    CHECK_EQ(normalize_math("\\times"), std::string("*"));
    CHECK_EQ(normalize_math("\uff11\uff12"), std::string("12"));
    CHECK_EQ(normalize_math("\U0001d465+\U0001d6fc"), std::string("x+alpha"));
    CHECK_EQ(normalize_math("\u2220"), std::string("\u2220"));
    CHECK_EQ(normalize_math("\\pi"), std::string("pi"));
    CHECK_EQ(normalize_math("\\begin{cases} x=1 \\\\ y=2 \\end{cases}"), std::string(" x=1 , y=2 "));
    CHECK_EQ(normalize_math("\\log_{2}(8)"), std::string("log2(8)"));
    CHECK(is_default_separator(',', '1', '2'));
    CHECK(!is_default_separator('.', '1', '2'));
    CHECK(is_default_separator('.', 'x', 'y'));
    CHECK(is_separator_string(",;@#"));
    CHECK(!is_separator_string("x=1"));
}

static Rational evalExactRational(const std::string &raw, bool &ok) {
    std::string norm = normalize_math(raw);
    ParseOptions po;
    po.declared = scanDeclaredNames(norm);
    std::string err;
    NodePtr n = parseExpression(norm, po, err);
    if (!n) {
        ok = false;
        return Rational();
    }
    Surd s;
    std::string e;
    if (evalExact(n, {}, s, e) && s.isRational()) {
        ok = true;
        return s.coef;
    }
    ok = false;
    return Rational();
}

static void testExpr() {
    struct Case { const char *in; const char *want; };
    const Case cases[] = {
        {"5!", "120"},
        {"6^8", "1679616"},
        {"5!+6^2", "156"},
        {"1/2+1/3", "5/6"},
        {"2+sqrt4", "4"},
        {"2*3+4", "10"},
        {"2^3^2", "512"},
        {"-2^2", "-4"},
        {"(1+2)*3", "9"},
        {"50%", "1/2"},
        {"10%*3", "3/10"},
        {"sqrt(9)+sqrt(16)", "7"},
        {"cbrt(27)", "3"},
        {"max(3,7,5)", "7"},
        {"min(3,7,5)", "3"},
        {"gcd(12,18)", "6"},
        {"lcm(4,6)", "12"},
        {"abs(-3)", "3"},
        {"floor(7/2)", "3"},
        {"round(7/2)", "4"},
        {"log(100)", "2"},
        {"log2(8)", "3"},
        {"\u221a2*\u221a2", "2"},
        {"2\u00d73", "6"},
        {"\u222b(1,2,3)", nullptr},
    };
    for (const auto &c : cases) {
        bool ok = false;
        Rational got = evalExactRational(c.in, ok);
        if (c.want == nullptr) {
            CHECK(!ok);
            continue;
        }
        CHECK(ok);
        if (ok) CHECK_EQ(got.str(), std::string(c.want));
    }
    // 数值模式下 5x6 = 30 (x 作为乘号)
    {
        std::string norm = normalize_math("5x6");
        ParseOptions po;
        po.numeric_only = true;
        std::string err;
        NodePtr n = parseExpression(norm, po, err);
        CHECK(n != nullptr);
        long double v = 0;
        CHECK(evalApprox(n, {}, v, err));
        CHECK_EQ(formatLongDouble(v, 0), std::string("30"));
    }
    // 符号模式下 2x 是 2*x
    {
        std::string norm = normalize_math("2x");
        ParseOptions po;
        po.declared = scanDeclaredNames(norm);
        std::string err;
        NodePtr n = parseExpression(norm, po, err);
        CHECK(n != nullptr);
        std::map<std::string, long double> env{{"x", 4}};
        long double v = 0;
        CHECK(evalApprox(n, env, v, err));
        CHECK_EQ(formatLongDouble(v, 0), std::string("8"));
    }
    // sin(30°) 精确 = 1/2
    {
        bool ok = false;
        CHECK_EQ(evalExactRational("sin(30\u00b0)", ok).str(), std::string("1/2"));
        CHECK(ok);
        CHECK_EQ(evalExactRational("sin(pi/6)", ok).str(), std::string("1/2"));
        CHECK_EQ(evalExactRational("cos(60\u00b0)", ok).str(), std::string("1/2"));
        CHECK_EQ(evalExactRational("tan(45\u00b0)", ok).str(), std::string("1"));
    }
    // AST -> Poly
    {
        std::string n2 = normalize_math("x^2+2x+1");
        ParseOptions po;
        po.declared = scanDeclaredNames(n2);
        std::string err;
        NodePtr n = parseExpression(n2, po, err);
        Poly p;
        CHECK(astToPoly(n, "x", p, err));
        CHECK_EQ(p.toPlain("x"), std::string("x^2 + 2x + 1"));
        CHECK_EQ(p.toLatex("x"), std::string("x^{2} + 2x + 1"));
    }
    // 拉格朗日
    {
        std::vector<std::pair<Rational, Rational>> pts = {{1, 3}, {2, 5}, {3, 9}};
        Poly p;
        std::string err;
        CHECK(lagrangePolynomial(pts, p, err));
        CHECK_EQ(p.toPlain("x"), std::string("x^2 - x + 3"));
        CHECK_EQ(p.eval(Rational(4)).str(), std::string("15"));
        std::vector<std::pair<Rational, Rational>> dup = {{1, 3}, {1, 4}};
        CHECK(!lagrangePolynomial(dup, p, err));
    }
    CHECK_EQ(superscriptNumber(4), std::string("\u2074"));
    CHECK_EQ(superscriptNumber(12), std::string("\u00b9\u00b2"));
}

static void testSolve() {
    SolveOptions o;
    o.decimals = 8;
    {
        auto r = solveEquations({"x^4=5"}, o);
        CHECK(r.ok);
        CHECK_EQ(static_cast<int>(r.roots.size()), 4);
        int cplx = 0, real = 0;
        for (auto &x : r.roots) (x.isComplex ? cplx : real)++;
        CHECK_EQ(real, 2);
        CHECK_EQ(cplx, 2);
    }
    {
        auto r = solveEquations({"x^2+1=0"}, o);
        CHECK(r.ok);
        CHECK_EQ(static_cast<int>(r.roots.size()), 2);
        CHECK(r.roots[0].plain == "-i");
        CHECK(r.roots[1].plain == "i");
    }
    {
        auto r = solveEquations({"x^2-2=0"}, o);
        CHECK(r.ok);
        CHECK_EQ(r.roots[0].plain, std::string("-\u221a2"));
        CHECK_EQ(r.roots[1].plain, std::string("\u221a2"));
    }
    {
        auto r = solveEquations({"x^2-2x+1=0"}, o);
        CHECK(r.ok);
        CHECK_EQ(static_cast<int>(r.roots.size()), 1);
        CHECK_EQ(r.roots[0].mult, 2);
    }
    {
        auto r = solveEquations({"x^3-6x^2+11x-6=0"}, o);
        CHECK(r.ok);
        CHECK_EQ(static_cast<int>(r.roots.size()), 3);
    }
    {
        auto r = solveEquations({"y=5x", "y=6z", "x=2z"}, o);
        CHECK(r.ok);
        CHECK(!r.infinite && !r.none);
        CHECK_EQ(static_cast<int>(r.solutionPlain.size()), 3);
    }
    {
        auto r = solveEquations({"y=6z", "x=2z"}, o);
        CHECK(r.ok);
        CHECK(r.infinite);
        CHECK_EQ(static_cast<int>(r.params.size()), 1);
    }
    {
        auto r = solveEquations({"x+y=1", "x+y=2"}, o);
        CHECK(r.ok);
        CHECK(r.none);
    }
    {
        auto r = solveEquations({"x+y=5", "x*y=6"}, o);
        CHECK(r.ok);
        CHECK_EQ(static_cast<int>(r.solutionPlain.size()), 2);
    }
    {
        auto r = solveEquations({"2x+3=7"}, o);
        CHECK(r.ok);
        CHECK_EQ(r.roots[0].plain, std::string("2"));
    }
    {
        auto r = solveEquations({"\u222b"}, o);
        (void)r;
    }
    {
        // LaTeX + 分式
        auto r = solveEquations({normalize_math("\\frac{x^2}{2}=2")}, o);
        CHECK(r.ok);
        CHECK_EQ(static_cast<int>(r.roots.size()), 2);
    }
    {
        auto r = solveEquations({"x^2=4", "x=2"}, o);
        CHECK(r.ok);
        CHECK_EQ(static_cast<int>(r.roots.size()), 1);
        CHECK_EQ(r.roots[0].plain, std::string("2"));
    }
}

static void testI18n() {
    setLang("zh");
    CHECK_EQ(L("A", "B"), std::string("A"));
    setLang("en");
    CHECK_EQ(L("A", "B"), std::string("B"));
    setLang("zh");
}

int main() {
    testBigInt();
    testRational();
    testUnicode();
    testExpr();
    testSolve();
    testI18n();
    std::cout << "\n通过 " << g_pass << " 项, 失败 " << g_fail << " 项\n";
    return g_fail == 0 ? 0 : 1;
}

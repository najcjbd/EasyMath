// EasyMath - 有理系数多项式
#pragma once

#include "expr.hpp"
#include "rational.hpp"

#include <string>
#include <utility>
#include <vector>

namespace em {

class Poly {
public:
    std::vector<Rational> c; // c[i] 为 x^i 的系数

    Poly() {}
    explicit Poly(const std::vector<Rational> &coef) : c(coef) { trim(); }
    explicit Poly(const Rational &r) : c{r} { trim(); }

    static Poly X() { return Poly(std::vector<Rational>{Rational(0), Rational(1)}); }
    static Poly constant(const Rational &r) { return Poly(r); }

    void trim();
    bool isZero() const { return c.empty(); }
    int degree() const { return static_cast<int>(c.size()) - 1; }
    Rational coeff(int i) const { return (i >= 0 && i < static_cast<int>(c.size())) ? c[i] : Rational(0); }
    Rational leading() const { return c.empty() ? Rational(0) : c.back(); }
    Rational constantTerm() const { return coeff(0); }

    Rational eval(const Rational &x) const;
    long double evalApprox(long double x) const;
    std::vector<Rational> evalAll(const std::vector<Rational> &xs) const;

    Poly operator+(const Poly &o) const;
    Poly operator-(const Poly &o) const;
    Poly operator*(const Poly &o) const;
    Poly operator-() const;
    Poly scale(const Rational &k) const;
    Poly pow(int e) const;

    void divmod(const Poly &b, Poly &q, Poly &r) const;
    Poly derivative() const;
    Poly monic() const;
    Poly multiplyLinear(const Rational &root) const; // *(x - root)
    Poly compose(const Poly &inner) const;

    // 系数化为整数(乘以最小公分母)
    Poly toIntegerCoefficients(BigInt &content) const;

    std::string toPlain(const std::string &var) const;
    std::string toPretty(const std::string &var) const; // 上标
    std::string toLatex(const std::string &var) const;
    std::string toSympy(const std::string &var) const; // SymPy 语法

private:
    void trimZero();
};

// AST -> 单变量多项式
bool astToPoly(const NodePtr &n, const std::string &var, Poly &out, std::string &err);

// 拉格朗日插值: 点 (xi, yi), 要求 xi 互不相同
bool lagrangePolynomial(const std::vector<std::pair<Rational, Rational>> &pts, Poly &out,
                        std::string &err);

// 字符串工具
std::string superscriptNumber(long long n);
std::string polyTermPlain(const Rational &coef, int deg, const std::string &var, bool first);

} // namespace em

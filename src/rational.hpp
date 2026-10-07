// EasyMath - 精确有理数
#pragma once

#include "bigint.hpp"

#include <string>
#include <vector>

namespace em {

class Rational {
public:
    Rational() : num_(0), den_(1) {}
    Rational(long long v) : num_(v), den_(1) {} // NOLINT
    Rational(const BigInt &n, const BigInt &d = BigInt(1)) : num_(n), den_(d) { normalize(); }

    static Rational fromDecimalString(const std::string &s, bool *ok = nullptr);
    static Rational fromDouble(long double x, long long maxDenominator = 1000000000LL);
    static Rational parse(const std::string &s, bool *ok = nullptr);

    const BigInt &num() const { return num_; }
    const BigInt &den() const { return den_; }

    bool isZero() const { return num_.isZero(); }
    bool isOne() const { return num_ == BigInt(1) && den_ == BigInt(1); }
    bool isInteger() const { return den_ == BigInt(1); }
    bool isNeg() const { return num_.isNeg(); }
    bool isEvenInteger() const { return isInteger() && num_.isEven(); }
    int sign() const { return num_.sign(); }

    Rational abs() const { return isNeg() ? -(*this) : *this; }
    Rational operator-() const { return Rational(-num_, den_); }
    Rational reciprocal() const { return Rational(den_, num_); }

    friend Rational operator+(const Rational &a, const Rational &b);
    friend Rational operator-(const Rational &a, const Rational &b);
    friend Rational operator*(const Rational &a, const Rational &b);
    friend Rational operator/(const Rational &a, const Rational &b);

    Rational &operator+=(const Rational &o) { return *this = *this + o; }
    Rational &operator-=(const Rational &o) { return *this = *this - o; }
    Rational &operator*=(const Rational &o) { return *this = *this * o; }
    Rational &operator/=(const Rational &o) { return *this = *this / o; }

    Rational pow(long long e) const;
    bool isPerfectSquare(Rational &root) const;

    int cmp(const Rational &o) const;
    bool operator==(const Rational &o) const { return cmp(o) == 0; }
    bool operator!=(const Rational &o) const { return cmp(o) != 0; }
    bool operator<(const Rational &o) const { return cmp(o) < 0; }
    bool operator<=(const Rational &o) const { return cmp(o) <= 0; }
    bool operator>(const Rational &o) const { return cmp(o) > 0; }
    bool operator>=(const Rational &o) const { return cmp(o) >= 0; }

    std::string str() const;
    std::string latex() const;
    std::string mixedStr() const;
    long double toLongDouble() const;
    bool fitsLongLong(long long &out) const;

    std::string toDecimal(int digits, bool *exact = nullptr, std::string *repeating = nullptr) const;
    std::string toExactDecimal() const;

private:
    BigInt num_;
    BigInt den_;

    void normalize();
};

// ============ 数值输出格式(含科学计数法策略) ============
enum class SciMode { Auto, Always, Never };

struct NumberFormat {
    int digits = 8;          // 小数位数
    SciMode sci = SciMode::Auto;
    int sciThreshold = 12;   // |v| >= 10^threshold 时用科学计数法
    bool trimZeros = true;
    int maxSignificant = 0;  // >0: 后端有效位上限(超出部分不可信)
};

// 按策略格式化: 过大/过小的近似值改用科学计数法
std::string formatNumber(long double v, const NumberFormat &nf);

// 定点格式化 long double (四舍五入到 digits 位, 可去尾零)
std::string formatLongDouble(long double v, int digits, bool trimZeros = true);

// 本平台 long double 的十进制有效位 (Windows/macOS-arm64 上等于 double)
int longDoubleDigits10();
bool longDoubleIsDouble();
void simplifySurd(const BigInt &D, BigInt &coef, BigInt &rad);
const std::vector<long long> &smallPrimes(std::size_t n);

} // namespace em

// EasyMath - 任意精度整数: GNU MP 后端 (编译时 -DEASYMATH_USE_GMP)
#ifdef EASYMATH_USE_GMP

#include "bigint.hpp"

#include <cctype>
#include <limits>
#include <cstdlib>

namespace em {

const char *BigInt::backendName() { return "GMP"; }
bool BigInt::usesGmp() { return true; }

BigInt::BigInt(long long v) {
    // arm64 上 long long 与 long 不同型, mpz_class 的算术类型构造是二义的
    if (v >= static_cast<long long>(std::numeric_limits<long>::min()) &&
        v <= static_cast<long long>(std::numeric_limits<long>::max()))
        v_ = static_cast<long>(v);
    else
        v_ = mpz_class(std::to_string(v), 10);
}

BigInt::BigInt(const std::string &decimal, bool *ok) { *this = fromString(decimal, ok); }

BigInt BigInt::fromString(const std::string &s, bool *ok) {
    BigInt r;
    std::string t;
    for (char c : s) {
        if (c == '_' || c == ',' || std::isspace(static_cast<unsigned char>(c))) continue;
        t += c;
    }
    if (t.empty()) {
        if (ok) *ok = false;
        return r;
    }
    std::size_t i = 0;
    bool neg = false;
    if (t[0] == '+' || t[0] == '-') {
        neg = (t[0] == '-');
        i = 1;
    }
    if (i >= t.size()) {
        if (ok) *ok = false;
        return r;
    }
    for (std::size_t j = i; j < t.size(); ++j) {
        if (!std::isdigit(static_cast<unsigned char>(t[j]))) {
            if (ok) *ok = false;
            return r;
        }
    }
    r.v_ = mpz_class(t.substr(i), 10);
    if (neg) r.v_ = -r.v_;
    if (ok) *ok = true;
    return r;
}

std::string BigInt::str() const { return v_.get_str(); }

long double BigInt::toLongDouble() const { return std::strtold(v_.get_str().c_str(), nullptr); }

bool BigInt::fitsLongLong(long long &out) const {
    static const mpz_class lo(std::string("-9223372036854775808"));
    static const mpz_class hi(std::string("9223372036854775807"));
    if (v_ < lo || v_ > hi) return false;
    if (sizeof(long) >= sizeof(long long)) {
        out = static_cast<long long>(v_.get_si());
    } else {
        out = std::strtoll(v_.get_str().c_str(), nullptr, 10);
    }
    return true;
}

std::size_t BigInt::decimalDigits() const {
    if (v_ == 0) return 1;
    return static_cast<std::size_t>(mpz_sizeinbase(v_.get_mpz_t(), 10));
}

BigInt BigInt::abs() const {
    BigInt r;
    r.v_ = ::abs(v_);
    return r;
}

BigInt BigInt::operator-() const {
    BigInt r;
    r.v_ = -v_;
    return r;
}

int BigInt::cmp(const BigInt &o) const { return mpz_cmp(v_.get_mpz_t(), o.v_.get_mpz_t()); }

BigInt operator+(const BigInt &a, const BigInt &b) {
    BigInt r;
    r.v_ = a.v_ + b.v_;
    return r;
}
BigInt operator-(const BigInt &a, const BigInt &b) {
    BigInt r;
    r.v_ = a.v_ - b.v_;
    return r;
}
BigInt operator*(const BigInt &a, const BigInt &b) {
    BigInt r;
    r.v_ = a.v_ * b.v_;
    return r;
}
BigInt operator/(const BigInt &a, const BigInt &b) {
    BigInt r;
    if (b.v_ == 0) return r;
    r.v_ = a.v_ / b.v_;
    return r;
}
BigInt operator%(const BigInt &a, const BigInt &b) {
    BigInt r;
    if (b.v_ == 0) return r;
    r.v_ = a.v_ % b.v_;
    return r;
}

void BigInt::divmod(const BigInt &a, const BigInt &b, BigInt &q, BigInt &r) {
    if (b.v_ == 0) {
        q = BigInt();
        r = BigInt();
        return;
    }
    // GMP 不允许目的操作数与源操作数重叠(内部实现里 r 常与 a 复用同一对象)
    if (&q == &a || &q == &b || &r == &a || &r == &b) {
        mpz_class qq, rr;
        mpz_tdiv_qr(qq.get_mpz_t(), rr.get_mpz_t(), a.v_.get_mpz_t(), b.v_.get_mpz_t());
        q.v_ = qq;
        r.v_ = rr;
        return;
    }
    mpz_tdiv_qr(q.v_.get_mpz_t(), r.v_.get_mpz_t(), a.v_.get_mpz_t(), b.v_.get_mpz_t());
}

BigInt BigInt::gcd(BigInt a, BigInt b) {
    BigInt r;
    mpz_gcd(r.v_.get_mpz_t(), a.v_.get_mpz_t(), b.v_.get_mpz_t());
    return r;
}

BigInt BigInt::lcm(const BigInt &a, const BigInt &b) {
    BigInt r;
    if (a.v_ == 0 || b.v_ == 0) return r;
    mpz_lcm(r.v_.get_mpz_t(), a.v_.get_mpz_t(), b.v_.get_mpz_t());
    return r;
}

BigInt BigInt::pow(const BigInt &base, unsigned long long exp) {
    BigInt r;
    mpz_pow_ui(r.v_.get_mpz_t(), base.v_.get_mpz_t(), static_cast<unsigned long>(exp));
    return r;
}

bool BigInt::sqrtExact(const BigInt &n, BigInt &out) {
    if (n.rawSign() < 0) return false;
    if (n.v_ == 0) {
        out = BigInt();
        return true;
    }
    if (!mpz_perfect_square_p(n.v_.get_mpz_t())) return false;
    BigInt r;
    mpz_sqrt(r.v_.get_mpz_t(), n.v_.get_mpz_t());
    out = r;
    return true;
}

bool BigInt::nthRootExact(const BigInt &n, unsigned k, BigInt &out) {
    if (k == 0) return false;
    if (k == 1) {
        out = n;
        return true;
    }
    if (n.v_ == 0) {
        out = BigInt();
        return true;
    }
    if (n.rawSign() < 0 && k % 2 == 0) return false;
    BigInt m = n.abs();
    BigInt r;
    int exact = mpz_root(r.v_.get_mpz_t(), m.v_.get_mpz_t(), static_cast<unsigned long>(k));
    if (!exact) return false;
    out = (n.rawSign() < 0) ? -r : r;
    return true;
}

} // namespace em

#endif // EASYMATH_USE_GMP

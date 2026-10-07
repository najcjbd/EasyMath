// EasyMath - 任意精度整数
// 两套后端: 自带实现(base 1e9) 或 GNU MP (编译时 -DEASYMATH_USE_GMP)
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#ifdef EASYMATH_USE_GMP
#include <gmpxx.h>
#endif

namespace em {

class BigInt {
public:
    BigInt() = default;
    BigInt(long long v); // NOLINT
    explicit BigInt(const std::string &decimal, bool *ok = nullptr);

    static BigInt fromString(const std::string &s, bool *ok = nullptr);
    static BigInt pow(const BigInt &base, unsigned long long exp);

    bool isZero() const { return rawIsZero(); }
    bool isNeg() const { return rawSign() < 0; }
    bool isEven() const { return rawIsEven(); }
    int sign() const { return rawSign(); }

    std::string str() const;
    long double toLongDouble() const;
    bool fitsLongLong(long long &out) const;

    BigInt abs() const;
    BigInt operator-() const;

    int cmp(const BigInt &o) const;
    bool operator==(const BigInt &o) const { return cmp(o) == 0; }
    bool operator!=(const BigInt &o) const { return cmp(o) != 0; }
    bool operator<(const BigInt &o) const { return cmp(o) < 0; }
    bool operator<=(const BigInt &o) const { return cmp(o) <= 0; }
    bool operator>(const BigInt &o) const { return cmp(o) > 0; }
    bool operator>=(const BigInt &o) const { return cmp(o) >= 0; }

    friend BigInt operator+(const BigInt &a, const BigInt &b);
    friend BigInt operator-(const BigInt &a, const BigInt &b);
    friend BigInt operator*(const BigInt &a, const BigInt &b);
    friend BigInt operator/(const BigInt &a, const BigInt &b);
    friend BigInt operator%(const BigInt &a, const BigInt &b);

    static void divmod(const BigInt &a, const BigInt &b, BigInt &q, BigInt &r);
    static BigInt gcd(BigInt a, BigInt b);
    static BigInt lcm(const BigInt &a, const BigInt &b);

    static bool sqrtExact(const BigInt &n, BigInt &out);
    static bool nthRootExact(const BigInt &n, unsigned k, BigInt &out);

    std::size_t decimalDigits() const;

    // 后端信息
    static const char *backendName();
    static bool usesGmp();

private:
#ifdef EASYMATH_USE_GMP
    mpz_class v_{0};
    int rawSign() const { return mpz_sgn(v_.get_mpz_t()); }
    bool rawIsZero() const { return v_ == 0; }
    bool rawIsEven() const { return mpz_even_p(v_.get_mpz_t()) != 0; }
#else
    static const uint32_t BASE = 1000000000u;
    static const int BASE_DIGITS = 9;

    int sign_ = 0;
    std::vector<uint32_t> mag_;

    int rawSign() const { return sign_; }
    bool rawIsZero() const { return sign_ == 0; }
    bool rawIsEven() const { return mag_.empty() || (mag_[0] % 2u) == 0u; }

    void trim();
    static void trim(std::vector<uint32_t> &v);
    static int cmpMag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b);
    static std::vector<uint32_t> addMag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b);
    static std::vector<uint32_t> subMag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b);
    static std::vector<uint32_t> mulMag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b);
    static void divmodMag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b,
                          std::vector<uint32_t> &q, std::vector<uint32_t> &r);
#endif
};

} // namespace em

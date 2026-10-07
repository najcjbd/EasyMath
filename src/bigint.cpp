// EasyMath - 任意精度整数: 自带后端 (未启用 GMP 时编译)
#ifndef EASYMATH_USE_GMP

// EasyMath - 任意精度整数实现
#include "bigint.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace em {

const char *BigInt::backendName() { return "builtin"; }
bool BigInt::usesGmp() { return false; }

void BigInt::trim() {
    trim(mag_);
    if (mag_.empty()) sign_ = 0;
}

void BigInt::trim(std::vector<uint32_t> &v) {
    while (!v.empty() && v.back() == 0) v.pop_back();
}

BigInt::BigInt(long long v) {
    if (v == 0) return;
    sign_ = v < 0 ? -1 : 1;
    unsigned long long m = (v < 0) ? (0ULL - static_cast<unsigned long long>(v))
                                   : static_cast<unsigned long long>(v);
    while (m > 0) {
        mag_.push_back(static_cast<uint32_t>(m % BASE));
        m /= BASE;
    }
}

BigInt::BigInt(const std::string &decimal, bool *ok) { *this = fromString(decimal, ok); }

BigInt BigInt::fromString(const std::string &s, bool *ok) {
    BigInt r;
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    int sg = 1;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        if (s[i] == '-') sg = -1;
        ++i;
    }
    std::string digits;
    for (; i < s.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(s[i]))) digits += s[i];
        else if (s[i] == '_' || s[i] == ',') continue;
        else break;
    }
    if (digits.empty()) {
        if (ok) *ok = false;
        return r;
    }
    std::size_t nz = digits.find_first_not_of('0');
    if (nz == std::string::npos) {
        if (ok) *ok = true;
        return r;
    }
    digits = digits.substr(nz);
    for (std::size_t end = digits.size(); end > 0;) {
        std::size_t start = (end >= static_cast<std::size_t>(BASE_DIGITS)) ? end - BASE_DIGITS : 0;
        uint32_t limb = static_cast<uint32_t>(std::stoul(digits.substr(start, end - start)));
        r.mag_.push_back(limb);
        end = start;
    }
    r.trim();
    r.sign_ = r.mag_.empty() ? 0 : sg;
    if (ok) *ok = true;
    return r;
}

std::string BigInt::str() const {
    if (sign_ == 0) return "0";
    std::string out;
    if (sign_ < 0) out += '-';
    out += std::to_string(mag_.back());
    for (std::size_t i = mag_.size() - 1; i-- > 0;) {
        std::string g = std::to_string(mag_[i]);
        out.append(static_cast<std::size_t>(BASE_DIGITS) - g.size(), '0');
        out += g;
    }
    return out;
}

long double BigInt::toLongDouble() const {
    long double r = 0;
    for (std::size_t i = mag_.size(); i-- > 0;) r = r * static_cast<long double>(BASE) + mag_[i];
    return sign_ < 0 ? -r : r;
}

bool BigInt::fitsLongLong(long long &out) const {
    if (mag_.size() > 3) return false;
    unsigned long long v = 0;
    for (std::size_t i = mag_.size(); i-- > 0;) v = v * BASE + mag_[i];
    if (sign_ >= 0) {
        if (v > 9223372036854775807ULL) return false;
        out = static_cast<long long>(v);
    } else {
        if (v > 9223372036854775808ULL) return false;
        out = (v == 9223372036854775808ULL) ? (-9223372036854775807LL - 1) : -static_cast<long long>(v);
    }
    return true;
}

std::size_t BigInt::decimalDigits() const {
    if (sign_ == 0) return 1;
    std::size_t d = (mag_.size() - 1) * static_cast<std::size_t>(BASE_DIGITS);
    uint32_t top = mag_.back();
    while (top > 0) {
        ++d;
        top /= 10;
    }
    return d;
}

BigInt BigInt::abs() const {
    BigInt r = *this;
    if (r.sign_ < 0) r.sign_ = 1;
    return r;
}

BigInt BigInt::operator-() const {
    BigInt r = *this;
    r.sign_ = -r.sign_;
    return r;
}

int BigInt::cmpMag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (std::size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

int BigInt::cmp(const BigInt &o) const {
    if (sign_ != o.sign_) return sign_ < o.sign_ ? -1 : 1;
    if (sign_ == 0) return 0;
    int c = cmpMag(mag_, o.mag_);
    return sign_ < 0 ? -c : c;
}

std::vector<uint32_t> BigInt::addMag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    std::vector<uint32_t> r;
    std::size_t n = std::max(a.size(), b.size());
    r.reserve(n + 1);
    uint32_t carry = 0;
    for (std::size_t i = 0; i < n; ++i) {
        uint64_t s = carry;
        if (i < a.size()) s += a[i];
        if (i < b.size()) s += b[i];
        r.push_back(static_cast<uint32_t>(s % BASE));
        carry = static_cast<uint32_t>(s / BASE);
    }
    if (carry) r.push_back(carry);
    return r;
}

std::vector<uint32_t> BigInt::subMag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    std::vector<uint32_t> r;
    r.reserve(a.size());
    int64_t borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        int64_t cur = static_cast<int64_t>(a[i]) - borrow - (i < b.size() ? b[i] : 0);
        if (cur < 0) {
            cur += BASE;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r.push_back(static_cast<uint32_t>(cur));
    }
    trim(r);
    return r;
}

std::vector<uint32_t> BigInt::mulMag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    if (a.empty() || b.empty()) return {};
    std::vector<uint32_t> r(a.size() + b.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        uint64_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            uint64_t cur = static_cast<uint64_t>(r[i + j]) + static_cast<uint64_t>(a[i]) * b[j] + carry;
            r[i + j] = static_cast<uint32_t>(cur % BASE);
            carry = cur / BASE;
        }
        std::size_t k = i + b.size();
        while (carry && k < r.size()) {
            uint64_t cur = static_cast<uint64_t>(r[k]) + carry;
            r[k] = static_cast<uint32_t>(cur % BASE);
            carry = cur / BASE;
            ++k;
        }
    }
    trim(r);
    return r;
}

void BigInt::divmodMag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b,
                       std::vector<uint32_t> &q, std::vector<uint32_t> &r) {
    q.clear();
    r.clear();
    if (b.empty()) return;
    int c = cmpMag(a, b);
    if (c < 0) {
        r = a;
        return;
    }
    if (b.size() == 1) {
        uint64_t d = b[0], rem = 0;
        q.assign(a.size(), 0);
        for (std::size_t i = a.size(); i-- > 0;) {
            uint64_t cur = rem * BASE + a[i];
            q[i] = static_cast<uint32_t>(cur / d);
            rem = cur % d;
        }
        trim(q);
        if (rem) r.push_back(static_cast<uint32_t>(rem));
        return;
    }
    int n = static_cast<int>(b.size());
    int m = static_cast<int>(a.size()) - n;
    uint32_t d = static_cast<uint32_t>(BASE / (static_cast<uint64_t>(b.back()) + 1));
    std::vector<uint32_t> v(b.size()), u(a.size() + 1, 0);
    {
        uint64_t carry = 0;
        for (std::size_t i = 0; i < b.size(); ++i) {
            uint64_t cur = static_cast<uint64_t>(b[i]) * d + carry;
            v[i] = static_cast<uint32_t>(cur % BASE);
            carry = cur / BASE;
        }
        carry = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            uint64_t cur = static_cast<uint64_t>(a[i]) * d + carry;
            u[i] = static_cast<uint32_t>(cur % BASE);
            carry = cur / BASE;
        }
        u[a.size()] = static_cast<uint32_t>(carry);
    }
    q.assign(static_cast<std::size_t>(m) + 1, 0);
    for (int j = m; j >= 0; --j) {
        uint64_t num = static_cast<uint64_t>(u[j + n]) * BASE + u[j + n - 1];
        uint64_t qhat = num / v[n - 1];
        uint64_t rhat = num % v[n - 1];
        while (qhat >= BASE || qhat * v[n - 2] > rhat * BASE + u[j + n - 2]) {
            --qhat;
            rhat += v[n - 1];
            if (rhat >= BASE) break;
        }
        uint64_t carry = 0;
        int64_t borrow = 0;
        for (int i = 0; i < n; ++i) {
            uint64_t p = qhat * v[i] + carry;
            carry = p / BASE;
            uint64_t p2 = p % BASE;
            int64_t sub = static_cast<int64_t>(u[i + j]) - static_cast<int64_t>(p2) - borrow;
            if (sub < 0) {
                sub += BASE;
                borrow = 1;
            } else {
                borrow = 0;
            }
            u[i + j] = static_cast<uint32_t>(sub);
        }
        int64_t sub = static_cast<int64_t>(u[j + n]) - static_cast<int64_t>(carry) - borrow;
        if (sub < 0) {
            sub += BASE;
            borrow = 1;
        } else {
            borrow = 0;
        }
        u[j + n] = static_cast<uint32_t>(sub);
        if (borrow) {
            --qhat;
            uint64_t c2 = 0;
            for (int i = 0; i < n; ++i) {
                uint64_t s = static_cast<uint64_t>(u[i + j]) + v[i] + c2;
                u[i + j] = static_cast<uint32_t>(s % BASE);
                c2 = s / BASE;
            }
            u[j + n] = static_cast<uint32_t>((u[j + n] + c2) % BASE);
        }
        q[static_cast<std::size_t>(j)] = static_cast<uint32_t>(qhat);
    }
    trim(q);
    std::vector<uint32_t> rr(u.begin(), u.begin() + n);
    if (d > 1) {
        uint64_t rem = 0;
        for (std::size_t i = rr.size(); i-- > 0;) {
            uint64_t cur = rem * BASE + rr[i];
            rr[i] = static_cast<uint32_t>(cur / d);
            rem = cur % d;
        }
    }
    trim(rr);
    r = rr;
}

BigInt operator+(const BigInt &a, const BigInt &b) {
    BigInt r;
    if (a.sign_ == 0) return b;
    if (b.sign_ == 0) return a;
    if (a.sign_ == b.sign_) {
        r.mag_ = BigInt::addMag(a.mag_, b.mag_);
        r.sign_ = a.sign_;
        r.trim();
        return r;
    }
    int c = BigInt::cmpMag(a.mag_, b.mag_);
    if (c == 0) return r;
    if (c > 0) {
        r.mag_ = BigInt::subMag(a.mag_, b.mag_);
        r.sign_ = a.sign_;
    } else {
        r.mag_ = BigInt::subMag(b.mag_, a.mag_);
        r.sign_ = b.sign_;
    }
    r.trim();
    return r;
}

BigInt operator-(const BigInt &a, const BigInt &b) { return a + (-b); }

BigInt operator*(const BigInt &a, const BigInt &b) {
    BigInt r;
    if (a.sign_ == 0 || b.sign_ == 0) return r;
    r.mag_ = BigInt::mulMag(a.mag_, b.mag_);
    r.sign_ = a.sign_ * b.sign_;
    r.trim();
    return r;
}

void BigInt::divmod(const BigInt &a, const BigInt &b, BigInt &q, BigInt &r) {
    if (b.sign_ == 0) {
        q = BigInt();
        r = BigInt();
        return;
    }
    std::vector<uint32_t> qm, rm;
    divmodMag(a.mag_, b.mag_, qm, rm);
    q.mag_ = qm;
    q.sign_ = qm.empty() ? 0 : a.sign_ * b.sign_;
    q.trim();
    r.mag_ = rm;
    r.sign_ = rm.empty() ? 0 : a.sign_;
    r.trim();
}

BigInt operator/(const BigInt &a, const BigInt &b) {
    BigInt q, r;
    BigInt::divmod(a, b, q, r);
    return q;
}

BigInt operator%(const BigInt &a, const BigInt &b) {
    BigInt q, r;
    BigInt::divmod(a, b, q, r);
    return r;
}

BigInt BigInt::gcd(BigInt a, BigInt b) {
    a = a.abs();
    b = b.abs();
    while (!b.isZero()) {
        BigInt q, r;
        divmod(a, b, q, r);
        a = b;
        b = r;
    }
    return a;
}

BigInt BigInt::lcm(const BigInt &a, const BigInt &b) {
    if (a.isZero() || b.isZero()) return BigInt();
    BigInt g = gcd(a, b);
    return (a / g * b).abs();
}

BigInt BigInt::pow(const BigInt &base, unsigned long long exp) {
    BigInt result(1), b = base;
    while (exp) {
        if (exp & 1ULL) result = result * b;
        exp >>= 1ULL;
        if (exp) b = b * b;
    }
    return result;
}

bool BigInt::sqrtExact(const BigInt &n, BigInt &out) {
    if (n.isNeg()) return false;
    if (n.isZero()) {
        out = BigInt();
        return true;
    }
    long double approx = n.toLongDouble();
    if (!(approx > 0) || approx > 1e300L) return false;
    BigInt x(static_cast<long long>(std::sqrt(approx)));
    if (x.isZero()) x = BigInt(1);
    for (int i = 0; i < 300; ++i) {
        BigInt nx = (x + n / x) / BigInt(2);
        if (nx >= x) break;
        x = nx;
    }
    while (x * x < n) x = x + BigInt(1);
    while (x * x > n) x = x - BigInt(1);
    if (x * x == n) {
        out = x;
        return true;
    }
    return false;
}

bool BigInt::nthRootExact(const BigInt &n, unsigned k, BigInt &out) {
    if (k == 0) return false;
    if (k == 1) {
        out = n;
        return true;
    }
    if (n.isNeg()) {
        if (k % 2 == 0) return false;
        BigInt p;
        if (!nthRootExact(-n, k, p)) return false;
        out = -p;
        return true;
    }
    if (n.isZero()) {
        out = BigInt();
        return true;
    }
    long double approx = n.toLongDouble();
    long double x0 = std::pow(approx, 1.0L / static_cast<long double>(k));
    if (!(x0 > 0) || x0 > 1e300L) return false;
    BigInt x(static_cast<long long>(x0));
    if (x.isZero()) x = BigInt(1);
    BigInt K(k);
    for (int i = 0; i < 300; ++i) {
        BigInt xk1 = BigInt::pow(x, k - 1);
        if (xk1.isZero()) break;
        BigInt nx = ((K - BigInt(1)) * x + n / xk1) / K;
        if (nx >= x) break;
        x = nx;
    }
    while (BigInt::pow(x, k) < n) x = x + BigInt(1);
    while (BigInt::pow(x, k) > n) x = x - BigInt(1);
    if (BigInt::pow(x, k) == n) {
        out = x;
        return true;
    }
    return false;
}

} // namespace em

#endif // EASYMATH_USE_GMP

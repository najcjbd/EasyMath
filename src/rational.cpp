// EasyMath - 精确有理数实现
#include "rational.hpp"

#include "interrupt.hpp"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace em {

// MSVC 的 long double 与 double 同型, 且其 printf 不支持 L 修饰符(%Lf/%Le);
// Apple arm64 的 long double 也等于 double。此时必须走 double 版本。
#if defined(_MSC_VER) || (defined(__APPLE__) && defined(__aarch64__))
#define EASYMATH_LD_IS_DOUBLE 1
#endif

static int ldFormat(char *buf, std::size_t n, bool sci, int prec, long double v) {
#if defined(EASYMATH_LD_IS_DOUBLE)
    return sci ? std::snprintf(buf, n, "%.*e", prec, static_cast<double>(v))
               : std::snprintf(buf, n, "%.*f", prec, static_cast<double>(v));
#else
    return sci ? std::snprintf(buf, n, "%.*Le", prec, v)
               : std::snprintf(buf, n, "%.*Lf", prec, v);
#endif
}

int longDoubleDigits10() {
#if defined(EASYMATH_LD_IS_DOUBLE)
    return 15;
#else
    return LDBL_DIG;
#endif
}

bool longDoubleIsDouble() {
#if defined(EASYMATH_LD_IS_DOUBLE)
    return true;
#else
    return false;
#endif
}

// 去掉小数尾零与多余的 "."
static std::string trimFraction(std::string s) {
    if (s.find('.') != std::string::npos) {
        std::size_t e = s.find_last_not_of('0');
        if (e != std::string::npos) {
            if (s[e] == '.') --e;
            s = s.substr(0, e + 1);
        }
    }
    bool allZero = true;
    for (char c : s)
        if (c >= '1' && c <= '9') allZero = false;
    if (allZero && s.size() >= 2 && s[0] == '-') s = s.substr(1);
    return s;
}

// 科学计数法: 末尾补零去掉, 指数去掉前导零 (e+43 / e-13)
static std::string normalizeSci(std::string s) {
    std::size_t e = s.find_first_of("eE");
    if (e == std::string::npos) return s;
    std::string mant = s.substr(0, e), ex = s.substr(e + 1);
    if (mant.find('.') != std::string::npos) mant = trimFraction(mant);
    if (ex.size() > 2 && (ex[0] == '-' || ex[0] == '+')) {
        std::size_t z = 1;
        while (z + 1 < ex.size() && ex[z] == '0') ++z;
        ex = ex.substr(0, 1) + ex.substr(z);
    }
    return mant + "e" + ex;
}

std::string formatNumber(long double v, const NumberFormat &nf) {
    if (!std::isfinite(static_cast<double>(v))) {
        if (std::isnan(static_cast<double>(v))) return "nan";
        return v < 0 ? "-inf" : "inf";
    }
    int digits = nf.digits;
    if (digits < 0) digits = 0;
    if (digits > 200) digits = 200;
    if (nf.maxSignificant > 0) {
        int cap = nf.maxSignificant > 1 ? nf.maxSignificant - 1 : 0;
        if (digits > cap) digits = cap;
    }
    long double av = std::fabs(v);
    bool sci = false;
    if (v != 0) {
        if (nf.sci == SciMode::Always) {
            sci = true;
        } else if (nf.sci == SciMode::Auto) {
            long double mag = std::log10(av);
            if (mag >= static_cast<long double>(nf.sciThreshold)) sci = true;
            else if (mag < -static_cast<long double>(digits + 1)) sci = true;
        }
    }
    char buf[4096];
    std::string out;
    if (!sci) {
        ldFormat(buf, sizeof(buf), false, digits, v);
        out = nf.trimZeros ? trimFraction(buf) : std::string(buf);
        if (nf.sci == SciMode::Auto && v != 0) {
            bool allZero = true;
            for (char c : out)
                if (c >= '1' && c <= '9') allZero = false;
            if (allZero) sci = true;
        }
    }
    if (sci) {
        char sbuf[512];
        ldFormat(sbuf, sizeof(sbuf), true, digits, v);
        out = normalizeSci(sbuf);
    }
    return out;
}

std::string formatLongDouble(long double v, int digits, bool trimZeros) {
    NumberFormat nf;
    nf.digits = digits;
    nf.sci = SciMode::Never;
    nf.trimZeros = trimZeros;
    return formatNumber(v, nf);
}

[[maybe_unused]] 

void Rational::normalize() {
    if (den_.isZero()) {
        num_ = BigInt(0);
        den_ = BigInt(1);
        return;
    }
    if (den_.isNeg()) {
        num_ = -num_;
        den_ = -den_;
    }
    if (num_.isZero()) {
        den_ = BigInt(1);
        return;
    }
    BigInt g = BigInt::gcd(num_, den_);
    if (!(g == BigInt(1))) {
        num_ = num_ / g;
        den_ = den_ / g;
    }
}

Rational Rational::fromDecimalString(const std::string &s, bool *ok) {
    Rational r;
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    int sg = 1;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        if (s[i] == '-') sg = -1;
        ++i;
    }
    std::string ip, fp;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ip += s[i++];
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) fp += s[i++];
    }
    if (ip.empty() && fp.empty()) {
        if (ok) *ok = false;
        return r;
    }
    BigInt n = ip.empty() ? BigInt(0) : BigInt(ip);
    BigInt d(1);
    if (!fp.empty()) {
        d = BigInt::pow(BigInt(10), fp.size());
        n = n * d + BigInt(fp);
    }
    // 科学计数法指数
    long long exp10 = 0;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        bool eneg = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            eneg = (s[i] == '-');
            ++i;
        }
        std::string ed;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ed += s[i++];
        if (ed.empty() || ed.size() > 5) {
            if (ok) *ok = false;
            return r;
        }
        exp10 = std::stoll(ed);
        if (eneg) exp10 = -exp10;
    }
    if (exp10 > 0) {
        n = n * BigInt::pow(BigInt(10), static_cast<unsigned long long>(exp10));
    } else if (exp10 < 0) {
        d = d * BigInt::pow(BigInt(10), static_cast<unsigned long long>(-exp10));
    }
    r = Rational(sg < 0 ? -n : n, d);
    if (ok) *ok = true;
    return r;
}

Rational Rational::fromDouble(long double x, long long maxDen) {
    if (!std::isfinite(static_cast<double>(x))) return Rational();
    int sg = x < 0 ? -1 : 1;
    x = std::fabs(x);
    BigInt p0(0), q0(1), p1(1), q1(0);
    long double frac = x;
    for (int i = 0; i < 64; ++i) {
        long long a = static_cast<long long>(std::floor(frac));
        BigInt p2 = BigInt(a) * p1 + p0;
        BigInt q2 = BigInt(a) * q1 + q0;
        long long dummy = 0;
        if (!q2.fitsLongLong(dummy) || std::llabs(dummy) > maxDen) break;
        p0 = p1; q0 = q1;
        p1 = p2; q1 = q2;
        long double rem = frac - static_cast<long double>(a);
        if (rem < 1e-18L) break;
        frac = 1.0L / rem;
        if (frac > 1e18L) break;
    }
    if (q1.isZero()) return Rational();
    return Rational(sg < 0 ? -p1 : p1, q1);
}

Rational Rational::parse(const std::string &s, bool *ok) {
    std::size_t slash = s.find('/');
    if (slash != std::string::npos) {
        bool o1 = false, o2 = false;
        BigInt a = BigInt::fromString(s.substr(0, slash), &o1);
        BigInt b = BigInt::fromString(s.substr(slash + 1), &o2);
        if (o1 && o2 && !b.isZero()) {
            if (ok) *ok = true;
            return Rational(a, b);
        }
        if (ok) *ok = false;
        return Rational();
    }
    return fromDecimalString(s, ok);
}

Rational operator+(const Rational &a, const Rational &b) {
    return Rational(a.num_ * b.den_ + b.num_ * a.den_, a.den_ * b.den_);
}
Rational operator-(const Rational &a, const Rational &b) {
    return Rational(a.num_ * b.den_ - b.num_ * a.den_, a.den_ * b.den_);
}
Rational operator*(const Rational &a, const Rational &b) { return Rational(a.num_ * b.num_, a.den_ * b.den_); }
Rational operator/(const Rational &a, const Rational &b) {
    if (b.num_.isZero()) return Rational();
    return Rational(a.num_ * b.den_, a.den_ * b.num_);
}

Rational Rational::pow(long long e) const {
    if (e == 0) return Rational(1);
    if (e < 0) {
        if (num_.isZero()) return Rational();
        return Rational(BigInt::pow(den_, static_cast<unsigned long long>(-e)),
                        BigInt::pow(num_, static_cast<unsigned long long>(-e)));
    }
    return Rational(BigInt::pow(num_, static_cast<unsigned long long>(e)),
                    BigInt::pow(den_, static_cast<unsigned long long>(e)));
}

bool Rational::isPerfectSquare(Rational &root) const {
    if (isNeg()) return false;
    BigInt rn, rd;
    if (!BigInt::sqrtExact(num_, rn)) return false;
    if (!BigInt::sqrtExact(den_, rd)) return false;
    root = Rational(rn, rd);
    return true;
}

int Rational::cmp(const Rational &o) const {
    BigInt l = num_ * o.den_;
    BigInt r = o.num_ * den_;
    return l.cmp(r);
}

std::string Rational::str() const {
    if (den_ == BigInt(1)) return num_.str();
    return num_.str() + "/" + den_.str();
}

std::string Rational::latex() const {
    if (den_ == BigInt(1)) return num_.str();
    std::string n = num_.str(), d = den_.str();
    if (num_.isNeg()) return "-\\frac{" + n.substr(1) + "}{" + d + "}";
    return "\\frac{" + n + "}{" + d + "}";
}

std::string Rational::mixedStr() const {
    if (den_ == BigInt(1)) return num_.str();
    if (num_.abs() < den_) return str();
    BigInt q, r;
    BigInt::divmod(num_.abs(), den_, q, r);
    return (num_.isNeg() ? "-" : "") + q.str() + " " + r.str() + "/" + den_.str();
}

long double Rational::toLongDouble() const { return num_.toLongDouble() / den_.toLongDouble(); }

bool Rational::fitsLongLong(long long &out) const {
    if (!isInteger()) {
        long double v = toLongDouble();
        if (v == std::floor(v) && std::fabs(v) < 9.2e18L) {
            out = static_cast<long long>(v);
            return true;
        }
        return false;
    }
    return num_.fitsLongLong(out);
}

static bool allZeroDigits(const std::string &s) {
    for (char c : s)
        if (c >= '1' && c <= '9') return false;
    return true;
}

// 数值非零却因位数不足显示成 0 时, 回退到科学计数法
static std::string scientificOrKeep(const std::string &fixed, const Rational &v, int digits) {
    if (v.isZero() || !allZeroDigits(fixed)) return fixed;
    NumberFormat nf;
    nf.digits = digits > 0 ? digits : 1;
    nf.sci = SciMode::Always;
    std::string t = formatNumber(v.toLongDouble(), nf);
    if (!t.empty() && t.find_first_of("eE") != std::string::npos) return t;
    char buf[128];
    ldFormat(buf, sizeof(buf), true, nf.digits, v.toLongDouble());
    t = buf;
    // 1.48e-13 -> 1.48e-13 (保持简洁)
    std::size_t e = t.find('e');
    if (e != std::string::npos) {
        std::string mant = t.substr(0, e), ex = t.substr(e + 1);
        if (mant.find('.') != std::string::npos) {
            std::size_t last = mant.find_last_not_of('0');
            if (last != std::string::npos && mant[last] == '.') --last;
            mant = mant.substr(0, last + 1);
        }
        // 去掉指数前导零: e-13 / e+05 -> e-13 / e+5
        if (ex.size() > 2 && (ex[0] == '-' || ex[0] == '+')) {
            std::size_t z = 1;
            while (z + 1 < ex.size() && ex[z] == '0') ++z;
            ex = ex.substr(0, 1) + ex.substr(z);
        }
        t = mant + "e" + ex;
    }
    return t;
}

static void roundDigits(std::string &ip, std::string &fp, int keep) {
    bool up = false;
    if (static_cast<int>(fp.size()) > keep) up = fp[static_cast<std::size_t>(keep)] >= '5';
    if (static_cast<int>(fp.size()) > keep) fp.resize(static_cast<std::size_t>(keep));
    if (!up) return;
    int i = static_cast<int>(fp.size()) - 1;
    while (i >= 0) {
        if (fp[static_cast<std::size_t>(i)] < '9') {
            fp[static_cast<std::size_t>(i)] += 1;
            return;
        }
        fp[static_cast<std::size_t>(i)] = '0';
        --i;
    }
    int j = static_cast<int>(ip.size()) - 1;
    while (j >= 0) {
        if (ip[static_cast<std::size_t>(j)] < '9') {
            ip[static_cast<std::size_t>(j)] += 1;
            return;
        }
        ip[static_cast<std::size_t>(j)] = '0';
        --j;
    }
    ip = "1" + ip;
}

std::string Rational::toDecimal(int digits, bool *exactOut, std::string *repeatingOut) const {
    if (digits < 0) digits = 0;
    std::string sign = num_.isNeg() ? "-" : "";
    BigInt n = num_.abs();
    BigInt ip, rem;
    BigInt::divmod(n, den_, ip, rem);
    std::string ips = ip.str();
    auto finish = [&](const std::string &frac) {
        if (digits == 0) return sign + ips;
        return sign + ips + "." + frac;
    };
    if (rem.isZero()) {
        if (exactOut) *exactOut = true;
        if (repeatingOut) repeatingOut->clear();
        return finish(std::string(static_cast<std::size_t>(digits), '0'));
    }
    std::string fp;
    std::map<std::string, std::size_t> seen;
    bool repeating = false;
    std::size_t cycleStart = 0;
    int limit = digits + 2;
    while (static_cast<int>(fp.size()) < limit) {
        std::string key = rem.str();
        auto it = seen.find(key);
        if (it != seen.end()) {
            repeating = true;
            cycleStart = it->second;
            break;
        }
        seen[key] = fp.size();
        rem = rem * BigInt(10);
        BigInt d;
        BigInt::divmod(rem, den_, d, rem);
        long long dv = 0;
        d.fitsLongLong(dv);
        fp += static_cast<char>('0' + dv);
        if (rem.isZero()) break;
    }
    if (repeating && static_cast<int>(cycleStart) < digits) {
        if (exactOut) *exactOut = true;
        std::string prefix = fp.substr(0, cycleStart);
        std::string cycle = fp.substr(cycleStart);
        if (repeatingOut) *repeatingOut = cycle;
        std::string tail = prefix;
        if (digits > static_cast<int>(prefix.size())) {
            int need = digits - static_cast<int>(prefix.size());
            for (int i = 0; i < need; ++i) tail += cycle[static_cast<std::size_t>(i) % cycle.size()];
            tail += "...";
        }
        return finish(tail);
    }
    if (rem.isZero()) {
        if (exactOut) *exactOut = true;
        if (repeatingOut) repeatingOut->clear();
        roundDigits(ips, fp, digits);
        while (static_cast<int>(fp.size()) < digits) fp += '0';
        if (digits < static_cast<int>(fp.size())) fp.resize(static_cast<std::size_t>(digits));
        return finish(fp);
    }
    if (exactOut) *exactOut = false;
    if (repeatingOut) repeatingOut->clear();
    roundDigits(ips, fp, digits);
    while (static_cast<int>(fp.size()) < digits) fp += '0';
    if (digits < static_cast<int>(fp.size())) fp.resize(static_cast<std::size_t>(digits));
    return scientificOrKeep(finish(fp), *this, digits);
}

std::string Rational::toExactDecimal() const {
    BigInt d = den_;
    while (d % BigInt(2) == BigInt(0)) d = d / BigInt(2);
    while (d % BigInt(5) == BigInt(0)) d = d / BigInt(5);
    if (d == BigInt(1)) {
        int digits = 0;
        BigInt t = den_;
        while (!(t == BigInt(1))) {
            if (t % BigInt(2) == BigInt(0)) t = t / BigInt(2);
            else t = t / BigInt(5);
            ++digits;
        }
        bool exact = false;
        std::string s = toDecimal(digits, &exact);
        if (exact) {
            if (s.find('.') != std::string::npos) {
                std::size_t e = s.find_last_not_of('0');
                if (s[e] == '.') --e;
                s = s.substr(0, e + 1);
            }
            return s;
        }
    }
    // 循环小数: 重建 "0.1(6)" 形式
    // 注意: 分母若含有大素数, 循环节长度可达分母量级(上万亿位), 必须设上界,
    //       否则 1/6747528149883 这类输入会长时间卡住。
    const std::size_t kMaxCycleDigits = 512;
    std::string sign = num_.isNeg() ? "-" : "";
    BigInt n = num_.abs();
    BigInt ip, rem;
    BigInt::divmod(n, den_, ip, rem);
    std::string ips = ip.str();
    std::string fp;
    std::map<std::string, std::size_t> seen;
    std::size_t cycleStart = std::string::npos;
    while (!rem.isZero() && fp.size() < kMaxCycleDigits) {
        if (interruptRequested()) break;
        std::string key = rem.str();
        auto it = seen.find(key);
        if (it != seen.end()) {
            cycleStart = it->second;
            break;
        }
        seen[key] = fp.size();
        rem = rem * BigInt(10);
        BigInt dd;
        BigInt::divmod(rem, den_, dd, rem);
        long long dv = 0;
        dd.fitsLongLong(dv);
        fp += static_cast<char>('0' + dv);
    }
    if (fp.empty()) return sign + ips;
    if (cycleStart != std::string::npos) {
        std::string out = sign + ips + "." + fp.substr(0, cycleStart) + "(" + fp.substr(cycleStart) + ")";
        if (out.size() > 100) { // 循环节过长, 只展示前面一小段
            std::size_t keep = sign.size() + ips.size() + 1 + 40;
            out = out.substr(0, keep) + "...)";
        }
        return out;
    }
    if (rem.isZero()) {
        std::size_t e = fp.find_last_not_of('0');
        return sign + ips + "." + (e == std::string::npos ? std::string("0") : fp.substr(0, e + 1));
    }
    // 循环节过长(或超过搜索上限), 只展示前 40 位
    std::string shown = fp.size() > 40 ? fp.substr(0, 40) : fp;
    return sign + ips + "." + shown + "...";
}

static const std::vector<long long> &primeTable() {
    static const std::vector<long long> primes = [] {
        std::vector<long long> p;
        const int LIMIT = 100000;
        std::vector<bool> comp(static_cast<std::size_t>(LIMIT) + 1, false);
        for (int i = 2; i <= LIMIT; ++i) {
            if (!comp[static_cast<std::size_t>(i)]) {
                p.push_back(i);
                for (long long j = 1LL * i * i; j <= LIMIT; j += i) comp[static_cast<std::size_t>(j)] = true;
            }
        }
        return p;
    }();
    return primes;
}

const std::vector<long long> &smallPrimes(std::size_t n) {
    const auto &p = primeTable();
    (void)n;
    return p;
}

void simplifySurd(const BigInt &D, BigInt &coef, BigInt &rad) {
    coef = BigInt(1);
    rad = D;
    if (D.isNeg() || D.isZero() || D == BigInt(1)) return;
    BigInt n = D;
    for (long long p : primeTable()) {
        if (interruptRequested()) break;
        BigInt bp(p);
        if (bp * bp > n) break;
        BigInt p2 = bp * bp;
        while (n % p2 == BigInt(0)) {
            n = n / p2;
            coef = coef * bp;
        }
    }
    BigInt s;
    if (BigInt::sqrtExact(n, s) && !(s == BigInt(1))) {
        coef = coef * s;
        n = BigInt(1);
    }
    rad = n;
}

} // namespace em

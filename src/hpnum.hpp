// EasyMath - 任意精度浮点 (MPFR) 与复数 (MPC) 支持
// 仅在编译时定义 EASYMATH_USE_MPFR 时可用; 否则所有函数返回 false。
#pragma once

#include "expr.hpp"
#include "poly.hpp"
#include "solve.hpp"

#include <string>
#include <vector>

namespace em {

bool hpAvailable();
const char *hpBackendName();     // "MPFR" 或 "builtin(long double)"
std::string hpVersion();         // MPFR 版本字符串

// 由小数位数换算需要的二进制精度(位)
long hpPrecBitsForDigits(int digits);

// 以 prec 位精度求值表达式(实数域), 输出 digits 位小数的定点字符串
bool hpEvalToString(const NodePtr &n, long prec, int digits, std::string &out, std::string &err,
                    SciMode sci = SciMode::Never, int sciThreshold = 12);

// 高精度 tan(角度或弧度)
bool hpTan(const NodePtr &angle, bool degrees, long prec, int digits, std::string &out,
           std::string &err, SciMode sci = SciMode::Never, int sciThreshold = 12);

// 用 MPC 上的牛顿法把多项式的数值根精化到 prec 位, 并刷新其文本表示
bool hpPolishRoots(const Poly &p, std::vector<RootOut> &roots, long prec, int digits,
                   SciMode sci = SciMode::Never, int sciThreshold = 12);

// 单个复数根的精化(供外部复用)
bool hpPolishOne(const Poly &p, long double re, long double im, long prec, long double *outRe,
                 long double *outIm, std::string *plainOut, std::string *approxOut, int digits,
                 SciMode sci = SciMode::Never, int sciThreshold = 12);

} // namespace em

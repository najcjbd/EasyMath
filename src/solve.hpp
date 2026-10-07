// EasyMath - 方程求解
#pragma once

#include "expr.hpp"
#include "poly.hpp"

#include <complex>
#include <string>
#include <vector>

namespace em {

// a + b 形式的代数数 (b 可为纯根式)
struct AlgNum {
    Rational a;   // 有理部分
    Surd b;       // 实部中的根式部分
    Surd bi_;     // 虚部系数 (乘以 i)
    bool exact = true;
    std::complex<long double> approx{0, 0};

    static AlgNum fromRational(const Rational &r);
    static AlgNum fromSurd(const Surd &s);
    std::string plain() const;
    std::string latex() const;
};

struct RootOut {
    bool exact = false;
    bool isComplex = false;
    AlgNum value;
    std::complex<long double> numeric{0, 0};
    int mult = 1;
    std::string plain;       // 精确纯文本
    std::string latex;       // 精确 LaTeX
    std::string approxPlain;
    bool haveApprox = false; // 是否值得在精确形式后附上数值近似 // 近似文本(含复数)
};

struct SolveOptions {
    int decimals = 8;
    bool complexAllowed = true;
    bool realOnly = false;
    bool wantSteps = true;
    int maxNumericDegree = 64;
    bool highPrecision = false;  // 用 MPFR/MPC 精化数值根
    long hpPrecBits = 256;       // 目标精度(位)
    NumberFormat fmt;            // 数值输出格式(科学计数法策略等)
    std::vector<std::string> *notes = nullptr; // 可选: 收集说明信息
    // 规模闸门要给出"正确"的建议: 如果 SymPy 本来就已经试过并失败, 就不能再建议
    // "改用 --engine sympy"; 如果本机压根没装 SymPy, 则应建议先装。
    std::string numericInequality = "auto"; // auto | always | never (见 Config 注释)
    std::string constants;  // 视为常量的字母(逗号分隔)
    std::string derive;     // 由解反推的表达式(逗号分隔)
    bool sympyUsable = false;          // 本机有可用的 python3+sympy
    bool sympyTriedAndFailed = false;  // engine=auto/sympy 时已经试过 SymPy 但没成功
    long double scanLo = -100.0L;
    long double scanHi = 100.0L;
    int scanSamples = 20000;
};

struct SolveResult {
    bool ok = false;
    std::string err;

    enum Kind { ConstantEq, SingleVar, LinearSystem, NonlinearSystem } kind = ConstantEq;

    // 单变量
    std::string var;
    bool identity = false;
    bool none = false;
    std::vector<RootOut> roots;
    Poly poly;
    bool hasPoly = false;
    // 外部引擎给出的字符串形式多项式
    bool hasPolyStrings = false;
    std::string polyPlain, polyLatex, polyVar;
    // 一般解集合(如带 n∈Z 的三角函数解)
    bool hasGeneralSet = false;
    std::string generalSetPlain, generalSetLatex;

    // 多变量
    std::vector<std::string> vars;
    std::vector<std::string> solutionPlain; // "x = 3/4"
    std::vector<std::string> solutionLatex;
    // 与 solutionPlain 一一对应的数值近似 ("x ≈ 1.41421356, y ≈ ..."); 无需近似时为空串
    std::vector<std::string> solutionApprox;
    // 与 solutionPlain 一一对应的"约束"(不等式/非零), 如 "b > 0"、"b·c·(b+c) ≠ 0"
    std::vector<std::string> solutionCondPlain, solutionCondLatex;
    bool hasRelational = false; // 输入里含不等式/非零约束
    // 派生量: 与 solutionPlain 一一对应, 每个解一组 "expr = value"
    std::vector<std::vector<std::string>> derivedPlain, derivedLatex;
    std::string constantsList; // 被声明为常量的字母(逗号分隔)
    // 分组(按依赖的未确定量)与组内比例: 与 solutionPlain 一一对应
    std::vector<std::vector<std::string>> groupLines;
    std::vector<std::vector<std::string>> ratioLabels, ratioPlain, ratioLatex;
    std::vector<std::vector<bool>> ratioNumeric; // true=约掉参数后的纯数值比
    bool infinite = false;
    std::vector<std::string> params;

    std::string method;
    std::vector<std::string> notes;
    std::vector<std::string> steps;
};

// 按分隔符切分输入 (默认分隔符 + 自定义)
// 生成"课堂步骤"(一元一次/一元二次), 与引擎无关; 由 modes 在两条求解路径合流处调用
void appendPolySteps(SolveResult &res, const Poly &g);
// 由原始方程文本重建线性方程组并补上消元步骤(引擎无关; 供 modes 合流处调用)
bool appendLinearStepsFromText(const std::vector<std::string> &eqs, SolveResult &res);
// 复平面轨迹(z/w 当复变量): |z-c| REL r 与 |z-a| REL |z-b|; 其它情形返回 false
bool complexLocusFromText(const std::string &item, SolveResult &res);

std::vector<std::string> splitItems(const std::string &s, char32_t extraSep, bool splitOnSpace);

// 求解一组方程(每个字符串形如 "LHS=RHS" 或 "表达式")
SolveResult solveEquations(const std::vector<std::string> &eqs, const SolveOptions &opt);

// 单变量多项式求根(精确优先)
std::vector<RootOut> rootsOfPolynomial(const Poly &p, const SolveOptions &opt);

// 数值求根工具
std::vector<std::complex<long double>> durandKerner(
    const std::vector<std::complex<long double>> &coeffs, int maxIter = 1000,
    long double tol = 1e-20L);

// 复数格式化
std::string complexPlain(const std::complex<long double> &z, int decimals);
std::string complexLatex(const std::complex<long double> &z, int decimals);
std::string complexPlain(const std::complex<long double> &z, const NumberFormat &nf);
std::string complexLatex(const std::complex<long double> &z, const NumberFormat &nf);

std::string algNumPlain(const AlgNum &v);
std::string algNumLatex(const AlgNum &v);

} // namespace em

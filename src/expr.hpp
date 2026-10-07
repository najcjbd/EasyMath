// EasyMath - 表达式: 词法分析 / 语法分析 / AST / 精确与数值求值
#pragma once

#include "rational.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace em {

// ============ 精确无理数: coef * sqrt(rad) ============
struct Surd {
    Rational coef;
    BigInt rad; // >= 0

    Surd() : coef(0), rad(1) {}
    Surd(const Rational &r) : coef(r), rad(1) {} // NOLINT
    Surd(const Rational &c, const BigInt &r) : coef(c), rad(r) {}

    bool isRational() const { return rad == BigInt(1); }
    bool isZero() const { return coef.isZero(); }
    long double approx() const;
    std::string str() const;
    std::string latex() const;

    static bool sqrtOf(const Rational &x, Surd &out);
    static Surd fromRadical(const Rational &c, const BigInt &rad);

    Surd operator*(const Surd &o) const;
    Surd operator-() const;
    bool operator==(const Surd &o) const;
};

// ============ AST ============
enum class NT { Num, Var, Add, Sub, Mul, Div, Pow, Neg, Call, Fact, Percent };

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Node {
    NT t = NT::Num;
    Rational num;
    std::string name; // Var / Call
    std::vector<NodePtr> kids;
    std::size_t pos = 0;
    // 由"多字母标识符"拆出来的乘积(如 ax -> a·x): 后面若紧跟 ^n, 幂要作用在
    // 紧邻的因子 x 上(a·x²), 而不是整个乘积((a·x)²)。显式括号会清掉该标记。
    bool identSplit = false;

    static NodePtr num_(const Rational &r);
    static NodePtr var(const std::string &n, std::size_t pos = 0);
    static NodePtr op(NT t, NodePtr a, NodePtr b);
    static NodePtr neg(NodePtr a);
    static NodePtr call(const std::string &f, std::vector<NodePtr> args);
    static NodePtr fact(NodePtr a);
};

// ============ 词法 ============
enum class TK {
    Num, Ident, Plus, Minus, Star, Slash, Caret, LParen, RParen,
    LBracket, RBracket, Bang, Percent, Degree, Angle, Prod, Sum, Comma, Pipe, End
};

struct Token {
    TK k = TK::End;
    std::string text;
    Rational value;
    std::size_t pos = 0;
};

struct LexOptions {
    // 数值模式下未知字母可能被当作乘法运算符 (5x6 -> 30)
    bool numeric_only = false;
    std::set<std::string> declared; // 预先声明的变量名
};

bool lex(const std::string &s, const LexOptions &opt, std::vector<Token> &out, std::string &err);

// ============ 语法 ============
struct ParseOptions {
    bool numeric_only = false;         // 未知符号报错, 除了夹在数字之间的单字母乘法
    std::set<std::string> declared;    // 变量名白名单
    bool allow_implicit_call = true;   // sqrt6 / sin 30
};

// 解析整串(必须到结尾); 失败时 err 带位置信息
NodePtr parseExpression(const std::string &normalized, const ParseOptions &opt, std::string &err);

// 解析赋值/等式形式: 返回 LHS, RHS; 若无 '=' 则 lhs 为空, rhs 为整串
bool splitEquation(const std::string &normalized, std::string &lhs, std::string &rhs);

// 扫描输入中出现的变量名(用于 declared)
std::set<std::string> scanDeclaredNames(const std::string &normalized);

// ============ 求值 ============
bool isConstantName(const std::string &n);
bool isFunctionName(const std::string &n);
bool isGreekVarName(const std::string &n);
long double constantValue(const std::string &n, bool &ok);

// 精确求值(有理数 + 平方根); 失败返回 false
bool evalExact(const NodePtr &n, const std::map<std::string, Rational> &env, Surd &out, std::string &err);

// ---- 函数定义 f(x)=… (方程+函数类) ----
// 定义项(如 "f(x)=x^2-3")不参与求解, 只登记; 后续项里的 f(...) 会被内联展开。
struct FuncDefs {
    struct Def {
        std::vector<std::string> params;
        std::string body;      // 原文表达式(参数为形参名)
    };
    std::map<std::string, Def> defs;
};

// 判断某项是不是函数定义; 是则填 name/params/body 并返回 true
bool extractFuncDef(const std::string &item, std::string &name, std::vector<std::string> &params,
                    std::string &body);
// 把 item 里所有已登记函数的调用展开成表达式(按平衡括号取实参, 参数用 AST 替换后反解析, 并加括号)
std::string inlineFuncDefs(const std::string &item, const FuncDefs &fd, bool &used);
// AST 变量替换(不改原树)
NodePtr substVars(const NodePtr &n, const std::map<std::string, NodePtr> &m);

// 数值求值
bool evalApprox(const NodePtr &n, const std::map<std::string, long double> &env, long double &out,
                std::string &err);

// 结构工具
std::set<std::string> collectVars(const NodePtr &n);
bool substitute(const NodePtr &n, const std::map<std::string, NodePtr> &sub, NodePtr &out);
NodePtr simplifyConst(const NodePtr &n); // 常量折叠(数值), 保留符号结构
bool isNumericConst(const NodePtr &n, long double &out);

// 打印
std::string astPlain(const NodePtr &n);
std::string astLatex(const NodePtr &n);
std::string astSympy(const NodePtr &n); // 输出为 SymPy/Python 语法(显式 *, **, Rational)

// 根式的友好打印 (√3/3 形式)
std::string surdPlainNice(const Surd &s);
std::string surdLatexNice(const Surd &s);

// 大整数阶乘
bool factorialExact(long long n, BigInt &out);

} // namespace em

// EasyMath - 表达式实现
#include "expr.hpp"
#include "unicode.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace em {

// ============================ Surd ============================

long double Surd::approx() const {
    return coef.toLongDouble() * std::sqrt(rad.toLongDouble());
}

std::string Surd::str() const {
    if (coef.isZero()) return "0";
    if (isRational()) return coef.str();
    std::string rs = "\u221a(" + rad.str() + ")";
    if (coef.isOne()) return rs;
    if (coef == Rational(-1)) return "-" + rs;
    if (coef.isInteger()) return coef.str() + rs;
    return "(" + coef.str() + ")" + rs;
}

std::string Surd::latex() const {
    if (coef.isZero()) return "0";
    if (isRational()) return coef.latex();
    std::string rs = "\\sqrt{" + rad.str() + "}";
    if (coef.isOne()) return rs;
    if (coef == Rational(-1)) return "-" + rs;
    return coef.latex() + rs;
}

Surd Surd::fromRadical(const Rational &c, const BigInt &rad) {
    // 把 rad 中的平方因子提到系数里
    Surd r;
    if (rad.isZero()) {
        r.coef = Rational(0);
        r.rad = BigInt(1);
        return r;
    }
    BigInt k, t;
    simplifySurd(rad, k, t);
    r.coef = c * Rational(k);
    r.rad = t;
    return r;
}

bool Surd::sqrtOf(const Rational &x, Surd &out) {
    if (x.isNeg()) return false;
    if (x.isZero()) {
        out = Surd(Rational(0));
        return true;
    }
    // sqrt(p/q) = sqrt(p*q)/q
    BigInt p = x.num(), q = x.den();
    BigInt prod = p * q;
    BigInt k, t;
    simplifySurd(prod, k, t);
    out.coef = Rational(k, q);
    out.rad = t;
    return true;
}

Surd Surd::operator*(const Surd &o) const {
    Rational c = coef * o.coef;
    if (isRational() && o.isRational()) return Surd(c);
    if (isRational()) return Surd(c, o.rad);
    if (o.isRational()) return Surd(c, rad);
    return fromRadical(c, rad * o.rad);
}

Surd Surd::operator-() const { return Surd(-coef, rad); }

bool Surd::operator==(const Surd &o) const { return rad == o.rad && coef == o.coef; }

std::string surdPlainNice(const Surd &s) {
    if (s.isRational()) return s.coef.str();
    BigInt an = s.coef.num().abs();
    std::string num = (an == BigInt(1) ? std::string() : an.str()) + "\u221a" + s.rad.str();
    std::string sign = s.coef.isNeg() ? "-" : "";
    if (s.coef.den() == BigInt(1)) return sign + num;
    return sign + num + "/" + s.coef.den().str();
}

std::string surdLatexNice(const Surd &s) {
    if (s.isRational()) return s.coef.latex();
    BigInt an = s.coef.num().abs();
    std::string rad = "\\sqrt{" + s.rad.str() + "}";
    std::string num = (an == BigInt(1) ? rad : an.str() + rad);
    std::string sign = s.coef.isNeg() ? "-" : "";
    if (s.coef.den() == BigInt(1)) return sign + num;
    return sign + "\\frac{" + num + "}{" + s.coef.den().str() + "}";
}

// ============================ Node ============================

NodePtr Node::num_(const Rational &r) {
    auto n = std::make_shared<Node>();
    n->t = NT::Num;
    n->num = r;
    return n;
}
NodePtr Node::var(const std::string &name, std::size_t pos) {
    auto n = std::make_shared<Node>();
    n->t = NT::Var;
    n->name = name;
    n->pos = pos;
    return n;
}
NodePtr Node::op(NT t, NodePtr a, NodePtr b) {
    auto n = std::make_shared<Node>();
    n->t = t;
    n->kids.push_back(std::move(a));
    n->kids.push_back(std::move(b));
    return n;
}
NodePtr Node::neg(NodePtr a) {
    auto n = std::make_shared<Node>();
    n->t = NT::Neg;
    n->kids.push_back(std::move(a));
    return n;
}
NodePtr Node::call(const std::string &f, std::vector<NodePtr> args) {
    auto n = std::make_shared<Node>();
    n->t = NT::Call;
    n->name = f;
    n->kids = std::move(args);
    return n;
}
NodePtr Node::fact(NodePtr a) {
    auto n = std::make_shared<Node>();
    n->t = NT::Fact;
    n->kids.push_back(std::move(a));
    return n;
}

// ============================ 名称表 ============================

bool isConstantName(const std::string &n) { return n == "pi" || n == "e" || n == "tau"; }

bool isFunctionName(const std::string &n) {
    static const std::set<std::string> fns = {
        "sqrt", "cbrt", "root4", "abs", "exp", "ln", "log", "log2", "log10", "sin", "cos", "tan",
        "cot", "sec", "csc", "asin", "acos", "atan", "sinh", "cosh", "tanh", "floor", "ceil",
        "round", "sign", "max", "min", "gcd", "lcm", "deg", "sum", "prod"};
    return fns.count(n) > 0;
}

bool isGreekVarName(const std::string &n) {
    static const std::set<std::string> g = {
        "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa",
        "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "sigma", "tau", "upsilon", "phi",
        "chi", "psi", "omega", "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta",
        "Theta", "Iota", "Kappa", "Lambda", "Mu", "Nu", "Xi", "Omicron", "Pi", "Rho", "Sigma",
        "Tau", "Upsilon", "Phi", "Chi", "Psi", "Omega"};
    return g.count(n) > 0;
}

long double constantValue(const std::string &n, bool &ok) {
    ok = true;
    if (n == "pi") return 3.141592653589793238462643383279502884197L;
    if (n == "tau") return 6.283185307179586476925286766559005768394L;
    if (n == "e") return 2.718281828459045235360287471352662497757L;
    ok = false;
    return 0;
}

// ============================ 词法 ============================

static const std::set<std::string> &prefixWords() {
    static const std::set<std::string> w = {
        "sqrt", "cbrt", "root4", "abs", "exp", "ln", "log", "log2", "log10", "sin", "cos", "tan",
        "cot", "sec", "csc", "asin", "acos", "atan", "sinh", "cosh", "tanh", "floor", "ceil",
        "round", "sign", "max", "min", "gcd", "lcm", "deg", "sum", "prod", "pi", "tau"};
    return w;
}

static std::string longestPrefixWord(const std::string &t) {
    std::string best;
    for (const auto &w : prefixWords()) {
        if (w.size() < t.size() && t.compare(0, w.size(), w) == 0 && w.size() > best.size()) best = w;
    }
    return best;
}

bool lex(const std::string &s, const LexOptions &opt, std::vector<Token> &out, std::string &err) {
    out.clear();
    std::size_t i = 0;
    static const std::size_t kMaxTokens = 50000;
    bool tooMany = false;
    auto push = [&](TK k, const std::string &t, std::size_t p) {
        if (out.size() >= kMaxTokens) tooMany = true;
        Token tk;
        tk.k = k;
        tk.text = t;
        tk.pos = p;
        out.push_back(tk);
    };
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        // 绝对值竖线 |…| 与取整括号 ⌊⌋/⌈⌉
        // (竖线是不是分隔符由 splitItems 决定: 成对的当绝对值, 落单的当分隔符)
        if (c == '|') {
            push(TK::Pipe, "|", i);
            ++i;
            continue;
        }
        if (c == 0xE2 && i + 2 < s.size() &&
            static_cast<unsigned char>(s[i + 1]) == 0x8C) {
            unsigned char c3 = static_cast<unsigned char>(s[i + 2]);
            if (c3 == 0x8A) { // ⌊ floor(
                push(TK::Ident, "floor", i);
                push(TK::LParen, "(", i);
                i += 3;
                continue;
            }
            if (c3 == 0x88) { // ⌈ ceil(
                push(TK::Ident, "ceil", i);
                push(TK::LParen, "(", i);
                i += 3;
                continue;
            }
            if (c3 == 0x8B || c3 == 0x89) { // ⌋ / ⌉ -> )
                push(TK::RParen, ")", i);
                i += 3;
                continue;
            }
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        if (std::isdigit(c) || (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            std::size_t start = i;
            bool dot = false;
            while (i < s.size()) {
                unsigned char d = static_cast<unsigned char>(s[i]);
                if (std::isdigit(d)) {
                    ++i;
                } else if (d == '.' && !dot) {
                    dot = true;
                    ++i;
                } else {
                    break;
                }
            }
            // 科学计数法: 1.5e-3 / 2E10 (要求 e 后紧跟数字或带符号数字)
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                std::size_t j = i + 1;
                if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
                if (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
                    ++j;
                    while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
                    i = j;
                }
            }
            std::string t = s.substr(start, i - start);
            bool ok = false;
            Rational v = Rational::fromDecimalString(t, &ok);
            if (!ok) {
                err = "无法解析的数字: " + t;
                return false;
            }
            Token tk;
            tk.k = TK::Num;
            tk.value = v;
            tk.text = t;
            tk.pos = start;
            out.push_back(tk);
            continue;
        }
        if (std::isalpha(c)) {
            std::size_t start = i;
            while (i < s.size()) {
                unsigned char d = static_cast<unsigned char>(s[i]);
                if (std::isalnum(d) || d == '_') ++i;
                else break;
            }
            std::string text = s.substr(start, i - start);
            std::string pre;
            if (!isFunctionName(text) && !isConstantName(text)) pre = longestPrefixWord(text);
            if (!pre.empty()) {
                std::string rest = text.substr(pre.size());
                bool split = false;
                if (std::isdigit(static_cast<unsigned char>(rest[0]))) split = true;
                else if (rest.size() == 1 && (opt.declared.count(rest) > 0 || isGreekVarName(rest)))
                    split = true;
                if (split) {
                    push(TK::Ident, pre, start);
                    i = start + pre.size();
                    continue;
                }
            }
            if (opt.numeric_only && text.size() >= 2 && std::isalpha(static_cast<unsigned char>(text[0])) &&
                std::isdigit(static_cast<unsigned char>(text[1]))) {
                push(TK::Ident, text.substr(0, 1), start);
                i = start + 1;
                continue;
            }
            push(TK::Ident, text, start);
            continue;
        }
        std::size_t start = i;
        switch (c) {
            case '+': push(TK::Plus, "+", start); ++i; continue;
            case '-': push(TK::Minus, "-", start); ++i; continue;
            case '*': push(TK::Star, "*", start); ++i; continue;
            case '/': push(TK::Slash, "/", start); ++i; continue;
            case '^': push(TK::Caret, "^", start); ++i; continue;
            case '(': push(TK::LParen, "(", start); ++i; continue;
            case ')': push(TK::RParen, ")", start); ++i; continue;
            case '[': push(TK::LBracket, "[", start); ++i; continue;
            case ']': push(TK::RBracket, "]", start); ++i; continue;
            case '!':
                if (i + 1 < s.size() && s[i + 1] == '=') {
                    err = "位置 " + std::to_string(i) + ": 不支持的不等式运算符 '!='";
                    return false;
                }
                push(TK::Bang, "!", start); ++i; continue;
            case '%': push(TK::Percent, "%", start); ++i; continue;
            case ',': push(TK::Comma, ",", start); ++i; continue;
            case '=':
                if (i + 1 < s.size() && (s[i + 1] == '=' || s[i + 1] == '>')) { ++i; }
                err = "位置 " + std::to_string(i) + ": 表达式中不应出现 '=' (等式请用方程模式)";
                return false;
            case '<':
            case '>':
                err = "位置 " + std::to_string(i) + ": 不支持的不等式运算符";
                return false;
            case '~':
                err = "位置 " + std::to_string(i) + ": 不支持 '~=' 近似等式";
                return false;
            case '\\':
                err = "位置 " + std::to_string(i) + ": 无法识别的 LaTeX 命令 " + s.substr(i, 12);
                return false;
            default:
                break;
        }
        if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xB0) { // °
            push(TK::Degree, "°", start);
            i += 2;
            continue;
        }
        if (c == 0xE2 && i + 2 < s.size()) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
            if (b1 == 0x88 && b2 == 0xA0) { // ∠ U+2220
                push(TK::Angle, "∠", start);
                i += 3;
                continue;
            }
            if (b1 == 0x88 && b2 == 0x91) { // ∑ U+2211
                push(TK::Sum, "∑", start);
                i += 3;
                continue;
            }
            if (b1 == 0x88 && b2 == 0x8F) { // ∏ U+220F
                push(TK::Prod, "∏", start);
                i += 3;
                continue;
            }
            if (b1 == 0x88 && b2 == 0xAB) { // ∫ U+222B
                err = "位置 " + std::to_string(i) + ": 暂不支持积分符号 ∫";
                return false;
            }
            if (b1 == 0x89 && b2 == 0xA0) { // ≠
                err = "位置 " + std::to_string(i) + ": 不支持的不等式运算符 '≠'";
                return false;
            }
        }
        err = "位置 " + std::to_string(i) + ": 无法识别的字符 '" + s.substr(i, 1) + "'";
        return false;
    }
    if (tooMany) {
        err = "输入过长(记号数超过 " + std::to_string(kMaxTokens) + ")";
        return false;
    }
    Token e;
    e.k = TK::End;
    e.pos = s.size();
    out.push_back(e);
    return true;
}

// ============================ declared 名扫描 ============================

std::set<std::string> scanDeclaredNames(const std::string &normalized) {
    std::set<std::string> names;
    LexOptions lo;
    std::vector<Token> toks;
    std::string err;
    if (!lex(normalized, lo, toks, err)) return names;
    for (const auto &t : toks) {
        if (t.k != TK::Ident) continue;
        const std::string &n = t.text;
        if (isFunctionName(n) || isConstantName(n)) continue;
        if (isGreekVarName(n)) {
            names.insert(n);
            continue;
        }
        if (n.size() == 1) {
            names.insert(n);
            continue;
        }
        // 单字母 + 数字/下划线尾巴 (x1, x_2) -> 作为整体变量
        std::size_t j = 0;
        while (j < n.size() && std::isalpha(static_cast<unsigned char>(n[j]))) ++j;
        if (j == 1 && j < n.size()) names.insert(n);
    }
    return names;
}

// ============================ 语法分析 ============================

namespace {

constexpr int kMaxParseDepth = 1200;

class Parser {
public:
    Parser(const std::vector<Token> &t, const ParseOptions &o) : t_(t), o_(o) {}

    // 递归深度守卫: 防止 "((((...))))" 这类输入把栈打爆
    struct Guard {
        Parser *p;
        bool ok;
        Guard(Parser *self, std::string &err) : p(self), ok(++self->depth_ <= kMaxParseDepth) {
            if (!ok) err = "表达式嵌套过深(超过 " + std::to_string(kMaxParseDepth) + " 层)";
        }
        ~Guard() { --p->depth_; }
    };

    NodePtr parseAll(std::string &err) {
        NodePtr n = parseExpr(err);
        if (!n) return nullptr;
        if (cur().k != TK::End) {
            err = "位置 " + std::to_string(cur().pos) + ": 多余的记号 '" + tokDesc(cur()) + "'";
            return nullptr;
        }
        return n;
    }

private:
    const std::vector<Token> &t_;
    const ParseOptions &o_;
    std::size_t i_ = 0;
    int depth_ = 0;

    const Token &cur() const { return t_[i_]; }
    const Token &prev() const { return t_[i_ > 0 ? i_ - 1 : 0]; }
    const Token &next() const { return t_[std::min(i_ + 1, t_.size() - 1)]; }
    void adv() { if (i_ + 1 < t_.size()) ++i_; }

    static std::string tokDesc(const Token &t) {
        switch (t.k) {
            case TK::Num: return t.text;
            case TK::Ident: return t.text;
            case TK::End: return "结尾";
            default: return t.text;
        }
    }

    bool startsPrimary(const Token &t) const {
        switch (t.k) {
            case TK::Num:
            case TK::Ident:
            case TK::LParen:
            case TK::LBracket:
            case TK::Prod:
            case TK::Sum:
            case TK::Angle:
                return true;
            default:
                return false;
        }
    }

    NodePtr parseExpr(std::string &err) {
        Guard g(this, err);
        if (!g.ok) return nullptr;
        NodePtr left = parseTerm(err);
        if (!left) return nullptr;
        while (cur().k == TK::Plus || cur().k == TK::Minus) {
            TK k = cur().k;
            adv();
            NodePtr right = parseTerm(err);
            if (!right) return nullptr;
            left = Node::op(k == TK::Plus ? NT::Add : NT::Sub, left, right);
        }
        return left;
    }

    NodePtr parseTerm(std::string &err) {
        NodePtr left = parseUnary(err);
        if (!left) return nullptr;
        for (;;) {
            if (cur().k == TK::Star || cur().k == TK::Slash) {
                TK k = cur().k;
                adv();
                NodePtr right = parseUnary(err);
                if (!right) return nullptr;
                left = Node::op(k == TK::Star ? NT::Mul : NT::Div, left, right);
                continue;
            }
            if (startsPrimary(cur())) { // 隐式乘法 2x / 3(4) / 2sqrt3
                if (i_ == 0) break;
                NodePtr right = parseUnary(err);
                if (!right) return nullptr;
                left = Node::op(NT::Mul, left, right);
                continue;
            }
            break;
        }
        return left;
    }

    NodePtr parseUnary(std::string &err) {
        Guard g(this, err);
        if (!g.ok) return nullptr;
        if (cur().k == TK::Plus) {
            adv();
            return parseUnary(err);
        }
        if (cur().k == TK::Minus) {
            adv();
            NodePtr inner = parseUnary(err);
            if (!inner) return nullptr;
            return Node::neg(inner);
        }
        return parsePower(err);
    }

    NodePtr parsePower(std::string &err) {
        NodePtr base = parsePostfix(err);
        if (!base) return nullptr;
        if (cur().k == TK::Caret) {
            adv();
            NodePtr exp = parseUnary(err);
            if (!exp) return nullptr;
            // ax^2: ax 是被拆出来的 a·x, 按数学惯例 ^2 只作用于紧邻的 x -> a·(x²)。
            // (老实现直接 Pow(a·x, 2) = a²x², 是个实打实的错误。)
            if (base->t == NT::Mul && base->identSplit) {
                NodePtr lastFactor = rightmostFactor(base);
                NodePtr pw = Node::op(NT::Pow, lastFactor, exp);
                return replaceRightmost(base, pw);
            }
            return Node::op(NT::Pow, base, exp);
        }
        return base;
    }

    // 左结合乘积链的最后一个因子
    static NodePtr rightmostFactor(const NodePtr &n) {
        if (n->t == NT::Mul && n->kids.size() == 2) return rightmostFactor(n->kids[1]);
        return n;
    }

    static NodePtr replaceRightmost(const NodePtr &n, const NodePtr &repl) {
        if (n->t != NT::Mul || n->kids.size() != 2) return repl;
        NodePtr r = replaceRightmost(n->kids[1], repl);
        NodePtr out = Node::op(NT::Mul, n->kids[0], r);
        return out;
    }

    NodePtr parsePostfix(std::string &err) {
        NodePtr p = parsePrimary(err);
        if (!p) return nullptr;
        for (;;) {
            if (cur().k == TK::Bang) {
                adv();
                p = Node::fact(p);
            } else if (cur().k == TK::Percent) {
                adv();
                auto n = std::make_shared<Node>();
                n->t = NT::Percent;
                n->kids.push_back(p);
                p = n;
            } else if (cur().k == TK::Degree) {
                adv();
                if (p->t == NT::Call && p->name == "deg") break; // ∠45° 不重复包裹
                p = Node::call("deg", {p});
            } else {
                break;
            }
        }
        return p;
    }

    NodePtr parseArgs(std::string &err, std::vector<NodePtr> &args) {
        if (cur().k != TK::LParen) {
            err = "位置 " + std::to_string(cur().pos) + ": 期望 '('";
            return nullptr;
        }
        adv();
        if (cur().k == TK::RParen) {
            adv();
            return Node::num_(Rational(0)); // 空参数占位
        }
        for (;;) {
            NodePtr a = parseExpr(err);
            if (!a) return nullptr;
            args.push_back(a);
            if (cur().k == TK::Comma) {
                adv();
                continue;
            }
            if (cur().k == TK::RParen) {
                adv();
                break;
            }
            err = "位置 " + std::to_string(cur().pos) + ": 期望 ',' 或 ')'";
            return nullptr;
        }
        return args.empty() ? nullptr : args.back();
    }

    NodePtr parsePrimary(std::string &err) {
        const Token &t = cur();
        switch (t.k) {
            case TK::Num: {
                adv();
                return Node::num_(t.value);
            }
            case TK::Pipe: { // |…| = 绝对值(可嵌套: ||x|| )
                adv();
                NodePtr e = parseExpr(err);
                if (!e) return nullptr;
                if (cur().k != TK::Pipe) {
                    err = "位置 " + std::to_string(cur().pos) +
                          ": 绝对值竖线 | 没有配对的收尾(成对的 |…| 才是绝对值, 落单的 | 是分隔符)";
                    return nullptr;
                }
                adv();
                return Node::call("abs", {e});
            }
            case TK::LParen: {
                adv();
                NodePtr e = parseExpr(err);
                if (!e) return nullptr;
                if (cur().k != TK::RParen) {
                    err = "位置 " + std::to_string(cur().pos) + ": 缺少右括号 ')'";
                    return nullptr;
                }
                adv();
                e->identSplit = false; // 显式括号: (ax)^2 就是整个乘积的平方
                return e;
            }
            case TK::LBracket: {
                adv();
                NodePtr e = parseExpr(err);
                if (!e) return nullptr;
                if (cur().k != TK::RBracket) {
                    err = "位置 " + std::to_string(cur().pos) + ": 缺少右括号 ']'";
                    return nullptr;
                }
                adv();
                e->identSplit = false;
                return e;
            }
            case TK::Prod:
            case TK::Sum: {
                std::string fname = (t.k == TK::Prod) ? "prod" : "sum";
                std::size_t p = t.pos;
                adv();
                std::vector<NodePtr> args;
                if (cur().k == TK::LParen) {
                    parseArgs(err, args);
                    if (args.empty()) {
                        err = "位置 " + std::to_string(p) + ": " + fname + " 需要参数";
                        return nullptr;
                    }
                } else if (startsPrimary(cur())) {
                    NodePtr a = parsePower(err);
                    if (!a) return nullptr;
                    args.push_back(a);
                } else {
                    err = "位置 " + std::to_string(p) + ": " + fname + " 需要参数";
                    return nullptr;
                }
                return Node::call(fname, args);
            }
            case TK::Angle: {
                adv();
                NodePtr a = parsePostfix(err);
                if (!a) return nullptr;
                if (a->t == NT::Call && a->name == "deg") return a;
                return Node::call("deg", {a});
            }
            case TK::Ident:
                return parseIdent(err);
            default:
                err = "位置 " + std::to_string(t.pos) + ": 意外的记号 '" + tokDesc(t) + "'";
                return nullptr;
        }
    }

    // 注意: '|' 不在 startsPrimary 里 —— 它作为"隐式乘法因子"没有意义,
    // 反而会把绝对值的收尾竖线吃掉(2*|-3| 里的第一个 | 由运算符路径进来, 没问题)。

    // 判断当前记号是否是"夹在两个因子之间"的单字母(用于 5x6 -> 30)
    bool sandwiched() const {
        const Token &p = prev();
        bool prevFactor = (p.k == TK::Num || p.k == TK::RParen || p.k == TK::RBracket ||
                           p.k == TK::Bang || p.k == TK::Percent || p.k == TK::Degree);
        if (!prevFactor) return false;
        return startsPrimary(cur());
    }

    NodePtr parseIdent(std::string &err) {
        const Token t = cur();
        const std::string &n = t.text;
        bool sand = sandwiched(); // 必须在 adv() 之前判断
        adv();
        // C(n,k) 组合 / A(n,k) 排列: 只有紧跟 '(' 时才当函数(否则 C、A 还是普通未知量,
        // 例如 2*pi*r=C 里的 C)。展开成多项式: C(n,k)=n(n-1)…(n-k+1)/k!, A(n,k)=n(n-1)…(n-k+1)
        if ((n == "C" || n == "A") && cur().k == TK::LParen) {
            std::vector<NodePtr> args;
            parseArgs(err, args);
            if (args.empty() && !err.empty()) return nullptr;
            if (args.size() != 2) {
                err = "位置 " + std::to_string(t.pos) + ": " + n + " 需要两个参数: " + n + "(n,k)";
                return nullptr;
            }
            // k 必须是具体正整数(否则不支持)
            Rational kk;
            bool kConst = false;
            {
                Surd sv;
                std::string se;
                kConst = evalExact(args[1], {}, sv, se) && sv.isRational();
                if (kConst) kk = sv.coef;
            }
            if (!kConst || !kk.isInteger() || kk.isNeg() || kk.isZero()) {
                err = "位置 " + std::to_string(t.pos) + ": " + n +
                      " 的第二个参数需要正整数(如 " + n + "(n,2))";
                return nullptr;
            }
            long k = std::atol(kk.num().str().c_str());
            if (k < 1 || k > 20) {
                err = n + "(n,k) 目前只支持 k=1..20";
                return nullptr;
            }
            NodePtr prod;
            for (long j = 0; j < k; ++j) {
                NodePtr term = (j == 0) ? args[0]
                                        : Node::op(NT::Sub, args[0], Node::num_(Rational(j)));
                prod = prod ? Node::op(NT::Mul, prod, term) : term;
            }
            if (n == "A") return prod;
            // C: 再除以 k!
            Rational kfact(1);
            for (long j = 2; j <= k; ++j) kfact = kfact * Rational(j);
            return Node::op(NT::Div, prod, Node::num_(kfact));
        }
        if (isFunctionName(n)) {
            std::vector<NodePtr> args;
            if (cur().k == TK::LParen) {
                parseArgs(err, args);
                if (args.empty() && !err.empty()) return nullptr;
            } else if (o_.allow_implicit_call && startsPrimary(cur())) {
                NodePtr a = parsePower(err);
                if (!a) return nullptr;
                args.push_back(a);
            } else {
                err = "位置 " + std::to_string(t.pos) + ": 函数 " + n + " 缺少参数";
                return nullptr;
            }
            return Node::call(n, args);
        }
        if (isConstantName(n) && !o_.declared.count(n)) {
            return Node::var(n, t.pos); // 常量由求值阶段处理, 便于 pi/6 结构识别
        }
        if (o_.declared.count(n) || isGreekVarName(n)) return Node::var(n, t.pos);

        if (o_.numeric_only) {
            // i 是虚数单位: 必须在"被夹住的单字母当乘号"之前判掉,
            // 否则 2i 会被当成 2×1 = 2(静默算错), 而 1+i 又会报未知符号。
            if (n == "i") return Node::var("i", t.pos);
            if (n.size() == 1 && sand) return Node::num_(Rational(1)); // 隐式乘法运算符
            err = "位置 " + std::to_string(t.pos) + ": 未知符号 '" + n + "' (数值模式下不能含未知量)";
            return nullptr;
        }
        // 数字尾巴 -> 作为一个整体变量 (x1, y_2)
        std::size_t j = 0;
        while (j < n.size() && std::isalpha(static_cast<unsigned char>(n[j]))) ++j;
        if (j == 1 && j < n.size()) return Node::var(n, t.pos);
        // 其余多字母 -> 拆成单字母/数字的乘积
        NodePtr acc;
        for (std::size_t k = 0; k < n.size(); ++k) {
            char c = n[k];
            NodePtr part;
            if (std::isalpha(static_cast<unsigned char>(c))) part = Node::var(std::string(1, c), t.pos);
            else if (std::isdigit(static_cast<unsigned char>(c))) part = Node::num_(Rational(c - '0'));
            else continue; // '_'
            acc = acc ? Node::op(NT::Mul, acc, part) : part;
        }
        if (!acc) {
            err = "位置 " + std::to_string(t.pos) + ": 无法识别的符号 '" + n + "'";
            return nullptr;
        }
        acc->identSplit = true; // 见 Node::identSplit 的说明
        return acc;
    }
};

} // namespace

namespace {

// 用显式栈计算 AST 深度: 像 "1+1+1+..." / "xxxx..." 这种左深链会退化
// 成和项数一样深的树, 递归打印/求值会打爆栈, 必须在解析出口拦住。
constexpr int kMaxAstDepth = 2000;

int astDepth(const NodePtr &root) {
    std::vector<std::pair<NodePtr, int>> st;
    st.reserve(64);
    st.push_back({root, 1});
    int maxd = 0;
    while (!st.empty()) {
        auto item = st.back();
        st.pop_back();
        const NodePtr &n = item.first;
        int d = item.second;
        if (!n) continue;
        if (d > maxd) maxd = d;
        if (maxd > kMaxAstDepth) return maxd; // 提前退出
        for (const auto &k : n->kids) st.push_back({k, d + 1});
    }
    return maxd;
}

} // namespace

NodePtr parseExpression(const std::string &normalized, const ParseOptions &opt, std::string &err) {
    LexOptions lo;
    lo.numeric_only = opt.numeric_only;
    lo.declared = opt.declared;
    std::vector<Token> toks;
    if (!lex(normalized, lo, toks, err)) return nullptr;
    Parser p(toks, opt);
    NodePtr n = p.parseAll(err);
    if (!n) return nullptr;
    if (astDepth(n) > kMaxAstDepth) {
        err = "表达式过于复杂(嵌套或项数超过 " + std::to_string(kMaxAstDepth) + ")";
        return nullptr;
    }
    return n;
}

bool splitEquation(const std::string &normalized, std::string &lhs, std::string &rhs) {
    int depth = 0;
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        char c = normalized[i];
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        else if (c == '=' && depth == 0) {
            if (i + 1 < normalized.size() && normalized[i + 1] == '=') continue;
            if (i > 0 && (normalized[i - 1] == '<' || normalized[i - 1] == '>' || normalized[i - 1] == '!' ||
                          normalized[i - 1] == '~')) continue;
            lhs = normalized.substr(0, i);
            rhs = normalized.substr(i + 1);
            return true;
        }
    }
    lhs.clear();
    rhs = normalized;
    return false;
}

// ============================ 阶乘 ============================

bool factorialExact(long long n, BigInt &out) {
    if (n < 0) return false;
    if (n > 20000) return false;
    out = BigInt(1);
    for (long long i = 2; i <= n; ++i) out = out * BigInt(i);
    return true;
}

// ============================ 精确求值 ============================

namespace {

// 精确求幂的结果规模预算。
// base^e 的十进制位数 ≈ digits(base) * e; 超过预算就拒绝精确求值, 让上层走近似。
// 原因: 单次 GMP 运算无法被 Ctrl+C 打断, 例如把 999999999^999(8991 位)代入 x^1000
// 会生成约 900 万位的整数, 一次运算就要几分钟, 期间用户的中断请求完全无响应。
// 预算按后端缩放: GMP 用分治乘法, 内置后端是 O(n^2) 教科书乘法。
// 实测(桌面, 同一份源码): 10^1000000 用 GMP 0.19s, 用内置后端 15.5s(80 倍);
// 位数再翻倍还要 4 倍 —— Android 用的正是内置后端, 所以那里的预算必须收紧,
// 否则"拦不住的巨型运算"会变成几十秒的无响应。
long long maxExactPowDigits() {
    return std::strcmp(BigInt::backendName(), "GMP") == 0 ? 2000000 : 300000;
}

bool exactPowFitsInBudget(const Rational &base, long long e, std::string &err) {
    if (e == 0) return true;
    long long ae = e < 0 ? -e : e;
    long long bd = static_cast<long long>(base.num().decimalDigits());
    if (!base.isInteger()) bd += static_cast<long long>(base.den().decimalDigits());
    // 只有 0 / ±1 不膨胀; 注意 2 也是"1 位数"但 2^30000000 有 900 万位
    if (base.isZero() || base == Rational(1) || base == Rational(-1)) return true;
    long long budget = maxExactPowDigits();
    if (bd > budget / ae) {
        err = "结果约 " + std::to_string(bd * ae) + " 位十进制数, 超出精确求值上限(" +
              std::to_string(budget) + " 位); 请缩小规模或用近似值";
        return false;
    }
    return true;
}

bool toIntegerExponent(const Surd &s, long long &e) {
    if (!s.isRational()) return false;
    if (!s.coef.isInteger()) return false;
    if (!s.coef.num().fitsLongLong(e)) return false;
    return true;
}

// 从 AST 中提取 pi 的有理倍数 (pi/6, 2pi, 3*pi/4 ...)
bool extractPiMultiple(const NodePtr &n, Rational &mult) {
    if (!n) return false;
    if (n->t == NT::Var && n->name == "pi") {
        mult = Rational(1);
        return true;
    }
    if (n->t == NT::Neg) {
        Rational m;
        if (extractPiMultiple(n->kids[0], m)) {
            mult = -m;
            return true;
        }
        return false;
    }
    if (n->t == NT::Mul) {
        Rational a, b;
        if (extractPiMultiple(n->kids[0], a)) {
            Surd s;
            std::string e;
            if (evalExact(n->kids[1], {}, s, e) && s.isRational()) {
                mult = a * s.coef;
                return true;
            }
        }
        if (extractPiMultiple(n->kids[1], b)) {
            Surd s;
            std::string e;
            if (evalExact(n->kids[0], {}, s, e) && s.isRational()) {
                mult = b * s.coef;
                return true;
            }
        }
        return false;
    }
    if (n->t == NT::Div) {
        Rational a;
        if (!extractPiMultiple(n->kids[0], a)) return false;
        Surd s;
        std::string e;
        if (evalExact(n->kids[1], {}, s, e) && s.isRational() && !s.coef.isZero()) {
            mult = a / s.coef;
            return true;
        }
        return false;
    }
    return false;
}

// 度数的精确三角函数
bool exactTrigDeg(const std::string &fn, const Rational &degIn, Surd &out, std::string &err) {
    Rational d = degIn;
    // 归一化到 [0, 360)
    if (!d.isInteger()) return false;
    long long dd = 0;
    if (!d.num().fitsLongLong(dd)) return false;
    dd %= 360;
    if (dd < 0) dd += 360;

    auto half = [](int num) { return Surd(Rational(num, 2)); };
    Surd s, c;
    switch (dd) {
        case 0: case 180: s = Surd(Rational(0)); break;
        case 30: case 150: s = half(1); break;
        case 45: case 135: s = Surd::fromRadical(Rational(1, 2), BigInt(2)); break;
        case 60: case 120: s = Surd::fromRadical(Rational(1, 2), BigInt(3)); break;
        case 90: s = Surd(Rational(1)); break;
        case 210: case 330: s = half(-1); break;
        case 225: case 315: s = Surd::fromRadical(Rational(-1, 2), BigInt(2)); break;
        case 240: case 300: s = Surd::fromRadical(Rational(-1, 2), BigInt(3)); break;
        case 270: s = Surd(Rational(-1)); break;
        default: return false;
    }
    int cd = (90 - dd + 720) % 360;
    switch (cd) {
        case 0: case 180: c = Surd(Rational(0)); break;
        case 30: case 150: c = half(1); break;
        case 45: case 135: c = Surd::fromRadical(Rational(1, 2), BigInt(2)); break;
        case 60: case 120: c = Surd::fromRadical(Rational(1, 2), BigInt(3)); break;
        case 90: c = Surd(Rational(1)); break;
        case 210: case 330: c = half(-1); break;
        case 225: case 315: c = Surd::fromRadical(Rational(-1, 2), BigInt(2)); break;
        case 240: case 300: c = Surd::fromRadical(Rational(-1, 2), BigInt(3)); break;
        case 270: c = Surd(Rational(-1)); break;
        default: return false;
    }
    if (fn == "sin") { out = s; return true; }
    if (fn == "cos") { out = c; return true; }
    if (fn == "tan") {
        if (c.isZero() && c.isRational()) {
            err = "tan(" + degIn.str() + "°) 无定义";
            return false;
        }
        if (c.isRational() && s.isRational()) {
            out = Surd(s.coef / c.coef);
            return true;
        }
        // (a*sqrt(r)) / (b*sqrt(t)) = a/(b*t) * sqrt(r*t)
        Rational coef = s.coef / (c.coef * Rational(c.rad));
        out = Surd::fromRadical(coef, s.rad * c.rad);
        return true;
    }
    return false;
}

bool isDegNode(const NodePtr &n, Rational *degOut) {
    if (n && n->t == NT::Call && n->name == "deg" && n->kids.size() == 1) {
        Surd s;
        std::string e;
        if (evalExact(n->kids[0], {}, s, e) && s.isRational()) {
            if (degOut) *degOut = s.coef;
            return true;
        }
    }
    return false;
}

} // namespace


// ==================== 函数定义 f(x)=… ====================

NodePtr substVars(const NodePtr &n, const std::map<std::string, NodePtr> &m) {
    if (!n) return nullptr;
    if (n->t == NT::Var) {
        auto it = m.find(n->name);
        if (it != m.end()) return it->second;
        return n;
    }
    auto c = std::make_shared<Node>(*n);
    c->kids.clear();
    for (const auto &k : n->kids) c->kids.push_back(substVars(k, m));
    return c;
}

static bool identOk(const std::string &s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

bool extractFuncDef(const std::string &raw, std::string &name, std::vector<std::string> &params,
                    std::string &body) {
    std::string s = raw;   // 注意: 不能先 normalize_math —— 它会把 f(x) 拆成 f*(x)
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    std::size_t st = i;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
    name = s.substr(st, i - st);
    if (!identOk(name) || isFunctionName(name) || isGreekVarName(name)) return false;  // sin/log/pi 等不算自定义
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= s.size() || s[i] != '(') return false;
    int depth = 0;
    std::size_t close = std::string::npos;
    for (std::size_t k = i; k < s.size(); ++k) {
        if (s[k] == '(') ++depth;
        else if (s[k] == ')') {
            if (--depth == 0) { close = k; break; }
        }
    }
    if (close == std::string::npos) return false;
    std::string plist = s.substr(i + 1, close - i - 1);
    // 形参必须是普通标识符(可多个, 逗号分隔)
    std::vector<std::string> ps;
    std::string cur;
    for (char c : plist) {
        if (c == ',') { ps.push_back(cur); cur.clear(); }
        else cur += c;
    }
    ps.push_back(cur);
    for (auto &x : ps) {
        std::string t;
        for (char c : x)
            if (!std::isspace(static_cast<unsigned char>(c))) t += c;
        if (!identOk(t) || isFunctionName(t)) return false;
        params.push_back(t);
    }
    if (params.empty() || params[0].empty()) return false;
    std::size_t j = close + 1;
    while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) ++j;
    if (j >= s.size() || s[j] != '=') return false;
    // 不能是 == 之类的比较
    if (j + 1 < s.size() && s[j + 1] == '=') return false;
    body = normalize_math(s.substr(j + 1));   // 函数体是普通表达式, 可以归一化
    if (body.find_first_not_of(" \t") == std::string::npos) return false;
    return true;
}

std::string inlineFuncDefs(const std::string &itemRaw, const FuncDefs &fd, bool &used) {
    used = false;
    if (fd.defs.empty()) return itemRaw;
    std::string s = itemRaw;
    for (int guard = 0; guard < 8; ++guard) {
        bool changed = false;
        for (const auto &pr : fd.defs) {
            const std::string &fn = pr.first;
            const FuncDefs::Def &def = pr.second;
            std::size_t pos = 0;
            while ((pos = s.find(fn + "(", pos)) != std::string::npos) {
                // 前面是标识符字符 -> 不是独立函数名
                if (pos > 0 && (std::isalnum(static_cast<unsigned char>(s[pos - 1])) || s[pos - 1] == '_')) {
                    pos += fn.size();
                    continue;
                }
                std::size_t open = pos + fn.size();
                int depth = 0;
                std::size_t close = std::string::npos;
                for (std::size_t k = open; k < s.size(); ++k) {
                    if (s[k] == '(') ++depth;
                    else if (s[k] == ')') {
                        if (--depth == 0) { close = k; break; }
                    }
                }
                if (close == std::string::npos) break;
                std::string argText = s.substr(open + 1, close - open - 1);
                // 拆实参(按顶层逗号)
                std::vector<std::string> args;
                std::string cur;
                int d2 = 0;
                for (char c : argText) {
                    if (c == '(') ++d2;
                    if (c == ')') --d2;
                    if (c == ',' && d2 == 0) { args.push_back(cur); cur.clear(); }
                    else cur += c;
                }
                args.push_back(cur);
                if (args.size() != def.params.size()) {
                    pos = close + 1;
                    continue;   // 参数个数不符: 不动它(留给解析器报错)
                }
                // 形参 -> 实参 AST 替换, 再反解析成文本(整体加括号保证优先级)
                std::map<std::string, NodePtr> sub;
                bool okAll = true;
                for (std::size_t ai = 0; ai < args.size(); ++ai) {
                    ParseOptions po;
                    std::string perr;
                    NodePtr an = parseExpression(normalize_math(args[ai]), po, perr);
                    if (!an) { okAll = false; break; }
                    sub[def.params[ai]] = an;
                }
                if (!okAll) { pos = close + 1; continue; }
                ParseOptions po;
                std::string perr;
                NodePtr bn = parseExpression(def.body, po, perr);
                if (!bn) { pos = close + 1; continue; }
                NodePtr inl = substVars(bn, sub);
                std::string repl = "(" + astPlain(inl) + ")";
                s = s.substr(0, pos) + repl + s.substr(close + 1);
                pos += repl.size();
                changed = true;
                used = true;
            }
        }
        if (!changed) break;
    }
    return s;
}

bool evalExact(const NodePtr &n, const std::map<std::string, Rational> &env, Surd &out, std::string &err) {
    if (!n) {
        err = "空表达式";
        return false;
    }
    switch (n->t) {
        case NT::Num:
            out = Surd(n->num);
            return true;
        case NT::Var: {
            auto it = env.find(n->name);
            if (it != env.end()) {
                out = Surd(it->second);
                return true;
            }
            err = "未知量 " + n->name;
            return false;
        }
        case NT::Neg: {
            Surd a;
            if (!evalExact(n->kids[0], env, a, err)) return false;
            out = -a;
            return true;
        }
        case NT::Add:
        case NT::Sub: {
            Surd a, b;
            if (!evalExact(n->kids[0], env, a, err)) return false;
            if (!evalExact(n->kids[1], env, b, err)) return false;
            bool sub = (n->t == NT::Sub);
            if (a.rad == b.rad) {
                out = Surd(sub ? a.coef - b.coef : a.coef + b.coef, a.rad);
                return true;
            }
            if (a.isZero()) {
                out = sub ? -b : b;
                return true;
            }
            if (b.isZero()) {
                out = a;
                return true;
            }
            err = "无法精确合并不同的根式, 将使用近似值";
            return false;
        }
        case NT::Mul: {
            Surd a, b;
            if (!evalExact(n->kids[0], env, a, err)) return false;
            if (!evalExact(n->kids[1], env, b, err)) return false;
            out = a * b;
            return true;
        }
        case NT::Div: {
            Surd a, b;
            if (!evalExact(n->kids[0], env, a, err)) return false;
            if (!evalExact(n->kids[1], env, b, err)) return false;
            if (b.coef.isZero()) {
                err = "除以零";
                return false;
            }
            if (b.isRational()) {
                out = Surd(a.coef / b.coef, a.rad);
                return true;
            }
            if (a.rad == b.rad) {
                out = Surd(a.coef / b.coef);
                return true;
            }
            err = "无法精确化简根式除法, 将使用近似值";
            return false;
        }
        case NT::Pow: {
            Surd a, b;
            if (!evalExact(n->kids[0], env, a, err)) return false;
            if (!evalExact(n->kids[1], env, b, err)) return false;
            long long e = 0;
            if (!toIntegerExponent(b, e)) {
                err = "指数不是整数, 无法精确求值";
                return false;
            }
            // 是否可精确求值由"结果规模预算"决定, 而不是指数大小:
            // 2^30000 只有 9031 位, 完全可以精确算; 而小指数也可能爆(如 (10^9000)^1000)。
            // 预算见 exactPowFitsInBudget: 超过约 200 万位就退化到近似值。
            if (a.isRational()) {
                if (e < 0 && a.coef.isZero()) {
                    err = "0 的负数次幂";
                    return false;
                }
                if (!exactPowFitsInBudget(a.coef, e, err)) return false;
                out = Surd(a.coef.pow(e));
                return true;
            }
            // (c√r)^e
            BigInt r = a.rad;
            Rational c = a.coef;
            long long ae = std::llabs(e);
            if (!exactPowFitsInBudget(c, e, err)) return false;
            if (!exactPowFitsInBudget(Rational(r), e, err)) return false;
            Rational cn = c.pow(ae);
            BigInt rn = BigInt::pow(r, static_cast<unsigned long long>(ae));
            if (e < 0) {
                cn = cn.reciprocal();
                rn = BigInt(1); // 分母有理化后近似处理
                err = "根式的负整数次幂无法精确表示";
                return false;
            }
            Surd t;
            if (!Surd::sqrtOf(Rational(rn), t)) {
                err = "无法精确求值";
                return false;
            }
            out = Surd(t.coef * cn, t.rad);
            return true;
        }
        case NT::Fact: {
            Surd a;
            if (!evalExact(n->kids[0], env, a, err)) return false;
            long long k = 0;
            if (!a.isRational() || !a.coef.isInteger() || !a.coef.num().fitsLongLong(k) || k < 0) {
                err = "只有非负整数可以精确阶乘";
                return false;
            }
            BigInt f;
            if (!factorialExact(k, f)) {
                err = "阶乘过大";
                return false;
            }
            out = Surd(Rational(f));
            return true;
        }
        case NT::Percent: {
            Surd a;
            if (!evalExact(n->kids[0], env, a, err)) return false;
            out = Surd(a.coef / Rational(100), a.rad);
            return true;
        }
        case NT::Call: {
            const std::string &f = n->name;
            if (f == "deg") {
                err = "角度值需要三角函数或直线模式处理";
                return false;
            }
            if (f == "sum" || f == "prod") {
                Surd acc = (f == "sum") ? Surd(Rational(0)) : Surd(Rational(1));
                for (auto &k : n->kids) {
                    Surd v;
                    if (!evalExact(k, env, v, err)) return false;
                    if (f == "prod") {
                        acc = acc * v;
                        continue;
                    }
                    if (acc.rad == v.rad) acc = Surd(acc.coef + v.coef, acc.rad);
                    else if (acc.isZero()) acc = v;
                    else if (v.isZero()) { /* 不变 */ }
                    else {
                        err = "无法精确合并不同的根式, 将使用近似值";
                        return false;
                    }
                }
                out = acc;
                return true;
            }
            if (f == "sin" || f == "cos" || f == "tan") {
                Rational deg;
                if (isDegNode(n->kids[0], &deg)) {
                    if (exactTrigDeg(f, deg, out, err)) return true;
                }
                Rational mult;
                if (extractPiMultiple(n->kids[0], mult)) {
                    if (exactTrigDeg(f, mult * Rational(180), out, err)) return true;
                }
                err = "该角度的三角函数无法用精确根式表示";
                return false;
            }
            std::vector<Surd> args;
            for (auto &k : n->kids) {
                Surd v;
                if (!evalExact(k, env, v, err)) return false;
                args.push_back(v);
            }
            if (f == "sqrt") {
                if (args.size() != 1 || !args[0].isRational()) {
                    err = "sqrt 需要有理数参数";
                    return false;
                }
                if (!Surd::sqrtOf(args[0].coef, out)) {
                    err = "负数开平方需要复数支持";
                    return false;
                }
                return true;
            }
            if (f == "cbrt" || f == "root4") {
                if (args.size() != 1 || !args[0].isRational()) {
                    err = "根式需要有理数参数";
                    return false;
                }
                unsigned k = (f == "cbrt") ? 3u : 4u;
                BigInt rn, rd;
                if (args[0].coef.isNeg() && k % 2 == 0) {
                    err = "负数开偶次方需要复数支持";
                    return false;
                }
                BigInt n = args[0].coef.num(), d = args[0].coef.den();
                if (!BigInt::nthRootExact(n, k, rn) || !BigInt::nthRootExact(d, k, rd)) {
                    err = "不是完全 " + std::to_string(k) + " 次方";
                    return false;
                }
                out = Surd(Rational(rn, rd));
                return true;
            }
            if (f == "abs") {
                if (args.size() != 1) {
                    err = "abs 需要 1 个参数";
                    return false;
                }
                out = Surd(args[0].coef.abs(), args[0].rad);
                return true;
            }
            if (f == "floor" || f == "ceil" || f == "round" || f == "sign") {
                if (args.size() != 1 || !args[0].isRational()) {
                    err = f + " 需要有理数参数";
                    return false;
                }
                const Rational &x = args[0].coef;
                BigInt q, r;
                BigInt::divmod(x.num(), x.den(), q, r);
                if (x.isNeg() && !r.isZero()) q = q - BigInt(1); // 向下取整
                // q 已经是 floor(x)(负数非整数时上面减过 1)。
                // ceil 只有两种情况: 整数就是 q, 非整数就是 q+1 —— 之前只给负数加了 1,
                // 于是 ceil(1.5) 错成 1(测试抓到的)。
                if (f == "floor") out = Surd(Rational(q));
                else if (f == "ceil") out = Surd(Rational(r.isZero() ? q : q + BigInt(1))); else if (f == "sign") {
                    out = Surd(Rational(x.isZero() ? 0 : (x.isNeg() ? -1 : 1)));
                } else {
                    Rational x2 = x * Rational(2);
                    BigInt q2, r2;
                    BigInt::divmod(x2.num(), x2.den(), q2, r2);
                    if (x2.isNeg() && !r2.isZero()) q2 = q2 - BigInt(1);
                    out = Surd(Rational(q2 + BigInt(1), 2));
                }
                return true;
            }
            if (f == "max" || f == "min") {
                if (args.empty()) {
                    err = f + " 需要参数";
                    return false;
                }
                Surd best = args[0];
                for (std::size_t k = 1; k < args.size(); ++k) {
                    bool take = (f == "max") ? (args[k].approx() > best.approx())
                                             : (args[k].approx() < best.approx());
                    if (take) best = args[k];
                }
                out = best;
                return true;
            }
            if (f == "gcd" || f == "lcm") {
                if (args.size() < 2) {
                    err = f + " 需要至少 2 个参数";
                    return false;
                }
                BigInt acc = args[0].coef.num();
                for (std::size_t k = 1; k < args.size(); ++k) {
                    acc = (f == "gcd") ? BigInt::gcd(acc, args[k].coef.num())
                                       : BigInt::lcm(acc, args[k].coef.num());
                }
                out = Surd(Rational(acc));
                return true;
            }
            if (f == "log" && args.size() == 2) return false;  // 两参 log 交给数值/SymPy
            if (f == "log" || f == "log10" || f == "log2" || f == "ln") {
                if (args.size() != 1 || !args[0].isRational() || !args[0].coef.isInteger()) {
                    err = f + " 的精确值需要整数参数";
                    return false;
                }
                const BigInt &v = args[0].coef.num();
                if (f == "ln") {
                    if (v == BigInt(1)) {
                        out = Surd(Rational(0));
                        return true;
                    }
                    err = "ln 无法精确表示";
                    return false;
                }
                BigInt base = (f == "log2") ? BigInt(2) : BigInt(10);
                if (v == BigInt(1)) {
                    out = Surd(Rational(0));
                    return true;
                }
                if (v.sign() <= 0) {
                    err = "对数的真数必须为正";
                    return false;
                }
                BigInt t = v;
                long long e = 0;
                while (t % base == BigInt(0) && !(t == BigInt(1))) {
                    t = t / base;
                    ++e;
                }
                if (t == BigInt(1)) {
                    out = Surd(Rational(e));
                    return true;
                }
                err = "该对数值无法精确表示";
                return false;
            }
            if (f == "exp") {
                if (args.size() == 1 && args[0].isRational() && args[0].coef.isZero()) {
                    out = Surd(Rational(1));
                    return true;
                }
                err = "exp 无法精确表示";
                return false;
            }
            err = "函数 " + f + " 无法精确求值, 将使用近似值";
            return false;
        }
    }
    err = "不支持的表达式";
    return false;
}

// ============================ 数值求值 ============================

namespace {
long double roundHalfUp(long double x) { return std::floor(x + 0.5L); }
long long gcdll(long long a, long long b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) {
        long long t = a % b;
        a = b;
        b = t;
    }
    return a;
}
}

bool evalApprox(const NodePtr &n, const std::map<std::string, long double> &env, long double &out,
                std::string &err) {
    if (!n) {
        err = "空表达式";
        return false;
    }
    auto ev = [&](const NodePtr &k, long double &v) { return evalApprox(k, env, v, err); };
    switch (n->t) {
        case NT::Num:
            out = n->num.toLongDouble();
            return true;
        case NT::Var: {
            auto it = env.find(n->name);
            if (it != env.end()) {
                out = it->second;
                return true;
            }
            bool ok = false;
            long double c = constantValue(n->name, ok);
            if (ok) {
                out = c;
                return true;
            }
            err = "未知量 " + n->name;
            return false;
        }
        case NT::Neg: {
            long double a;
            if (!ev(n->kids[0], a)) return false;
            out = -a;
            return true;
        }
        case NT::Add:
        case NT::Sub: {
            long double a, b;
            if (!ev(n->kids[0], a) || !ev(n->kids[1], b)) return false;
            out = (n->t == NT::Add) ? a + b : a - b;
            return true;
        }
        case NT::Mul: {
            long double a, b;
            if (!ev(n->kids[0], a) || !ev(n->kids[1], b)) return false;
            out = a * b;
            return true;
        }
        case NT::Div: {
            long double a, b;
            if (!ev(n->kids[0], a) || !ev(n->kids[1], b)) return false;
            if (b == 0) {
                err = "除以零";
                return false;
            }
            out = a / b;
            return true;
        }
        case NT::Pow: {
            long double a, b;
            if (!ev(n->kids[0], a) || !ev(n->kids[1], b)) return false;
            if (a < 0 && b != std::floor(b)) {
                err = "负数的非整数次幂需要复数支持";
                return false;
            }
            out = std::pow(a, b);
            if (!std::isfinite(out)) { // 溢出成 inf 只会误导, 明确报错
                err = "数值溢出(结果超出浮点范围)";
                return false;
            }
            return true;
        }
        case NT::Fact: {
            long double a;
            if (!ev(n->kids[0], a)) return false;
            if (a >= 0 && a == std::floor(a) && a <= 170) {
                long double r = 1;
                for (long long i = 2; i <= static_cast<long long>(a); ++i) r *= static_cast<long double>(i);
                out = r;
                return true;
            }
            out = std::tgamma(a + 1.0L);
            return true;
        }
        case NT::Percent: {
            long double a;
            if (!ev(n->kids[0], a)) return false;
            out = a / 100.0L;
            return true;
        }
        case NT::Call: {
            const std::string &f = n->name;
            std::vector<long double> a;
            for (auto &k : n->kids) {
                long double v;
                if (!ev(k, v)) return false;
                a.push_back(v);
            }
            const long double PI = 3.141592653589793238462643383279502884197L;
            auto need = [&](std::size_t k) {
                if (a.size() != k) {
                    err = f + " 需要 " + std::to_string(k) + " 个参数";
                    return false;
                }
                return true;
            };
            if (f == "sqrt") {
                if (!need(1)) return false;
                if (a[0] < 0) {
                    err = "负数开平方需要复数支持";
                    return false;
                }
                out = std::sqrt(a[0]);
                return true;
            }
            if (f == "cbrt") {
                if (!need(1)) return false;
                out = std::cbrt(a[0]);
                return true;
            }
            if (f == "root4") {
                if (!need(1)) return false;
                if (a[0] < 0) {
                    err = "负数开四次方需要复数支持";
                    return false;
                }
                out = std::sqrt(std::sqrt(a[0]));
                return true;
            }
            if (f == "abs") {
                if (!need(1)) return false;
                out = std::fabs(a[0]);
                return true;
            }
            if (f == "exp") {
                if (!need(1)) return false;
                out = std::exp(a[0]);
                return true;
            }
            if (f == "ln") {
                if (!need(1)) return false;
                if (a[0] <= 0) {
                    err = "ln 的定义域为正数";
                    return false;
                }
                out = std::log(a[0]);
                return true;
            }
            if (f == "log" && a.size() == 2) {   // log(x, 底)
                if (a[0] <= 0 || a[1] <= 0 || a[1] == 1) {
                    err = "log(x, 底) 要求 x>0, 底>0 且 底≠1";
                    return false;
                }
                out = std::log(a[0]) / std::log(a[1]);
                return true;
            }
            if (f == "log" || f == "log10") {
                if (!need(1)) return false;
                if (a[0] <= 0) {
                    err = "log 的定义域为正数";
                    return false;
                }
                out = std::log10(a[0]);
                return true;
            }
            if (f == "log2") {
                if (!need(1)) return false;
                if (a[0] <= 0) {
                    err = "log2 的定义域为正数";
                    return false;
                }
                out = std::log2(a[0]);
                return true;
            }
            if (f == "sin" || f == "cos" || f == "tan" || f == "cot" || f == "sec" || f == "csc") {
                if (!need(1)) return false;
                // deg(...) 已在下面转换为弧度
                long double v = a[0];
                long double s = std::sin(v), c = std::cos(v);
                if (f == "sin") out = s;
                else if (f == "cos") out = c;
                else if (f == "tan") {
                    if (c == 0) {
                        err = "tan 在此角度无定义";
                        return false;
                    }
                    out = s / c;
                } else if (f == "cot") {
                    if (s == 0) {
                        err = "cot 在此角度无定义";
                        return false;
                    }
                    out = c / s;
                } else if (f == "sec") {
                    if (c == 0) {
                        err = "sec 在此角度无定义";
                        return false;
                    }
                    out = 1 / c;
                } else {
                    if (s == 0) {
                        err = "csc 在此角度无定义";
                        return false;
                    }
                    out = 1 / s;
                }
                return true;
            }
            if (f == "asin" || f == "acos" || f == "atan") {
                if (!need(1)) return false;
                if ((f == "asin" || f == "acos") && (a[0] < -1 || a[0] > 1)) {
                    err = f + " 的定义域为 [-1,1]";
                    return false;
                }
                out = (f == "asin") ? std::asin(a[0]) : (f == "acos") ? std::acos(a[0]) : std::atan(a[0]);
                return true;
            }
            if (f == "sinh") {
                if (!need(1)) return false;
                out = std::sinh(a[0]);
                return true;
            }
            if (f == "cosh") {
                if (!need(1)) return false;
                out = std::cosh(a[0]);
                return true;
            }
            if (f == "tanh") {
                if (!need(1)) return false;
                out = std::tanh(a[0]);
                return true;
            }
            if (f == "floor") {
                if (!need(1)) return false;
                out = std::floor(a[0]);
                return true;
            }
            if (f == "ceil") {
                if (!need(1)) return false;
                out = std::ceil(a[0]);
                return true;
            }
            if (f == "round") {
                if (!need(1)) return false;
                out = roundHalfUp(a[0]);
                return true;
            }
            if (f == "sign") {
                if (!need(1)) return false;
                out = (a[0] > 0) ? 1 : (a[0] < 0 ? -1 : 0);
                return true;
            }
            if (f == "max" || f == "min") {
                if (a.empty()) {
                    err = f + " 需要参数";
                    return false;
                }
                out = a[0];
                for (std::size_t k = 1; k < a.size(); ++k)
                    out = (f == "max") ? std::max(out, a[k]) : std::min(out, a[k]);
                return true;
            }
            if (f == "gcd" || f == "lcm") {
                if (a.size() < 2) {
                    err = f + " 需要至少 2 个参数";
                    return false;
                }
                long long acc = static_cast<long long>(a[0]);
                for (std::size_t k = 1; k < a.size(); ++k) {
                    long long v = static_cast<long long>(a[k]);
                    long long aa = acc < 0 ? -acc : acc, bb = v < 0 ? -v : v;
                    if (f == "gcd") acc = gcdll(aa, bb);
                    else acc = (aa == 0 || bb == 0) ? 0 : (acc / gcdll(aa, bb)) * v;
                }
                out = static_cast<long double>(acc);
                return true;
            }
            if (f == "deg") {
                if (!need(1)) return false;
                out = a[0] * PI / 180.0L;
                return true;
            }
            if (f == "sum" || f == "prod") {
                if (a.empty()) {
                    err = f + " 需要参数";
                    return false;
                }
                long double acc = (f == "sum") ? 0 : 1;
                for (long double v : a) acc = (f == "sum") ? acc + v : acc * v;
                out = acc;
                return true;
            }
            err = "未知函数 " + f;
            return false;
        }
    }
    err = "不支持的表达式";
    return false;
}

// ============================ 结构工具 ============================

std::set<std::string> collectVars(const NodePtr &n) {
    std::set<std::string> s;
    if (!n) return s;
    if (n->t == NT::Var) {
        bool ok = false;
        constantValue(n->name, ok);
        if (!ok) s.insert(n->name);
    }
    for (auto &k : n->kids) {
        auto sub = collectVars(k);
        s.insert(sub.begin(), sub.end());
    }
    return s;
}

bool substitute(const NodePtr &n, const std::map<std::string, NodePtr> &sub, NodePtr &out) {
    if (!n) return false;
    if (n->t == NT::Var) {
        auto it = sub.find(n->name);
        if (it != sub.end()) {
            out = it->second;
            return true;
        }
        out = n;
        return true;
    }
    auto nn = std::make_shared<Node>();
    nn->t = n->t;
    nn->num = n->num;
    nn->name = n->name;
    nn->pos = n->pos;
    for (auto &k : n->kids) {
        NodePtr c;
        if (!substitute(k, sub, c)) return false;
        nn->kids.push_back(c);
    }
    out = nn;
    return true;
}

bool isNumericConst(const NodePtr &n, long double &out) {
    std::string err;
    return evalApprox(n, {}, out, err);
}

NodePtr simplifyConst(const NodePtr &n) {
    if (!n) return n;
    std::vector<NodePtr> kids;
    for (auto &k : n->kids) kids.push_back(simplifyConst(k));
    if (kids.size() == n->kids.size()) {
        auto nn = std::make_shared<Node>(*n);
        nn->kids = kids;
        if (nn->kids.empty() && nn->t != NT::Num && nn->t != NT::Var) return nn;
        if (collectVars(nn).empty()) {
            Surd s;
            std::string err;
            if (evalExact(nn, {}, s, err) && s.isRational() && s.coef.den().decimalDigits() < 60)
                return Node::num_(s.coef);
        }
        return nn;
    }
    return n;
}

// ============================ 打印 ============================

namespace {

int precOf(const NodePtr &n) {
    switch (n->t) {
        case NT::Add:
        case NT::Sub:
            return 1;
        case NT::Mul:
        case NT::Div:
            return 2;
        case NT::Neg:
            return 3;
        case NT::Pow:
            return 4;
        case NT::Fact:
        case NT::Percent:
            return 5;
        default:
            return 6;
    }
}

bool isSimpleFactor(const NodePtr &n) {
    return n->t == NT::Var || n->t == NT::Call || n->t == NT::Pow || n->t == NT::Mul;
}

std::string plainImpl(const NodePtr &n, int parentPrec, bool rightSide);
std::string latexImpl(const NodePtr &n, int parentPrec, bool rightSide);

std::string kidsPlain(const NodePtr &n) {
    std::string s;
    for (std::size_t i = 0; i < n->kids.size(); ++i) {
        if (i) s += ", ";
        s += plainImpl(n->kids[i], 0, false);
    }
    return s;
}

std::string kidsLatex(const NodePtr &n) {
    std::string s;
    for (std::size_t i = 0; i < n->kids.size(); ++i) {
        if (i) s += ", ";
        s += latexImpl(n->kids[i], 0, false);
    }
    return s;
}

std::string plainImpl(const NodePtr &n, int parentPrec, bool rightSide) {
    int p = precOf(n);
    // 分数/小数在纯文本里写成 "a/b"(除法) 或 "-a/b"(再加负号), 不是原子节点:
    //   2^0.5   -> 2^1/2   (会被读成 (2^1)/2)
    //   1/0.5   -> 1/1/2   (会被读成 (1/1)/2 = 0.5, 实际是 2)
    //   0.25^0.5-> 1/4^1/2
    // 所以按它打印出来的形态降级优先级, 需要时自动补括号。
    // LaTeX 那边不用改: rac{}{} 自带分组。
    if (n->t == NT::Num && !n->num.isInteger()) p = n->num.isNeg() ? 1 : 2;
    std::string body;
    switch (n->t) {
        case NT::Num:
            body = n->num.str();
            break;
        case NT::Var:
            body = (n->name == "pi") ? "\u03c0" : n->name;
            break;
        case NT::Neg: {
            // 分数直接连着负号写: -1/2 (而不是 -(1/2)); 该形态本身已自定括号
            if (n->kids[0]->t == NT::Num) {
                // 子节点通常是非负的字面量, 负号由 Neg 本身提供;
                // 若子节点已带负号(不常见), 直接用它的形态, 避免出现 --
                body = n->kids[0]->num.isNeg() ? n->kids[0]->num.str()
                                               : "-" + n->kids[0]->num.str();
                break;
            }
            int cp = precOf(n->kids[0]);
            body = (cp < 3) ? "-(" + plainImpl(n->kids[0], 0, false) + ")"
                            : "-" + plainImpl(n->kids[0], 3, false);
            break;
        }
        case NT::Add:
        case NT::Sub:
            // 右子树在减法下需要更紧的优先级: a-(b+c) 不能打成 a-b+c
            body = plainImpl(n->kids[0], 1, false) + (n->t == NT::Add ? " + " : " - ") +
                   plainImpl(n->kids[1], (n->t == NT::Add) ? 1 : 2, true);
            break;
        case NT::Mul: {
            const NodePtr &a = n->kids[0];
            const NodePtr &b = n->kids[1];
            if (a->t == NT::Num && !a->num.isInteger() && isSimpleFactor(b)) {
                body = a->num.str() + plainImpl(b, 2, true);
            } else if (a->t == NT::Num && b->t == NT::Var) {
                body = a->num.str() + plainImpl(b, 2, true);
            } else {
                body = plainImpl(a, 2, false) + "*" + plainImpl(b, 2, true);
            }
            break;
        }
        case NT::Div:
            body = plainImpl(n->kids[0], 2, false) + "/" + plainImpl(n->kids[1], 3, true);
            break;
        case NT::Pow:
            // (x^2)^3 的底数必须加括号, 否则会被读成 x^(2^3)
            body = plainImpl(n->kids[0], 5, false) + "^" + plainImpl(n->kids[1], 5, true);
            break;
        case NT::Fact:
            body = plainImpl(n->kids[0], 5, false) + "!";
            break;
        case NT::Percent:
            body = plainImpl(n->kids[0], 5, false) + "%";
            break;
        case NT::Call: {
            const std::string &f = n->name;
            if (f == "sqrt") body = "\u221a(" + kidsPlain(n) + ")";
            else if (f == "cbrt") body = "\u221b(" + kidsPlain(n) + ")";
            else if (f == "root4") body = "\u221c(" + kidsPlain(n) + ")";
            else if (f == "deg") body = kidsPlain(n) + "\u00b0";
            else if (f == "sum") body = "\u2211(" + kidsPlain(n) + ")";
            else if (f == "prod") body = "\u220f(" + kidsPlain(n) + ")";
            else body = f + "(" + kidsPlain(n) + ")";
            break;
        }
    }
    bool needParen = p < parentPrec ||
                     (rightSide && p == parentPrec && (n->t == NT::Sub || n->t == NT::Div || n->t == NT::Neg));
    if (needParen) return "(" + body + ")";
    return body;
}

std::string latexImpl(const NodePtr &n, int parentPrec, bool rightSide) {
    int p = precOf(n);
    std::string body;
    switch (n->t) {
        case NT::Num:
            body = n->num.latex();
            break;
        case NT::Var:
            if (n->name == "pi") body = "\\pi";
            else if (isGreekVarName(n->name)) body = "\\" + n->name;
            else body = n->name;
            break;
        case NT::Neg:
            body = "-" + latexImpl(n->kids[0], 3, false);
            break;
        case NT::Add:
        case NT::Sub:
            body = latexImpl(n->kids[0], 1, false) + (n->t == NT::Add ? " + " : " - ") +
                   latexImpl(n->kids[1], (n->t == NT::Add) ? 1 : 2, true);
            break;
        case NT::Mul: {
            const NodePtr &a = n->kids[0];
            const NodePtr &b = n->kids[1];
            if (a->t == NT::Num && isSimpleFactor(b)) {
                body = a->num.latex() + " " + latexImpl(b, 2, true);
            } else if (b->t == NT::Num) {
                body = latexImpl(a, 2, false) + " \\cdot " + b->num.latex();
            } else {
                body = latexImpl(a, 2, false) + " \\cdot " + latexImpl(b, 2, true);
            }
            break;
        }
        case NT::Div:
            body = "\\frac{" + latexImpl(n->kids[0], 0, false) + "}{" + latexImpl(n->kids[1], 0, false) +
                   "}";
            break;
        case NT::Pow:
            body = latexImpl(n->kids[0], 5, false) + "^{" + latexImpl(n->kids[1], 0, false) + "}";
            break;
        case NT::Fact:
            body = latexImpl(n->kids[0], 5, false) + "!";
            break;
        case NT::Percent:
            body = latexImpl(n->kids[0], 5, false) + "\\%";
            break;
        case NT::Call: {
            const std::string &f = n->name;
            if (f == "sqrt") body = "\\sqrt{" + kidsLatex(n) + "}";
            else if (f == "cbrt") body = "\\sqrt[3]{" + kidsLatex(n) + "}";
            else if (f == "root4") body = "\\sqrt[4]{" + kidsLatex(n) + "}";
            else if (f == "deg") body = kidsLatex(n) + "^{\\circ}";
            else if (f == "sum") body = "\\sum\\left(" + kidsLatex(n) + "\\right)";
            else if (f == "prod") body = "\\prod\\left(" + kidsLatex(n) + "\\right)";
            else if (f == "abs") body = "\\left|" + kidsLatex(n) + "\\right|";
            else if (f == "log2") body = "\\log_{2}\\left(" + kidsLatex(n) + "\\right)";
            else if (f == "log10") body = "\\log_{10}\\left(" + kidsLatex(n) + "\\right)";
            else if (f == "log") body = "\\log\\left(" + kidsLatex(n) + "\\right)";
            else if (f == "asin" || f == "acos" || f == "atan") body = "\\" + f + "\\left(" + kidsLatex(n) + "\\right)";
            else {
                static const std::set<std::string> named = {"sin", "cos", "tan", "cot", "sec", "csc",
                                                            "sinh", "cosh", "tanh", "log", "ln", "exp",
                                                            "max", "min", "gcd", "lcm"};
                if (named.count(f)) body = "\\" + f + "\\left(" + kidsLatex(n) + "\\right)";
                else body = "\\operatorname{" + f + "}\\left(" + kidsLatex(n) + "\\right)";
            }
            break;
        }
    }
    bool needParen = p < parentPrec || (rightSide && p == parentPrec && (n->t == NT::Sub || n->t == NT::Div));
    if (needParen) return "\\left(" + body + "\\right)";
    return body;
}

} // namespace

// ---------- SymPy 语法打印 (供外部引擎使用) ----------
namespace {

std::string sympyKids(const NodePtr &n) {
    std::string s;
    for (std::size_t i = 0; i < n->kids.size(); ++i) {
        if (i) s += ", ";
        s += astSympy(n->kids[i]);
    }
    return s;
}

} // namespace

std::string astSympy(const NodePtr &n) {
    if (!n) return "0";
    switch (n->t) {
        case NT::Num:
            if (n->num.isInteger()) return n->num.num().str();
            return "Rational(" + n->num.num().str() + ", " + n->num.den().str() + ")";
        case NT::Var:
            if (n->name == "pi") return "pi";
            if (n->name == "e") return "E";
            if (n->name == "tau") return "(2*pi)";
            return n->name;
        case NT::Neg:
            return "(-" + astSympy(n->kids[0]) + ")";
        case NT::Add:
            return "(" + astSympy(n->kids[0]) + " + " + astSympy(n->kids[1]) + ")";
        case NT::Sub:
            return "(" + astSympy(n->kids[0]) + " - " + astSympy(n->kids[1]) + ")";
        case NT::Mul:
            return "(" + astSympy(n->kids[0]) + "*" + astSympy(n->kids[1]) + ")";
        case NT::Div:
            return "(" + astSympy(n->kids[0]) + "/" + astSympy(n->kids[1]) + ")";
        case NT::Pow:
            return "(" + astSympy(n->kids[0]) + ")**(" + astSympy(n->kids[1]) + ")";
        case NT::Fact:
            return "factorial(" + astSympy(n->kids[0]) + ")";
        case NT::Percent:
            return "(" + astSympy(n->kids[0]) + "/100)";
        case NT::Call: {
            const std::string &f = n->name;
            if (f == "sqrt") return "sqrt(" + sympyKids(n) + ")";
            if (f == "cbrt") return "cbrt(" + sympyKids(n) + ")";
            if (f == "root4") return "sqrt(sqrt(" + sympyKids(n) + "))";
            if (f == "abs") return "Abs(" + sympyKids(n) + ")";
            if (f == "ln") return "log(" + sympyKids(n) + ")";
            if (f == "log" || f == "log10") return "log(" + sympyKids(n) + ", 10)";
            if (f == "log2") return "log(" + sympyKids(n) + ", 2)";
            if (f == "ceil") return "ceiling(" + sympyKids(n) + ")";
            if (f == "round") return "floor(" + sympyKids(n) + " + Rational(1, 2))";
            if (f == "sign") return "sign(" + sympyKids(n) + ")";
            if (f == "max") return "Max(" + sympyKids(n) + ")";
            if (f == "min") return "Min(" + sympyKids(n) + ")";
            if (f == "deg") return "((" + sympyKids(n) + ")*pi/180)";
            if (f == "sum") return "Add(" + sympyKids(n) + ")";
            if (f == "prod") return "Mul(" + sympyKids(n) + ")";
            return f + "(" + sympyKids(n) + ")";
        }
    }
    return "0";
}

std::string astPlain(const NodePtr &n) { return n ? plainImpl(n, 0, false) : ""; }
std::string astLatex(const NodePtr &n) { return n ? latexImpl(n, 0, false) : ""; }

} // namespace em

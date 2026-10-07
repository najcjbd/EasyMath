// EasyMath - Unicode / 数学输入归一化层实现
#include "unicode.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace em {

// ============================ UTF-8 ============================

std::vector<cp_t> utf8_decode(const std::string &s) {
    std::vector<cp_t> out;
    out.reserve(s.size());
    std::size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        cp_t c = 0;
        int extra = 0;
        if (b < 0x80) {
            c = b;
            extra = 0;
        } else if ((b & 0xE0) == 0xC0) {
            c = b & 0x1F;
            extra = 1;
        } else if ((b & 0xF0) == 0xE0) {
            c = b & 0x0F;
            extra = 2;
        } else if ((b & 0xF8) == 0xF0) {
            c = b & 0x07;
            extra = 3;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (i + static_cast<std::size_t>(extra) >= n) {
            out.push_back(0xFFFD);
            break;
        }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            unsigned char cb = static_cast<unsigned char>(s[i + k]);
            if ((cb & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            c = (c << 6) | (cb & 0x3F);
        }
        if (!ok) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(c);
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

std::string utf8_encode(cp_t c) {
    std::string r;
    if (c < 0x80) {
        r += static_cast<char>(c);
    } else if (c < 0x800) {
        r += static_cast<char>(0xC0 | (c >> 6));
        r += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        r += static_cast<char>(0xE0 | (c >> 12));
        r += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        r += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        r += static_cast<char>(0xF0 | (c >> 18));
        r += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        r += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        r += static_cast<char>(0x80 | (c & 0x3F));
    }
    return r;
}

std::string utf8_encode(const std::vector<cp_t> &v) {
    std::string r;
    for (cp_t c : v) r += utf8_encode(c);
    return r;
}

std::size_t utf8_length(const std::string &s) { return utf8_decode(s).size(); }

std::string utf8_substr_cp(const std::string &s, std::size_t from, std::size_t count) {
    auto v = utf8_decode(s);
    if (from >= v.size()) return "";
    std::size_t to = std::min(v.size(), from + count);
    return utf8_encode(std::vector<cp_t>(v.begin() + static_cast<long>(from), v.begin() + static_cast<long>(to)));
}

// ============================ 字符分类 ============================

bool is_ascii_digit(cp_t c) { return c >= '0' && c <= '9'; }
bool is_ascii_alpha(cp_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_ascii_alnum(cp_t c) { return is_ascii_alpha(c) || is_ascii_digit(c); }
bool is_space_cp(cp_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || c == 0x3000;
}

static bool is_greek_cp(cp_t c) {
    return (c >= 0x0391 && c <= 0x03A9) || (c >= 0x03B1 && c <= 0x03C9) || (c >= 0x03D1 && c <= 0x03D7) ||
           (c >= 0x03F0 && c <= 0x03F5);
}
bool is_ident_start(cp_t c) { return is_ascii_alpha(c) || is_greek_cp(c); }
bool is_ident_cont(cp_t c) { return is_ident_start(c) || is_ascii_digit(c) || c == '_'; }

bool is_default_separator(cp_t c, cp_t prev, cp_t next) {
    if (is_space_cp(c)) return true;
    switch (c) {
        case ',':
        case ';':
        case ':':
        case '@':
        case '#':
        case '$':
        case '?':
        case '~':
        case '`':
        case '\'':
        case '"':
            return true;
        case '\\':
            return !is_ascii_alpha(next); // \frac 等 LaTeX 命令不作为分隔符
        case '|':
        case 0xFF0C:
        case 0x3001:
        case 0xFF1B:
        case 0xFF1A:
        case 0x3002:
        case 0xFF1F:
        case 0xFF01:
        case 0x2018:
        case 0x2019:
        case 0x201C:
        case 0x201D:
            return true;
        case '.':
            return !(is_ascii_digit(prev) && is_ascii_digit(next));
        default:
            return false;
    }
}

bool is_separator_string(const std::string &s, char32_t extra) {
    auto v = utf8_decode(s);
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!is_space_cp(v[i])) {
            cp_t prev = (i > 0) ? v[i - 1] : 0;
            cp_t next = (i + 1 < v.size()) ? v[i + 1] : 0;
            if (v[i] == extra) continue;
            if (!is_default_separator(v[i], prev, next)) return false;
        }
    }
    return true;
}

// ============================ 上下标 ============================

static const std::unordered_map<cp_t, std::string> &sup_table() {
    static const std::unordered_map<cp_t, std::string> t = {
        {0x2070, "0"}, {0x00B9, "1"}, {0x00B2, "2"}, {0x00B3, "3"}, {0x2074, "4"},
        {0x2075, "5"}, {0x2076, "6"}, {0x2077, "7"}, {0x2078, "8"}, {0x2079, "9"},
        {0x207A, "+"}, {0x207B, "-"}, {0x207C, "="}, {0x207D, "("}, {0x207E, ")"},
        {0x207F, "n"}, {0x2071, "i"},
        {0x1D43, "a"}, {0x1D47, "b"}, {0x1D48, "d"}, {0x1D49, "e"}, {0x1D4D, "g"},
        {0x02B0, "h"}, {0x02B2, "j"}, {0x1D4F, "k"}, {0x02E1, "l"}, {0x1D50, "m"},
        {0x1D52, "o"}, {0x1D56, "p"}, {0x02B3, "r"}, {0x02E2, "s"}, {0x1D57, "t"},
        {0x1D58, "u"}, {0x1D5B, "v"}, {0x02B7, "w"}, {0x02E3, "x"}, {0x02B8, "y"},
        {0x1D9C, "c"}, {0x1DA0, "f"},
    };
    return t;
}

static const std::unordered_map<cp_t, std::string> &sub_table() {
    static const std::unordered_map<cp_t, std::string> t = {
        {0x2080, "0"}, {0x2081, "1"}, {0x2082, "2"}, {0x2083, "3"}, {0x2084, "4"},
        {0x2085, "5"}, {0x2086, "6"}, {0x2087, "7"}, {0x2088, "8"}, {0x2089, "9"},
        {0x208A, "+"}, {0x208B, "-"}, {0x208C, "="}, {0x208D, "("}, {0x208E, ")"},
        {0x2090, "a"}, {0x2091, "e"}, {0x2095, "h"}, {0x2096, "k"}, {0x2097, "l"},
        {0x2098, "m"}, {0x2099, "n"}, {0x209A, "p"}, {0x209B, "s"}, {0x209C, "t"},
        {0x2093, "x"}, {0x1D62, "i"}, {0x1D63, "r"}, {0x1D64, "u"}, {0x1D65, "v"},
        {0x2C7C, "j"},
    };
    return t;
}

bool map_superscript(cp_t c, std::string &out) {
    const auto &t = sup_table();
    auto it = t.find(c);
    if (it == t.end()) return false;
    out = it->second;
    return true;
}

bool map_subscript(cp_t c, std::string &out) {
    const auto &t = sub_table();
    auto it = t.find(c);
    if (it == t.end()) return false;
    out = it->second;
    return true;
}

// ============================ 希腊字母 / 数学字母变体 ============================

struct GreekEntry { cp_t c; const char *name; };

static const GreekEntry kGreek[] = {
    {0x0391, "Alpha"}, {0x0392, "Beta"},  {0x0393, "Gamma"}, {0x0394, "Delta"}, {0x0395, "Epsilon"},
    {0x0396, "Zeta"},  {0x0397, "Eta"},   {0x0398, "Theta"}, {0x0399, "Iota"},  {0x039A, "Kappa"},
    {0x039B, "Lambda"},{0x039C, "Mu"},    {0x039D, "Nu"},    {0x039E, "Xi"},    {0x039F, "Omicron"},
    {0x03A0, "Pi"},    {0x03A1, "Rho"},   {0x03A3, "Sigma"}, {0x03A4, "Tau"},   {0x03A5, "Upsilon"},
    {0x03A6, "Phi"},   {0x03A7, "Chi"},   {0x03A8, "Psi"},   {0x03A9, "Omega"},
    {0x03B1, "alpha"}, {0x03B2, "beta"},  {0x03B3, "gamma"}, {0x03B4, "delta"}, {0x03B5, "epsilon"},
    {0x03B6, "zeta"},  {0x03B7, "eta"},   {0x03B8, "theta"}, {0x03B9, "iota"},  {0x03BA, "kappa"},
    {0x03BB, "lambda"},{0x03BC, "mu"},    {0x03BD, "nu"},    {0x03BE, "xi"},    {0x03BF, "omicron"},
    {0x03C0, "pi"},    {0x03C1, "rho"},   {0x03C2, "sigma"}, {0x03C3, "sigma"}, {0x03C4, "tau"},
    {0x03C5, "upsilon"},{0x03C6, "phi"},  {0x03C7, "chi"},   {0x03C8, "psi"},   {0x03C9, "omega"},
    {0x03D1, "theta"}, {0x03D5, "phi"},   {0x03D6, "pi"},    {0x03F0, "kappa"}, {0x03F1, "rho"},
    {0x03F5, "epsilon"},{0x2202, "partial"},{0x2207, "nabla"},
};

bool greek_to_name(cp_t c, std::string &out) {
    for (const auto &e : kGreek) {
        if (e.c == c) {
            out = e.name;
            return true;
        }
    }
    return false;
}

bool math_alphanumeric_to_ascii(cp_t c, std::string &out) {
    if (c >= 0x1D7CE && c <= 0x1D7FF) {
        static const cp_t starts[] = {0x1D7CE, 0x1D7D8, 0x1D7E2, 0x1D7EC, 0x1D7F6};
        for (cp_t st : starts) {
            if (c >= st && c <= st + 9) {
                out = std::string(1, static_cast<char>('0' + (c - st)));
                return true;
            }
        }
    }
    static const cp_t latinBlocks[][2] = {
        {0x1D400, 0x1D41A}, {0x1D434, 0x1D44E}, {0x1D468, 0x1D482}, {0x1D49C, 0x1D4B6},
        {0x1D4D0, 0x1D4EA}, {0x1D504, 0x1D51E}, {0x1D56C, 0x1D586}, {0x1D538, 0x1D552},
        {0x1D5A0, 0x1D5BA}, {0x1D5D4, 0x1D5EE}, {0x1D608, 0x1D622}, {0x1D63C, 0x1D656},
        {0x1D670, 0x1D68A},
    };
    for (const auto &b : latinBlocks) {
        if (c >= b[0] && c <= b[0] + 25) {
            out = std::string(1, static_cast<char>('A' + (c - b[0])));
            return true;
        }
        if (c >= b[1] && c <= b[1] + 25) {
            out = std::string(1, static_cast<char>('a' + (c - b[1])));
            return true;
        }
    }
    static const std::unordered_map<cp_t, std::string> holes = {
        {0x2102, "C"}, {0x210A, "g"}, {0x210B, "H"}, {0x210C, "H"}, {0x210D, "H"}, {0x210E, "h"},
        {0x2110, "I"}, {0x2111, "I"}, {0x2112, "L"}, {0x2115, "N"}, {0x2119, "P"}, {0x211A, "Q"},
        {0x211B, "R"}, {0x211C, "R"}, {0x211D, "R"}, {0x2124, "Z"}, {0x2128, "Z"}, {0x212C, "B"},
        {0x212D, "C"}, {0x212F, "e"}, {0x2130, "E"}, {0x2131, "F"}, {0x2133, "M"}, {0x2134, "o"},
        {0x1D455, "h"}, {0x1D6A4, "i"}, {0x1D6A5, "j"},
    };
    auto it = holes.find(c);
    if (it != holes.end()) {
        out = it->second;
        return true;
    }
    static const cp_t greekStarts[] = {0x1D6A8, 0x1D6E2, 0x1D71C, 0x1D756, 0x1D790, 0x1D7CA};
    for (cp_t S : greekStarts) {
        cp_t capEnd = S + 0x18;
        cp_t smallStart = S + 0x1A, smallEnd = smallStart + 0x18;
        if (c >= S && c <= capEnd) {
            cp_t idx = c - S;
            cp_t plain;
            if (idx <= 16) plain = 0x0391 + idx;
            else if (idx == 17) plain = 0x03D1;
            else plain = 0x03A3 + (idx - 18);
            if (greek_to_name(plain, out)) return true;
        }
        if (c >= smallStart && c <= smallEnd) {
            cp_t idx = c - smallStart;
            cp_t plain;
            if (idx <= 16) plain = 0x03B1 + idx;
            else if (idx == 17) plain = 0x03C2;
            else plain = 0x03C3 + (idx - 18);
            if (greek_to_name(plain, out)) return true;
        }
    }
    return false;
}

// ============================ 中文数学词 ============================

struct WordRepl { const char *from; const char *to; };

std::string replace_math_words(const std::string &in) {
    static const WordRepl kWords[] = {
        {"自然对数", "ln"}, {"常用对数", "log"}, {"平方根", "sqrt"}, {"立方根", "cbrt"},
        {"四次方根", "root4"}, {"根号", "sqrt"}, {"绝对值", "abs"}, {"反正弦", "asin"},
        {"反余弦", "acos"}, {"反正切", "atan"}, {"双曲正弦", "sinh"}, {"双曲余弦", "cosh"},
        {"双曲正切", "tanh"}, {"正弦", "sin"}, {"余弦", "cos"}, {"正切", "tan"},
        {"余切", "cot"}, {"正割", "sec"}, {"余割", "csc"}, {"圆周率", "pi"},
        {"弧度", "rad"}, {"乘以", "*"}, {"乘", "*"}, {"除以", "/"},
        {"加上", "+"}, {"加", "+"}, {"减去", "-"}, {"减", "-"}, {"等于", "="},
        {"degrees", "°"}, {"degree", "°"}, {"dgree", "°"}, {"degs", "°"}, {"deg", "°"},
        {"radians", "rad"}, {"radian", "rad"},
        {"派", "pi"}, {"度", "°"}, {"负", "-"}, {"正", "+"},
    };
    std::string s = in;
    for (const auto &w : kWords) {
        std::string from = w.from, to = w.to;
        if (from.empty()) continue;
        // "deg(" 是函数调用(deg(180) = π, 与 180° 等价), 别换成角度符号 °
        if (from == "deg") {
            std::size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                std::size_t q = pos + from.size();
                while (q < s.size() && (s[q] == ' ' || s[q] == '\t')) ++q;
                if (q < s.size() && s[q] == '(') {
                    pos += from.size();
                    continue;
                }
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
            continue;
        }
        std::size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    return s;
}

// ============================ LaTeX 子集 ============================

static void skip_spaces(const std::string &s, std::size_t &i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

static std::string read_group(const std::string &s, std::size_t &i) {
    skip_spaces(s, i);
    if (i < s.size() && s[i] == '{') {
        int depth = 0;
        std::size_t start = i + 1;
        std::size_t j = i;
        for (; j < s.size(); ++j) {
            if (s[j] == '{') ++depth;
            else if (s[j] == '}') {
                --depth;
                if (depth == 0) break;
            }
        }
        std::string inner = (j <= s.size()) ? s.substr(start, j - start) : s.substr(start);
        i = (j < s.size()) ? j + 1 : s.size();
        return inner;
    }
    if (i >= s.size()) return "";
    if (s[i] == '\\') {
        std::size_t j = i + 1;
        while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) ++j;
        std::string t = s.substr(i, j - i);
        i = j;
        return t;
    }
    unsigned char b = static_cast<unsigned char>(s[i]);
    std::size_t len = 1;
    if ((b & 0xE0) == 0xC0) len = 2;
    else if ((b & 0xF0) == 0xE0) len = 3;
    else if ((b & 0xF8) == 0xF0) len = 4;
    std::string t = s.substr(i, len);
    i += len;
    return t;
}

static std::string latex_to_ascii(const std::string &s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        char ch = s[i];
        if (ch == '$') { ++i; continue; }
        if (ch == '&') { out += ' '; ++i; continue; }
        if (ch == '~') { out += ' '; ++i; continue; }
        if (ch != '\\') {
            if (ch == '{') out += '(';
            else if (ch == '}') out += ')';
            else out += ch;
            ++i;
            continue;
        }
        ++i;
        if (i >= s.size()) { out += '\\'; break; }
        if (!std::isalpha(static_cast<unsigned char>(s[i]))) {
            char c2 = s[i++];
            if (c2 == '\\') { out += ','; continue; }
            if (c2 == ',' || c2 == ';' || c2 == '!' || c2 == ' ') { out += ' '; continue; }
            if (c2 == '{') { out += '('; continue; }
            if (c2 == '}') { out += ')'; continue; }
            if (c2 == '%') { continue; }
            out += c2;
            continue;
        }
        std::size_t j = i;
        while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) ++j;
        std::string cmd = s.substr(i, j - i);
        i = j;

        auto keep_group = [&](const std::string &c) {
            return c == "text" || c == "operatorname" || c == "mathrm" || c == "mathbf" || c == "mathit" ||
                   c == "mathbb" || c == "mathcal" || c == "mathfrak" || c == "mathsf" || c == "mathtt" ||
                   c == "overline" || c == "underline" || c == "bar" || c == "vec" || c == "hat" ||
                   c == "tilde" || c == "dot" || c == "ddot" || c == "boldsymbol" || c == "pmb";
        };
        auto drop_next = [&](const std::string &c) {
            return c == "left" || c == "right" || c == "big" || c == "Big" || c == "bigg" || c == "Bigg" ||
                   c == "bigl" || c == "bigr" || c == "Bigl" || c == "Bigr" || c == "displaystyle" ||
                   c == "textstyle" || c == "limits" || c == "nolimits" || c == "substack";
        };
        if (cmd == "frac" || cmd == "dfrac" || cmd == "tfrac" || cmd == "cfrac") {
            std::string a = read_group(s, i), b = read_group(s, i);
            out += "((" + a + ")/(" + b + "))";
        } else if (cmd == "sqrt") {
            skip_spaces(s, i);
            std::string n;
            if (i < s.size() && s[i] == '[') {
                std::size_t close = s.find(']', i);
                if (close != std::string::npos) {
                    n = s.substr(i + 1, close - i - 1);
                    i = close + 1;
                }
            }
            std::string a = read_group(s, i);
            if (n.empty()) out += "sqrt(" + a + ")";
            else out += "((" + a + ")^(1/(" + n + ")))";
        } else if (cmd == "begin" || cmd == "end") {
            read_group(s, i);
        } else if (cmd == "times" || cmd == "cdot" || cmd == "ast" || cmd == "star") {
            out += '*';
        } else if (cmd == "div") {
            out += '/';
        } else if (cmd == "pm") {
            out += "±";
        } else if (cmd == "mp") {
            out += "∓";
        } else if (cmd == "le" || cmd == "leq" || cmd == "leqslant") {
            out += "<=";
        } else if (cmd == "ge" || cmd == "geq" || cmd == "geqslant") {
            out += ">=";
        } else if (cmd == "ne" || cmd == "neq") {
            out += "!=";
        } else if (cmd == "approx") {
            out += "~=";
        } else if (cmd == "equiv") {
            out += "==";
        } else if (cmd == "infty") {
            out += "inf";
        } else if (cmd == "sum") {
            out += "∑";
        } else if (cmd == "prod") {
            out += "∏";
        } else if (cmd == "int") {
            out += "∫";
        } else if (cmd == "angle") {
            out += "∠";
        } else if (cmd == "circ" || cmd == "degree") {
            out += "°";
        } else if (cmd == "ldots" || cmd == "dots" || cmd == "cdots") {
            out += ',';
        } else if (cmd == "log" || cmd == "lg") {
            std::size_t save = i;
            skip_spaces(s, i);
            if (i < s.size() && s[i] == '_') {
                ++i;
                std::string sub = read_group(s, i);
                out += "log" + sub;
            } else {
                i = save;
                out += (cmd == "lg") ? "log10" : "log";
            }
        } else if (keep_group(cmd)) {
            out += read_group(s, i);
        } else if (drop_next(cmd)) {
            skip_spaces(s, i);
            if (i < s.size() && s[i] == '.') ++i;
        } else {
            static const std::unordered_map<std::string, cp_t> greekNames = {
                {"alpha", 0x03B1}, {"beta", 0x03B2}, {"gamma", 0x03B3}, {"delta", 0x03B4},
                {"epsilon", 0x03B5}, {"varepsilon", 0x03F5}, {"zeta", 0x03B6}, {"eta", 0x03B7},
                {"theta", 0x03B8}, {"vartheta", 0x03D1}, {"iota", 0x03B9}, {"kappa", 0x03BA},
                {"lambda", 0x03BB}, {"mu", 0x03BC}, {"nu", 0x03BD}, {"xi", 0x03BE},
                {"omicron", 0x03BF}, {"pi", 0x03C0}, {"varpi", 0x03D6}, {"rho", 0x03C1},
                {"sigma", 0x03C3}, {"tau", 0x03C4}, {"upsilon", 0x03C5}, {"phi", 0x03C6},
                {"varphi", 0x03D5}, {"chi", 0x03C7}, {"psi", 0x03C8}, {"omega", 0x03C9},
                {"Gamma", 0x0393}, {"Delta", 0x0394}, {"Theta", 0x0398}, {"Lambda", 0x039B},
                {"Xi", 0x039E}, {"Pi", 0x03A0}, {"Sigma", 0x03A3}, {"Phi", 0x03A6},
                {"Psi", 0x03A8}, {"Omega", 0x03A9},
            };
            auto git = greekNames.find(cmd);
            if (git != greekNames.end()) {
                std::string nm2;
                greek_to_name(git->second, nm2);
                out += nm2;
            } else if (cmd == "sin" || cmd == "cos" || cmd == "tan" || cmd == "cot" || cmd == "sec" ||
                       cmd == "csc" || cmd == "arcsin" || cmd == "arccos" || cmd == "arctan" ||
                       cmd == "sinh" || cmd == "cosh" || cmd == "tanh" || cmd == "ln" || cmd == "exp" ||
                       cmd == "abs" || cmd == "max" || cmd == "min" || cmd == "gcd" || cmd == "lcm" ||
                       cmd == "floor" || cmd == "ceil") {
                out += cmd;
            } else {
                out += "\\" + cmd;
            }
        }
    }
    return out;
}

// ============================ 主归一化 ============================

static const std::unordered_map<cp_t, std::string> &char_table() {
    static const std::unordered_map<cp_t, std::string> t = {
        {0xFF10, "0"}, {0xFF11, "1"}, {0xFF12, "2"}, {0xFF13, "3"}, {0xFF14, "4"},
        {0xFF15, "5"}, {0xFF16, "6"}, {0xFF17, "7"}, {0xFF18, "8"}, {0xFF19, "9"},
        {0xFF21, "A"}, {0xFF22, "B"}, {0xFF23, "C"}, {0xFF24, "D"}, {0xFF25, "E"}, {0xFF26, "F"},
        {0xFF27, "G"}, {0xFF28, "H"}, {0xFF29, "I"}, {0xFF2A, "J"}, {0xFF2B, "K"}, {0xFF2C, "L"},
        {0xFF2D, "M"}, {0xFF2E, "N"}, {0xFF2F, "O"}, {0xFF30, "P"}, {0xFF31, "Q"}, {0xFF32, "R"},
        {0xFF33, "S"}, {0xFF34, "T"}, {0xFF35, "U"}, {0xFF36, "V"}, {0xFF37, "W"}, {0xFF38, "X"},
        {0xFF39, "Y"}, {0xFF3A, "Z"},
        {0xFF41, "a"}, {0xFF42, "b"}, {0xFF43, "c"}, {0xFF44, "d"}, {0xFF45, "e"}, {0xFF46, "f"},
        {0xFF47, "g"}, {0xFF48, "h"}, {0xFF49, "i"}, {0xFF4A, "j"}, {0xFF4B, "k"}, {0xFF4C, "l"},
        {0xFF4D, "m"}, {0xFF4E, "n"}, {0xFF4F, "o"}, {0xFF50, "p"}, {0xFF51, "q"}, {0xFF52, "r"},
        {0xFF53, "s"}, {0xFF54, "t"}, {0xFF55, "u"}, {0xFF56, "v"}, {0xFF57, "w"}, {0xFF58, "x"},
        {0xFF59, "y"}, {0xFF5A, "z"},
        {0x00D7, "*"}, {0x2715, "*"}, {0x2716, "*"}, {0x2A2F, "*"}, {0x22C5, "*"}, {0x00B7, "*"},
        {0x2219, "*"}, {0x2217, "*"}, {0x2218, "*"}, {0x2731, "*"}, {0x2062, "*"}, {0xFF0A, "*"},
        {0x00F7, "/"}, {0x2215, "/"}, {0x2044, "/"}, {0xFF0F, "/"}, {0x29F8, "/"},
        {0x2212, "-"}, {0x2013, "-"}, {0x2014, "-"}, {0x2015, "-"}, {0x2010, "-"}, {0xFF0D, "-"},
        {0xFF0B, "+"}, {0x2795, "+"}, {0xFF1D, "="}, {0x2260, "!="}, {0x2264, "<="}, {0x2265, ">="},
        {0xFF3E, "^"}, {0x2192, "->"}, {0x21D2, "=>"},
        {0x221A, "sqrt"}, {0x221B, "cbrt"}, {0x221C, "root4"},
        {0x03C0, "pi"}, {0x03A0, "pi"}, {0x1D70B, "pi"},
        {0x221E, "inf"},
        {0x2220, "∠"},
        {0x2211, "∑"}, {0x220F, "∏"}, {0x222B, "∫"},
        {0x00B0, "°"},
        {0x2032, "'"}, {0x2033, "''"},
        {0x00B1, "±"}, {0x2213, "∓"},
        {0xFF05, "%"},
    };
    return t;
}

std::string normalize_math(const std::string &in) {
    std::string work = in;
    if (work.find('\\') != std::string::npos) {
        work = latex_to_ascii(work);
    } else {
        std::size_t a = work.find_first_not_of(" \t\r\n");
        std::size_t b = work.find_last_not_of(" \t\r\n");
        if (a != std::string::npos && b > a && work[a] == '$') {
            std::size_t da = (work[a + 1] == '$') ? 2 : 1;
            std::size_t db = (work[b] == '$' && b > a && work[b - 1] == '$') ? 2 : 1;
            if (b + 1 - a >= da + db) work = work.substr(a + da, b + 1 - a - da - db);
        }
    }
    work = replace_math_words(work);

    std::vector<cp_t> cps = utf8_decode(work);
    std::string out;
    out.reserve(work.size());
    const auto &ct = char_table();
    for (std::size_t i = 0; i < cps.size(); ++i) {
        cp_t c = cps[i];
        std::string sup;
        if (map_superscript(c, sup)) {
            std::string run = sup;
            std::size_t j = i + 1;
            while (j < cps.size()) {
                std::string t;
                if (!map_superscript(cps[j], t)) break;
                run += t;
                ++j;
            }
            i = j - 1;
            out += "^(" + run + ")";
            continue;
        }
        std::string sub;
        if (map_subscript(c, sub)) {
            std::string run = sub;
            std::size_t j = i + 1;
            while (j < cps.size()) {
                std::string t;
                if (!map_subscript(cps[j], t)) break;
                run += t;
                ++j;
            }
            i = j - 1;
            bool prevIdent = !out.empty() && (std::isalnum(static_cast<unsigned char>(out.back())) ||
                                              out.back() == '_' || out.back() == ')');
            if (prevIdent) out += "_" + run;
            else out += run;
            continue;
        }
        std::string al;
        if (math_alphanumeric_to_ascii(c, al)) {
            out += al;
            continue;
        }
        std::string gn;
        if (greek_to_name(c, gn)) {
            out += gn;
            continue;
        }
        auto it = ct.find(c);
        if (it != ct.end()) {
            out += it->second;
            continue;
        }
        if (c == '{') { out += '('; continue; }
        if (c == '}') { out += ')'; continue; }
        out += utf8_encode(c);
    }
    return out;
}

bool has_degree_suffix(const std::string &normalized) {
    std::size_t e = normalized.size();
    while (e > 0 && std::isspace(static_cast<unsigned char>(normalized[e - 1]))) --e;
    if (e == 0) return false;
    std::string t = normalized.substr(0, e);
    if (t.size() >= 2 && t.compare(t.size() - 2, 2, "\xC2\xB0") == 0) return true;
    if (t.size() >= 3) {
        std::string tail3 = t.substr(t.size() - 3);
        if (tail3 == "deg" || tail3 == "rad") return true;
    }
    return false;
}

} // namespace em

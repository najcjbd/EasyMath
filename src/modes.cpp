// EasyMath - 求值 / 直线 / 解方程 模式实现
#include "modes.hpp"

#include "engine.hpp"
#include "hpnum.hpp"
#include "expr.hpp"
#include "i18n.hpp"
#include "interrupt.hpp"
#include "solve.hpp"
#include "unicode.hpp"
#include "glyph.hpp"
#include "truetype.hpp"
#include "zipfile.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <climits>
#include <sstream>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace em {

// 把 Config 的数值输出策略转成 NumberFormat。
// 走 long double 时, 有效位受平台限制(Windows/macOS-arm64 上等于 double:15 位;
// Linux x86:18 位; Linux arm64:33 位), 超出部分没有意义, 因此限制有效位数。
// 实际"能用"的高精度后端。必须与 highPrecisionValue() 的判定完全一致:
// 曾经这里只看 hpfloat != builtin, 于是把 hpfloat=mpfr 但没有 MPFR 的情况
// 也算成"有高精度", 结果 long double 只算对约 33 位, 却照样打印 50 位小数,
// 后面 17 位是纯噪声(√6 会打成 ...5891240..., 正确是 ...5891391...)。
static bool mpfrUsable(const Config &cfg) {
    return (cfg.hpfloat == "mpfr" || cfg.hpfloat == "auto") && hpAvailable();
}
static bool sympyHighUsable(const Config &cfg) {
    bool want = (cfg.hpfloat == "sympy" || (cfg.hpfloat == "auto" && cfg.engine != "builtin"));
    return want && sympyUsable(cfg.pythonPath);
}
static bool highPrecisionAvailable(const Config &cfg) {
    return mpfrUsable(cfg) || sympyHighUsable(cfg);
}

static NumberFormat numberFormat(const Config &cfg) {
    NumberFormat nf;
    nf.digits = cfg.decimals;
    nf.sciThreshold = cfg.sciThreshold;
    nf.trimZeros = true;
    if (cfg.scientific == "always") nf.sci = SciMode::Always;
    else if (cfg.scientific == "never") nf.sci = SciMode::Never;
    else nf.sci = SciMode::Auto;
    // 只有真的拿得到高精度时才放开位数限制, 否则按平台 long double 截断(不打印噪声位)
    nf.maxSignificant = highPrecisionAvailable(cfg) ? 0 : longDoubleDigits10();
    return nf;
}

static std::string fmtNum(long double v, const Config &cfg) {
    return formatNumber(v, numberFormat(cfg));
}

// 解方程输入的空格分隔: "a=b+c 1/a+1/b=1/c"、"a>b>c>0 a=b+c" 这类写法很常见,
// 但 "x^2 - 5 = 0" 里的空格不能拆。规则: 按空白试拆, 只有当**每一段**都自带
// '=' 或关系运算符时才采用拆分; 否则保持原样(交给解析器给出明确错误)。
static std::vector<std::string> refineEquationItems(const std::vector<std::string> &items) {
    std::vector<std::string> out;
    for (const auto &it : items) {
        bool hasSpace = it.find_first_of(" \t") != std::string::npos;
        std::vector<std::string> segs;
        if (hasSpace) {
            std::string cur;
            for (char c : it) {
                if (c == ' ' || c == '\t') {
                    if (!cur.empty()) { segs.push_back(cur); cur.clear(); }
                } else {
                    cur += c;
                }
            }
            if (!cur.empty()) segs.push_back(cur);
        }
        bool allLookLikeEquation = segs.size() >= 2;
        for (const auto &g : segs) {
            std::string n = normalize_math(g);
            if (n.find('=') == std::string::npos && !textHasRelation(n)) {
                allLookLikeEquation = false;
                break;
            }
        }
        if (allLookLikeEquation) {
            for (auto &g : segs) out.push_back(g);
        } else {
            out.push_back(it);
        }
    }
    return out;
}

// 小数位超过 long double 可靠范围时, 依次尝试 MPFR -> SymPy -> 放弃
static long hpPrecFor(const Config &cfg) {
    if (cfg.precisionBits > 0) return cfg.precisionBits;
    return hpPrecBitsForDigits(cfg.decimals);
}

static bool highPrecisionValue(const NodePtr &n, const Config &cfg, std::string &out) {
    if (cfg.decimals <= 15 || cfg.hpfloat == "builtin") return false;
    std::string err;
    NumberFormat nf = numberFormat(cfg);
    if (mpfrUsable(cfg) &&
        hpEvalToString(n, hpPrecFor(cfg), cfg.decimals, out, err, nf.sci, nf.sciThreshold))
        return true;
    if (sympyHighUsable(cfg)) {
        std::vector<std::string> vals;
        std::string e2;
        NumberFormat nf2 = numberFormat(cfg);
        // 高精度数值请求在手机上本来就慢(每次都要起 Python+mpmath): 给它更长的超时,
        // 宁可等, 也不要因为 15 秒超时退回 long double 只给十几位(实测用户就遇到这个)。
        int hpTimeout = cfg.engineTimeoutMs < 60000 ? 60000 : cfg.engineTimeoutMs;
        if (evalWithSympy({astSympy(n)}, cfg.decimals, cfg.pythonPath, hpTimeout, vals, e2,
                          nf2.sci, nf2.sciThreshold) &&
            !vals.empty()) {
            out = vals[0];
            return true;
        }
    }
    return false;
}

// 小数位钳位: 用户可能手填一个很大的数(比如 5000)。
// 需求: 拿不出那么多位时, 不要打噪声位, 而是**退到本机能精确给出的最大位数**并说明。
//   - 精确有理数: 大整数长除法, 上限只受硬上限 kMaxDecimals 限制
//   - 无理数(√2/π/e/三角): 需要 MPFR 或 SymPy; 都没有就只能到 long double 的可靠位数

// 高精度后端(MPFR/SymPy)没给出数值时, 无理数实际只有 long double 的可靠位数。
// 用户要的位数超过它时, 多出来的全是**二进制尾巴**(不是真数字) —— 这里收住并说明。
static int decimalsForIrrational(const Config &cfg, bool hpOk, double magnitude, std::string &note) {
    if (hpOk) return cfg.decimals;
    int intDigits = 1;
    double av = std::fabs(magnitude);
    if (av >= 1 && std::isfinite(av)) intDigits = int(std::floor(std::log10(av))) + 1;
    int maxExact = em::longDoubleDigits10() - intDigits - 1;
    if (maxExact < 1) maxExact = 1;
    if (cfg.decimals > maxExact) {
        if (note.empty())
            note = L("注意: 这次的高精度后端没给出数值(本机没有 MPFR), 该值只有 ", "note: no high-precision backend for this value; only ") +
                    std::to_string(maxExact) +
                    L(" 位是可靠的, 已按此位数输出(再多就是二进制尾巴, 不是真数字)",
                      " reliable digits; printing that many (more would be binary tail)");
        return maxExact;
    }
    return cfg.decimals;
}

static int clampDecimals(const Config &cfg, bool needIrrational, std::string &note,
                         double magnitude = 1.0) {
    int want = cfg.decimals;
    if (want <= 200) return want;   // 200 以内是原来的正常算法
    if (want > em::kMaxDecimals) {
        note += L("小数位要求 ", "requested decimals ") + std::to_string(want) +
                L(" 超过上限, 已按 ", " exceeds the cap; using ") + std::to_string(em::kMaxDecimals) +
                L(" 位处理。", " instead. ");
        want = em::kMaxDecimals;
    }
    if (needIrrational && !mpfrUsable(cfg) && !sympyHighUsable(cfg)) {
        // long double 的 "33 位" 是**有效数字**: 整数部分也要占位, 末尾再留 1 位安全边界,
        // 否则最后一位会是噪声(测试抓到的: √2 打到 33 位小数时末位就错了)
        int intDigits = 1;
        double av = std::fabs(magnitude);
        if (av >= 1 && std::isfinite(av)) intDigits = int(std::floor(std::log10(av))) + 1;
        int maxExact = em::longDoubleDigits10() - intDigits - 1;
        if (maxExact < 1) maxExact = 1;
        if (want > maxExact) {
            note += L("本机没有 MPFR/SymPy, 无理数最多精确到 ", "no MPFR/SymPy here; irrational values are exact to ") +
                    std::to_string(maxExact) + L(" 位, 已按 ", " digits at most; using ") +
                    std::to_string(maxExact) + L(" 位输出。", " instead. ");
            want = maxExact;
        }
    }
    return want;
}

// ---- 求值模式的复数/常数辅助 ----
namespace {

bool exprHasConstant(const NodePtr &n) {
    if (!n) return false;
    if (n->t == NT::Var && (n->name == "pi" || n->name == "e" || n->name == "tau")) return true;
    for (const auto &k : n->kids)
        if (exprHasConstant(k)) return true;
    return false;
}

bool astHasImagUnit(const NodePtr &n) {
    if (!n) return false;
    if (n->t == NT::Var && n->name == "i") return true;
    for (const auto &k : n->kids)
        if (astHasImagUnit(k)) return true;
    return false;
}

// 把 SymPy 文本里的标识符 i 换成虚数单位 I(只换独立的 i, 不动 pi/sin 里的字母)
std::string sympyImagUnit(const std::string &t) {
    std::string o;
    for (std::size_t i = 0; i < t.size();) {
        unsigned char c = static_cast<unsigned char>(t[i]);
        if (std::isalpha(c) || c == '_') {
            std::size_t j = i;
            while (j < t.size() &&
                   (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '_'))
                ++j;
            std::string w = t.substr(i, j - i);
            o += (w == "i") ? std::string("I") : w;
            i = j;
        } else {
            o.push_back(t[i]);
            ++i;
        }
    }
    return o;
}

// 复数求值: 走 SymPy(能同时给精确形式与数值)。SymPy 不在时如实说明。
bool evalComplexViaSympy(const NodePtr &n, const Config &cfg, std::string &exactPlain,
                         std::string &exactLatex, std::string &numStr, std::string &err) {
    std::vector<std::string> ep, el, vals;
    NumberFormat nf = numberFormat(cfg);
    std::string e2;
    std::string expr = sympyImagUnit(astSympy(n));
    // 精确形式/常数也走 SymPy: 手机上的桥冷启动慢, 给它同样的宽限(超时会让用户看到
    // "精确形式不可用: 未知量 pi" 这类误导信息)
    int symTimeout = cfg.engineTimeoutMs < 60000 ? 60000 : cfg.engineTimeoutMs;
    if (!evalExactAndNumericWithSympy({expr}, cfg.decimals, cfg.pythonPath, symTimeout, ep,
                                      el, vals, e2, nf.sci, nf.sciThreshold)) {
        err = e2.empty() ? L("需要 SymPy 才能算复数(本机没有可用的 python3+sympy)",
                             "complex evaluation needs SymPy (no usable python3+sympy)")
                         : e2;
        return false;
    }
    if (!ep.empty()) exactPlain = ep[0];
    if (!el.empty()) exactLatex = el[0];
    // 显示习惯: SymPy 把自然常数写成 E、把 e^x 写成 exp(x), 这里改成用户更认得的写法
    auto tidyExp = [](std::string t) {
        // 独立标识符 E -> e
        std::string o;
        for (std::size_t i = 0; i < t.size();) {
            if (std::isalpha(static_cast<unsigned char>(t[i])) || t[i] == '_') {
                std::size_t j = i;
                while (j < t.size() && (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '_'))
                    ++j;
                std::string w = t.substr(i, j - i);
                o += (w == "E") ? std::string("e") : w;
                i = j;
            } else {
                o.push_back(t[i]);
                ++i;
            }
        }
        // 整体是 exp(...) 时写成 e^(...)
        if (o.rfind("exp(", 0) == 0 && o.back() == ')')
            o = "e^(" + o.substr(4, o.size() - 5) + ")";
        return o;
    };
    if (!exactPlain.empty()) exactPlain = tidyExp(exactPlain);
    if (!exactLatex.empty()) exactLatex = tidyExp(exactLatex);
    if (!vals.empty()) numStr = vals[0];
    return true;
}

} // namespace

// ============================================================
//                          求值 / 开方
// ============================================================

ModeOutput runEval(const std::string &input, const Config &cfg) {
    ModeOutput mo;
    mo.modeName = "eval";
    mo.title = L("数值求值", "Evaluation");
    mo.saveable = true;
    Report &rep = mo.report;
    rep.title = mo.title;
    rep.line(Chan::Input, L("输入: ", "Input: ") + input);

    char32_t extra = 0;
    if (!cfg.extraSeparator.empty()) extra = utf8_decode(cfg.extraSeparator)[0];
    auto items = splitItems(input, extra, false);
    items = refineEquationItems(items);
    if (items.empty()) items.push_back(input);
    // 函数定义(f(x)=…): 登记后把后续项里的 f(…) 内联, 于是 --eval "f(x)=x^2-3, f(2)" 可用
    {
        FuncDefs fdefs;
        std::vector<std::string> kept;
        for (const auto &it : items) {
            std::string dname, dbody;
            std::vector<std::string> dparams;
            if (extractFuncDef(it, dname, dparams, dbody) && fdefs.defs.find(dname) == fdefs.defs.end()) {
                bool usedInner = false;
                std::string body2 = inlineFuncDefs(dbody, fdefs, usedInner);
                fdefs.defs[dname] = FuncDefs::Def{dparams, body2};
                rep.line(Chan::Note, L("函数定义: ", "function defined: ") + dname + " = " + dbody);
                continue;
            }
            bool used = false;
            std::string ex = inlineFuncDefs(it, fdefs, used);
            kept.push_back(used ? ex : it);
        }
        if (!fdefs.defs.empty()) items = kept;
    }

    int shown = 0;
    bool clampedShown = false;
    for (const auto &raw : items) {
        std::string norm = normalize_math(raw);
        if (norm.empty()) continue;
        ParseOptions po;
        po.numeric_only = true;
        std::string err;
        NodePtr n = parseExpression(norm, po, err);
        if (!n) {
            mo.error = err;
            mo.exitCode = 3;
            rep.line(Chan::Note, L("无法解析: ", "cannot parse: ") + raw + " -> " + err);
            continue;
        }
        ++shown;
        std::string plainExpr = astPlain(n);
        std::string latexExpr = astLatex(n);
        rep.line(Chan::Normalized, L("规范形式: ", "Normalized: ") + plainExpr, latexExpr);

        Surd s;
        std::string e1;
        bool exact = evalExact(n, {}, s, e1);
        long double v = 0;
        std::string e2;
        bool approx = evalApprox(n, {}, v, e2);

        // 含虚数单位(或算到一半发现需要复数): 交给 SymPy, 同时给出精确形式与数值
        bool complexCase = astHasImagUnit(n) ||
                           (!exact && e1.find("复数") != std::string::npos) ||
                           (!exact && !approx && e2.find("复数") != std::string::npos);
        if (complexCase) {
            std::string cp, cl, cn, ce;
            if (!evalComplexViaSympy(n, cfg, cp, cl, cn, ce)) {
                mo.error = ce;
                mo.exitCode = mo.exitCode ? mo.exitCode : 3;
                rep.line(Chan::Note, L("无法求值: ", "cannot evaluate: ") + ce);
                continue;
            }
            std::string line = plainExpr;
            std::string latexLine = latexExpr;
            if (!cp.empty() && cp != plainExpr) {
                line += " = " + cp;
                latexLine += " = " + (cl.empty() ? cp : cl);
            }
            if (!cn.empty()) {
                line += "  (≈ " + cn + ")";
                latexLine += "  (\approx " + cn + ")";
            }
            rep.line(Chan::PlainForm, line, latexLine);
            if (!cn.empty())
                rep.line(Chan::ApproxForm, plainExpr + " \u2248 " + cn,
                         latexExpr + " \\approx " + cn);
            continue;
        }

        std::string plain, latex;
        bool isRat = exact && s.isRational();
        // 手填了很大的小数位时: 先算出"这次能精确给到多少位", 后面一律用它打印
        std::string clampNote;
        Config cPrint = cfg;
        cPrint.decimals = clampDecimals(cfg, !isRat, clampNote, exact ? s.approx() : double(v));
        if (!clampNote.empty() && !clampedShown) {
            clampedShown = true;
            rep.line(Chan::Note, clampNote);
        }
        std::string exactStr = exact ? surdPlainNice(s) : std::string();
        std::string exactLatex = exact ? surdLatexNice(s) : std::string();
        if (!exact) {
            if (!approx) {
                std::string eShow = e2.empty() ? e1 : e2;
                // "规模超限"是用户能自己解决的原因(缩小规模/降低位数), 优先展示它,
                // 而不是让后面的浮点溢出把它盖掉
                if (e1.find("超出精确求值上限") != std::string::npos) eShow = e1;
                mo.error = eShow;
                mo.exitCode = mo.exitCode ? mo.exitCode : 1;
                mo.report.line(Chan::Note, L("无法求值: ", "cannot evaluate: ") + eShow);
                continue;
            }
            std::string approxStr = fmtNum(v, cPrint);
            std::string hp;
            bool hpOk = highPrecisionValue(n, cfg, hp);
            if (hpOk) approxStr = hp;
            cPrint.decimals = decimalsForIrrational(cfg, hpOk, exact ? s.approx() : double(v), clampNote);
            if (!clampNote.empty() && !clampedShown) {
                clampedShown = true;
                rep.line(Chan::Note, clampNote);
            }
            // 含数学常量(pi/e/tau)时, 让 SymPy 给一个精确形式, 例如 pi/6 或 e^2
            // 含数学常量(pi/e/tau)时, 让 SymPy 给一个精确形式(pi/6、e^2、tau=2π …);
            // 精确形式与显示形式相同时(例如 π 本身)也照样算"有精确形式", 只是不重复写等号。
            // 注意: 这里只"多给一行", 不能 continue —— 否则会少掉后面的近似值/小数行,
            // 双引擎对拍(与内置引擎的输出对齐)会因此不一致。
            bool sympyExactGiven = false;
            // 内置精确算不出来时用 SymPy 试一次: asin(1/2)=pi/6、log(8,2)=3、pi/6 都能给精确形式。
            // engine=builtin 或没有 SymPy 就跳过(保持原来的近似行为)。
            if (cfg.engine != "builtin" && sympyUsable(cfg.pythonPath)) {
                std::string cp, cl, cn, ce;
                if (evalComplexViaSympy(n, cfg, cp, cl, cn, ce) && !cp.empty()) {
                    std::string line = plainExpr;
                    std::string ll = latexExpr;
                    if (cp != plainExpr) {
                        line += " = " + cp;
                        ll += " = " + (cl.empty() ? cp : cl);
                    }
                    if (!cn.empty()) {
                        line += "  (\u2248 " + cn + ")";
                        ll += "  (\\approx " + cn + ")";
                    }
                    rep.line(Chan::PlainForm, line, ll);   // 与求值模式其它结果同通道
                    sympyExactGiven = true;
                }
            }
            plain = plainExpr + " \u2248 " + approxStr;
            latex = latexExpr + " \\approx " + approxStr;
            mo.report.line(Chan::ApproxForm,
                           L("近似值: ", "Approximate: ") + plainExpr + " \u2248 " + approxStr,
                           latexExpr + " \\approx " + approxStr);
            if (!e1.empty() && !sympyExactGiven)
                mo.report.line(Chan::Note, L("精确形式不可用: ", "exact form unavailable: ") + e1);
        } else if (exactStr != plainExpr) {
            plain = plainExpr + " = " + exactStr;
            latex = latexExpr + " = " + exactLatex;
        } else if (isRat && !s.coef.isInteger()) {
            plain = plainExpr + " \u2248 " + s.coef.toDecimal(cPrint.decimals);
            latex = plain;
        } else if (!isRat) {
            std::string approxStr = fmtNum(s.approx(), cfg);
            std::string hp;
            bool hpOk = highPrecisionValue(n, cfg, hp);
            if (hpOk) approxStr = hp;
            cPrint.decimals = decimalsForIrrational(cfg, hpOk, exact ? s.approx() : double(v), clampNote);
            if (!clampNote.empty() && !clampedShown) {
                clampedShown = true;
                rep.line(Chan::Note, clampNote);
            }
            plain = plainExpr + " \u2248 " + approxStr;
            latex = latexExpr + " \\approx " + approxStr;
        } else {
            plain = plainExpr;
            latex = latexExpr;
        }
        if (exact && isRat && !s.coef.isInteger()) {
            std::string dec = s.coef.toExactDecimal();
            NumberFormat nfR = numberFormat(cfg);
            std::string decRounded = s.coef.toDecimal(cPrint.decimals);
            long double avR = std::fabs(s.coef.toLongDouble());
            if (avR > 0 && (std::log10(avR) >= nfR.sciThreshold ||
                            std::log10(avR) < -(cfg.decimals + 1)))
                decRounded = formatNumber(s.coef.toLongDouble(), nfR);
            mo.report.line(Chan::DecimalForm,
                           L("小数形式: ", "Decimal: ") + exactStr + " = " + dec + " \u2248 " + decRounded,
                           exactLatex + " = " + dec + " \\approx " + decRounded);
        }
        if (exact && !isRat) {
            std::string approxStr = fmtNum(s.approx(), cfg);
            std::string hp;
            bool hpOk = highPrecisionValue(n, cfg, hp);
            if (hpOk) approxStr = hp;
            cPrint.decimals = decimalsForIrrational(cfg, hpOk, exact ? s.approx() : double(v), clampNote);
            if (!clampNote.empty() && !clampedShown) {
                clampedShown = true;
                rep.line(Chan::Note, clampNote);
            }
            mo.report.line(Chan::ApproxForm,
                           L("近似值: ", "Approximate: ") + plainExpr + " \u2248 " + approxStr,
                           latexExpr + " \\approx " + approxStr);
        }
        mo.report.line(Chan::PlainForm, plain, latex);
        mo.report.line(Chan::LatexForm, latex, latex);
    }
    if (shown == 0) {
        if (mo.error.empty()) mo.error = L("没有可求值的表达式", "no expression to evaluate");
        mo.exitCode = 3;
        return mo;
    }
    mo.ok = true;
    if (mo.exitCode != 0) mo.ok = false;
    return mo;
}

// ============================================================
//                     过原点直线 (角度 -> 函数)
// ============================================================

static bool specialSlope(const Rational &degMod180, Surd &k, bool &vertical, bool &isSpecial) {
    vertical = false;
    isSpecial = true;
    long long d = 0;
    if (!degMod180.isInteger() || !degMod180.num().fitsLongLong(d)) {
        isSpecial = false;
        return false;
    }
    d %= 180;
    if (d < 0) d += 180;
    switch (d) {
        case 0:
            k = Surd(Rational(0));
            return true;
        case 30:
            k = Surd::fromRadical(Rational(1, 3), BigInt(3));
            return true;
        case 45:
            k = Surd(Rational(1));
            return true;
        case 60:
            k = Surd::fromRadical(Rational(1), BigInt(3));
            return true;
        case 90:
            vertical = true;
            k = Surd(Rational(0));
            return true;
        case 120:
            k = Surd::fromRadical(Rational(-1), BigInt(3));
            return true;
        case 135:
            k = Surd(Rational(-1));
            return true;
        case 150:
            k = Surd::fromRadical(Rational(-1, 3), BigInt(3));
            return true;
        default:
            isSpecial = false;
            return false;
    }
}

ModeOutput runLine(const std::string &input, const Config &cfg) {
    ModeOutput mo;
    mo.modeName = "line";
    mo.title = L("过原点的直线", "Line through the origin");
    mo.saveable = true;
    Report &rep = mo.report;
    rep.title = mo.title;
    rep.line(Chan::Input, L("输入: ", "Input: ") + input);

    std::string norm = normalize_math(input);
    std::string body = norm;
    bool degrees = cfg.degreesDefault;
    {
        std::string t = body;
        std::string noSpace;
        for (char c : t)
            if (!std::isspace(static_cast<unsigned char>(c))) noSpace += c;
        body = noSpace;
    }
    if (body.size() >= 2 && body.compare(body.size() - 2, 2, "\xC2\xB0") == 0) {
        degrees = true;
        body = body.substr(0, body.size() - 2);
    } else if (body.size() >= 3 && body.compare(body.size() - 3, 3, "rad") == 0) {
        degrees = false;
        body = body.substr(0, body.size() - 3);
    } else if (body.size() >= 3 && body.compare(body.size() - 3, 3, "\xcf\x80") == 0) {
        degrees = false;
    }
    if (body.empty()) {
        mo.error = L("缺少角度数值", "missing angle value");
        mo.exitCode = 3;
        return mo;
    }
    ParseOptions po;
    po.numeric_only = true;
    std::string err;
    NodePtr n = parseExpression(body, po, err);
    if (!n) {
        mo.error = err;
        mo.exitCode = 3;
        return mo;
    }
    long double angVal = 0;
    std::string e2;
    if (!evalApprox(n, {}, angVal, e2)) {
        mo.error = e2;
        mo.exitCode = 3;
        return mo;
    }
    long double degVal = degrees ? angVal : angVal * 180.0L / 3.141592653589793238462643383279502884197L;
    long double radVal = degrees ? angVal * 3.141592653589793238462643383279502884197L / 180.0L : angVal;

    // 度<->弧度的展示值: 上面的 π 常量虽然给到 40 位(超出 long double 精度), 但
    // long double 本身只有约 33 位(手机上 arm64 相同, 32 位 ARM 只有 15 位),
    // 所以小数位要求超过 15 位时仍优先走高精度路径(MPFR/SymPy)。
    // (曾经: π 常量只有 21 位 → 135° 打成 2.35619449019234492884, 正确是 ...885)。
    std::string degStr = fmtNum(degVal, cfg);
    std::string radStr = fmtNum(radVal, cfg);
    if (cfg.decimals > 15 && cfg.hpfloat != "builtin") {
        NodePtr piNode = Node::var("pi");
        NodePtr deg180 = Node::num_(Rational(180));
        std::string hpDeg, hpRad;
        bool okDeg = false, okRad = false;
        if (degrees) {
            okDeg = highPrecisionValue(n, cfg, hpDeg);
            okRad = highPrecisionValue(Node::op(NT::Div, Node::op(NT::Mul, n, piNode), deg180),
                                       cfg, hpRad);
        } else {
            okRad = highPrecisionValue(n, cfg, hpRad);
            okDeg = highPrecisionValue(Node::op(NT::Div, Node::op(NT::Mul, n, deg180), piNode),
                                       cfg, hpDeg);
        }
        if (okDeg) degStr = hpDeg;
        if (okRad) radStr = hpRad;
    }

    rep.line(Chan::Normalized,
             L("角度: ", "Angle: ") + degStr + "\u00b0 = " + radStr + " rad",
             degStr + "^{\\circ} = " + radStr + "\\,\\mathrm{rad}");

    // 归一化到 [0,180)
    long double d180 = std::fmod(degVal, 180.0L);
    if (d180 < 0) d180 += 180.0L;

    Rational degRat = Rational::fromDouble(degVal, 1000000);
    Surd k;
    bool vertical = false, isSpecial = false;
    bool haveExact = false;
    if (std::fabs(degVal - std::floor(degVal + 0.5L)) < 1e-12L)
        haveExact = specialSlope(degRat, k, vertical, isSpecial);

    std::string plain, latex;
    if (haveExact && isSpecial && vertical) {
        plain = L("垂直于 x 轴: x = 0 (斜率不存在)", "Vertical line: x = 0 (slope undefined)");
        latex = "x = 0";
        rep.line(Chan::Note, L("该直线与 x 轴夹角为 90°, 是 y 轴", "The line is the y-axis (90 to the x-axis)"));
    } else if (haveExact && isSpecial) {
        std::string ks = surdPlainNice(k);
        std::string kl = surdLatexNice(k);
        std::string xPart, xPartL;
        if (k.isRational() && k.coef.isOne()) xPart = "x";
        else if (k.isRational() && k.coef == Rational(-1)) xPart = "-x";
        else if (k.coef.isInteger()) xPart = ks + "x";
        else xPart = "(" + ks + ")x";
        if (k.isRational() && k.coef.isOne()) xPartL = "x";
        else if (k.isRational() && k.coef == Rational(-1)) xPartL = "-x";
        else xPartL = kl + "x";
        if (k.isZero()) {
            plain = "y = 0";
            latex = "y = 0";
        } else {
            plain = "y = " + xPart;
            latex = "y = " + xPartL;
        }
        std::string ksAbs = surdPlainNice(k);
        rep.line(Chan::PlainForm, L("斜率: k = tan(", "Slope: k = tan(") + degStr + "\u00b0) = " + ksAbs,
                 "k = \\tan(" + degStr + "^{\\circ}) = " + surdLatexNice(k));
        rep.line(Chan::Note, L("该角度的正切值可以用根式精确表示", "The tangent has an exact radical form"));
    } else {
        long double kv = std::tan(radVal);
        if (std::fabs(std::cos(radVal)) < 1e-12L) {
            plain = L("垂直于 x 轴: x = 0 (斜率不存在)", "Vertical line: x = 0 (slope undefined)");
            latex = "x = 0";
        } else {
            std::string ks = fmtNum(kv, cfg);
            if (cfg.decimals > 15 && cfg.hpfloat != "builtin") {
                std::string hp, herr;
                bool wantMpfr = (cfg.hpfloat == "auto" || cfg.hpfloat == "mpfr");
                NumberFormat nf = numberFormat(cfg);
                if (wantMpfr && hpAvailable() &&
                    hpTan(n, degrees, hpPrecFor(cfg), cfg.decimals, hp, herr, nf.sci, nf.sciThreshold))
                    ks = hp;
                else if (highPrecisionValue(Node::call("tan", {degrees ? Node::call("deg", {n}) : n}), cfg, hp))
                    ks = hp;
            }
            plain = "y \u2248 " + ks + "x";
            latex = "y \\approx " + ks + "x";
            rep.line(Chan::PlainForm, L("斜率: k = tan(", "Slope: k = tan(") + degStr +
                                          "\u00b0) \u2248 " + ks,
                     "k = \\tan(" + degStr + "^{\\circ}) \\approx " + ks);
            rep.line(Chan::Note, L("正切值无法用简单根式精确表示, 使用近似值(保留 ",
                                   "Tangent has no simple exact radical form; using approximation (") +
                                  std::to_string(cfg.decimals) + L(" 位小数)", " decimals)"));
        }
    }
    rep.line(Chan::Polynomial, L("函数: ", "Function: ") + plain, latex);
    rep.line(Chan::PlainForm, plain, latex);
    rep.line(Chan::LatexForm, latex, latex);
    mo.ok = true;
    return mo;
}

// ============================================================
//                          解方程
// ============================================================

ModeOutput runSolve(const std::string &input, const Config &cfg) {
    ModeOutput mo;
    mo.modeName = "solve";
    mo.title = L("解方程", "Equation solving");
    mo.saveable = true;
    Report &rep = mo.report;
    rep.title = mo.title;
    rep.line(Chan::Input, L("输入: ", "Input: ") + input);

    char32_t extra = 0;
    if (!cfg.extraSeparator.empty()) extra = utf8_decode(cfg.extraSeparator)[0];
    auto items = splitItems(input, extra, false);
    items = refineEquationItems(items);
    // 函数定义(f(x)=…)必须在"引擎分发之前"处理: 否则默认引擎(SymPy)会把 f(x)=0 当成含未知量 f 的方程。
    {
        FuncDefs fdefs;
        std::vector<std::string> kept;
        for (const auto &it : items) {
            std::string dname, dbody;
            std::vector<std::string> dparams;
            if (extractFuncDef(it, dname, dparams, dbody) && fdefs.defs.find(dname) == fdefs.defs.end()) {
                std::string sig = dname + "(";
                for (std::size_t k = 0; k < dparams.size(); ++k) sig += (k ? ", " : "") + dparams[k];
                sig += ")";
                bool usedInner = false;
                std::string body2 = inlineFuncDefs(dbody, fdefs, usedInner);
                fdefs.defs[dname] = FuncDefs::Def{dparams, body2};
                rep.line(Chan::Note, L("函数定义: ", "function defined: ") + sig + " = " + dbody);
                continue;
            }
            bool used = false;
            std::string ex = inlineFuncDefs(it, fdefs, used);
            kept.push_back(used ? ex : it);
        }
        if (!fdefs.defs.empty()) {
            if (kept.empty()) {
                mo.error = L("只给了函数定义, 没有要求解的方程(例如 f(x)=0)", "only function definitions given");
                mo.exitCode = 3;
                return mo;
            }
            items = kept;
        }
    }
    if (items.empty()) {
        mo.error = L("没有输入方程", "no equation given");
        mo.exitCode = 3;
        return mo;
    }
    // 复平面轨迹: |z - c| = r / < r / > r, |z-a| = |z-b| (z、w 当复变量)。
    // 必须放在不等式/引擎处理之前: 否则 |z-1|<2 会被当实数不等式、|z-1|=2 会被当实数绝对值方程。
    SolveResult locusRes;
    bool locusDone = false;
    if (items.size() == 1 && (items[0].find("abs(") != std::string::npos ||
                             items[0].find('|') != std::string::npos))
        locusDone = complexLocusFromText(items[0], locusRes);
    // 有的输入会被 refineEquationItems 拆开(如 |z-1|<2), 所以也直接拿整串原文试一次
    if (!locusDone && (input.find('|') != std::string::npos || input.find("abs(") != std::string::npos))
        locusDone = complexLocusFromText(input, locusRes);

    // 含不等式/非零约束时必须有 SymPy: 内置求解器只处理等式, 不能给出区间与排除条件
    bool hasRel = false;
    for (const auto &it : items)
        if (textHasRelation(normalize_math(it))) hasRel = true;
    if (hasRel) {
        bool usable = (cfg.engine != "builtin") && sympyUsable(cfg.pythonPath);
        if (!usable) {
            mo.error = L("输入里有不等式/非零约束(如 a>b>c>0、abc≠0), 这类需要 SymPy 引擎; "
                         "当前引擎不可用 —— 请安装 python3+sympy, 或把 engine 设为 auto/sympy",
                         "inequality/nonzero constraints require the SymPy engine (unavailable now)");
            mo.exitCode = 1;
            rep.note(L("求解失败: ", "Failed: ") + mo.error);
            return mo;
        }
    }

    SolveOptions so;
    so.decimals = cfg.decimals;
    so.numericInequality = cfg.numericInequality; // 必须在调用 SymPy 之前设置
    so.constants = cfg.constants;
    so.derive = cfg.derive;
    so.realOnly = cfg.realOnly;
    so.complexAllowed = cfg.complexAllowed;
    so.wantSteps = cfg.out.step;
    so.scanLo = cfg.scanLo;
    so.scanHi = cfg.scanHi;
    so.highPrecision = (cfg.hpfloat != "builtin") && hpAvailable();
    so.fmt = numberFormat(cfg);
    so.hpPrecBits = hpPrecFor(cfg);

    SolveResult res;
    bool usedExternal = false;
    std::string eerr2; // SymPy 失败原因(含约束时要直接报出来, 不能回退掩盖)
    if (locusDone) res = locusRes;
    if (!locusDone && cfg.engine != "builtin") {
        EngineInfo ei = detectSympyEngine(cfg.pythonPath);
        if (ei.available) {
            std::string &eerr = eerr2;
            if (solveWithSympy(items, so, cfg.pythonPath, cfg.engineTimeoutMs, res, eerr)) {
                usedExternal = true;
            } else if (cfg.engine == "sympy") {
                rep.line(Chan::Note, L("SymPy 未能求解: ", "SymPy could not solve: ") + eerr);
            } else if (!eerr.empty()) {
                rep.line(Chan::Step, L("SymPy 回退: ", "SymPy fallback: ") + eerr);
            }
        } else if (cfg.engine == "sympy") {
            mo.error = L("外部引擎不可用: ", "external engine unavailable: ") + ei.error;
            mo.exitCode = 1;
            return mo;
        }
    }
    // 含约束时不能回退到内置求解器(它只处理等式, 会报一个与真正原因无关的解析错误)
    if (hasRel && !usedExternal && !locusDone) {   // 轨迹已判定时不要再报"约束求解失败"
        mo.error = L("约束求解失败: ", "constraint solving failed: ") +
                   (eerr2.empty() ? L("SymPy 未返回结果", "SymPy returned nothing") : eerr2);
        mo.exitCode = 1;
        rep.note(L("求解失败: ", "Failed: ") + mo.error);
        return mo;
    }
    so.sympyUsable = sympyUsable(cfg.pythonPath);
    so.sympyTriedAndFailed = (cfg.engine != "builtin") && so.sympyUsable && !usedExternal;
    if (!locusDone && !usedExternal) res = solveEquations(items, so);
    if (!res.ok) {
        mo.error = res.err;
        mo.exitCode = 3;
        rep.note(L("求解失败: ", "Failed: ") + res.err);
        return mo;
    }
    rep.line(Chan::Normalized, L("识别到 ", "detected ") + std::to_string(res.vars.size()) +
                                   L(" 个未知量: ", " unknown(s): ") + [&] {
                                       std::string s;
                                       for (std::size_t i = 0; i < res.vars.size(); ++i) {
                                           if (i) s += ", ";
                                           s += res.vars[i];
                                       }
                                       return s;
                                   }());
    // 不管 SymPy 还是内置引擎, 只要结果里有单变量多项式就生成课堂步骤
    if (res.steps.empty()) {
        if (res.hasPoly) {
            appendPolySteps(res, res.poly);
        } else if (res.hasPolyStrings && !res.polyPlain.empty() && !res.polyVar.empty()) {
            // SymPy 路径给的是多项式字符串, 用同一个解析器再解析成 Poly(往返已被测试覆盖)
            ParseOptions po;
            std::string perr;
            NodePtr pn = parseExpression(normalize_math(res.polyPlain), po, perr);
            Poly pp;
            if (pn && astToPoly(pn, res.polyVar, pp, perr)) appendPolySteps(res, pp);
        }
    }
    // 线性方程组的消元步骤也一样: 引擎无关, 在合流处补
    if (res.steps.empty() && res.vars.size() >= 2 && !res.none && !res.infinite)
        appendLinearStepsFromText(items, res);   // 内部会校验是否真是线性且方程个数=未知量个数
    rep.line(Chan::Step, L("方法: ", "Method: ") + res.method);
    // 真·解题步骤(中学课堂过程): 由求解器给出, --show step 才显示
    for (const auto &st : res.steps) rep.line(Chan::Step, "  " + st, "\\text{" + st + "}");
    for (const auto &n : res.notes) rep.line(Chan::Note, n);

    if (usedExternal && cfg.realOnly) {
        std::vector<RootOut> keep;
        for (const auto &r : res.roots)
            if (!r.isComplex) keep.push_back(r);
        bool hadComplex = keep.size() != res.roots.size();
        res.roots = keep;
        if (res.roots.empty() && hadComplex && !res.hasGeneralSet) {
            res.none = true;
            res.notes.push_back(L("在实数范围内无解", "no real solutions"));
        }
    }
    if (res.identity) {
        rep.line(Chan::SolutionSet, L("解: 任意值(恒等式)", "Solution: any value (identity)"));
        mo.ok = true;
        return mo;
    }
    if (res.none) {
        rep.line(Chan::SolutionSet, L("解: 无解", "Solution: none"));
        mo.ok = true;
        return mo;
    }
    if (res.hasPoly) {
        rep.line(Chan::Polynomial, L("化简为 ", "Simplified to ") + "P(" + res.var + ") = " + res.poly.toPlain(res.var),
                 "P(" + res.var + ") = " + res.poly.toLatex(res.var));
    } else if (res.hasPolyStrings) {
        rep.line(Chan::Polynomial,
                 L("化简为 ", "Simplified to ") + "P(" + res.polyVar + ") = " + res.polyPlain,
                 "P(" + res.polyVar + ") = " + res.polyLatex);
    }
    if (!res.constantsList.empty()) {
        rep.line(Chan::Note, L("常量(不求解): ", "constants (not solved): ") + res.constantsList);
    }
    if (res.hasGeneralSet) {
        rep.line(Chan::SolutionSet, L("解集: ", "Solution set: ") + res.generalSetPlain, res.generalSetLatex);
    }
    if (res.kind == SolveResult::SingleVar) {
        int idx = 1;
        for (const auto &r : res.roots) {
            std::string label = res.var + (res.roots.size() > 1 ? ("_" + std::to_string(idx)) : "");
            std::string mult = r.mult > 1 ? (L(" (", " (") + std::to_string(r.mult) + L(" 重根)", "-fold)")) : "";
            std::string plain = label + " = " + r.plain + mult;
            std::string latex = label + " = " + r.latex + mult;
            if (!r.exact) {
                plain = label + " \u2248 " + r.approxPlain + mult;
                latex = label + " \\approx " + r.approxPlain + mult;
            } else if (r.haveApprox && !r.approxPlain.empty()) {
                plain += L("  (≈ ", "  (≈ ") + r.approxPlain + ")";
                latex += "\\;(\\approx " + r.approxPlain + ")";
            }
            rep.line(Chan::SolutionSet, plain, latex);
            ++idx;
        }
    } else {
        for (std::size_t i = 0; i < res.solutionPlain.size(); ++i) {
            std::string p = res.solutionPlain[i];
            std::string l = (i < res.solutionLatex.size()) ? res.solutionLatex[i] : p;
            if (res.kind == SolveResult::NonlinearSystem && !res.infinite)
                p = L("解 ", "Solution ") + std::to_string(i + 1) + ": " + p;
            rep.line(Chan::SolutionSet, p, l);
            // 约束(不等式/非零): 紧跟在该解之后
            if (i < res.solutionCondPlain.size() && !res.solutionCondPlain[i].empty()) {
                const std::string cl =
                        (i < res.solutionCondLatex.size() && !res.solutionCondLatex[i].empty())
                                ? res.solutionCondLatex[i]
                                : res.solutionCondPlain[i];
                rep.line(Chan::SolutionSet,
                         L("    约束: ", "    subject to: ") + res.solutionCondPlain[i],
                         "\\text{" + L("约束", "subject to") + "}: " + cl);
            }
            // 派生量(由解反推的表达式): 每个解一组, 各占一行
            if (i < res.derivedPlain.size()) {
                for (std::size_t k = 0; k < res.derivedPlain[i].size(); ++k) {
                    const std::string &dp = res.derivedPlain[i][k];
                    const std::string &dl = (k < res.derivedLatex[i].size()) ? res.derivedLatex[i][k] : dp;
                    rep.line(Chan::SolutionSet, L("    求值: ", "    value: ") + dp,
                             "\\text{" + L("求值", "value") + "}: " + dl);
                }
            }
            // 精确解含根式时, 补一行数值(用户要的是"看得出大小 + 位数够")
            if (i < res.solutionApprox.size() && !res.solutionApprox[i].empty()) {
                rep.line(Chan::SolutionSet, L("    数值: ", "    numeric: ") + res.solutionApprox[i],
                         "\\text{" + L("数值", "numeric") + "}: " + res.solutionApprox[i]);
            }
        }
        if (res.infinite && !res.params.empty()) {
            std::string pl;
            for (std::size_t i = 0; i < res.params.size(); ++i) {
                if (i) pl += ", ";
                pl += res.params[i];
            }
            if (res.hasRelational)
                rep.line(Chan::Note, L("其中 ", "where ") + pl +
                                             L(" 为自由参数, 取值范围见下方约束",
                                               " are free parameters; see the constraints below"));
            else
                rep.line(Chan::Note, L("其中 ", "where ") + pl +
                                             L(" 为任意实数(自由参数)", " are free real parameters"));
        }
    }
    // ---- 分组与比例: 纯粹的附加信息 ----
    // 放在最后单独一段(独立通道 group), 前面所有输出与本功能不存在时逐字一致。
    {
        bool anyGroup = false;
        for (const auto &v : res.groupLines)
            if (!v.empty()) anyGroup = true;
        for (const auto &v : res.ratioLabels)
            if (!v.empty()) anyGroup = true;
        if (anyGroup) {
            const std::size_t nSol = res.solutionPlain.size();
            const std::size_t rows = std::max(res.groupLines.size(), res.ratioLabels.size());
            for (std::size_t i = 0; i < rows; ++i) {
                // 多解时标明这条属于哪个解, 单解时不啰嗦
                const std::string pre = (nSol > 1)
                        ? (L("解 ", "solution ") + std::to_string(i + 1) + " ")
                        : std::string();
                if (i < res.groupLines.size())
                    for (const auto &g : res.groupLines[i])
                        rep.line(Chan::Group, "    " + pre + g);
                if (i < res.ratioLabels.size()) {
                    for (std::size_t k = 0; k < res.ratioLabels[i].size(); ++k) {
                        const std::string &lab = res.ratioLabels[i][k];
                        const std::string &rp = (k < res.ratioPlain[i].size()) ? res.ratioPlain[i][k] : "";
                        const std::string &rl = (k < res.ratioLatex[i].size()) ? res.ratioLatex[i][k] : rp;
                        bool num = (i < res.ratioNumeric.size() && k < res.ratioNumeric[i].size())
                                       ? res.ratioNumeric[i][k] : true;
                        const std::string tag = num ? L("比例", "ratio")
                                                    : L("比例(含未确定量)",
                                                        "ratio (in terms of parameters)");
                        rep.line(Chan::Group,
                                 "    " + pre + tag + ": " + lab + " = " + rp,
                                 "\\text{" + pre + tag + "}: " + lab + " = " + rl);
                    }
                }
            }
        }
    }
    mo.ok = true;
    return mo;
}

// ---------------- 字形模式: 文字/手绘 -> 函数 ----------------
namespace {

std::string formatSci(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3e", v);
    return std::string(buf);
}

std::string glyphNum(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return std::string(buf);
}

std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool fileReadable(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (f) {
        std::fclose(f);
        return true;
    }
    return false;
}

// 找字体包: cfg.fontPack > ~/Math/vivo_Sans.zip > ./vivo_Sans.zip > ~/vivo_Sans.zip
std::string resolveFontPath(const Config &cfg, std::string &err) {
    if (!cfg.fontPack.empty()) {
        if (!fileReadable(cfg.fontPack)) {
            err = L("指定的字体包打不开: ", "cannot open font pack: ") + cfg.fontPack;
            return std::string();
        }
        return cfg.fontPack;
    }
    std::vector<std::string> cands;
    const char *home = std::getenv("HOME");
    if (home) cands.push_back(std::string(home) + "/Math/vivo_Sans.zip");
    cands.push_back("vivo_Sans.zip");
    if (home) cands.push_back(std::string(home) + "/vivo_Sans.zip");
    for (const auto &c : cands)
        if (fileReadable(c)) return c;
    err = L("没找到字体包: 用 --font-pack 指定 .zip 或 .ttf(默认找 ~/Math/vivo_Sans.zip)",
            "no font pack found: pass --font-pack (.zip or .ttf); default looks at ~/Math/vivo_Sans.zip");
    return std::string();
}

// 挑字体:
//   1) --font 是数字 -> 按 fontPackList() 里的序号(1 起)
//   2) --font 给了名字子串 -> 大小写不敏感匹配文件名, 多个命中取最大的(中文覆盖更全)
//   3) 没给(默认) -> 优先 vivo Sans(用户提供的那套), 再优先 Regular, 再优先体积大
//      (vivo Sans 简体 Regular 约 7.5MB, 中英文都全; Global 只有拉丁且小得多)
// 返回 false 时 err 里会说明怎么列出可选项。
// macOS 压缩时会在 __MACOSX/ 下塞一份 "._xxx" 的资源叉文件, 不是真字体:
// 既不能用来选(会解析失败), 也会打乱序号, 统一过滤掉。
std::vector<std::size_t> usableTtfEntries(const ZipArchive &z) {
    std::vector<std::size_t> out;
    for (std::size_t i : z.findSuffix(".ttf")) {
        const ZipEntry &e = z.entries()[i];
        std::string n = e.nameAscii;
        std::size_t slash = n.find_last_of('/');
        std::string base = (slash == std::string::npos) ? n : n.substr(slash + 1);
        if (n.rfind("__MACOSX", 0) == 0) continue;
        if (base.rfind("._", 0) == 0) continue;
        if (e.size < 1024) continue; // 太小的一定不是字体
        out.push_back(i);
    }
    return out;
}

bool pickFontIndex(const ZipArchive &z, const std::vector<std::size_t> &ttfs,
                   const std::string &pickSpec, std::size_t &pick, std::string &err) {
    if (ttfs.empty()) {
        err = L("字体包里没有 .ttf", "no .ttf in the pack");
        return false;
    }
    std::string spec = pickSpec;
    {
        std::size_t b = spec.find_first_not_of(" \t\r\n");
        std::size_t e2 = spec.find_last_not_of(" \t\r\n");
        spec = (b == std::string::npos) ? std::string() : spec.substr(b, e2 - b + 1);
    }
    if (spec.empty()) { // 默认: 显式优先 vivo Sans
        long bestScore = LONG_MIN;
        std::size_t bestSize = 0;
        for (std::size_t i : ttfs) {
            const ZipEntry &e = z.entries()[i];
            std::string n = lowerAscii(e.nameAscii);
            long score = 0;
            if (n.find("vivosans") != std::string::npos) score += 1000;   // vivo Sans 家族
            if (n.find("regular") != std::string::npos) score += 200;     // 正体
            if (n.find("italic") != std::string::npos) score -= 100;
            if (n.find("global") != std::string::npos) score -= 50;       // Global 只有拉丁
            if (n.find("cond") != std::string::npos || n.find("exp") != std::string::npos) score -= 50;
            if (score > bestScore || (score == bestScore && e.size > bestSize)) {
                bestScore = score;
                bestSize = e.size;
                pick = i;
            }
        }
        return true;
    }
    bool numeric = !spec.empty();
    for (char c : spec)
        if (!std::isdigit(static_cast<unsigned char>(c))) numeric = false;
    if (numeric) {
        long idx = std::atol(spec.c_str());
        if (idx < 1 || idx > long(ttfs.size())) {
            err = L("字体序号超出范围(1..", "font index out of range (1..") +
                  std::to_string(ttfs.size()) + L("); 用 --font-list 看可选项",
                                                  "); use --font-list to see the options");
            return false;
        }
        pick = ttfs[std::size_t(idx) - 1];
        return true;
    }
    std::string want = lowerAscii(spec);
    bool found = false;
    long bestScore = LONG_MIN;
    std::size_t bestSize = 0;
    for (std::size_t i : ttfs) {
        const ZipEntry &e = z.entries()[i];
        std::string n = lowerAscii(e.nameAscii);
        // 名字里可能有中文(如 "vivo Sans简体"): 除了 ASCII 抹平后的名字, 也比对解码后的真实名字
        std::string nreal = lowerAscii(e.name);
        if (n.find(want) == std::string::npos && nreal.find(want) == std::string::npos) continue;
        long score = 0;
        if (n.find("regular") != std::string::npos) score += 10;   // 同一家族优先正体
        if (n.find("italic") != std::string::npos) score -= 5;
        if (!found || score > bestScore || (score == bestScore && e.size > bestSize)) {
            found = true;
            bestScore = score;
            bestSize = e.size;
            pick = i;
        }
    }
    if (!found) {
        err = L("字体包里没有名字含 \"", "no font name contains \"") + spec +
              L("\" 的字体; 用 --font-list 看有哪些", "\"; use --font-list to list them");
        return false;
    }
    return true;
}

std::string shortName(const std::string &asciiPath) {
    std::string n = asciiPath;
    std::size_t slash = n.find_last_of('/');
    if (slash != std::string::npos) n = n.substr(slash + 1);
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    return n;
}

bool loadFontBytes(const Config &cfg, std::vector<uint8_t> &data, std::string &desc, std::string &err,
                   std::size_t *pickOut = nullptr, bool *isZipOut = nullptr) {
    std::string path = resolveFontPath(cfg, err);
    if (path.empty()) return false;
    bool isZip = path.size() > 4 && lowerAscii(path.substr(path.size() - 4)) == ".zip";
    if (!isZip) {
        if (!readWholeFile(path, data, err)) return false;
        desc = path;
        if (isZipOut) *isZipOut = false;
        return true;
    }
    ZipArchive z;
    if (!z.openPath(path, err)) return false;
    auto ttfs = usableTtfEntries(z);
    std::size_t pick = 0;
    if (!pickFontIndex(z, ttfs, cfg.fontPick, pick, err)) return false;
    if (!z.extract(pick, data, err)) return false;
    desc = shortName(z.entries()[pick].name) + " @ " + path;
    if (pickOut) *pickOut = pick;      // 绝对条目序号(用于回退时跳过它)
    if (isZipOut) *isZipOut = true;
    return true;
}


// 字体包内的"回退字体": 主字体没有某个字形时, 依次在包里找别的字体。
// 用户报过的问题: 包里明明有中文字体, 但默认选中的那一份没有该字, 结果报"缺字"。
struct FontFallback {
    ZipArchive zip;
    std::vector<std::size_t> entries;   // 可用 .ttf 的绝对序号
    std::vector<std::unique_ptr<TrueTypeFont>> cache;
    std::vector<char> loaded;
    std::size_t primary = std::string::npos;
    std::string path;
    bool active = false;
    std::vector<std::string> usedNames;   // 实际用上的回退字体(去重, 用于提示)

    // 找第一个含 cp 的字体; 找不到返回 nullptr(此时确实缺字)
    const TrueTypeFont *find(uint32_t cp, std::string &err) {
        for (std::size_t k = 0; k < entries.size(); ++k) {
            if (entries[k] == primary) continue;
            if (!loaded[k]) {
                loaded[k] = 1;
                std::vector<uint8_t> d;
                std::string e2;
                if (!zip.extract(entries[k], d, e2)) continue;
                auto f = std::unique_ptr<TrueTypeFont>(new TrueTypeFont());
                if (!f->load(std::move(d), e2)) continue;
                cache[k] = std::move(f);
            }
            if (cache[k] && cache[k]->glyphIndex(cp) != 0) {
                std::string nm = shortName(zip.entries()[entries[k]].name);
                if (std::find(usedNames.begin(), usedNames.end(), nm) == usedNames.end())
                    usedNames.push_back(nm);
                return cache[k].get();
            }
        }
        (void)err;
        return nullptr;
    }
};

bool openFontFallback(const Config &cfg, const std::string &path, std::size_t primaryEntry,
                      FontFallback &fb, std::string &err) {
    fb.active = false;
    if (!cfg.fontFallback) return true;
    if (!fb.zip.openPath(path, err)) return true;   // 打不开就悄悄放弃回退(主字体照常用)
    fb.entries = usableTtfEntries(fb.zip);
    if (fb.entries.size() < 2) return true;
    fb.cache.resize(fb.entries.size());
    fb.loaded.assign(fb.entries.size(), 0);
    fb.primary = primaryEntry;
    fb.path = path;
    fb.active = true;
    return true;
}

} // namespace

// 机器可读的字体清单(安卓"选择字体"对话框用):
//   CHOSEN\t<默认序号>\nFONT\t<序号>\t<KB>\t<名字>\n…
// 失败时第一行是 ERR\t<原因>

// 可变字体信息(fvar): 轴与命名实例。给 --font-variations 用, 也是"非默认实例"的第一阶段。

// 把配置里的 --font-instance / --font-axis 解析成"每个轴的用户坐标", 并做严格校验。
// 非法实例值、未知轴标签、超出轴范围都明确报错(不静默取默认)。
namespace {
// 数值转字符串(去掉尾随 0): 400.0000 -> 400, 0.5000 -> 0.5
std::string fmtG(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string t(buf);
    while (t.size() > 1 && t.back() == '0') t.pop_back();
    if (!t.empty() && t.back() == '.') t.pop_back();
    return t;
}
} // namespace

static bool resolveAxisCoords(const Config &cfg, const std::vector<TrueTypeFont::VarAxis> &axes,
                              const std::vector<TrueTypeFont::VarInstance> &insts,
                              std::vector<double> &out, std::string &err) {
    out.clear();
    for (const auto &a : axes) out.push_back(a.defV);
    if (!cfg.fontInstance.empty()) {
        const std::string &want = cfg.fontInstance;
        long idx = -1;
        bool numeric = true;
        for (char c : want)
            if (!std::isdigit(static_cast<unsigned char>(c))) numeric = false;
        if (numeric) idx = std::atol(want.c_str()) - 1;   // 序号从 1 开始(与 --font-list 一致)
        long found = -1;
        if (idx >= 0 && std::size_t(idx) < insts.size()) found = idx;
        else {
            for (std::size_t i = 0; i < insts.size(); ++i)
                if (insts[i].name == want) { found = long(i); break; }
        }
        if (found < 0) {
            err = L("字体实例不存在: ", "no such font instance: ") + want + L("(可用: 1..", " (available: 1..") +
                  std::to_string(insts.size()) + L(" 或实例名; 用 --font-variations 看列表)", ")");
            return false;
        }
        if (insts[std::size_t(found)].coords.size() == axes.size())
            out = insts[std::size_t(found)].coords;
        return true;
    }
    if (cfg.fontAxis.empty()) return true;
    // 解析 tag=value,tag=value
    std::string spec = cfg.fontAxis;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : spec) {
        if (c == ',' || c == ';' || c == ' ') { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) parts.push_back(cur);
    for (const auto &pt : parts) {
        std::size_t eq = pt.find('=');
        if (eq == std::string::npos) {
            err = L("轴值要写成 标签=数值, 例如 wght=700; 收到: ", "axis values look like tag=value: ") + pt;
            return false;
        }
        std::string tag = pt.substr(0, eq), val = pt.substr(eq + 1);
        bool found = false;
        for (std::size_t i = 0; i < axes.size(); ++i) {
            if (axes[i].tag != tag) continue;
            found = true;
            double v = 0;
            try {
                v = std::stod(val);
            } catch (...) {
                err = L("轴 ", "axis ") + tag + L(" 的值不是数字: ", " value is not a number: ") + val;
                return false;
            }
            if (v < axes[i].minV - 1e-9 || v > axes[i].maxV + 1e-9) {
                err = L("轴 ", "axis ") + tag + L(" 的值超出范围 [", " out of range [") + fmtG(axes[i].minV) +
                      ", " + fmtG(axes[i].maxV) + L("]: ", "]: ") + val;
                return false;
            }
            out[i] = v;
        }
        if (!found) {
            err = L("字体没有这个轴: ", "no such axis: ") + tag + L("(用 --font-variations 看有哪些轴)", "");
            return false;
        }
    }
    return true;
}

// --varied <字符>: 报告"在配置的轴/实例下"该字符的点数与包围盒(用于验证插值真的改变了轮廓)

// --kern <两个字符>: 报告 GPOS kern 的字距值(字体单位)
std::string kernInfo(const Config &cfg, const std::string &pair, std::string &err) {
    std::vector<uint8_t> data;
    std::string desc;
    if (!loadFontBytes(cfg, data, desc, err)) return "";
    TrueTypeFont f;
    std::string ferr;
    if (!f.load(std::move(data), ferr)) { err = ferr; return ""; }
    auto cps = utf8_decode(pair);
    if (cps.size() < 2) { err = L("请给两个字, 例如 AV", "give two characters, e.g. AV"); return ""; }
    int l = f.glyphIndex(uint32_t(cps[0])), r = f.glyphIndex(uint32_t(cps[1]));
    std::string out = "FONT\t" + desc + "\n";
    out += std::string("HASKERN\t") + (f.hasKern() ? "1" : "0") + "\n";
    out += std::string("PAIR\t") + pair + "\t字距=" + std::to_string(f.pairKern(l, r)) + "\t(字体单位; 负=更紧)\n";
    return out;
}


// OTF/CFF 文字 -> 函数(多字符): cmap 取字形、hmtx 取推进宽度、Type2 解释后走同一套拟合。
// 说明: 这是 CFF 字体的"正常入口"在 CLI 侧的落地; 桌面交互模式仍走 glyf 路径。
std::string cffTextFunctions(const Config &cfg, const std::string &text, std::string &err) {
    std::vector<uint8_t> buf;
    if (!readWholeFile(cfg.fontPack, buf, err)) return "";
    CffInfo ci;
    std::string e0;
    if (!cffReadInfo(buf, ci, e0)) { err = e0; return ""; }
    // hmtx: numberOfHMetrics 来自 hhea(偏移 34)
    long nMetrics = 0, hmtxOff = 0;
    {
        auto u16 = [&](std::size_t i) { return uint16_t((uint16_t(buf[i]) << 8) | buf[i + 1]); };
        auto u32 = [&](std::size_t i) {
            return uint32_t((uint32_t(buf[i]) << 24) | (uint32_t(buf[i + 1]) << 16) |
                            (uint32_t(buf[i + 2]) << 8) | buf[i + 3]);
        };
        std::size_t num = u16(4);
        for (std::size_t i = 0; i < num; ++i) {
            std::size_t r = 12 + i * 16;
            if (r + 16 > buf.size()) break;
            if (std::memcmp(&buf[r], "hhea", 4) == 0) {
                std::size_t ho = u32(r + 8);
                if (ho + 36 <= buf.size()) nMetrics = int16_t(u16(ho + 34));
            }
            if (std::memcmp(&buf[r], "hmtx", 4) == 0) hmtxOff = u32(r + 8);
        }
    }
    std::string out;
    double pen = 0;
    auto cps = utf8_decode(text);
    int drawn = 0, segTotal = 0;
    for (cp_t c : cps) {
        if (is_space_cp(c)) { pen += 250; continue; }
        int gid = 0;
        std::string cerr;
        if (!cffCmapLookup(buf, uint32_t(c), gid, cerr)) {
            out += L("缺字: ", "missing: ") + utf8_encode(c) + "\n";
            continue;
        }
        int adv = 500;
        if (hmtxOff && nMetrics > 0) {
            long k = std::min<long>(gid, nMetrics - 1);
            std::size_t p2 = hmtxOff + std::size_t(k) * 4;
            if (p2 + 2 <= buf.size()) adv = int((uint16_t(buf[p2]) << 8) | buf[p2 + 1]);
        }
        CffPath path;
        std::string perr;
        if (!cffGlyphPath(buf, gid, path, perr)) {
            out += L("取轮廓失败: ", "outline failed: ") + utf8_encode(c) + " —— " + perr + "\n";
            pen += adv;
            continue;
        }
        // 相邻采样点连成折线, 按字符原点平移后拟合
        TtContour cur;
        auto addLine = [&](double ax, double ay, double bx, double by) {
            TtSeg sg;
            sg.kind = TtKind::Line;
            sg.p0.x = ax; sg.p0.y = ay; sg.p1.x = bx; sg.p1.y = by;
            cur.segs.push_back(sg);
        };
        for (std::size_t i = 1; i < path.pts.size(); ++i)
            addLine(path.pts[i-1].first, path.pts[i-1].second, path.pts[i].first, path.pts[i].second);
        out += L("字符 ", "char ") + utf8_encode(c) + L(" (gid ", " (gid ") + std::to_string(gid) +
               L(", 推进 ", ", advance ") + std::to_string(adv) + L("):", "):") + "\n";
        if (!cur.segs.empty()) {
            GlyphContour gc = fitContour(cur, 0.5, true, 0.35, 1.0, pen, 0);
            for (std::size_t k = 0; k < gc.segs.size(); ++k) {
                out += "  段" + std::to_string(k + 1) + ": " + segPlain(gc.segs[k], int(k), "x") + "\n";
                ++segTotal;
            }
        }
        pen += adv;
        ++drawn;
    }
    std::string head = L("OTF(CFF) 文字 -> 函数: 字符 ", "OTF/CFF text: chars ") + std::to_string(drawn) +
                       L(" 个, 段合计 ", ", segments ") + std::to_string(segTotal) +
                       L("; 基线笔位累计 ", "; pen total ") + glyphNum(pen) + "\n";
    return head + out;
}

std::string variedInfo(const Config &cfg, const std::string &ch, std::string &err) {
    std::vector<uint8_t> data;
    std::string desc;
    if (!loadFontBytes(cfg, data, desc, err)) return "";
    TrueTypeFont f;
    std::string ferr;
    if (!f.load(std::move(data), ferr)) { err = ferr; return ""; }
    std::vector<TrueTypeFont::VarAxis> axes;
    std::vector<TrueTypeFont::VarInstance> insts;
    bool isVar = f.variations(axes, insts);
    std::vector<double> coords;
    if (isVar && !resolveAxisCoords(cfg, axes, insts, coords, err)) return "ERR\t" + err + "\n";
    auto cps = utf8_decode(ch);
    if (cps.empty()) { err = L("请给一个字符", "give one character"); return ""; }
    int gid = f.glyphIndex(uint32_t(cps[0]));
    if (!gid) { err = L("字体里没有这个字", "glyph not in font"); return ""; }
    std::vector<double> xs, ys;
    std::vector<int> ep;
    bool sparse = false;
    if (!f.variedGlyphPoints(gid, coords, xs, ys, ep, sparse)) {
        err = L("取不到该字形的点(可能是复合字形)", "cannot read points (composite?)");
        return "";
    }
    double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        x0 = std::min(x0, xs[i]); x1 = std::max(x1, xs[i]);
        y0 = std::min(y0, ys[i]); y1 = std::max(y1, ys[i]);
    }
    std::string out = "FONT\t" + desc + "\n";
    out += std::string("VARIABLE\t") + (isVar ? "1" : "0") + "\n";
    out += "COORDS";
    for (std::size_t i = 0; i < axes.size(); ++i)
        out += "\t" + axes[i].tag + "=" + fmtG(i < coords.size() ? coords[i] : 0);
    out += "\n";
    out += "GLYPH\t" + ch + "\t点=" + std::to_string(xs.size()) + "\t包围盒 x[" + fmtG(x0) + "," +
           fmtG(x1) + "] y[" + fmtG(y0) + "," + fmtG(y1) + "]" +
           (sparse ? L("\t(该字形含稀疏点数元组, 已按默认字重)", "\t(sparse tuples; using default)") : "") + "\n";
    return out;
}

std::string fontVariations(const Config &cfg, std::string &err) {
    auto num = [](double v) {   // 去掉尾随 0, 例: 400.000 -> 400, 0.500 -> 0.5
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f", v);
        std::string t(buf);
        while (t.size() > 1 && t.back() == '0') t.pop_back();
        if (!t.empty() && t.back() == '.') t.pop_back();
        return t;
    };
    std::vector<uint8_t> data;
    std::string desc;
    if (!loadFontBytes(cfg, data, desc, err)) return "";
    TrueTypeFont f;
    std::string ferr;
    if (!f.load(std::move(data), ferr)) {
        err = ferr;
        return "";
    }
    std::vector<TrueTypeFont::VarAxis> axes;
    std::vector<TrueTypeFont::VarInstance> insts;
    std::string out = "FONT\t" + desc + "\n";
    if (!f.variations(axes, insts)) {
        out += L("VARIABLE\t0\t(这份字体不是可变字体)\n", "VARIABLE\t0\t(not a variable font)\n");
        return out;
    }
    out += "VARIABLE\t1\n";
    for (const auto &a : axes) {
        std::string nm = a.name.empty() ? std::string() : ("\t" + a.name);
        out += "AXIS\t" + a.tag + "\t" + num(a.minV) + "\t" + num(a.defV) + "\t" +
               num(a.maxV) + nm + "\n";
    }
    for (std::size_t i = 0; i < insts.size(); ++i) {
        out += "INSTANCE\t" + std::to_string(i + 1) + "\t" + insts[i].name + "\t";
        for (std::size_t k = 0; k < insts[i].coords.size(); ++k)
            out += (k ? "," : "") + num(insts[i].coords[k]);
        out += "\n";
    }
    return out;
}

std::string fontPackEntries(const Config &cfg, std::string &err) {
    std::string path = resolveFontPath(cfg, err);
    if (path.empty()) return "ERR\t" + err + "\n";
    bool isZip = path.size() > 4 && lowerAscii(path.substr(path.size() - 4)) == ".zip";
    if (!isZip) return "CHOSEN\t1\nFONT\t1\t0\t" + path + "\n";
    ZipArchive z;
    if (!z.openPath(path, err)) return "ERR\t" + err + "\n";
    auto ttfs = usableTtfEntries(z);
    std::size_t pick = 0;
    std::string perr;
    bool haveDefault = pickFontIndex(z, ttfs, "", pick, perr);
    std::string out;
    out += "CHOSEN\t" + std::to_string(haveDefault ? pick + 1 : 1) + "\n";
    for (std::size_t k = 0; k < ttfs.size(); ++k) {
        const ZipEntry &e = z.entries()[ttfs[k]];
        // RAWHEX 是原始名字字节: 安卓(Bionic 没有 iconv)用它按 GBK 解码出正确名字
        out += "FONT\t" + std::to_string(k + 1) + "\t" + std::to_string(e.size / 1024) + "\t" +
               e.name + "\t" + e.nameRawHex + "\n";
    }
    out += "PACK\t" + path + "\n";
    return out;
}

// 列出字体包里的字体(给 --font-list 和安卓的"选择字体"用)
std::string fontPackList(const Config &cfg, std::string &err, std::string *chosenOut) {
    std::string path = resolveFontPath(cfg, err);
    if (path.empty()) return std::string();
    bool isZip = path.size() > 4 && lowerAscii(path.substr(path.size() - 4)) == ".zip";
    std::ostringstream o;
    if (!isZip) {
        o << L("不是 .zip, 直接就是单个字体文件: ", "not a .zip; it is a single font file: ") << path
          << "\n";
        if (chosenOut) *chosenOut = "1";
        return o.str();
    }
    ZipArchive z;
    if (!z.openPath(path, err)) return std::string();
    auto ttfs = usableTtfEntries(z);
    std::size_t pick = 0;
    std::string perr;
    bool haveDefault = pickFontIndex(z, ttfs, "", pick, perr);
    o << L("字体包: ", "font pack: ") << path << L("  (共 ", "  (") << ttfs.size() << L(" 个 .ttf)", " .ttf)")
      << "\n";
    for (std::size_t k = 0; k < ttfs.size(); ++k) {
        const ZipEntry &e = z.entries()[ttfs[k]];
        o << "  " << (k + 1) << ") " << (e.size / 1024) << "KB\t" << e.name
          << ((haveDefault && ttfs[k] == pick) ? L("   <- 默认", "   <- default") : "") << "\n";
    }
    if (chosenOut) *chosenOut = haveDefault ? std::to_string(pick + 1) : std::string("1");
    o << L("用 --font <序号|名字子串> 换字体(默认优先 vivo Sans 的 Regular)",
           "use --font <index|name substring> to switch (default prefers vivo Sans Regular)")
      << "\n";
    return o.str();
}

// 前向声明: 定义在文件后部(轴/实例 -> 每轴用户坐标, 带严格校验)
static bool resolveAxisCoords(const Config &cfg, const std::vector<TrueTypeFont::VarAxis> &axes,
                              const std::vector<TrueTypeFont::VarInstance> &insts,
                              std::vector<double> &out, std::string &err);

ModeOutput runGlyph(const std::string &input, const Config &cfg) {
    ModeOutput mo;
    mo.title = L("字形 → 函数", "Glyph → functions");
    mo.modeName = L("字形", "glyph");
    Report &rep = mo.report;
    // ---- 手绘(安卓): 输入是笔画, 走中心线拟合, 不用字体 ----
    if (!cfg.strokes.empty()) {
        std::vector<std::vector<std::pair<double, double>>> strokes;
        for (std::size_t i = 0; i < cfg.strokes.size();) {
            std::size_t bar = cfg.strokes.find('|', i);
            std::string one = cfg.strokes.substr(i, bar == std::string::npos ? std::string::npos : bar - i);
            std::vector<std::pair<double, double>> st;
            std::size_t j = 0;
            while (j < one.size()) {
                std::size_t semi = one.find(';', j);
                std::string pairTxt = one.substr(j, semi == std::string::npos ? std::string::npos : semi - j);
                std::size_t comma = pairTxt.find(',');
                if (comma != std::string::npos) {
                    try {
                        double x = std::stod(pairTxt.substr(0, comma));
                        double y = std::stod(pairTxt.substr(comma + 1));
                        st.push_back({x, y});
                    } catch (...) {
                    }
                }
                if (semi == std::string::npos) break;
                j = semi + 1;
            }
            if (st.size() >= 2) strokes.push_back(std::move(st));
            if (bar == std::string::npos) break;
            i = bar + 1;
        }
        if (strokes.empty()) {
            mo.exitCode = 3;
            mo.error = L("没有有效的笔画(每个笔画至少两个点)", "no valid strokes (need >=2 points each)");
            return mo;
        }
        AnchorSpec anchor = parseAnchor(cfg.glyphAnchor);
        if (!anchor.ok) {
            mo.exitCode = 3;
            mo.error = anchor.err;
            return mo;
        }
        GlyphShape sh;
        std::string serr;
        if (!shapeFromStrokes(strokes, std::max(0.5, cfg.glyphTol), sh, serr)) {
            mo.exitCode = 3;
            mo.error = serr;
            return mo;
        }
        double dx = anchor.x - sh.x0, dy = anchor.y - sh.y0;
        if (anchor.mirrorX) {
            mirrorShapeX(sh, sh.x0 + sh.x1);
            dx = anchor.x - sh.x0;
        }
        translateShape(sh, dx, dy);
        rep.line(Chan::Note, L("手绘: 笔画 ", "hand-drawn: strokes ") + std::to_string(strokes.size()) +
                                     L(" 条 → ", " -> ") + std::to_string(sh.segCount()) + L(" 个函数", " functions") +
                                     L(", 拟合容差 ", ", fit tolerance ") + glyphNum(std::max(0.5, cfg.glyphTol)));
        int ci = 0;
        for (const auto &c : sh.contours) {
            ++ci;
            int si = 0;
            for (const auto &sg : c.segs) {
                ++si;
                std::string tag = L("  笔画", "  stroke") + std::to_string(ci) + L(" 段", " seg") +
                                  std::to_string(si) + ": ";
                rep.line(Chan::SolutionSet,
                         tag + segPlain(sg, cfg.decimals) + (sg.exact ? "" : L("  (近似)", "  (approx)")),
                         "\\text{" + L("笔画", "stroke") + std::to_string(ci) + "\\ " + L("段", "seg") +
                                 std::to_string(si) + ":}\\ " + segLatex(sg, cfg.decimals));
            }
        }
        for (const auto &ef : sh.explicitFns)
            rep.line(Chan::SolutionSet, L("  显式: ", "  explicit: ") + explicitPlain(ef, cfg.decimals) +
                                                (ef.exact ? "" : L("  (近似)", "  (approx)")),
                     "\\text{" + L("显式", "explicit") + ":}\\ " + explicitLatex(ef, cfg.decimals));
        rep.line(Chan::SolutionSet, L("合计: 函数 ", "total: functions ") + std::to_string(sh.segCount()) +
                                         L(" 个", ""));
        rep.line(Chan::Note, L("左下角坐标: (", "anchor (bottom-left): (") + glyphNum(anchor.x) + ", " +
                                     glyphNum(anchor.y) + ")" +
                                     (anchor.mirrorX ? L(" 已左右镜像", " mirrored") : ""));
        rep.extraHtml = "<div class=\"glyph-preview\">\n" + shapeSvg(sh, 3, 24.0) + "</div>\n";
        mo.previewSvg = shapeSvg(sh, 3, 24.0);
        mo.saveable = true;
        mo.ok = true;
        return mo;
    }
    if (input.empty()) {
        mo.exitCode = 3;
        mo.error = L("没有输入文字", "no text given");
        return mo;
    }
    // 末尾内联坐标: 形如 "你好 (10,20)" 或 ":你好 (10,20)" 都行
    std::string text = input;
    std::string anchorText = cfg.glyphAnchor;
    if (anchorText.empty()) {
        std::size_t close = text.find_last_not_of(" \t\r\n");
        if (close != std::string::npos && text[close] == ')') {
            std::size_t open = text.rfind('(', close);
            if (open != std::string::npos) {
                std::size_t start = open;
                if (start > 0 && text[start - 1] == ':') --start;
                AnchorSpec trial = parseAnchor(text.substr(start, close - start + 1));
                if (trial.ok) {
                    anchorText = text.substr(start, close - start + 1);
                    text = text.substr(0, start);
                }
            }
        }
    }
    AnchorSpec anchor = parseAnchor(anchorText);
    if (!anchor.ok) {
        mo.exitCode = 3;
        mo.error = anchor.err;
        return mo;
    }
    std::vector<uint8_t> fontData;
    std::string desc, err;
    std::size_t primaryEntry = std::string::npos;
    bool packIsZip = false;
    if (!loadFontBytes(cfg, fontData, desc, err, &primaryEntry, &packIsZip)) {
        mo.exitCode = 3;
        mo.error = err;
        return mo;
    }
    TrueTypeFont font;
    if (!font.load(std::move(fontData), err)) {
        mo.exitCode = 3;
        mo.error = L("字体解析失败: ", "font parse failed: ") + err;
        return mo;
    }
    // 可变字体: 若配置了 --font-axis / --font-instance, 解析成每轴坐标并接入轮廓
    std::vector<double> axisCoords;
    {
        std::vector<TrueTypeFont::VarAxis> vaxes;
        std::vector<TrueTypeFont::VarInstance> vinsts;
        if (font.variations(vaxes, vinsts) && (!cfg.fontAxis.empty() || !cfg.fontInstance.empty())) {
            std::string aerr;
            if (!resolveAxisCoords(cfg, vaxes, vinsts, axisCoords, aerr)) {
                mo.error = L("变体设置无效: ", "invalid variation setting: ") + aerr;
                mo.exitCode = 1;
                return mo;
            }
            std::string what;
            for (std::size_t k = 0; k < vaxes.size(); ++k) {
                if (k) what += ", ";
                what += vaxes[k].tag + "=" + std::to_string(int(axisCoords[k]));
            }
            rep.line(Chan::Note, L("变体: ", "variation: ") + what +
                                 (cfg.fontInstance.empty() ? std::string() : L(" (来自实例 ", " (from instance ") +
                                                                     cfg.fontInstance + ")"));
        } else if (!cfg.fontAxis.empty() || !cfg.fontInstance.empty()) {
            rep.line(Chan::Note, L("提示: 这份字体不是可变字体, 已忽略轴/实例设置",
                                   "note: not a variable font; axis/instance ignored"));
        }
    }
    bool sparseWarned = false;
    FontFallback fallback;
    if (packIsZip) {
        std::string ferr;
        std::string packPath = resolveFontPath(cfg, ferr);
        openFontFallback(cfg, packPath, primaryEntry, fallback, ferr);
    }
    const double size = cfg.glyphSize;
    int prevGid = -1;                 // GPOS 成对字距: 上一个字形号
    double kernTotal = 0;             // 本行字距合计(字体单位), 仅用于说明"是否发生字距"
    double prevAdvance = 0;           // 上一字的推进(连字替换时要回退)
    std::string prevChar;             // 上一字原文(提示用)
    int ligaCount = 0;
    const double scale = size / double(font.unitsPerEm());
    const double tol = cfg.glyphTol;
    if (interruptRequested()) return mo;

    rep.line(Chan::Note, L("字体: ", "font: ") + (font.familyName().empty() ? desc : font.familyName()) +
                                 L("  (", "  (") + desc + L("), unitsPerEm=", "), unitsPerEm=") +
                                 std::to_string(font.unitsPerEm()) +
                                 L(", 字号=", ", size=") + glyphNum(size) +
                                 L(", 拟合容差=", ", fit tolerance=") + glyphNum(tol) +
                                 L(" 字体单位", " font units"));
    if (anchor.mirrorX)
        rep.line(Chan::Note, L("已按要求左右镜像(: 前缀)", "mirrored left-right (the ':' prefix)"));

    struct Item {
        std::string ch;
        GlyphShape shape;
    };
    std::vector<Item> items;
    double pen = 0;
    int missing = 0, drawn = 0;
    for (cp_t cp : utf8_decode(text)) {
        if (cp == '\n' || cp == '\r') continue;
        int gid = font.glyphIndex(uint32_t(cp));
        double adv = (gid ? double(font.advance(gid)) : double(font.unitsPerEm()) / 2.0) * scale;
        if (cp == ' ' || cp == '\t') {
            pen += adv;
            continue;
        }
        const TrueTypeFont *useFont = &font;
        if (gid == 0) {
            // 主字体没有 -> 在字体包里找别的字体(用户报过: 明明有中文字体却报"缺字")
            std::string ferr;
            const TrueTypeFont *alt = fallback.active ? fallback.find(uint32_t(cp), ferr) : nullptr;
            if (alt) {
                useFont = alt;
                int g2 = alt->glyphIndex(uint32_t(cp));
                adv = double(alt->advance(g2)) * (size / double(alt->unitsPerEm()));
                rep.line(Chan::Note, L("回退字体: ", "fallback font: ") + utf8_encode(cp) + L(" 用 ", " -> ") +
                                             (alt->familyName().empty() ? std::string("?") : alt->familyName()));
            } else {
                ++missing;
                rep.line(Chan::Note, L("缺字: ", "missing glyph: ") + utf8_encode(cp) +
                                             L("(整个字体包里都没有, 已跳过)", " (not in any font of the pack, skipped)"));
                pen += adv;
                continue;
            }
        }
        // GSUB 连字: 前一字形 + 当前字形 若能合成连字, 则回退上一字并用连字字形重新生成
        int forceGid = -1;
        {
            int gcur = useFont->glyphIndex(uint32_t(cp));
            if (cfg.liga && prevGid >= 0 && gcur != 0) {
                int lg = useFont->ligature(prevGid, gcur);
                if (lg != 0) {
                    pen -= prevAdvance;                 // 回到上一字的起点
                    if (!items.empty()) items.pop_back();
                    forceGid = lg;
                    prevGid = lg;                       // 后续字与连字字形继续配对
                    prevAdvance = 0;
                    ++ligaCount;
                    rep.line(Chan::Tip, L("连字: ", "ligature: ") + prevChar + utf8_encode(cp) +
                                         L(" -> 字形 ", " -> glyph ") + std::to_string(lg));
                }
            }
        }
        // GPOS 成对字距: 加在"推进之前"(负值=更紧)
        {
            int g2 = useFont->glyphIndex(uint32_t(cp));
            if (cfg.kern && prevGid >= 0 && g2 != 0) {
                int kv = useFont->pairKern(prevGid, g2);
                if (kv != 0) {
                    double ks = double(kv) * (size / double(useFont->unitsPerEm()));
                    pen += ks;
                    kernTotal += ks;
                }
            }
            prevGid = g2;
        }
        GlyphShape sh;
        bool sparseGlyph = false;
        if (!shapeFromGlyph(*useFont, uint32_t(cp), size, pen, 0, tol, sh, err,
                            useFont == &font ? axisCoords : std::vector<double>(), &sparseGlyph, forceGid)) {
            ++missing;
            rep.line(Chan::Note, L("无法解析: ", "cannot trace: ") + utf8_encode(cp) + " —— " + err);
            pen += adv;
            continue;
        }
        prevAdvance = adv;
        prevChar = utf8_encode(cp);
        pen += adv;
        ++drawn;
        items.push_back(Item{utf8_encode(cp), std::move(sh)});
        if (interruptRequested()) return mo;
    }
    if (items.empty()) {
        if (cfg.kern && kernTotal != 0)
            rep.line(Chan::Tip, L("字距调整(GPOS kern): 本行合计 ", "GPOS kern total: ") +
                                fmtG(kernTotal) + L(" 字体单位", " font units"));
        mo.exitCode = 3;
        mo.error = L("没有可以还原的字(全部缺失或是空白)", "nothing to trace (all missing or blank)");
        return mo;
    }
    // 合并成一个图形: 供预览与整体定位
    GlyphShape all;
    all.x0 = all.y0 = 1e18;
    all.x1 = all.y1 = -1e18;
    for (auto &it : items) {
        for (const auto &c : it.shape.contours) all.contours.push_back(c);
        for (const auto &f : it.shape.explicitFns) all.explicitFns.push_back(f);
        all.x0 = std::min(all.x0, it.shape.x0);
        all.y0 = std::min(all.y0, it.shape.y0);
        all.x1 = std::max(all.x1, it.shape.x1);
        all.y1 = std::max(all.y1, it.shape.y1);
    }
    all.srcSegs = 0;
    for (auto &it : items) all.srcSegs += it.shape.srcSegs;
    // 镜像(在自己的盒子里左右翻, 锚点仍是左下角) + 平移到锚点
    if (anchor.mirrorX) {
        for (auto &it : items) {
            mirrorShapeX(it.shape, it.shape.x0 + it.shape.x1);
            it.shape.x0 = it.shape.x1 = 0; // 已就地翻好, 下面统一重算
        }
        mirrorShapeX(all, all.x0 + all.x1);
        all.x0 = all.x1 = 0;
        // 重新计算包围盒
        all.x0 = all.y0 = 1e18;
        all.x1 = all.y1 = -1e18;
        all.contours.clear();
        all.explicitFns.clear();
        for (auto &it : items) {
            for (const auto &c : it.shape.contours) all.contours.push_back(c);
            for (const auto &f : it.shape.explicitFns) all.explicitFns.push_back(f);
            all.x0 = std::min(all.x0, it.shape.x0);
            all.y0 = std::min(all.y0, it.shape.y0);
            all.x1 = std::max(all.x1, it.shape.x1);
            all.y1 = std::max(all.y1, it.shape.y1);
        }
    }
    double dx = anchor.x - all.x0;
    double dy = anchor.y - all.y0;
    translateShape(all, dx, dy);
    for (auto &it : items) translateShape(it.shape, dx, dy);

    // 输出: 每个字的函数
    int totalFns = 0, totalExplicit = 0, totalSrc = 0;
    for (auto &it : items) {
        const GlyphShape &sh = it.shape;
        totalFns += sh.segCount();
        totalExplicit += int(sh.explicitFns.size());
        totalSrc += sh.srcSegs;
        rep.line(Chan::SolutionSet,
                 L("字形 \"", "glyph \"") + it.ch + L("\": ", "\": ") + shapeStats(sh),
                 "\\text{" + L("字形", "glyph") + " }\\text{" + it.ch + "}: " +
                         std::to_string(sh.segCount()) + L(" 个函数", " functions"));
        int ci = 0;
        for (const auto &c : sh.contours) {
            ++ci;
            int si = 0;
            for (const auto &s : c.segs) {
                ++si;
                std::string tag = L("  轮廓", "  contour") + std::to_string(ci) + L(" 段", " seg") +
                                  std::to_string(si) + ": ";
                rep.line(Chan::SolutionSet, tag + segPlain(s, cfg.decimals) + (s.exact ? "" : L("  (近似)", "  (approx)")),
                         "\\text{" + L("轮廓", "contour") + std::to_string(ci) + "\\ " + L("段", "seg") +
                                 std::to_string(si) + ":}\\ " + segLatex(s, cfg.decimals) +
                                 (s.exact ? "" : "\\ \\text{" + L("近似", "approx") + "}"));
            }
        }
        for (const auto &f : sh.explicitFns) {
            rep.line(Chan::SolutionSet,
                     L("  显式: ", "  explicit: ") + explicitPlain(f, cfg.decimals) +
                             (f.exact ? "" : L("  (近似)", "  (approx)")),
                     "\\text{" + L("显式", "explicit") + ":}\\ " + explicitLatex(f, cfg.decimals) +
                             (f.exact ? "" : "\\ \\text{" + L("近似", "approx") + "}"));
        }
    }
    // 统计与诚实边界
    std::string notes = L("合计: 字形 ", "total: glyphs ") + std::to_string(drawn) +
                        L(" 个, 函数 ", " , functions ") + std::to_string(totalFns) +
                        L(" 个(字体自带分段 ", " (font segments ") + std::to_string(totalSrc) + ")";
    if (totalExplicit) notes += L(", 其中显式 y=f(x) ", ", explicit y=f(x) ") + std::to_string(totalExplicit) +
                                L(" 条", "");
    if (missing) notes += L("; 缺字 ", "; missing ") + std::to_string(missing) + L(" 个", "");
    rep.line(Chan::SolutionSet, notes);
    rep.line(Chan::Tip,
             L("说明: 系数后没标\"近似\"的就是由字体原始分段精确换算来的(有理数); 标了的是在容差内拟合出来的。"
               "函数数不会比字体自带分段更多。已做 GPOS 成对字距(--kern 查字对, --no-kern 关闭)与 GSUB 连字(--no-liga 关闭)。",
               "note: coefficients without 'approx' come exactly from the font's own segments (rationals); "
               "the others are fitted within tolerance. Function count never exceeds the font's own segment count. "
               "Kerning/ligatures are not applied."));
    if (!anchorText.empty())
        rep.line(Chan::Note, L("左下角坐标: (", "anchor (bottom-left): (") + glyphNum(anchor.x) + ", " +
                                      glyphNum(anchor.y) + ")" +
                                      (anchor.mirrorX ? L(" 已左右镜像", " mirrored") : ""));

    if (fallback.active && !fallback.usedNames.empty()) {
        std::string names;
        for (std::size_t i = 0; i < fallback.usedNames.size(); ++i) {
            if (i) names += ", ";
            names += fallback.usedNames[i];
        }
        rep.line(Chan::Tip, L("提示: 主字体缺的字是从同一字体包里的这些字体补的: ", 
                              "note: missing glyphs were taken from other fonts in the same pack: ") + names +
                                L("(可用 --font 换主字体, --no-font-fallback 关掉)", " (use --font / --no-font-fallback)"));
    }
    // 预览(SVG): 自包含。顺便做一次"图 = 函数"自检并打在结果里:
    // 把预览路径反解回多项式, 与打印出来的那些函数逐段比对(偏差只应来自小数位舍入)
    const int svgDecimals = 3;
    mo.previewSvg = shapeSvg(all, svgDecimals, std::max(1.0, cfg.glyphSize / 20.0));
    rep.extraHtml = "<div class=\"glyph-preview\">\n" + mo.previewSvg + "</div>\n";
    GlyphSelfCheck chk = glyphSelfCheck(all, svgDecimals, cfg.decimals);
    if (chk.ok)
        rep.line(Chan::Verify,
                 L("自检: 预览图 ↔ 函数 最大偏差 ", "self-check: preview vs functions max deviation ") +
                         formatSci(chk.svgVsCurve) + L(" (打印 ", " (printed to ") +
                         std::to_string(svgDecimals) + L(" 位小数)", " decimals)") +
                         L("; 函数文本(", "; function text (") + std::to_string(cfg.decimals) +
                         L(" 位小数)相对误差 ", " decimals) relative error ") +
                         formatSci(chk.textLoss) + L(", 共 ", ", ") + std::to_string(chk.segs) +
                         L(" 段", " segments"));
    else
        rep.line(Chan::Verify, L("自检未通过: ", "self-check failed: ") + chk.detail);
    mo.saveable = true;
    mo.ok = true;
    return mo;
}

std::string guessMode(const std::string &input) {
    std::string norm = normalize_math(input);
    bool hasEq = norm.find('=') != std::string::npos;
    int commas = 0;
    for (char c : norm)
        if (c == ',' || c == ';') ++commas;
    if (hasEq && commas >= 2) return "lagrange";
    if (hasEq) return "solve";
    bool deg = false;
    for (char c : input)
        if (c == 'd' || c == 'D') deg = true;
    if (norm.find("\xC2\xB0") != std::string::npos) deg = true;
    (void)deg;
    return "eval";
}



// ============================================================
//                      拉格朗日插值
// ============================================================

namespace {

struct OptRat {
    bool set = false;
    Rational v;
};

std::string trimWs(const std::string &s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 安全解析点序号: 超过范围时报错而不是抛异常
bool parseIndexText(const std::string &t, int &out) {
    if (t.empty() || t.size() > 9) return false;
    for (char c : t)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    long v = std::strtol(t.c_str(), nullptr, 10);
    if (v < 0 || v > 100000000L) return false;
    out = static_cast<int>(v);
    return true;
}

bool parseCoordName(const std::string &in, bool &isX, int &index, std::string &why) {
    std::string s;
    for (char c : in)
        if (!std::isspace(static_cast<unsigned char>(c))) s += c;
    if (s.empty()) {
        why = L("空的坐标名", "empty coordinate name");
        return false;
    }
    char c0 = s[0];
    auto digitsOnly = [](const std::string &t) {
        if (t.empty()) return false;
        for (char c : t)
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        return true;
    };
    if (c0 == 'x' || c0 == 'X' || c0 == 'y' || c0 == 'Y') {
        isX = (c0 == 'x' || c0 == 'X');
        std::string rest = s.substr(1);
        if (!rest.empty() && (rest[0] == '_' || rest[0] == '^')) rest = rest.substr(1);
        if (rest.size() >= 2 && rest.front() == '(' && rest.back() == ')') rest = rest.substr(1, rest.size() - 2);
        if (rest.empty()) {
            index = -1;
            return true;
        }
        if (digitsOnly(rest)) {
            if (!parseIndexText(rest, index)) {
                why = L("点序号超出范围(0..100000000): ", "point index out of range (0..100000000): ") + rest;
                return false;
            }
            return true;
        }
        why = L("无法识别的坐标名: ", "unrecognized coordinate name: ") + in;
        return false;
    }
    if (std::isdigit(static_cast<unsigned char>(c0))) {
        std::size_t i = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && i + 1 == s.size()) {
            char c = s[i];
            if (c == 'x' || c == 'X' || c == 'y' || c == 'Y') {
                isX = (c == 'x' || c == 'X');
                if (!parseIndexText(s.substr(0, i), index)) {
                    why = L("点序号超出范围(0..100000000): ", "point index out of range: ") + s.substr(0, i);
                    return false;
                }
                return true;
            }
        }
    }
    why = L("无法识别的坐标名: ", "unrecognized coordinate name: ") + in;
    return false;
}

bool evalRationalValue(const std::string &exprRaw, Rational &out, std::string &err) {
    std::string norm = normalize_math(exprRaw);
    ParseOptions po;
    po.numeric_only = true;
    std::string perr;
    NodePtr n = parseExpression(norm, po, perr);
    if (!n) {
        err = perr;
        return false;
    }
    Surd s;
    std::string e1;
    if (evalExact(n, {}, s, e1) && s.isRational()) {
        out = s.coef;
        return true;
    }
    err = L("坐标值必须是精确数值(整数/分数/小数或可精确求值的式子), 无法处理: ",
            "coordinate must be an exact number (integer/fraction/decimal), cannot handle: ") +
          exprRaw;
    return false;
}

bool splitTopComma(const std::string &s, std::string &a, std::string &b) {
    int depth = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        else if ((c == ',' || c == ';') && depth == 0) {
            a = s.substr(0, i);
            b = s.substr(i + 1);
            return true;
        }
    }
    return false;
}

std::string factorPlain(const Rational &xj, const std::string &var) {
    if (xj.isZero()) return var;
    if (xj.isNeg()) return "(" + var + " + " + (-xj).str() + ")";
    return "(" + var + " - " + xj.str() + ")";
}

std::string factorLatex(const Rational &xj, const std::string &var) {
    if (xj.isZero()) return var;
    if (xj.isNeg()) return "\\left(" + var + " + " + (-xj).latex() + "\\right)";
    return "\\left(" + var + " - " + xj.latex() + "\\right)";
}

} // namespace

ModeOutput runLagrange(const std::string &input, const Config &cfg) {
    ModeOutput mo;
    mo.modeName = "lagrange";
    mo.title = L("拉格朗日插值", "Lagrange interpolation");
    mo.saveable = true;
    Report &rep = mo.report;
    rep.title = mo.title;
    rep.line(Chan::Input, L("输入: ", "Input: ") + input);

    char32_t extra = 0;
    if (!cfg.extraSeparator.empty()) extra = utf8_decode(cfg.extraSeparator)[0];
    auto items = splitItems(input, extra, true);
    if (items.empty()) {
        mo.error = L("没有输入数据点", "no data points given");
        mo.exitCode = 3;
        return mo;
    }
    std::map<int, std::pair<OptRat, OptRat>> pts;
    auto autoIndex = [&](bool isX) {
        for (int i = 1; i < 100000; ++i) {
            auto it = pts.find(i);
            bool set = false;
            if (it != pts.end()) set = isX ? it->second.first.set : it->second.second.set;
            if (!set) return i;
        }
        return 1;
    };
    auto assign = [&](int idx, bool isX, const Rational &v, std::string &err) -> bool {
        OptRat &slot = isX ? pts[idx].first : pts[idx].second;
        if (slot.set) {
            if (!(slot.v == v)) {
                err = L("第 ", "point ") + std::to_string(idx) + L(" 个点的 ", " ") +
                      (isX ? "x" : "y") + L(" 被重复赋值且不一致", " assigned twice with different values");
                return false;
            }
            return true;
        }
        slot.set = true;
        slot.v = v;
        return true;
    };

    for (const auto &itemRaw : items) {
        if (interruptRequested()) { // 点数极多时逐点解析也可能很久
            mo.error = L("计算已被中断", "interrupted by user");
            mo.exitCode = 130;
            return mo;
        }
        std::string item = trimWs(itemRaw);
        if (item.empty()) continue;
        std::string lhs, rhs;
        bool hasEq = splitEquation(item, lhs, rhs);
        if (hasEq) {
            std::string Ls = trimWs(lhs), Rs = trimWs(rhs);
            if (Ls.empty() || Rs.empty()) {
                mo.error = L("片段缺少一边: ", "incomplete item: ") + item;
                mo.exitCode = 3;
                return mo;
            }
            bool isX = false;
            int index = -1;
            std::string why;
            if (!parseCoordName(Ls, isX, index, why)) {
                // 也可能是 P1=(a,b) 形式
                std::string p = Ls;
                if ((p.size() >= 2) && (p[0] == 'P' || p[0] == 'p')) {
                    std::string numPart = p.substr(1);
                    if (!numPart.empty() && numPart[0] == '_') numPart = numPart.substr(1);
                    int idx = 0;
                    bool okIdx = !numPart.empty();
                    for (char c : numPart)
                        if (!std::isdigit(static_cast<unsigned char>(c))) okIdx = false;
                    if (okIdx) okIdx = parseIndexText(numPart, idx);
                    if (okIdx) {
                        std::string a, b;
                        std::string r2 = Rs;
                        if (r2.size() >= 2 && (r2.front() == '(' || r2.front() == '[') &&
                            (r2.back() == ')' || r2.back() == ']'))
                            r2 = r2.substr(1, r2.size() - 2);
                        if (!splitTopComma(r2, a, b)) {
                            mo.error = L("点 ", "point ") + p + L(" 需要形如 (x,y)", " needs the form (x,y)");
                            mo.exitCode = 3;
                            return mo;
                        }
                        Rational xv, yv;
                        std::string err;
                        if (!evalRationalValue(a, xv, err) || !evalRationalValue(b, yv, err)) {
                            mo.error = err;
                            mo.exitCode = 3;
                            return mo;
                        }
                        if (!assign(idx, true, xv, err) || !assign(idx, false, yv, err)) {
                            mo.error = err;
                            mo.exitCode = 3;
                            return mo;
                        }
                        continue;
                    }
                }
                mo.error = why + L("  (可用形式: x=.., y=.., x1=.., y1=.., 3x=.., x_3=.., x^3=.., P1=(a,b))",
                                   "  (supported: x=.., y=.., x1=.., y1=.., 3x=.., x_3=.., x^3=.., P1=(a,b))");
                mo.exitCode = 3;
                return mo;
            }
            Rational v;
            std::string err;
            if (!evalRationalValue(Rs, v, err)) {
                mo.error = err;
                mo.exitCode = 3;
                return mo;
            }
            if (index < 0) index = autoIndex(isX);
            if (index <= 0) {
                mo.error = L("点序号必须 >= 1", "point index must be >= 1");
                mo.exitCode = 3;
                return mo;
            }
            if (!assign(index, isX, v, err)) {
                mo.error = err;
                mo.exitCode = 3;
                return mo;
            }
            continue;
        }
        // 无 '=' : 支持 (a,b) / [a,b] / {a,b}
        std::string r2 = item;
        bool bracketed = r2.size() >= 2 && ((r2.front() == '(' && r2.back() == ')') ||
                                            (r2.front() == '[' && r2.back() == ']') ||
                                            (r2.front() == '{' && r2.back() == '}'));
        if (bracketed) {
            std::string inner = r2.substr(1, r2.size() - 2);
            std::string a, b;
            if (splitTopComma(inner, a, b)) {
                Rational xv, yv;
                std::string err;
                if (!evalRationalValue(a, xv, err) || !evalRationalValue(b, yv, err)) {
                    mo.error = err;
                    mo.exitCode = 3;
                    return mo;
                }
                int idx = std::max(autoIndex(true), autoIndex(false));
                if (!assign(idx, true, xv, err) || !assign(idx, false, yv, err)) {
                    mo.error = err;
                    mo.exitCode = 3;
                    return mo;
                }
                continue;
            }
        }
        mo.error = L("无法识别的片段: ", "unrecognized item: ") + item;
        mo.exitCode = 3;
        return mo;
    }

    // 检查完整性
    std::vector<std::pair<Rational, Rational>> data;
    std::vector<std::string> incomplete;
    for (auto &kv : pts) {
        if (!kv.second.first.set || !kv.second.second.set) {
            incomplete.push_back(std::to_string(kv.first));
            continue;
        }
        data.push_back({kv.second.first.v, kv.second.second.v});
    }
    if (!incomplete.empty()) {
        std::string s;
        for (std::size_t i = 0; i < incomplete.size(); ++i) {
            if (i) s += ", ";
            s += incomplete[i];
        }
        if (data.size() >= 2) {
            rep.line(Chan::Note, L("以下点缺少坐标, 已忽略: ", "ignored incomplete points: ") + s);
        } else {
            mo.error = L("以下点缺少坐标: ", "these points are incomplete: ") + s +
                       L(" (每个点都需要 x 和 y)", " (each point needs both x and y)");
            mo.exitCode = 3;
            return mo;
        }
    }
    if (data.size() < 2) {
        mo.error = L("至少需要 2 个完整的数据点才能确定函数, 当前只有 ", "at least 2 complete points are required, got ") +
                   std::to_string(data.size());
        mo.exitCode = 3;
        return mo;
    }
    // 去重/矛盾检查
    std::vector<std::pair<Rational, Rational>> uniq;
    for (const auto &p : data) {
        if (interruptRequested()) { // 去重是 O(n^2)
            mo.error = L("计算已被中断", "interrupted by user");
            mo.exitCode = 130;
            return mo;
        }
        bool merged = false;
        for (auto &q : uniq) {
            if (q.first == p.first) {
                if (!(q.second == p.second)) {
                    mo.error = L("同一个 x=", "same x=") + p.first.str() + L(" 对应了不同的 y 值, 无法确定函数",
                                                                          " has different y values; no unique function");
                    mo.exitCode = 3;
                    return mo;
                }
                merged = true;
                break;
            }
        }
        if (!merged) uniq.push_back(p);
    }
    if (uniq.size() < data.size())
        rep.line(Chan::Note, L("有重复的数据点, 已合并", "duplicate points merged"));
    if (uniq.size() < 2) {
        mo.error = L("去重后不足 2 个不同的 x 值", "fewer than 2 distinct x values after merging");
        mo.exitCode = 3;
        return mo;
    }
    std::sort(uniq.begin(), uniq.end(),
              [](const std::pair<Rational, Rational> &a, const std::pair<Rational, Rational> &b) {
                  return a.first < b.first;
              });

    // 数据点回显
    {
        std::string s = L("数据点: ", "Data points: ");
        for (std::size_t i = 0; i < uniq.size(); ++i) {
            if (i) s += ", ";
            s += "(" + uniq[i].first.str() + ", " + uniq[i].second.str() + ")";
        }
        rep.line(Chan::Normalized, s);
    }

    Poly poly;
    std::string err;
    if (!lagrangePolynomial(uniq, poly, err)) {
        mo.error = err;
        mo.exitCode = 1;
        return mo;
    }

    // 拉格朗日形式
    {
        std::string generalP = "P(x) = \u03a3 y_i \u00b7 \u220f (x - x_j) / (x_i - x_j)";
        std::string generalL =
            "P(x)=\\sum_{i=1}^{n} y_i\\prod_{j\\neq i}\\frac{x-x_j}{x_i-x_j}";
        rep.line(Chan::LagrangeForm, L("拉格朗日形式: ", "Lagrange form: ") + generalP, generalL);
        std::string sp, sl;
        for (std::size_t i = 0; i < uniq.size(); ++i) {
            std::string num, den, numL, denL;
            for (std::size_t j = 0; j < uniq.size(); ++j) {
                if (i == j) continue;
                num += factorPlain(uniq[j].first, "x");
                numL += factorLatex(uniq[j].first, "x");
                den += "(" + uniq[i].first.str() + " - " + uniq[j].first.str() + ")";
                denL += "\\left(" + uniq[i].first.latex() + " - " + uniq[j].first.latex() + "\\right)";
            }
            std::string termP, termL;
            if (num.empty()) {
                termP = uniq[i].second.str();
                termL = uniq[i].second.latex();
            } else {
                termP = uniq[i].second.str() + "\u00b7" + num + "/(" + den + ")";
                termL = "\\frac{" + uniq[i].second.latex() + numL + "}{" + denL + "}";
            }
            if (i) {
                sp += " + ";
                sl += "+";
            }
            sp += termP;
            sl += termL;
        }
        rep.line(Chan::LagrangeForm, "P(x) = " + sp, "P(x)=" + sl);
    }
    // 化简结果
    rep.line(Chan::Polynomial, "P(x) = " + poly.toPlain("x"), "P(x) = " + poly.toLatex("x"));
    if (cfg.prettyUnicode)
        rep.line(Chan::Polynomial, L("简写: ", "Pretty: ") + "P(x) = " + poly.toPretty("x"));
    rep.line(Chan::PlainForm, "y = " + poly.toPlain("x"), "y = " + poly.toLatex("x"));
    rep.line(Chan::LatexForm, "y = " + poly.toLatex("x"), "y = " + poly.toLatex("x"));
    rep.line(Chan::Step, L("次数: ", "Degree: ") + std::to_string(poly.degree()) + L(" (数据点个数 - 1)",
                                                                                    " (= number of points - 1)"));

    // 因式分解 (需要外部引擎)
    if (cfg.engine != "builtin" && poly.degree() >= 2 && sympyUsable(cfg.pythonPath)) {
        std::string fp, fl, fe;
        if (factorWithSympy(poly.toSympy("x"), "x", cfg.pythonPath, cfg.engineTimeoutMs, fp, fl, fe)) {
            auto strip = [](const std::string &t) {
                std::string r;
                for (char c : t)
                    if (!std::isspace(static_cast<unsigned char>(c))) r += c;
                return r;
            };
            if (strip(fp) != strip(poly.toPlain("x")))
                rep.line(Chan::Polynomial, L("因式分解: ", "Factored: ") + "P(x) = " + fp, "P(x) = " + fl);
        }
    }
    // 验算
    for (const auto &p : uniq) {
        Rational got = poly.eval(p.first);
        std::string plain = "P(" + p.first.str() + ") = " + got.str();
        std::string latex = "P\\left(" + p.first.latex() + "\\right) = " + got.latex();
        if (got == p.second) {
            plain += "  \u2713";
            latex += "\\;\\checkmark";
        } else {
            plain += "  \u2717";
            latex += "\\;\\times";
        }
        rep.line(Chan::Verify, plain, latex);
    }
    mo.ok = true;
    return mo;
}

} // namespace em

// EasyMath - 配置实现
#include "config.hpp"

#include "i18n.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace em {

const char *kVersion = "1.62.0";

// 小数位上限: <=200 走原来的(内置/SymPy)算法; >200 走高精度特殊算法,
// 拿不出那么多位就钳到本机能精确的最大位数(见 modes.cpp 的 clampDecimals)
const int kMaxDecimals = 100000;

Config defaultConfig() {
    Config c;
    const char *env = std::getenv("EASYMATH_LANG");
    if (env) c.lang = env;
    return c;
}

std::string defaultConfigPath() {
    const char *env = std::getenv("EASYMATH_CONFIG");
    if (env && *env) return env;
    const char *home = std::getenv("HOME");
    if (home && *home) return std::string(home) + "/.easymath.conf";
    return ".easymath.conf";
}

static std::string trim(const std::string &s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool toBool(const std::string &v, bool &out) {
    std::string s = v;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "1" || s == "true" || s == "yes" || s == "on" || s == "y" || s == "是" || s == "开") {
        out = true;
        return true;
    }
    if (s == "0" || s == "false" || s == "no" || s == "off" || s == "n" || s == "否" || s == "关") {
        out = false;
        return true;
    }
    return false;
}

struct ChanName { const char *name; Chan chan; };

static const ChanName kChans[] = {
    {"banner", Chan::Banner},           {"input", Chan::Input},
    {"normalized", Chan::Normalized},   {"note", Chan::Note},
    {"step", Chan::Step},               {"steps", Chan::Step},
    {"lagrange", Chan::LagrangeForm},   {"polynomial", Chan::Polynomial},
    {"plain", Chan::PlainForm},         {"decimal", Chan::DecimalForm},
    {"approx", Chan::ApproxForm},       {"latex", Chan::LatexForm},
    {"markdown", Chan::MarkdownForm},   {"md", Chan::MarkdownForm},
    {"html", Chan::HtmlForm},           {"solution", Chan::SolutionSet},
    {"group", Chan::Group},             {"grouping", Chan::Group},
    {"verify", Chan::Verify},           {"file", Chan::FileInfo},
    {"prompt", Chan::Prompt},           {"tip", Chan::Tip},
};

std::vector<std::string> allChannelNames() {
    std::vector<std::string> v;
    for (const auto &c : kChans) v.push_back(c.name);
    return v;
}

std::vector<std::string> canonicalChannelNames() {
    return {"banner", "input",    "normalized", "note",   "step",    "lagrange", "polynomial",
            "plain",  "decimal",  "approx",     "latex",  "markdown", "html",     "solution",
            "group",  "verify",   "file",       "prompt", "tip"};
}

bool setChannel(Config &cfg, const std::string &name, bool on) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto &c : kChans) {
        if (n == c.name) {
            switch (c.chan) {
                case Chan::Banner: cfg.out.banner = on; return true;
                case Chan::Input: cfg.out.input = on; return true;
                case Chan::Normalized: cfg.out.normalized = on; return true;
                case Chan::Note: cfg.out.note = on; return true;
                case Chan::Step: cfg.out.step = on; return true;
                case Chan::LagrangeForm: cfg.out.lagrangeForm = on; return true;
                case Chan::Polynomial: cfg.out.polynomial = on; return true;
                case Chan::PlainForm: cfg.out.plainForm = on; return true;
                case Chan::DecimalForm: cfg.out.decimalForm = on; return true;
                case Chan::ApproxForm: cfg.out.approxForm = on; return true;
                case Chan::LatexForm: cfg.out.latexForm = on; return true;
                case Chan::MarkdownForm: cfg.out.markdownForm = on; return true;
                case Chan::HtmlForm: cfg.out.htmlForm = on; return true;
                case Chan::SolutionSet: cfg.out.solutionSet = on; return true;
                case Chan::Group: cfg.out.group = on; return true;
                case Chan::Verify: cfg.out.verify = on; return true;
                case Chan::FileInfo: cfg.out.fileInfo = on; return true;
                case Chan::Prompt: cfg.out.prompt = on; return true;
                case Chan::Tip: cfg.out.tip = on; return true;
            }
        }
    }
    return false;
}

bool channelEnabled(const Config &cfg, Chan c) {
    switch (c) {
        case Chan::Banner: return cfg.out.banner;
        case Chan::Input: return cfg.out.input;
        case Chan::Normalized: return cfg.out.normalized;
        case Chan::Note: return cfg.out.note;
        case Chan::Step: return cfg.out.step;
        case Chan::LagrangeForm: return cfg.out.lagrangeForm;
        case Chan::Polynomial: return cfg.out.polynomial;
        case Chan::PlainForm: return cfg.out.plainForm;
        case Chan::DecimalForm: return cfg.out.decimalForm;
        case Chan::ApproxForm: return cfg.out.approxForm;
        case Chan::LatexForm: return cfg.out.latexForm;
        case Chan::MarkdownForm: return cfg.out.markdownForm;
        case Chan::HtmlForm: return cfg.out.htmlForm;
        case Chan::SolutionSet: return cfg.out.solutionSet;
        case Chan::Group: return cfg.out.group;
        case Chan::Verify: return cfg.out.verify;
        case Chan::FileInfo: return cfg.out.fileInfo;
        case Chan::Prompt: return cfg.out.prompt;
        case Chan::Tip: return cfg.out.tip;
    }
    return true;
}

bool applyConfigKV(Config &cfg, const std::string &keyIn, const std::string &value, std::string &err) {
    std::string key = keyIn;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto asBool = [&](bool &dst) {
        bool b = false;
        if (!toBool(value, b)) {
            err = L("配置项 ", "option ") + key + L(" 需要布尔值(true/false)", " needs a boolean (true/false)");
            return false;
        }
        dst = b;
        return true;
    };
    if (key == "lang" || key == "language") {
        cfg.lang = value;
        setLang(value);
        return true;
    }
    if (key == "decimals") {
        try {
            int d = std::stoi(value);
            // 大于硬上限不报错: 保留用户填的数, 由模式侧钳到能精确给出的位数并提示
            // (用户要求: 填太大就退到"最大的能填的数字", 而不是拒绝)
            if (d < 0 || d > 1000000000) throw std::out_of_range("range");
            cfg.decimals = d;
        } catch (...) {
            err = L("decimals 必须是 0..1e9 的整数(超过 100000 会被钳到本机能精确的位数)",
                    "decimals must be an integer 0..1e9 (above 100000 it is clamped)");
            return false;
        }
        return true;
    }
    if (key == "savedir" || key == "outdir") { cfg.saveDir = value; return true; }
    if (key == "usedefaultname" || key == "default_name") return asBool(cfg.useDefaultName);
    if (key == "nametemplate" || key == "name_template") { cfg.nameTemplate = value; return true; }
    if (key == "overwrite") return asBool(cfg.overwrite);
    if (key == "savehtml" || key == "save") return asBool(cfg.saveHtml);
    if (key == "asksave") return asBool(cfg.askSave);
    if (key == "mathjax") return asBool(cfg.mathjax);
    if (key == "savemarkdown" || key == "savemd" || key == "savemarkdownalso")
        return asBool(cfg.saveMarkdownAlso);
    if (key == "prettyunicode" || key == "pretty") return asBool(cfg.prettyUnicode);
    if (key == "exactpreferred" || key == "exact") return asBool(cfg.exactPreferred);
    if (key == "degreesdefault" || key == "degreemode") return asBool(cfg.degreesDefault);
    if (key == "realonly" || key == "real") return asBool(cfg.realOnly);
    if (key == "complex") return asBool(cfg.complexAllowed);
    if (key == "separator" || key == "sep") { cfg.extraSeparator = value; return true; }
    if (key == "configpath") return true; // 生成配置时的只读信息, 回读时忽略
    if (key == "engine") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (v != "auto" && v != "builtin" && v != "sympy") {
            err = L("engine 只能是 auto / builtin / sympy", "engine must be auto / builtin / sympy");
            return false;
        }
        cfg.engine = v;
        return true;
    }
    if (key == "python" || key == "pythonpath") { cfg.pythonPath = value; return true; }
    if (key == "bigint" || key == "intbackend") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (v != "auto" && v != "gmp" && v != "builtin") {
            err = L("bigint 只能是 auto / gmp / builtin", "bigint must be auto / gmp / builtin");
            return false;
        }
        cfg.bigint = v;
        return true;
    }
    if (key == "hpfloat" || key == "hp") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (v != "auto" && v != "mpfr" && v != "sympy" && v != "builtin") {
            err = L("hpfloat 只能是 auto / mpfr / sympy / builtin",
                    "hpfloat must be auto / mpfr / sympy / builtin");
            return false;
        }
        cfg.hpfloat = v;
        return true;
    }
    if (key == "numericInequality" || key == "numericinequality" || key == "ineqnum") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (v != "auto" && v != "always" && v != "never") {
            err = L("numericInequality 只能是 auto / always / never",
                    "numericInequality must be auto / always / never");
            return false;
        }
        cfg.numericInequality = v;
        return true;
    }
    if (key == "constants" || key == "const") {
        cfg.constants = trim(value);
        return true;
    }
    if (key == "fontaxis" || key == "axis") { cfg.fontAxis = value; return true; }
    if (key == "fontinstance" || key == "instance") { cfg.fontInstance = value; return true; }
    if (key == "liga" || key == "ligature") return asBool(cfg.liga);
    if (key == "kern" || key == "kerning") return asBool(cfg.kern);
    if (key == "fontfallback" || key == "fallback") return asBool(cfg.fontFallback);
    if (key == "strokes" || key == "draw") {
        cfg.strokes = value;
        return true;
    }
    if (key == "fontpack" || key == "font" || key == "fontpick" || key == "glyphsize" ||
        key == "size" || key == "glyphtol" || key == "fittol" || key == "anchor" || key == "at") {
        if (key == "fontpack") {
            cfg.fontPack = trim(value);
            return true;
        }
        if (key == "font" || key == "fontpick") {
            cfg.fontPick = trim(value);
            return true;
        }
        if (key == "anchor" || key == "at") {
            cfg.glyphAnchor = trim(value);
            return true;
        }
        std::stringstream ss(value);
        double d = 0;
        if (!(ss >> d) || d <= 0) {
            err = L("需要正数: ", "expects a positive number: ") + key;
            return false;
        }
        if (key == "glyphsize" || key == "size") {
            if (d < 1 || d > 1e9) {
                err = L("字号要在 1..1e9 之间", "size must be 1..1e9");
                return false;
            }
            cfg.glyphSize = d;
        } else {
            if (d > 1000) {
                err = L("拟合容差要在 0..1000 之间", "fitTol must be 0..1000");
                return false;
            }
            cfg.glyphTol = d;
        }
        return true;
    }
    if (key == "derive" || key == "derived") {
        cfg.derive = trim(value);
        return true;
    }
    if (key == "scientific" || key == "sci") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (v != "auto" && v != "always" && v != "never") {
            err = L("scientific 只能是 auto / always / never", "scientific must be auto / always / never");
            return false;
        }
        cfg.scientific = v;
        return true;
    }
    if (key == "scithreshold" || key == "sci_threshold") {
        try {
            int t = std::stoi(value);
            if (t < 1 || t > 300) throw std::out_of_range("range");
            cfg.sciThreshold = t;
        } catch (...) {
            err = L("sciThreshold 必须是 1..300 的整数", "sciThreshold must be 1..300");
            return false;
        }
        return true;
    }
    if (key == "precision" || key == "precisionbits") {
        try {
            int b = std::stoi(value);
            if (b < 0 || b > 100000) throw std::out_of_range("range");
            cfg.precisionBits = b;
        } catch (...) {
            err = L("precision 必须是 0..100000 的位数", "precision must be 0..100000 bits");
            return false;
        }
        return true;
    }
    if (key == "enginetimeout" || key == "engine_timeout") {
        try {
            int t = std::stoi(value);
            if (t < 100 || t > 600000) throw std::out_of_range("range");
            cfg.engineTimeoutMs = t;
        } catch (...) {
            err = L("engineTimeout 必须是 100..600000 的毫秒数", "engineTimeout must be 100..600000 ms");
            return false;
        }
        return true;
    }
    if (key == "scanlo") {
        cfg.scanLo = std::strtold(value.c_str(), nullptr);
        return true;
    }
    if (key == "scanhi") {
        cfg.scanHi = std::strtold(value.c_str(), nullptr);
        return true;
    }
    if (key == "show" || key == "hide") {
        bool on = (key == "show");
        std::stringstream ss(value);
        std::string item;
        while (std::getline(ss, item, ',')) {
            item = trim(item);
            if (item.empty()) continue;
            if (item == "all") {
                for (const auto &n : allChannelNames()) setChannel(cfg, n, on);
                continue;
            }
            if (!setChannel(cfg, item, on)) {
                err = L("未知的输出通道: ", "unknown output channel: ") + item;
                return false;
            }
        }
        return true;
    }
    if (key.rfind("out.", 0) == 0 || key.rfind("show.", 0) == 0) {
        std::string ch = key.substr(key.find('.') + 1);
        bool on = true;
        bool b = false;
        if (toBool(value, b)) on = b;
        if (!setChannel(cfg, ch, on)) {
            err = L("未知的输出通道: ", "unknown output channel: ") + ch;
            return false;
        }
        return true;
    }
    err = L("未知配置项: ", "unknown option: ") + keyIn;
    return false;
}

Config loadConfig(const std::string &path, bool &ok, std::string &err, bool warn) {
    Config cfg = defaultConfig();
    ok = true;
    std::ifstream in(path);
    if (!in) {
        ok = false;
        err = L("无法读取配置文件: ", "cannot read config file: ") + path;
        return cfg;
    }
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        std::size_t eq = t.find('=');
        if (eq == std::string::npos) {
            err = L("配置文件第 ", "config line ") + std::to_string(lineno) + L(" 行缺少 '='", " has no '='");
            ok = false;
            continue;
        }
        std::string k = trim(t.substr(0, eq));
        std::string v = trim(t.substr(eq + 1));
        // 去掉行尾注释: '#' 前有空白且不是值的第一个字符(这样 separator = # 仍然可用)
        std::size_t hash = v.find(" #");
        if (hash != std::string::npos && hash > 0) v = trim(v.substr(0, hash));
        std::size_t tabc = v.find("\t#");
        if (tabc != std::string::npos && tabc > 0) v = trim(v.substr(0, tabc));
        if (!applyConfigKV(cfg, k, v, err)) {
            ok = false;
            if (warn) std::cerr << "EasyMath: " << path << ":" << lineno << ": " << err << "\n";
        }
    }
    cfg.configPath = path;
    cfg.loadedFromFile = true;
    setLang(cfg.lang);
    return cfg;
}

bool saveConfig(const Config &cfg, const std::string &path, std::string &err) {
    std::ofstream out(path);
    if (!out) {
        err = L("无法写入配置文件: ", "cannot write config file: ") + path;
        return false;
    }
    out << "# EasyMath 配置文件 / configuration file\n";
    out << "# 语法: key = value\n\n";
    out << "lang = " << cfg.lang << "\n";
    out << "decimals = " << cfg.decimals << "\n";
    out << "saveDir = " << cfg.saveDir << "\n";
    out << "useDefaultName = " << (cfg.useDefaultName ? "true" : "false") << "\n";
    out << "nameTemplate = " << cfg.nameTemplate << "\n";
    out << "overwrite = " << (cfg.overwrite ? "true" : "false") << "\n";
    out << "saveHtml = " << (cfg.saveHtml ? "true" : "false") << "\n";
    out << "askSave = " << (cfg.askSave ? "true" : "false") << "\n";
    out << "saveMarkdown = " << (cfg.saveMarkdownAlso ? "true" : "false") << "\n";
    out << "mathjax = " << (cfg.mathjax ? "true" : "false") << "\n";
    out << "prettyUnicode = " << (cfg.prettyUnicode ? "true" : "false") << "\n";
    out << "exactPreferred = " << (cfg.exactPreferred ? "true" : "false") << "\n";
    out << "degreesDefault = " << (cfg.degreesDefault ? "true" : "false") << "\n";
    out << "realOnly = " << (cfg.realOnly ? "true" : "false") << "\n";
    out << "complex = " << (cfg.complexAllowed ? "true" : "false") << "\n";
    out << "separator = " << cfg.extraSeparator << "\n";
    out << "engine = " << cfg.engine << "\n";
    out << "python = " << cfg.pythonPath << "\n";
    out << "engineTimeout = " << cfg.engineTimeoutMs << "\n";
    out << "bigint = " << cfg.bigint << "\n";
    out << "hpfloat = " << cfg.hpfloat << "\n";
    out << "precision = " << cfg.precisionBits << "\n";
    out << "scientific = " << cfg.scientific << "\n";
    out << "numericInequality = " << cfg.numericInequality << "\n";
    out << "constants = " << cfg.constants << "\n";
    out << "derive = " << cfg.derive << "\n";
    out << "fontPack = " << cfg.fontPack << "   # 字体包(.zip)或 .ttf; 空=自动找 ~/Math/vivo_Sans.zip\n";
    out << "font = " << cfg.fontPick << "   # 字体筛选子串\n";
    out << "glyphSize = " << cfg.glyphSize << "   # 字形模式字号(坐标单位)\n";
    out << "glyphTol = " << cfg.glyphTol << "   # 拟合容差(字体单位)\n";
    out << "anchor = " << cfg.glyphAnchor << "   # 左下角坐标: (x,x) / x,x / :(x,x)(: 左右镜像)\n";
    out << "sciThreshold = " << cfg.sciThreshold << "\n\n";
    out << "# 输出通道开关\n";
    for (const auto &n : canonicalChannelNames()) {
        out << "out." << n << " = 1\n";
    }
    out << "\n# 关闭示例: out.step = 0\n";
    return true;
}

std::string dumpConfig(const Config &cfg) {
    std::ostringstream o;
    o << "lang = " << cfg.lang << "\n";
    o << "decimals = " << cfg.decimals << "\n";
    o << "saveDir = " << cfg.saveDir << "\n";
    o << "useDefaultName = " << (cfg.useDefaultName ? "true" : "false") << "\n";
    o << "nameTemplate = " << cfg.nameTemplate << "\n";
    o << "overwrite = " << (cfg.overwrite ? "true" : "false") << "\n";
    o << "saveHtml = " << (cfg.saveHtml ? "true" : "false") << "\n";
    o << "askSave = " << (cfg.askSave ? "true" : "false") << "\n";
    o << "saveMarkdownAlso = " << (cfg.saveMarkdownAlso ? "true" : "false") << "\n";
    o << "mathjax = " << (cfg.mathjax ? "true" : "false") << "\n";
    o << "prettyUnicode = " << (cfg.prettyUnicode ? "true" : "false") << "\n";
    o << "exactPreferred = " << (cfg.exactPreferred ? "true" : "false") << "\n";
    o << "degreesDefault = " << (cfg.degreesDefault ? "true" : "false") << "\n";
    o << "realOnly = " << (cfg.realOnly ? "true" : "false") << "\n";
    o << "complex = " << (cfg.complexAllowed ? "true" : "false") << "\n";
    o << "separator = " << cfg.extraSeparator << "\n";
    o << "engine = " << cfg.engine << "\n";
    o << "python = " << cfg.pythonPath << "\n";
    o << "engineTimeout = " << cfg.engineTimeoutMs << "\n";
    o << "bigint = " << cfg.bigint << "\n";
    o << "hpfloat = " << cfg.hpfloat << "\n";
    o << "precision = " << cfg.precisionBits << "\n";
    o << "scientific = " << cfg.scientific << "\n";
    o << "numericInequality = " << cfg.numericInequality << "\n";
    o << "constants = " << cfg.constants << "\n";
    o << "derive = " << cfg.derive << "\n";
    o << "fontPack = " << cfg.fontPack << "\n";
    o << "font = " << cfg.fontPick << "\n";
    o << "liga = " << (cfg.liga ? "true" : "false") << "\n";
    o << "kern = " << (cfg.kern ? "true" : "false") << "\n";
    o << "fontFallback = " << (cfg.fontFallback ? "true" : "false") << "\n";
    if (!cfg.fontAxis.empty()) o << "fontAxis = " << cfg.fontAxis << "\n";
    if (!cfg.fontInstance.empty()) o << "fontInstance = " << cfg.fontInstance << "\n";
    o << "glyphSize = " << cfg.glyphSize << "\n";
    o << "glyphTol = " << cfg.glyphTol << "\n";
    o << "anchor = " << cfg.glyphAnchor << "\n";
    o << "sciThreshold = " << cfg.sciThreshold << "\n";
    return o.str();
}

} // namespace em

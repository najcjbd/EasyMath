// EasyMath - 命令行解析实现
#include "cli.hpp"

#include "i18n.hpp"
#include "output.hpp"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <iostream>
#include <sstream>
#include <cstdio>

namespace em {

// 版本号定义在 config.cpp(唯一真源, 安卓 native 也编那一份)
const char *kProgramName = "EasyMath";

static bool startsWith(const std::string &s, const std::string &p) { return s.rfind(p, 0) == 0; }

std::string versionText() {
    return std::string(kProgramName) + " " + kVersion + " - " +
           L("数学工具: 拉格朗日插值 / 解方程 / 求值开方 / 过原点直线 / 字形→函数",
             "math tool: Lagrange interpolation / equation solving / evaluation / lines by angle");
}

std::string helpText(const Config &cfg) {
    (void)cfg;
    std::ostringstream o;
    o << versionText() << "\n\n";
    o << L("用法: ", "Usage: ") << kProgramName << " [模式] [输入] [选项]\n\n";
    o << L("模式(互斥, 同时给出多个会报错):\n", "Modes (mutually exclusive):\n");
    o << "  -l, --lagrange <点集>   " << L("拉格朗日插值, 例如 \"x=1,y=3 x=2,y=5 x=3,y=9\"\n",
                                              "Lagrange interpolation, e.g. \"x=1,y=3 x=2,y=5\"\n");
    o << "  -s, --solve <方程>      " << L("解方程(可多个, 逗号分隔), 例如 \"y=5x, y=6z, x=2z\"\n",
                                             "solve equations, e.g. \"y=5x, y=6z, x=2z\"\n");
    o << "  -e, --eval <式子>       " << L("求值/开方, 例如 \"5!\", \"6^8\", \"√6\", \"5x6\"\n",
                                             "evaluate, e.g. \"5!\", \"6^8\", \"sqrt6\", \"5x6\"\n");
    o << "  -L, --line <角度>       " << L("过原点直线, 例如 \"45°\", \"135\", \"0.5rad\"\n",
                                             "line through origin by angle, e.g. \"45deg\", \"135\"\n");
    o << "  -i, --interactive       " << L("进入交互模式\n", "interactive mode\n");
    o << "\n" << L("选项:\n", "Options:\n");
    o << "  --lang <zh|en>          " << L("界面语言\n", "UI language\n");
    o << "  --decimals <n>          " << L("小数位数(默认 8)\n", "decimal places (default 8)\n");
    o << "  --sep <字符>            " << L("自定义分隔符\n", "custom separator\n");
    o << "  --deg | --rad           " << L("直线模式默认角度/弧度\n", "default degree/radian for angle mode\n");
    o << "  --real | --complex      " << L("只求实根 / 允许复根\n", "real roots only / allow complex roots\n");
    o << "  --save | --no-save      " << L("是否保存 HTML\n", "save HTML or not\n");
    o << "  --ask-save | --no-ask-save " << L("交互模式是否询问保存\n", "ask before saving in interactive mode\n");
    o << "  --out <文件名>          " << L("自定义文件名(不含扩展名)\n", "custom file name (no extension)\n");
    o << "  --outdir <目录>         " << L("保存目录(默认当前目录)\n", "output directory (default .)\n");
    o << "  --name-template <模板>  " << L("默认命名模板, 占位 {date} {n} {mode} {time}\n",
                                             "default name template, placeholders {date} {n} {mode} {time}\n");
    o << "  --overwrite | --no-overwrite " << L("重名时覆盖 / 加(1)\n", "overwrite on conflict / add (1)\n");
    o << "  --show <通道,...>       " << L("开启输出通道, 例如 --show plain,latex\n",
                                             "enable output channels, e.g. --show plain,latex\n");
    o << "  --hide <通道,...>       " << L("关闭输出通道 (all 表示全部)\n", "disable output channels (all)\n");
    o << "  --engine <auto|builtin|sympy> " << L("求解引擎: 自动/内置/SymPy\n", "solver engine: auto/builtin/sympy\n");
    o << "  --python <路径>         " << L("python3 可执行文件路径\n", "path to python3\n");
    o << "  --engine-timeout <毫秒> " << L("外部引擎超时(默认 15000)\n", "external engine timeout (default 15000)\n");
    o << "  --bigint <auto|gmp|builtin>    " << L("大整数后端(编译期决定)\n", "big integer backend (compile time)\n");
    o << "  --hpfloat <auto|mpfr|sympy|builtin> " << L("高精度浮点后端\n", "high-precision float backend\n");
    o << "  --precision <位>        " << L("MPFR 精度(位), 0=按小数位自动\n", "MPFR precision in bits, 0=auto\n");
    o << "  --scientific <auto|always|never> " << L("科学计数法策略(默认 auto)\n", "scientific-notation policy (default auto)\n");
    o << "  --sci-threshold <n>     " << L("|v|>=10^n 时用科学计数法(默认 12)\n", "use scientific for |v|>=10^n (default 12)\n");
    o << "  --numeric-ineq <auto|always|never> " << L("不等式的数值解集策略(默认 auto)\n", "numeric inequality strategy (default auto)\n");
    o << "  --const <字母,...>      " << L("解方程: 把这些字母当常量(不求解), 如 --const a,b,c\n", "solve: treat these as constants, e.g. --const a,b,c\n");
    o << "  --derive <表达式,...>   " << L("解方程: 由解反推这些表达式的值, 如 --derive abc,xy\n", "solve: report these expressions' values, e.g. --derive abc,xy\n");    o << "  -g, --glyph <文字>      " << L("字形模式: 把文字还原成尽可能少的函数, 如 --glyph \"你好\"\n", "glyph: trace text into as few functions as possible\n");
    o << "  --font-pack <zip|ttf>   " << L("字体包(默认找 ~/Math/vivo_Sans.zip)\n", "font pack (default ~/Math/vivo_Sans.zip)\n");
    o << "  --font <序号|子串>      " << L("选字体(默认优先 vivo Sans 的 Regular; 用 --font-list 看可选项)\n", "pick a font by index/substring (default: vivo Sans Regular)\n");
    o << "  --font-list             " << L("列出字体包里的字体与默认选中的那个\n", "list the fonts in the pack and which one is default\n");
    o << "  --size <数>             " << L("字形模式的字号(默认 1000)\n", "glyph size (default 1000)\n");
    o << "  --fit-tol <数>          " << L("拟合容差(字体单位, 默认 0.5; 越小越贴函数越多)\n", "fit tolerance (font units, default 0.5)\n");
    o << "  --at <(x,x)>            " << L("左下角坐标, 支持 (x,x) / x,x; 前缀 : 表示左右镜像\n", "bottom-left anchor: (x,x) / x,x; ':' mirror left-right\n");
    o << "  --engine-info           " << L("显示引擎/数值后端探测结果\n", "show engine and numeric backend status\n");
    o << "  --dump-engine-script    " << L("输出内嵌的 SymPy 桥脚本\n", "print the embedded SymPy bridge script\n");
    o << "  --set <键=值>           " << L("设置任意配置项(可重复)\n", "set any config key (repeatable)\n");
    o << "  --config <文件>         " << L("指定配置文件\n", "config file\n");
    o << "  --no-config             " << L("忽略配置文件\n", "ignore config file\n");
    o << "  --print-config          " << L("显示当前配置\n", "print current config\n");
    o << "  --dump-config           " << L("输出默认配置到 stdout\n", "dump default config\n");
    o << "  -h, --help              " << L("帮助\n", "help\n");
    o << "  -v, --version           " << L("版本\n", "version\n");
    o << "\n" << L("输出通道: ", "Output channels: ");
    for (const auto &n : allChannelNames()) o << n << " ";
    o << "\n\n" << L("示例:\n", "Examples:\n");
    o << "  " << kProgramName << " --lagrange \"x=1,y=3 x=2,y=5 x=3,y=9\" --save\n";
    o << "  " << kProgramName << " --solve \"x^4=5\" --complex\n";
    o << "  " << kProgramName << " --eval \"5! + 6^8\"\n";
    o << "  " << kProgramName << " --line \"45°\"\n";
    o << "  " << kProgramName << " --glyph \"你好\" --at \"(0,0)\" --save\n";
    return o.str();
}

CliResult parseArgs(int argc, char **argv) {
    CliResult r;
    r.cfg = defaultConfig();
    r.cfg.configPath = defaultConfigPath();
    bool configLoaded = false;
    std::vector<std::string> modeFlags;
    std::vector<std::pair<std::string, std::string>> overrides;
    std::string configPath;
    bool explicitConfig = false;

    // 模式参数缺失时不报错, 而是进入交互模式补全。
    // 返回 (值, 是否显式给出): 显式给空串(如 --solve "" 或 --solve=)算"给了", 
    // 交给模式自己报错, 不再掉进交互菜单干等 stdin。
    auto modeValue = [&](int &i, const char *longName) -> std::pair<std::string, bool> {
        std::string cur = argv[i];
        std::string p = std::string(longName) + "=";
        if (startsWith(cur, p)) return {cur.substr(p.size()), true};
        if (i + 1 >= argc) return {std::string(), false};
        std::string nxt = argv[i + 1];
        // 只有 "--字母..." 这种才当作"下一个选项"; 否则视作表达式(如 "-5!", "-----1")
        if (nxt.size() >= 3 && nxt[0] == '-' && nxt[1] == '-' &&
            std::isalpha(static_cast<unsigned char>(nxt[2])))
            return {std::string(), false};
        return {argv[++i], true};
    };
    auto needValue = [&](int &i, const std::string &flag) -> std::string {
        if (i + 1 >= argc) {
            r.ok = false;
            r.exitCode = 2;
            r.error = L("选项 ", "option ") + flag + L(" 缺少参数", " requires an argument");
            return "";
        }
        return argv[++i];
    };

    // 先扫描 --config / --no-config, 便于后面加载
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-config") r.noConfig = true;
        else if (a == "--config" && i + 1 < argc) {
            configPath = argv[i + 1];
            explicitConfig = true;
        } else if (startsWith(a, "--config=")) {
            configPath = a.substr(9);
            explicitConfig = true;
        }
    }
    if (explicitConfig) {
        bool ok = true;
        std::string err;
        Config c = loadConfig(configPath, ok, err, true);
        r.cfg = c;
        r.cfg.configPath = configPath;
        configLoaded = ok;
        if (!ok) {
            std::cerr << em::kProgramName << ": " << err << "\n";
        }
    } else if (!r.noConfig) {
        bool ok = true;
        std::string err;
        std::string p = defaultConfigPath();
        struct stat_t {
        };
        FILE *f = std::fopen(p.c_str(), "rb");
        if (f) {
            std::fclose(f);
            Config c = loadConfig(p, ok, err, true);
            r.cfg = c; // 即使有坏行, 已成功解析的配置仍然生效
            r.cfg.configPath = p;
            configLoaded = ok;
        }
    }
    setLang(r.cfg.lang);

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto valueOf = [&](const char *longName) -> std::string {
            std::string s = a;
            std::string p = std::string(longName) + "=";
            if (startsWith(s, p)) return s.substr(p.size());
            return needValue(i, longName);
        };
        if (a == "-h" || a == "--help") {
            r.wantHelp = true;
        } else if (a == "-v" || a == "--version") {
            r.wantVersion = true;
        } else if (a == "--dump-config") {
            r.wantDumpConfig = true;
        } else if (a == "--print-config") {
            r.wantPrintConfig = true;
        } else if (a == "--bigint" || startsWith(a, "--bigint=")) {
            std::string v = valueOf("--bigint");
            if (!r.ok) return r;
            overrides.push_back({"bigint", v});
        } else if (a == "--hpfloat" || a == "--hp" || startsWith(a, "--hpfloat=") ||
                   startsWith(a, "--hp=")) {
            std::string v = valueOf("--hpfloat");
            if (!r.ok) return r;
            overrides.push_back({"hpfloat", v});
        } else if (a == "--scientific" || a == "--sci" || startsWith(a, "--scientific=") ||
                   startsWith(a, "--sci=")) {
            std::string v = valueOf("--scientific");
            if (!r.ok) return r;
            overrides.push_back({"scientific", v});
        } else if (a == "--numeric-ineq" || startsWith(a, "--numeric-ineq=") ||
                   a == "--numericinequality" || startsWith(a, "--numericinequality=")) {
            std::string v = valueOf(a.find("--numericinequality") == 0 ? "--numericinequality"
                                                                      : "--numeric-ineq");
            if (!r.ok) return r;
            overrides.push_back({"numericInequality", v});
        } else if (a == "--const" || a == "--constants" || startsWith(a, "--const=") ||
                   startsWith(a, "--constants=")) {
            std::string v = valueOf(a.find("--constants") == 0 ? "--constants" : "--const");
            if (!r.ok) return r;
            overrides.push_back({"constants", v});
        } else if (a == "--derive" || startsWith(a, "--derive=")) {
            std::string v = valueOf("--derive");
            if (!r.ok) return r;
            overrides.push_back({"derive", v});
        } else if (a == "--no-scientific") {
            overrides.push_back({"scientific", "never"});
        } else if (a == "--sci-threshold" || startsWith(a, "--sci-threshold=")) {
            std::string v = valueOf("--sci-threshold");
            if (!r.ok) return r;
            overrides.push_back({"sciThreshold", v});
        } else if (a == "--precision" || startsWith(a, "--precision=")) {
            std::string v = valueOf("--precision");
            if (!r.ok) return r;
            overrides.push_back({"precision", v});
        } else if (a == "--engine-info" || a == "--backend-info") {
            r.wantEngineInfo = true;
        } else if (a == "--dump-engine-script") {
            r.wantEngineScript = true;
        } else if (a == "--engine" || startsWith(a, "--engine=")) {
            std::string v = valueOf("--engine");
            if (!r.ok) return r;
            overrides.push_back({"engine", v});
        } else if (a == "--python" || startsWith(a, "--python=")) {
            std::string v = valueOf("--python");
            if (!r.ok) return r;
            overrides.push_back({"python", v});
        } else if (a == "--engine-timeout" || startsWith(a, "--engine-timeout=")) {
            std::string v = valueOf("--engine-timeout");
            if (!r.ok) return r;
            overrides.push_back({"engineTimeout", v});
        } else if (a == "-i" || a == "--interactive") {
            r.wantInteractive = true;
        } else if (a == "-l" || a == "--lagrange" || startsWith(a, "--lagrange=")) {
            modeFlags.push_back("lagrange");
            auto mv = modeValue(i, "--lagrange");
            if (!mv.second) r.inputMissing = true;
            else r.input = mv.first;
        } else if (a == "-s" || a == "--solve" || startsWith(a, "--solve=")) {
            modeFlags.push_back("solve");
            auto mv = modeValue(i, "--solve");
            if (!mv.second) r.inputMissing = true;
            else r.input = mv.first;
        } else if (a == "-e" || a == "--eval" || a == "--calc" || startsWith(a, "--eval=") ||
                   startsWith(a, "--calc=")) {
            modeFlags.push_back("eval");
            auto mv = modeValue(i, "--eval");
            if (!mv.second) r.inputMissing = true;
            else r.input = mv.first;
        } else if (a == "-L" || a == "--line" || a == "--angle" || startsWith(a, "--line=") ||
                   startsWith(a, "--angle=")) {
            modeFlags.push_back("line");
            auto mv = modeValue(i, "--line");
            if (!mv.second) r.inputMissing = true;
            else r.input = mv.first;
        } else if (a == "-g" || a == "--glyph" || a == "--text" || startsWith(a, "--glyph=") ||
                   startsWith(a, "--text=")) {
            modeFlags.push_back("glyph");
            auto mv = modeValue(i, startsWith(a, "--text") ? "--text" : "--glyph");
            if (!mv.second) r.inputMissing = true;
            else r.input = mv.first;
        } else if (a == "--no-liga") {
            overrides.push_back({"liga", "false"});
        } else if (a == "--no-kern") {
            overrides.push_back({"kern", "false"});
        } else if (a == "--no-font-fallback") {
            overrides.push_back({"fontFallback", "false"});
        } else if (a == "--font-axis" || startsWith(a, "--font-axis=")) {
            std::string v = valueOf("--font-axis");
            if (!r.ok) return r;
            overrides.push_back({"fontAxis", v});
        } else if (a == "--font-instance" || startsWith(a, "--font-instance=")) {
            std::string v = valueOf("--font-instance");
            if (!r.ok) return r;
            overrides.push_back({"fontInstance", v});
        } else if (a == "--cff-text") {
            std::string v = valueOf("--cff-text");
            if (!r.ok) return r;
            r.cffChar = v;
            r.wantCffText = true;
        } else if (a == "--cff-outline") {
            std::string v = valueOf("--cff-outline");
            if (!r.ok) return r;
            r.cffChar = v;
            r.wantCffOutline = true;
        } else if (a == "--cff-info") {
            std::string v = valueOf("--cff-info");
            if (!r.ok) return r;
            r.cffPath = v;
            r.wantCffInfo = true;
        } else if (a == "--kern") {
            std::string v = valueOf("--kern");
            if (!r.ok) return r;
            r.kernPair = v;
            r.wantKern = true;
        } else if (a == "--varied") {
            std::string v = valueOf("--varied");
            if (!r.ok) return r;
            r.variedChar = v;
            r.wantVaried = true;
        } else if (a == "--font-variations") {
            r.wantFontVariations = true;
        } else if (a == "--font-entries") {
            r.wantFontEntries = true;
        } else if (a == "--font-list" || a == "--fonts") {
            r.wantFontList = true;
        } else if (a == "--font-pack" || startsWith(a, "--font-pack=")) {
            std::string v = valueOf("--font-pack");
            if (!r.ok) return r;
            overrides.push_back({"fontPack", v});
        } else if (a == "--font" || startsWith(a, "--font=")) {
            std::string v = valueOf("--font");
            if (!r.ok) return r;
            overrides.push_back({"font", v});
        } else if (a == "--glyph-size" || a == "--size" || startsWith(a, "--glyph-size=") ||
                   startsWith(a, "--size=")) {
            std::string v = valueOf(startsWith(a, "--size") ? "--size" : "--glyph-size");
            if (!r.ok) return r;
            overrides.push_back({"glyphSize", v});
        } else if (a == "--fit-tol" || startsWith(a, "--fit-tol=")) {
            std::string v = valueOf("--fit-tol");
            if (!r.ok) return r;
            overrides.push_back({"glyphTol", v});
        } else if (a == "--at" || a == "--anchor" || startsWith(a, "--at=") ||
                   startsWith(a, "--anchor=")) {
            std::string v = valueOf(startsWith(a, "--anchor") ? "--anchor" : "--at");
            if (!r.ok) return r;
            overrides.push_back({"anchor", v});
        } else if (a == "--lang" || startsWith(a, "--lang=")) {
            std::string v = valueOf("--lang");
            if (!r.ok) return r;
            overrides.push_back({"lang", v});
        } else if (a == "--decimals" || startsWith(a, "--decimals=")) {
            std::string v = valueOf("--decimals");
            if (!r.ok) return r;
            overrides.push_back({"decimals", v});
        } else if (a == "--sep" || a == "--separator" || startsWith(a, "--sep=") ||
                   startsWith(a, "--separator=")) {
            std::string v = valueOf("--sep");
            if (!r.ok) return r;
            overrides.push_back({"separator", v});
        } else if (a == "--deg" || a == "--degree") {
            overrides.push_back({"degreesDefault", "true"});
        } else if (a == "--rad" || a == "--radian") {
            overrides.push_back({"degreesDefault", "false"});
        } else if (a == "--real" || a == "--real-only") {
            overrides.push_back({"realOnly", "true"});
        } else if (a == "--complex") {
            overrides.push_back({"realOnly", "false"});
            overrides.push_back({"complex", "true"});
        } else if (a == "--save") {
            overrides.push_back({"saveHtml", "true"});
            r.saveFlag = true;
        } else if (a == "--no-save") {
            overrides.push_back({"saveHtml", "false"});
            r.saveFlag = true;
        } else if (a == "--ask-save") {
            overrides.push_back({"askSave", "true"});
            r.askSaveFlag = true;
        } else if (a == "--no-ask-save") {
            overrides.push_back({"askSave", "false"});
            r.askSaveFlag = true;
        } else if (a == "--out" || startsWith(a, "--out=")) {
            std::string v = valueOf("--out");
            if (!r.ok) return r;
            r.outBaseName = v;
        } else if (a == "--outdir" || a == "--savedir" || startsWith(a, "--outdir=")) {
            std::string v = valueOf("--outdir");
            if (!r.ok) return r;
            overrides.push_back({"saveDir", v});
        } else if (a == "--name-template" || startsWith(a, "--name-template=")) {
            std::string v = valueOf("--name-template");
            if (!r.ok) return r;
            overrides.push_back({"nameTemplate", v});
        } else if (a == "--overwrite") {
            overrides.push_back({"overwrite", "true"});
        } else if (a == "--no-overwrite") {
            overrides.push_back({"overwrite", "false"});
        } else if (a == "--mathjax") {
            overrides.push_back({"mathjax", "true"});
        } else if (a == "--no-mathjax") {
            overrides.push_back({"mathjax", "false"});
        } else if (a == "--show" || startsWith(a, "--show=")) {
            std::string v = valueOf("--show");
            if (!r.ok) return r;
            overrides.push_back({"show", v});
        } else if (a == "--hide" || startsWith(a, "--hide=")) {
            std::string v = valueOf("--hide");
            if (!r.ok) return r;
            overrides.push_back({"hide", v});
        } else if (a == "--format") {
            std::string v = valueOf("--format");
            if (!r.ok) return r;
            overrides.push_back({"show", v});
        } else if (a == "--set" || startsWith(a, "--set=")) {
            std::string v = valueOf("--set");
            if (!r.ok) return r;
            std::size_t eq = v.find('=');
            if (eq == std::string::npos) {
                r.ok = false;
                r.exitCode = 2;
                r.error = L("--set 需要 键=值 形式", "--set requires key=value");
                return r;
            }
            overrides.push_back({v.substr(0, eq), v.substr(eq + 1)});
        } else if (a == "--config" || startsWith(a, "--config=")) {
            valueOf("--config");
            if (!r.ok) return r;
        } else if (a == "--no-config") {
            // 已处理
        } else if (!a.empty() && a[0] == '-') {
            r.ok = false;
            r.exitCode = 2;
            r.error = L("未知选项: ", "unknown option: ") + a + L("  (用 --help 查看帮助)", "  (see --help)");
            return r;
        } else {
            // 位置参数: 多个时用逗号连接(逗号是各模式的通用分隔符)
            if (r.input.empty()) r.input = a;
            else if (!a.empty()) r.input += "," + a;
        }
    }

    if (r.wantHelp || r.wantVersion || r.wantDumpConfig) return r;

    // 模式冲突
    std::sort(modeFlags.begin(), modeFlags.end());
    modeFlags.erase(std::unique(modeFlags.begin(), modeFlags.end()), modeFlags.end());
    if (modeFlags.size() > 1) {
        r.ok = false;
        r.exitCode = 2;
        std::string s;
        for (std::size_t i = 0; i < modeFlags.size(); ++i) {
            if (i) s += ", ";
            s += "--" + modeFlags[i];
        }
        r.error = L("模式冲突: 同时给出了 ", "conflicting modes: ") + s;
        r.conflicts = modeFlags;
        return r;
    }
    if (modeFlags.size() == 1) r.mode = modeFlags[0];

    if (r.wantInteractive) {
        r.mode.clear();
        r.inputMissing = false;
    }

    if (!r.ok) return r;

    for (const auto &kv : overrides) {
        std::string err;
        if (!applyConfigKV(r.cfg, kv.first, kv.second, err)) {
            r.ok = false;
            r.exitCode = 2;
            r.error = err;
            return r;
        }
    }
    setLang(r.cfg.lang);
    (void)configLoaded;
    return r;
}

} // namespace em

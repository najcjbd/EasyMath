// EasyMath - 主程序 (交互 + 非交互)
#include "cli.hpp"
#include "config.hpp"
#include "engine.hpp"
#include "hpnum.hpp"
#include "interrupt.hpp"
#include "fileio.hpp"
#include "i18n.hpp"
#include "modes.hpp"
#include "zipfile.hpp"
#include "truetype.hpp"
#include "glyph.hpp"
#include "output.hpp"
#include "bigint.hpp"
#include "unicode.hpp"

#include <csignal>
#include <locale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

volatile std::sig_atomic_t g_interrupt = 0;

void onSigint(int) {
    g_interrupt = 1;
    em::requestInterrupt(); // 让正在跑的长计算(数值求根/扫描/外部引擎)立即收手
}

bool interrupted() { return g_interrupt != 0; }

void installSignals() {
#if defined(_WIN32)
    // Windows 没有 sigaction/SIGPIPE; signal() 足够让 Ctrl+C 走安全退出分支
    std::signal(SIGINT, onSigint);
    std::signal(SIGBREAK, onSigint);
#else
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // 不设 SA_RESTART, 以便中断 read
    sigaction(SIGINT, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

std::string trimWs2(const std::string &s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 读入一行; 返回 false 表示 EOF 或中断
bool readLine(const std::string &prompt, std::string &out) {
    std::cout << prompt << std::flush;
    if (!std::getline(std::cin, out)) {
        out.clear();
        return false;
    }
    return true;
}

int exitForInterrupt() {
    std::cout << "\n" << em::L("已安全退出 (Ctrl+C)。", "Safely exited (Ctrl+C).") << "\n";
    return 130;
}

using em::Chan;
using em::Config;
using em::ModeOutput;

void printReport(const ModeOutput &mo, const Config &cfg) {
    std::string text = em::renderTerminal(mo.report, cfg);
    std::cout << text;
}

bool askSave(const Config &cfg) {
    if (!cfg.askSave) return cfg.saveHtml;
    if (!em::channelEnabled(cfg, Chan::Prompt)) return cfg.saveHtml;
    std::string ans;
    if (!readLine(em::L("是否保存为 HTML (Markdown 内嵌)? [Y/n] ",
                        "Save as HTML (with embedded Markdown)? [Y/n] "),
                  ans)) {
        return false;
    }
    std::string t = trimWs2(ans);
    if (t.empty()) return true;
    std::string l;
    for (char c : t) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return l == "y" || l == "yes" || l == "1" || t == "是" || t == "好";
}

void doSave(const ModeOutput &mo, const Config &cfg, const std::string &outBaseName) {
    std::string md = em::renderMarkdown(mo.report, cfg);
    std::string base = outBaseName.empty() ? mo.saveBaseName : outBaseName;
    em::SaveOutcome so = em::saveFunction(cfg, base, md, mo.title, mo.modeName, cfg.saveMarkdownAlso,
                                         mo.report.extraHtml, mo.previewSvg);
    if (!em::channelEnabled(cfg, Chan::FileInfo)) return;
    if (so.ok) {
        std::cout << em::L("已保存: ", "Saved: ") << so.htmlPath;
        if (!so.mdPath.empty()) std::cout << em::L("  以及 ", "  and ") << so.mdPath;
        if (!so.svgPath.empty()) std::cout << em::L("  以及 ", "  and ") << so.svgPath;
        std::cout << "\n";
        if (so.overwrote) std::cout << em::L("(已按配置覆盖同名文件)", "(existing file overwritten as configured)") << "\n";
        if (so.renamed) std::cout << em::L("(检测到同名文件, 已自动改用新文件名)", "(name conflict, renamed automatically)") << "\n";
    } else {
        std::cout << em::L("保存失败: ", "Save failed: ") << so.message << "\n";
    }
}

// 配置点名要某个高精度后端、但本程序没编译它时, 必须说一声:
// 否则用户会以为拿到了 --decimals 50 的 50 位, 实际只有 long double 的 30 多位。
// 只在"确实要超过 long double 位数"时提示, 每次运行最多一次。
static void warnIfHighPrecisionUnavailable(const Config &cfg) {
    static bool warned = false;
    if (warned) return;
    if (cfg.decimals <= static_cast<int>(em::longDoubleDigits10())) return;
    bool mpfrMissing = (cfg.hpfloat == "mpfr" || cfg.hpfloat == "auto") && !em::hpAvailable();
    bool sympyMissing = (cfg.hpfloat == "sympy" || cfg.hpfloat == "auto") &&
                        !em::sympyUsable(cfg.pythonPath);
    if (!mpfrMissing && !sympyMissing) return;
    if (cfg.hpfloat == "auto" && !(mpfrMissing && sympyMissing)) return; // auto 还有一条路可走
    warned = true;
    std::cerr << em::L("警告: 请求 ", "warning: requested ") << cfg.decimals
              << em::L(" 位小数, 但配置指定的高精度后端不可用(hpfloat=", " decimals, but hpfloat=")
              << cfg.hpfloat << em::L("), 实际只能给到约 ", "), limited to about ")
              << em::longDoubleDigits10()
              << em::L(" 位; 可改用 --hpfloat=sympy (需 python3+sympy) 或安装 libmpfr-dev 后重新编译\n",
                       " digits; try --hpfloat=sympy or install libmpfr-dev and rebuild\n");
}

int runOneMode(const std::string &mode, const std::string &input, const Config &cfg,
               bool interactive, const std::string &outBaseName) {
    if (interrupted()) return exitForInterrupt();
    warnIfHighPrecisionUnavailable(cfg);
    em::clearInterrupt(); // 每次计算开始前清掉上一次的中断请求
    ModeOutput mo;
    if (mode == "lagrange") mo = em::runLagrange(input, cfg);
    else if (mode == "solve") mo = em::runSolve(input, cfg);
    else if (mode == "eval") mo = em::runEval(input, cfg);
    else if (mode == "line") mo = em::runLine(input, cfg);
    else if (mode == "glyph") mo = em::runGlyph(input, cfg);
    else {
        std::cerr << em::L("未知模式: ", "unknown mode: ") << mode << "\n";
        return 2;
    }
    if (interrupted() || em::interruptRequested()) {
        std::cout << em::L("\n计算已中断。", "\ninterrupted.") << "\n";
        return 130;
    }
    printReport(mo, cfg);
    if (!mo.ok) {
        if (!mo.error.empty())
            std::cerr << em::L("错误: ", "error: ") << mo.error << "\n";
        return mo.exitCode == 0 ? 1 : mo.exitCode;
    }
    if (mo.saveable) {
        bool save = interactive ? askSave(cfg) : cfg.saveHtml;
        if (save) doSave(mo, cfg, outBaseName);
    }
    return 0;
}

void printBanner(const Config &cfg) {
    if (!em::channelEnabled(cfg, Chan::Banner)) return;
    std::cout << "==============================================\n";
    std::cout << " EasyMath " << em::kVersion << " - "
              << em::L("数学工具 (拉格朗日插值 / 解方程 / 求值开方 / 过原点直线 / 字形→函数)",
                       "math tool (Lagrange / solve / evaluate / line / glyph)")
              << "\n";
    std::cout << "==============================================\n";
}

void printMenu(const Config &cfg) {
    if (em::channelEnabled(cfg, Chan::Tip)) {
        std::cout << em::L("  1) 拉格朗日插值   2) 解方程   3) 求值/开方   4) 过原点直线\n"
                           "  5) 字形→函数       6) 配置     7) 帮助     0) 退出\n",
                           "  1) Lagrange   2) Solve   3) Evaluate   4) Line by angle\n"
                           "  5) Glyph->functions  6) Config   7) Help   0) Quit\n");
    }
}

std::string modePromptText(const std::string &mode, const Config &cfg) {
    (void)cfg;
    if (mode == "lagrange")
        return em::L("请输入数据点 (如 x=1,y=3 x=2,y=5 x=3,y=9): ",
                     "Enter data points (e.g. x=1,y=3 x=2,y=5): ");
    if (mode == "solve")
        return em::L("请输入方程 (多个用逗号分隔, 如 y=5x, y=6z, x=2z): ",
                     "Enter equations (comma separated, e.g. y=5x, y=6z, x=2z): ");
    if (mode == "eval")
        return em::L("请输入要求值的式子 (如 5! , 6^8 , \u221a6 , 5x6): ",
                     "Enter an expression (e.g. 5! , 6^8 , sqrt6 , 5x6): ");
    if (mode == "glyph")
        return em::L("请输入文字(可带左下角坐标, 如 你好(10,20) 或 :(10,20) 表示镜像): ",
                     "Enter text (optionally with a bottom-left anchor, e.g. Hello(10,20), ':' mirrors): ");
    if (mode == "line")
        return em::L("请输入与 x 轴的夹角 (如 45\u00b0 , 135 , 0.5rad): ",
                     "Enter the angle with the x-axis (e.g. 45deg , 135 , 0.5rad): ");
    return em::L("请输入: ", "Enter: ");
}

void configMenu(Config &cfg) {
    for (;;) {
        if (interrupted()) return;
        std::cout << "\n" << em::L("当前配置:", "Current configuration:") << "\n";
        std::cout << em::dumpConfig(cfg);
        std::cout << em::L("输入 键=值 修改配置, save 保存到默认配置文件, back 返回: ",
                           "Enter key=value to change, save to write config file, back to return: ");
        std::string line;
        if (!std::getline(std::cin, line)) return;
        std::string t = trimWs2(line);
        if (t.empty()) continue;
        if (t == "back" || t == "b" || t == "0" || t == em::L("返回", "back")) return;
        if (t == "save" || t == "s") {
            std::string err;
            std::string path = em::defaultConfigPath();
            if (em::saveConfig(cfg, path, err))
                std::cout << em::L("已保存配置到 ", "config saved to ") << path << "\n";
            else
                std::cout << em::L("保存失败: ", "save failed: ") << err << "\n";
            continue;
        }
        std::size_t eq = t.find('=');
        if (eq == std::string::npos) {
            std::cout << em::L("格式: 键=值", "format: key=value") << "\n";
            continue;
        }
        std::string k = trimWs2(t.substr(0, eq)), v = trimWs2(t.substr(eq + 1));
        std::string err;
        if (em::applyConfigKV(cfg, k, v, err)) std::cout << em::L("已设置 ", "set ") << k << "\n";
        else std::cout << em::L("错误: ", "error: ") << err << "\n";
    }
}

int interactiveShell(Config &cfg, const std::string &presetMode, const std::string &presetInput,
                     const std::string &outBaseName) {
    printBanner(cfg);
    std::string pendingMode = presetMode;
    std::string pendingInput = presetInput;
    for (;;) {
        if (interrupted()) return exitForInterrupt();
        std::string mode = pendingMode;
        if (mode.empty()) {
            printMenu(cfg);
            std::string choice;
            if (!readLine(em::L("请选择 [0-7]: ", "Choose [0-7]: "), choice)) {
                if (interrupted()) return exitForInterrupt();
                std::cout << "\n";
                return 0;
            }
            std::string c = trimWs2(choice);
            if (c.empty()) continue;
            if (c == "0" || c == "q" || c == "quit" || c == "exit" || c == em::L("退出", "quit")) {
                std::cout << em::L("再见!", "Bye!") << "\n";
                return 0;
            }
            if (c == "1" || c == "l" || c == "lagrange") mode = "lagrange";
            else if (c == "2" || c == "s" || c == "solve") mode = "solve";
            else if (c == "3" || c == "e" || c == "eval") mode = "eval";
            else if (c == "4" || c == "L" || c == "line") mode = "line";
            else if (c == "5" || c == "g" || c == "glyph") mode = "glyph";
            else if (c == "6" || c == "c" || c == "config") {
                configMenu(cfg);
                continue;
            } else if (c == "7" || c == "h" || c == "help") {
                std::cout << em::helpText(cfg);
                continue;
            } else {
                // 直接当作输入, 猜测模式
                pendingInput = c;
                mode = em::guessMode(c);
                std::cout << em::L("(按 ", "(using ") << mode << em::L(" 模式处理)", " mode)") << "\n";
            }
        }
        std::string input = pendingInput;
        pendingInput.clear();
        pendingMode.clear();
        while (!interrupted()) {
            if (!input.empty()) break;
            std::string line;
            if (!readLine(modePromptText(mode, cfg), line)) {
                if (interrupted()) return exitForInterrupt();
                std::cout << "\n";
                return 0;
            }
            input = trimWs2(line);
            if (input.empty()) break;
        }
        if (interrupted()) return exitForInterrupt();
        if (input.empty()) continue;
        int rc = runOneMode(mode, input, cfg, true, outBaseName);
        if (rc == 130) {
            // 交互模式: 中途打断只取消这一次计算, 回到菜单继续用
            // (在菜单上等输入时按 Ctrl+C 才是安全退出)
            em::clearInterrupt();
            g_interrupt = 0;
            std::cout << em::L("(已回到主菜单; 在菜单上再按一次 Ctrl+C 可退出)",
                               "(back to the menu; press Ctrl+C at the menu to quit)")
                      << "\n";
            continue;
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    installSignals();
    em::CliResult cli = em::parseArgs(argc, argv);
    if (cli.wantHelp) {
        std::cout << em::helpText(cli.cfg);
        return 0;
    }
    if (cli.wantVersion) {
        std::cout << em::versionText() << "\n";
        return 0;
    }
    if (cli.wantDumpConfig) {
        em::Config d = em::defaultConfig();
        std::string err;
        std::string tmp;
        (void)tmp;
        std::cout << em::dumpConfig(d);
        std::cout << "\n# 输出通道默认值\n";
        for (const auto &n : em::allChannelNames()) std::cout << "out." << n << " = 1\n";
        return 0;
    }
    if (!cli.ok) {
        std::cerr << em::kProgramName << ": " << cli.error << "\n";
        return cli.exitCode == 0 ? 2 : cli.exitCode;
    }
    if (cli.wantEngineScript) {
        std::cout << em::sympyEngineScript();
        return 0;
    }
    if (cli.wantEngineInfo) {
        em::EngineInfo ei = em::detectSympyEngine(cli.cfg.pythonPath);
        std::cout << em::L("符号引擎: ", "symbolic engine: ")
                  << (ei.available ? "SymPy " + ei.version : em::L("不可用", "unavailable")) << "\n";
        std::cout << "  python   = " << ei.python << "\n";
        if (!ei.available) std::cout << "  error    = " << ei.error << "\n";
        std::cout << "  engine   = " << cli.cfg.engine << "  (auto/builtin/sympy)\n";
        std::cout << em::L("大整数后端: ", "big integer backend: ") << em::BigInt::backendName()
                  << "  (bigint=" << cli.cfg.bigint << ")\n";
        std::cout << em::L("平台 long double: ", "platform long double: ")
                  << (em::longDoubleIsDouble() ? "== double" : "")
                  << em::L(" 有效十进位=", " decimal digits=") << em::longDoubleDigits10() << "\n";
        std::cout << em::L("高精度浮点: ", "high-precision float: ") << em::hpBackendName();
        if (em::hpAvailable()) std::cout << " " << em::hpVersion();
        std::cout << "  (hpfloat=" << cli.cfg.hpfloat << ", precision=" << cli.cfg.precisionBits
                  << ")\n";
        bool warn = false;
        if (cli.cfg.bigint == "gmp" && !em::BigInt::usesGmp()) {
            std::cout << em::L("  警告: 本二进制未编译 GMP 后端 (需 cmake -DEASYMATH_WITH_GMP=ON 并安装 libgmp-dev)\n",
                               "  warning: this binary was built without GMP (install libgmp-dev and rebuild)\n");
            warn = true;
        }
        if (cli.cfg.bigint == "builtin" && em::BigInt::usesGmp())
            std::cout << em::L("  提示: 本二进制已编译为 GMP 后端, 运行时无法切换(如需请重新编译)\n",
                               "  note: binary is built with GMP; rebuild to use the built-in backend\n");
        if ((cli.cfg.hpfloat == "mpfr") && !em::hpAvailable()) {
            std::cout << em::L("  警告: 未编译 MPFR 后端 (需 libmpfr-dev libmpc-dev)\n",
                               "  warning: MPFR backend not compiled in\n");
            warn = true;
        }
        (void)warn;
        return 0;
    }
    if (cli.wantCffText) {
        if (!cli.cffPath.empty()) cli.cfg.fontPack = cli.cffPath;   // 复用 --cff-info 给的字体路径
        std::string ferr;
        std::string out = em::cffTextFunctions(cli.cfg, cli.cffChar, ferr);
        if (out.empty()) {
            std::cerr << em::L("错误: ", "error: ") << ferr << "\n";
            return 3;
        }
        std::cout << out;
        return 0;
    }
    if (cli.wantCffOutline) {
        std::vector<uint8_t> buf;
        std::string rerr;
        if (!em::readWholeFile(cli.cffPath, buf, rerr)) {
            std::cerr << em::L("错误: ", "error: ") << rerr << "\n";
            return 3;
        }
        auto cps = em::utf8_decode(cli.cffChar);
        if (cps.empty()) {
            std::cerr << em::L("错误: 请给一个字符", "error: give one character") << "\n";
            return 3;
        }
        em::CffInfo ci;
        std::string e2;
        if (!em::cffReadInfo(buf, ci, e2)) {
            std::cerr << em::L("错误: ", "error: ") << e2 << "\n";
            return 3;
        }
        em::CffPath path;
        std::string e3;
        int wantGid = 35;
        std::string cerr3;
        if (!em::cffCmapLookup(buf, uint32_t(cps[0]), wantGid, cerr3)) {
            std::cerr << em::L("错误: ", "error: ") << cerr3 << "\n";
            return 3;
        }
        std::cout << "字符=" << cli.cffChar << "\tgid=" << wantGid << "\n";
        if (!em::cffGlyphPath(buf, wantGid, path, e3)) {
            std::cerr << em::L("错误: ", "error: ") << e3 << "\n";
            return 3;
        }
        double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
        for (const auto &p : path.pts) {
            x0 = std::min(x0, p.first); x1 = std::max(x1, p.first);
            y0 = std::min(y0, p.second); y1 = std::max(y1, p.second);
        }
        std::cout << "端点=" << path.ends.size() << "\t采样点=" << path.pts.size()
                  << "\t包围盒 x[" << long(x0) << "," << long(x1) << "] y[" << long(y0) << "," << long(y1) << "]"
                  << "\t子程序调用=" << path.subrCalls << "\n";
        // ---- B3: 把 CFF 路径转成轮廓段并送进拟合管线(与 glyf 路径共用 fitContour) ----
        {
            std::vector<em::TtContour> contours;
            em::TtContour cur;
            auto pushLine = [&](double ax, double ay, double bx, double by) {
                em::TtSeg sg;
                sg.kind = em::TtKind::Line;
                sg.p0.x = ax; sg.p0.y = ay; sg.p1.x = bx; sg.p1.y = by;
                cur.segs.push_back(sg);
            };
            // CffPath 的 pts 是按命令顺序记录的点; 曲线段我按 8 段折线采样过,
            // 这里用相邻点连成折线交给拟合(够验证"能出函数", 精度细化留下一轮)
            for (std::size_t i = 1; i < path.pts.size(); ++i)
                pushLine(path.pts[i-1].first, path.pts[i-1].second, path.pts[i].first, path.pts[i].second);
            if (!cur.segs.empty()) contours.push_back(cur);
            int segs = 0, contoursOut = 0;
            std::string firstPlain;
            for (const auto &c : contours) {
                em::GlyphContour gc = em::fitContour(c, 0.5, true, 0.35, 1.0, 0, 0);
                if (gc.segs.empty()) continue;
                ++contoursOut;
                segs += int(gc.segs.size());
                if (firstPlain.empty() && !gc.segs.empty()) firstPlain = em::segPlain(gc.segs[0], 0, "x");
            }
            std::cout << "拟合: 轮廓=" << contoursOut << "\t段数=" << segs << "\n";
            if (!firstPlain.empty()) std::cout << "首个函数: " << firstPlain << "\n";
            // 与字形模式同样的函数行(便于端到端核对: OTF 也能出函数)
            int ci2 = 0;
            for (const auto &c : contours) {
                em::GlyphContour gc = em::fitContour(c, 0.5, true, 0.35, 1.0, 0, 0);
                if (gc.segs.empty()) continue;
                ++ci2;
                for (std::size_t k = 0; k < gc.segs.size(); ++k)
                    std::cout << "  轮廓" << ci2 << " 段" << (k + 1) << ": "
                              << em::segPlain(gc.segs[k], int(k), "x") << "\n";
            }
        }
        std::cout << "前6端点:";
        for (std::size_t i = 0; i < 6 && i < path.ends.size(); ++i)
            std::cout << " (" << long(path.ends[i].first) << "," << long(path.ends[i].second) << ")";
        std::cout << "\n";
        return 0;
    }
    if (cli.wantCffInfo) {
        std::vector<uint8_t> buf;
        std::string rerr;
        if (!em::readWholeFile(cli.cffPath, buf, rerr)) {
            std::cerr << em::L("错误: ", "error: ") << rerr << "\n";
            return 3;
        }
        em::CffInfo ci;
        std::string cerr2;
        if (!em::cffReadInfo(buf, ci, cerr2)) {
            std::cerr << em::L("错误: ", "error: ") << cerr2 << "\n";
            return 3;
        }
        std::cout << "CFF\toff=" << ci.off << "\tlen=" << ci.len << "\t版本=" << ci.major << "." << ci.minor
                  << "\thdrSize=" << ci.hdrSize << "\n";
        std::cout << "NAME\t" << ci.nameCount << "\nTOPDICT\t" << ci.topDictCount << "\nSTRING\t"
                  << ci.stringCount << "\nGSUBRS\t" << ci.gsubrCount << "\n";
        std::cout << "CHARSTRINGS\toff=" << ci.charStringsOff << "\t条数=" << ci.charStringsCount << "\n";
        std::cout << "PRIVATE\tsize=" << ci.privateSize << "\toff=" << ci.privateOff
                  << "\tSubrs=" << ci.subrsOff << "\n";
        std::cout << "CHARSET\toff=" << ci.charsetOff << "\n";
        return 0;
    }
    if (cli.wantKern) {
        std::string ferr;
        std::string out = em::kernInfo(cli.cfg, cli.kernPair, ferr);
        if (out.empty()) {
            std::cerr << em::L("错误: ", "error: ") << ferr << "\n";
            return 3;
        }
        std::cout << out;
        return 0;
    }
    if (cli.wantVaried) {
        std::string ferr;
        std::string out = em::variedInfo(cli.cfg, cli.variedChar, ferr);
        if (out.empty() || out.compare(0, 4, "ERR\t") == 0) {
            std::cerr << em::L("错误: ", "error: ") << (ferr.empty() ? out : ferr) << "\n";
            return 3;
        }
        std::cout << out;
        return 0;
    }
    if (cli.wantFontVariations) {
        std::string ferr;
        std::string out = em::fontVariations(cli.cfg, ferr);
        if (out.empty()) {
            std::cerr << em::L("错误: ", "error: ") << ferr << "\n";
            return 3;
        }
        std::cout << out;
        return 0;
    }
    if (cli.wantFontEntries) {   // 机器可读清单(含 RAWHEX): 安卓端与测试用
        std::string ferr;
        std::string out = em::fontPackEntries(cli.cfg, ferr);
        if (out.empty() || out.compare(0, 4, "ERR\t") == 0) {
            std::cerr << em::L("错误: ", "error: ") << (ferr.empty() ? out : ferr) << "\n";
            return 3;
        }
        std::cout << out;
        return 0;
    }
    if (cli.wantFontList) {
        std::string ferr;
        std::string out = em::fontPackList(cli.cfg, ferr);
        if (out.empty()) {
            std::cerr << em::L("错误: ", "error: ") << ferr << "\n";
            return 3;
        }
        std::cout << out;
        return 0;
    }
    if (cli.wantPrintConfig) {
        std::cout << em::dumpConfig(cli.cfg);
        return 0;
    }
    Config cfg = cli.cfg;
    if (cli.mode.empty() || cli.inputMissing || cli.wantInteractive) {
        // 交互模式; 若给出了模式但缺少输入, 进入该模式补全
        std::string preset = cli.inputMissing ? cli.mode : std::string();
        std::string presetInput = cli.inputMissing ? cli.input : std::string();
        if (cli.wantInteractive) preset.clear();
        return interactiveShell(cfg, preset, presetInput, cli.outBaseName);
    }
    return runOneMode(cli.mode, cli.input, cfg, false, cli.outBaseName);
}

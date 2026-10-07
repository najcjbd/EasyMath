// EasyMath Android 桥: 把桌面端核心(Ubuntu/macOS/Windows 同一份源码)暴露给 Java
#include <jni.h>

#include <string>
#include <vector>

#include "bigint.hpp"
#include "config.hpp"
#include "engine.hpp"
#include "i18n.hpp"
#include "interrupt.hpp"
#include "hpnum.hpp"
#include "modes.hpp"
#include "output.hpp"

namespace {

// ---- 用 JNI 回调 Java, 由 Chaquopy 在进程内执行 Python ----
JNIEnv *g_env = nullptr;
jclass g_cls = nullptr;

bool javaPythonRunner(const std::string &, const std::vector<std::string> &args, std::string &out,
                      std::string &err) {
    if (!g_env || !g_cls) {
        err = "python runner unavailable";
        return false;
    }
    std::string joined;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) joined += '\x1f';
        joined += args[i];
    }
    jmethodID mid = g_env->GetStaticMethodID(g_cls, "runPython", "(Ljava/lang/String;)Ljava/lang/String;");
    if (!mid) {
        g_env->ExceptionClear();
        err = "runPython method not found";
        return false;
    }
    jstring jarg = g_env->NewStringUTF(joined.c_str());
    jstring jres = static_cast<jstring>(g_env->CallStaticObjectMethod(g_cls, mid, jarg));
    g_env->DeleteLocalRef(jarg);
    if (g_env->ExceptionCheck()) {
        g_env->ExceptionDescribe();
        g_env->ExceptionClear();
        err = "java exception in runPython";
        return false;
    }
    if (!jres) {
        err = "python returned null";
        return false;
    }
    const char *p = g_env->GetStringUTFChars(jres, nullptr);
    out = p ? p : "";
    if (p) g_env->ReleaseStringUTFChars(jres, p);
    g_env->DeleteLocalRef(jres);
    // Java 侧用 \u0001 前缀表示"执行失败, 后面是原因"(不能返回 null, 否则原因就丢了)
    if (!out.empty() && out[0] == '\x01') {
        err = out.substr(1);
        out.clear();
        return false;
    }
    return true;
}

std::string jsonEscape(const std::string &s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o;
}

const char *chanName(em::Chan c) {
    using em::Chan;
    switch (c) {
        case Chan::Banner: return "banner";
        case Chan::Input: return "input";
        case Chan::Normalized: return "normalized";
        case Chan::Note: return "note";
        case Chan::Step: return "step";
        case Chan::LagrangeForm: return "lagrange";
        case Chan::Polynomial: return "polynomial";
        case Chan::PlainForm: return "plain";
        case Chan::DecimalForm: return "decimal";
        case Chan::ApproxForm: return "approx";
        case Chan::LatexForm: return "latex";
        case Chan::MarkdownForm: return "markdown";
        case Chan::HtmlForm: return "html";
        case Chan::SolutionSet: return "solution";
        case Chan::Group: return "group";
        case Chan::Verify: return "verify";
        case Chan::FileInfo: return "file";
        case Chan::Prompt: return "prompt";
        case Chan::Tip: return "tip";
    }
    return "other";
}

std::string jstr(JNIEnv *env, jstring s) {
    if (!s) return std::string();
    const char *p = env->GetStringUTFChars(s, nullptr);
    std::string r = p ? p : "";
    if (p) env->ReleaseStringUTFChars(s, p);
    return r;
}

std::string sectionsToJson(const em::Report &rep) {
    std::string out = "[";
    bool firstSection = true;
    for (const auto &sec : rep.sections) {
        if (!firstSection) out += ",";
        firstSection = false;
        out += "{\"chan\":\"" + std::string(chanName(sec.chan)) + "\",";
        out += "\"title\":\"" + jsonEscape(sec.title) + "\",";
        out += "\"lines\":[";
        for (std::size_t i = 0; i < sec.plainLines.size(); ++i) {
            if (i) out += ",";
            std::string plain = sec.plainLines[i];
            std::string latex = (i < sec.latexLines.size()) ? sec.latexLines[i] : plain;
            out += "{\"plain\":\"" + jsonEscape(plain) + "\",\"latex\":\"" + jsonEscape(latex) + "\"}";
        }
        out += "]}";
    }
    out += "]";
    return out;
}

// 注册/注销 Chaquopy 进程内执行器(仅在需要时包一层, 保证成对)
static void attachPythonRunner(JNIEnv *env) {
    jclass cls = env->FindClass("com/easymath/app/NativeBridge");
    if (cls) {
        g_cls = static_cast<jclass>(env->NewGlobalRef(cls));
        g_env = env;
        em::setPythonRunner(javaPythonRunner);
    }
}

static void detachPythonRunner(JNIEnv *env) {
    em::setPythonRunner(nullptr);
    if (g_cls) {
        env->DeleteGlobalRef(g_cls);
        g_cls = nullptr;
    }
    g_env = nullptr;
}

static std::string pickOption(const std::string &v, const char *a, const char *b, const char *c,
                              const char *fallback) {
    if (v == a || v == b || v == c) return v;
    return fallback;
}

} // namespace

extern "C" {

// 界面上的"打断"按钮 -> 让正在运行的长计算尽快停止
JNIEXPORT void JNICALL Java_com_easymath_app_NativeBridge_cancel(JNIEnv *, jclass) {
    em::requestInterrupt();
}

JNIEXPORT jstring JNICALL Java_com_easymath_app_NativeBridge_version(JNIEnv *env, jclass) {
    return env->NewStringUTF((std::string("EasyMath ") + em::kVersion + " (android)").c_str());
}

// 后端自检(等价于桌面端的 --engine-info): 让用户能直接看到
// "有没有 SymPy、为什么退回内置、long double 多少位"
JNIEXPORT jstring JNICALL Java_com_easymath_app_NativeBridge_info(JNIEnv *env, jclass) {
    em::Config cfg = em::defaultConfig();
    cfg.hpfloat = "auto";
    cfg.bigint = "builtin";
    cfg.engine = "auto";
    attachPythonRunner(env);
    em::EngineInfo ei = em::detectSympyEngine(cfg.pythonPath);
    detachPythonRunner(env);

    std::string t = std::string("EasyMath ") + em::kVersion + " (android)\n";
    t += "大整数后端: " + std::string(em::BigInt::backendName()) + " (精确, 速度随位数变慢)\n";
    t += "平台 long double: 有效十进位=" + std::to_string(em::longDoubleDigits10()) + "\n";
    t += "高精度浮点: " + std::string(em::hpBackendName());
    t += em::hpAvailable() ? (" " + em::hpVersion()) : " (无 MPFR)";
    t += "\n符号引擎: ";
    if (ei.available) t += "SymPy " + ei.version + " (进程内 Chaquopy, 与桌面端同引擎)";
    else {
        t += "不可用 → 已退回内置引擎";
        if (!ei.error.empty()) t += "\n  原因: " + ei.error;
        t += "\n  (内置引擎仍是精确解, 但没有根式通解与 mpmath 任意精度)";
    }
    return env->NewStringUTF(t.c_str());
}

// 列出字体包里的字体(安卓"选择字体"对话框): CHOSEN/FONT/PACK 行
JNIEXPORT jstring JNICALL Java_com_easymath_app_NativeBridge_fonts(JNIEnv *env, jclass,
                                                                  jstring jfontpack) {
    em::Config cfg = em::defaultConfig();
    cfg.fontPack = jstr(env, jfontpack);
    std::string err;
    std::string out = em::fontPackEntries(cfg, err);
    return env->NewStringUTF(out.c_str());
}

// mode: lagrange | solve | eval | line | glyph
JNIEXPORT jstring JNICALL Java_com_easymath_app_NativeBridge_variations(JNIEnv *env, jclass,
                                                                        jstring jfontpack,
                                                                        jstring jfontpick) {
    em::Config cfg;
    cfg.fontPack = jstr(env, jfontpack);
    cfg.fontPick = jstr(env, jfontpick);
    std::string err;
    std::string out = em::fontVariations(cfg, err);
    if (out.empty()) out = "ERR\t" + err + "\n";
    return env->NewStringUTF(out.c_str());
}

JNIEXPORT jstring JNICALL Java_com_easymath_app_NativeBridge_run(JNIEnv *env, jclass, jstring jmode,
                                                                 jstring jinput, jint jdecimals,
                                                                 jboolean realOnly, jboolean showSteps,
                                                                 jstring jlang, jstring jengine,
                                                                 jstring jscientific,
                                                                 jstring jineq, jstring jconsts,
                                                                 jstring jderive,
                                                                 jstring janchor, jstring jfontpack,
                                                                 jstring jstrokes, jint jsize,
                                                                 jdouble jtol, jstring jfontpick,
                                                                 jstring jfontaxis,
                                                                 jstring jfontinstance) {
    em::clearInterrupt(); // 新一次计算
    std::string mode = jstr(env, jmode);
    std::string input = jstr(env, jinput);
    std::string lang = jstr(env, jlang);

    em::setLang(lang.empty() ? "zh" : lang);
    em::Config cfg = em::defaultConfig();
    cfg.lang = lang.empty() ? "zh" : lang;
    cfg.decimals = (jdecimals >= 0 && jdecimals <= em::kMaxDecimals) ? jdecimals : 8;
    cfg.realOnly = realOnly == JNI_TRUE;
    // 界面「设置」里可改: 求解引擎 / 科学计数法(与桌面端同名同义)
    cfg.scientific = pickOption(jstr(env, jscientific), "auto", "always", "never", "auto");
    cfg.numericInequality = pickOption(jstr(env, jineq), "auto", "always", "never", "auto");
    cfg.constants = jstr(env, jconsts);  // 解方程: 视为常量的字母
    cfg.derive = jstr(env, jderive);     // 解方程: 由解反推的表达式(派生量)
    cfg.glyphAnchor = jstr(env, janchor);       // 字形: 左下角坐标(支持 : 前缀镜像)
    cfg.fontPack = jstr(env, jfontpack);        // 字形: 用户导入的字体包(.zip)或 .ttf
    cfg.fontPick = jstr(env, jfontpick);        // 字形: 选哪个字体(序号或名字子串)

    cfg.fontPick = jstr(env, jfontpick);        // 字形: 选哪个字体(序号或名字子串)
    cfg.fontAxis = jstr(env, jfontaxis);
    cfg.fontInstance = jstr(env, jfontinstance);
    cfg.strokes = jstr(env, jstrokes);          // 字形: 手绘笔画
    if (jsize >= 1 && jsize <= 1000000) cfg.glyphSize = jsize;
    if (jtol > 0 && jtol <= 1000) cfg.glyphTol = jtol;
    cfg.sciThreshold = 12;
    cfg.hpfloat = "auto";    // 无 MPFR, 但有 SymPy/mpmath 时同样能给任意精度
    cfg.bigint = "builtin";  // 不编 GMP
    // 有 Chaquopy 内嵌的 SymPy 就用它(与桌面端同引擎), 否则自动退回内置引擎
    cfg.engine = pickOption(jstr(env, jengine), "auto", "sympy", "builtin", "auto");
    cfg.out.banner = false;
    cfg.out.tip = false;
    cfg.out.prompt = false;
    cfg.out.step = showSteps == JNI_TRUE;

    // 注册 Chaquopy 执行器(仅在本次调用期间有效)
    attachPythonRunner(env);

    em::ModeOutput mo;
    if (mode == "lagrange") mo = em::runLagrange(input, cfg);
    else if (mode == "solve") mo = em::runSolve(input, cfg);
    else if (mode == "line") mo = em::runLine(input, cfg);
    else if (mode == "glyph") mo = em::runGlyph(input, cfg);
    else mo = em::runEval(input, cfg);

    // 重要: 必须在 detachPythonRunner 之前探测, 否则会退化成 fork/exec python3,
    // 在 Android 上必然失败并(曾经)把失败结论永久缓存, 整个进程都不再用 SymPy。
    em::EngineInfo ei = em::detectSympyEngine(cfg.pythonPath);
    bool sympyOn = ei.available;
    detachPythonRunner(env);

    std::string json = "{";
    json += "\"ok\":" + std::string(mo.ok ? "true" : "false") + ",";
    json += "\"exitCode\":" + std::to_string(mo.exitCode) + ",";
    json += "\"error\":\"" + jsonEscape(mo.error) + "\",";
    json += "\"title\":\"" + jsonEscape(mo.title) + "\",";
    json += "\"mode\":\"" + jsonEscape(mo.modeName) + "\",";
    json += std::string("\"engine\":\"") + (sympyOn ? "sympy" : "builtin") + "\",";
    json += "\"engineVersion\":\"" + jsonEscape(ei.version) + "\",";
    json += "\"engineError\":\"" + jsonEscape(ei.error) + "\",";
    json += "\"sections\":" + sectionsToJson(mo.report) + ",";
    // 字形模式的自包含 SVG 预览(安卓端直接塞进结果 WebView)
    json += "\"svg\":\"" + jsonEscape(mo.previewSvg) + "\"";
    json += "}";
    return env->NewStringUTF(json.c_str());
}

} // extern "C"

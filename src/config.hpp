// EasyMath - 配置
#pragma once

#include <string>
#include <vector>

namespace em {

// 版本号唯一真源: 与 Android 的 versionName 相同, 打包脚本也读它
extern const char *kVersion;
extern const int kMaxDecimals;   // 小数位硬上限(100000)

// 输出通道: 任何一类输出都可以单独开启/关闭
enum class Chan {
    Banner,        // 程序抬头
    Input,         // 回显输入
    Normalized,    // 归一化后的输入
    Note,          // 说明/警告
    Step,          // 计算步骤
    LagrangeForm,  // 拉格朗日形式
    Polynomial,    // 化简后的多项式 P(x)
    PlainForm,     // 纯文本公式
    DecimalForm,   // 小数(精确形式)
    ApproxForm,    // 近似值
    LatexForm,     // LaTeX
    MarkdownForm,  // Markdown
    HtmlForm,      // HTML
    SolutionSet,   // 解集
    Group,         // 分组/比例(解之后的附加信息, 永远排在最后)
    Verify,        // 验算(把点代回函数)
    FileInfo,      // 保存文件信息
    Prompt,        // 交互提问
    Tip            // 提示
};

struct OutputToggles {
    bool banner = true;
    bool input = true;
    bool normalized = true;
    bool note = true;
    bool step = false;
    bool lagrangeForm = true;
    bool polynomial = true;
    bool plainForm = true;
    bool decimalForm = true;
    bool approxForm = true;
    bool latexForm = true;
    bool markdownForm = false;
    bool htmlForm = false;
    bool solutionSet = true;
    bool group = true;
    bool verify = true;
    bool fileInfo = true;
    bool prompt = true;
    bool tip = true;
};

struct Config {
    std::string lang = "zh";
    int decimals = 8;
    bool realOnly = false;
    bool complexAllowed = true;

    // 保存
    std::string saveDir = ".";
    bool useDefaultName = true;               // 是否使用默认命名
    std::string nameTemplate = "函数 {date} {n}"; // 默认输入名
    bool overwrite = false;                   // false: 防止覆盖(加(1))
    bool saveHtml = false;                    // 非交互默认是否保存
    bool askSave = true;                      // 交互模式询问是否保存
    bool mathjax = true;                      // HTML 中引入 MathJax CDN
    bool saveMarkdownAlso = true;             // 同时写 .md

    // 数学
    bool prettyUnicode = true;  // 纯文本用上标/√ 等
    bool exactPreferred = true; // 优先精确值
    bool degreesDefault = true; // 直线模式默认角度制
    std::string extraSeparator; // 自定义分隔符(可多个字符)

    // 外部符号引擎 (SymPy)
    std::string engine = "auto";   // auto | builtin | sympy
    std::string pythonPath = "python3";
    int engineTimeoutMs = 15000;

    // 数值后端 (编译期检测, 运行期可切换)
    std::string bigint = "auto";   // auto | gmp | builtin
    std::string hpfloat = "auto";  // auto | mpfr | sympy | builtin
    int precisionBits = 0;         // MPFR 精度(位); 0 = 按 decimals 自动
    std::string scientific = "auto"; // auto | always | never  科学计数法
    // 不等式/非零约束是否允许用"数值方法"给出解集(周期函数这类只能数值求):
    //   auto   = 单条周期关系给数值区间(带标注); 多条周期关系拒绝化简(默认, 保守)
    //   always = 多条周期关系也算(先求公共周期, 再逐段判定; 漏解风险更高)
    //   never  = 完全不用数值方法, 只保留原样列出的约束
    std::string numericInequality = "auto"; // auto | always | never
    // 解方程: 把哪些字母当"常量"(不求解、不算未知量), 逗号分隔, 如 "a,b,c"
    std::string constants;
    // 字形模式(文字/手绘 -> 函数)
    std::string fontPack;              // 字体包(.zip)或单个 .ttf; 空=自动找默认位置
    std::string fontPick;              // 字体筛选子串(默认 Regular)
    std::string fontAxis;              // 轴值, 例: "wght=700,opsz=18"
    std::string fontInstance;          // 命名实例: 序号(1 起)或名字
    bool kern = true;                  // GPOS 成对字距
    bool liga = true;                  // GSUB 连字(可用 --no-liga 关掉做对照)                  // GPOS 成对字距(可用 --no-kern 关掉做对照)
    bool fontFallback = true;          // 主字体缺字时, 在同一个字体包里换别的字体
    double glyphSize = 1000;           // 字号(输出坐标单位)
    double glyphTol = 0.5;             // 拟合容差(字体单位)
    std::string glyphAnchor;
    // 手绘笔画(安卓手绘模式): "x1,y1;x2,y2;...|x1,y1;..." 每段一条笔画, 坐标 y 向上
    std::string strokes;           // 左下角坐标, 如 (10,20) 或 :(10,20)(: = 左右镜像)
    // 解方程: 由解反推这些表达式的值(派生量), 逗号分隔, 如 "abc,xy"
    std::string derive;
    int sciThreshold = 12;           // |v| >= 10^threshold 时用科学计数法
    long double scanLo = -100.0L;
    long double scanHi = 100.0L;

    OutputToggles out;
    std::string configPath;
    bool loadedFromFile = false;
};

Config defaultConfig();
Config loadConfig(const std::string &path, bool &ok, std::string &err, bool warn = false);
bool saveConfig(const Config &cfg, const std::string &path, std::string &err);
std::string defaultConfigPath();
bool applyConfigKV(Config &cfg, const std::string &key, const std::string &value, std::string &err);
std::string dumpConfig(const Config &cfg);
bool channelEnabled(const Config &cfg, Chan c);
bool setChannel(Config &cfg, const std::string &name, bool on);
std::vector<std::string> allChannelNames();      // 含别名, 供 --show/--hide
std::vector<std::string> canonicalChannelNames(); // 每个通道一次, 供生成配置

} // namespace em

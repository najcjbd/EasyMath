// EasyMath - 五种模式(拉格朗日/解方程/求值/直线/字形)
#pragma once

#include "config.hpp"
#include "output.hpp"

#include <string>

namespace em {

struct ModeOutput {
    bool ok = false;
    int exitCode = 0; // 0 成功, 1 无解/不确定, 3 输入错误
    std::string error;
    Report report;
    std::string saveBaseName; // 用户指定的文件名(不含扩展名); 空 -> 默认命名
    std::string title;
    std::string modeName; // {mode} 占位
    bool saveable = false;
    std::string previewSvg;   // 自包含 SVG 预览(字形模式); 同时进了 report.extraHtml
};

ModeOutput runLagrange(const std::string &input, const Config &cfg);
ModeOutput runSolve(const std::string &input, const Config &cfg);
ModeOutput runEval(const std::string &input, const Config &cfg);
ModeOutput runLine(const std::string &input, const Config &cfg);
ModeOutput runGlyph(const std::string &input, const Config &cfg);
// 列出字体包里的字体(--font-list / 安卓"选择字体"); 失败返回空串并填 err
std::string fontPackList(const Config &cfg, std::string &err, std::string *chosenOut = nullptr);
// 机器可读清单(安卓界面用): CHOSEN/FONT/PACK 行
std::string fontPackEntries(const Config &cfg, std::string &err);
// 可变字体(fvar)轴/实例报告; 非可变字体给 VARIABLE\t0
std::string fontVariations(const Config &cfg, std::string &err);
// --varied <字符>: 在配置的轴/实例下报告点数与包围盒(验证插值生效)
std::string variedInfo(const Config &cfg, const std::string &ch, std::string &err);
// OTF(CFF) 文字 -> 函数(多字符, 用 hmtx 推进宽度)
std::string cffTextFunctions(const Config &cfg, const std::string &text, std::string &err);
// --kern <两个字>: GPOS kern 字距值(字体单位)
std::string kernInfo(const Config &cfg, const std::string &pair, std::string &err);

// 猜测一行输入适合哪种模式 (交互模式用)
std::string guessMode(const std::string &input);

} // namespace em

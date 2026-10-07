// EasyMath - 命令行解析
#pragma once

#include "config.hpp"

#include <string>
#include <vector>

namespace em {

// kVersion 见 config.hpp
extern const char *kProgramName;

struct CliResult {
    bool ok = true;
    int exitCode = 0;
    std::string error;
    std::vector<std::string> conflicts;

    bool wantHelp = false;
    bool wantVersion = false;
    bool wantDumpConfig = false;
    bool wantPrintConfig = false;
    bool wantInteractive = false;
    bool wantEngineInfo = false;
    bool wantFontList = false;
    bool wantFontEntries = false;
    bool wantFontVariations = false;
    bool wantVaried = false;
    bool wantKern = false;
    bool wantCffOutline = false;
    bool wantCffText = false;
    std::string cffChar;
    bool wantCffInfo = false;
    std::string cffPath;
    std::string kernPair;
    std::string variedChar;   // 机器可读清单(带 RAWHEX), 给安卓/测试用
    bool wantEngineScript = false;

    std::string mode;               // lagrange|solve|eval|line  (空: 交互)
    std::string input;              // 模式输入
    bool inputMissing = false;      // 给出了模式但没有输入
    std::string outBaseName;        // --out
    bool saveFlag = false;          // 是否显式给了 --save/--no-save
    bool askSaveFlag = false;
    bool noConfig = false;
    Config cfg;
};

CliResult parseArgs(int argc, char **argv);
std::string helpText(const Config &cfg);
std::string versionText();

} // namespace em

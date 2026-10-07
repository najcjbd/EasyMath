// EasyMath - 外部符号引擎 (SymPy) 桥
// 通过子进程调用 python3 + sympy 的 API, 把解析结果映射回内部结构。
#pragma once

#include "solve.hpp"

#include <string>
#include <vector>

namespace em {

struct EngineInfo {
    bool probed = false;
    bool available = false;
    std::string python;
    std::string version;
    std::string error;
};

// 宿主可注入的 Python 执行器(Android 上没有 python3 可执行文件,
// 由 Chaquopy 在进程内执行; 桌面端默认使用 fork/exec)。
// 文本里是否含关系运算符(不等式 / 非零): 这类条目在解方程模式下作为约束交给 SymPy
bool textHasRelation(const std::string &text);

// 逗号/分号分隔的列表 -> 去空项(供 constants / derive 使用)
std::vector<std::string> splitCommaList(const std::string &s);

using PythonRunner = bool (*)(const std::string &script, const std::vector<std::string> &args,
                              std::string &out, std::string &err);
void setPythonRunner(PythonRunner runner);
PythonRunner currentPythonRunner();

// 探测 python3 + sympy 是否可用(带缓存)
EngineInfo detectSympyEngine(const std::string &python);
bool sympyUsable(const std::string &python);

// 内嵌的 Python 桥脚本 (可用 --dump-engine-script 导出)
const char *sympyEngineScript();

// 用 SymPy 求解; 成功返回 true 并填充 out
bool solveWithSympy(const std::vector<std::string> &eqs, const SolveOptions &opt,
                    const std::string &python, int timeoutMs, SolveResult &out, std::string &err);

// 用 SymPy/mpmath 做任意精度数值求值(表达式须为 SymPy 语法)
bool evalWithSympy(const std::vector<std::string> &exprSyms, int digits, const std::string &python,
                   int timeoutMs, std::vector<std::string> &vals, std::string &err,
                   SciMode sci = SciMode::Never, int sciThreshold = 12);

// 用 SymPy 因式分解多项式
// 求值(复数/常数): 同时拿精确形式 + 数值
bool evalExactAndNumericWithSympy(const std::vector<std::string> &exprSyms, int digits,
                                  const std::string &python, int timeoutMs,
                                  std::vector<std::string> &exactPlain,
                                  std::vector<std::string> &exactLatex,
                                  std::vector<std::string> &vals, std::string &err, SciMode sci,
                                  int sciThreshold = 12);

bool factorWithSympy(const std::string &polySympy, const std::string &var, const std::string &python,
                     int timeoutMs, std::string &plain, std::string &latex, std::string &err);

} // namespace em

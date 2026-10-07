// EasyMath - 文件命名与保存
#pragma once

#include "config.hpp"

#include <string>

namespace em {

struct SaveOutcome {
    bool ok = false;
    bool overwrote = false;
    bool renamed = false;
    std::string htmlPath;
    std::string mdPath;
    std::string svgPath;
    std::string message;
};

std::string todayDateString(const std::string &sep = "-"); // "2026-9-18"
std::string tomorrowDateString(const std::string &sep = "-");
std::string nowTimeString(); // "HH:MM:SS"

bool fileExists(const std::string &path);
std::string joinPath(const std::string &dir, const std::string &name);
std::string ensureDir(const std::string &dir);

// 默认命名: 模板 -> 基名 (不含扩展名)
std::string buildDefaultBaseName(const Config &cfg, const std::string &dateStr, int index,
                                 const std::string &modeName);
// 扫描目录, 找出 "基名 日期 n.html" 中最大的 n (无则 0)
int maxExistingIndex(const std::string &dir, const std::string &baseName, const std::string &dateStr,
                     const std::string &ext);

// 处理重名: 返回最终可用的基名(不含扩展名)
std::string uniqueBaseName(const std::string &dir, const std::string &baseName, const std::string &ext,
                           bool overwrite, bool &overwrote, bool &renamed);

// 保存 (若 baseName 为空则用默认命名)
SaveOutcome saveFunction(const Config &cfg, const std::string &baseName, const std::string &markdown,
                         const std::string &title, const std::string &modeName, bool writeMd,
                         const std::string &extraHtml = "", const std::string &svg = "");

} // namespace em

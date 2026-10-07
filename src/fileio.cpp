// EasyMath - 文件命名与保存实现
#include "fileio.hpp"

#include "i18n.hpp"
#include "output.hpp"
#include "unicode.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace em {

// 可移植的本地时间: Windows(MSVC/MinGW) 用 localtime_s, 其它用 localtime_r
static void localTime(std::time_t t, std::tm &out) {
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
}

// 可移植的 UTF-8 路径(Windows 上按 UTF-8 解析, 避免中文文件名乱码)
static fs::path u8pathOf(const std::string &p) {
#if defined(__cpp_lib_char8_t)
    return fs::path(reinterpret_cast<const char8_t *>(p.c_str()));
#else
    return fs::u8path(p);
#endif
}

// 列出目录中的文件名(UTF-8)
static std::vector<std::string> listDir(const std::string &dir) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::directory_iterator it(u8pathOf(dir.empty() ? "." : dir), ec);
    if (ec) return out;
    fs::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::path &p = it->path();
#if defined(__cpp_lib_char8_t)
        std::u8string u8 = p.filename().u8string();
        out.emplace_back(reinterpret_cast<const char *>(u8.c_str()), u8.size());
#else
        out.push_back(p.filename().u8string());
#endif
    }
    return out;
}

std::string joinPath(const std::string &dir, const std::string &name) {
    if (dir.empty() || dir == ".") return name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

static std::string formatDate(const std::string &sep, int dayOffset) {
    std::time_t t = std::time(nullptr);
    t += static_cast<std::time_t>(dayOffset) * 86400;
    std::tm tmv{};
    localTime(t, tmv);
    return std::to_string(tmv.tm_year + 1900) + sep + std::to_string(tmv.tm_mon + 1) + sep +
           std::to_string(tmv.tm_mday);
}

// 只替换非法字符, 不裁剪空白 (供模板前后缀使用)
static std::string sanitizeChars(const std::string &t) {
    std::string cleaned;
    for (char c : t) {
        if (c == '/' || c == '\\') cleaned += '-';
        else cleaned += c;
    }
    return cleaned;
}

// 文件名整体安全化
static std::string sanitizeName(const std::string &t) {
    std::string cleaned = sanitizeChars(t);
    while (!cleaned.empty() && (cleaned.back() == ' ' || cleaned.back() == '.')) cleaned.pop_back();
    if (cleaned.empty()) cleaned = "EasyMath";
    return cleaned;
}

// 扫描 pre + 数字 + post + ext 的最大数字
static int maxIndexPrefixed(const std::string &dir, const std::string &pre, const std::string &post,
                            const std::string &ext) {
    int best = 0;
    for (const std::string &name : listDir(dir)) {
        if (name.size() <= pre.size() + post.size() + ext.size()) continue;
        if (name.compare(0, pre.size(), pre) != 0) continue;
        if (name.compare(name.size() - (post.size() + ext.size()), post.size() + ext.size(),
                         post + ext) != 0)
            continue;
        std::string mid = name.substr(pre.size(), name.size() - pre.size() - post.size() - ext.size());
        if (mid.empty()) continue;
        bool digits = true;
        for (char c : mid)
            if (!std::isdigit(static_cast<unsigned char>(c))) digits = false;
        if (!digits) continue;
        try {
            int n = std::stoi(mid);
            if (n > best) best = n;
        } catch (...) {
        }
    }
    return best;
}

std::string todayDateString(const std::string &sep) { return formatDate(sep, 0); }
std::string tomorrowDateString(const std::string &sep) { return formatDate(sep, 1); }

std::string nowTimeString() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localTime(t, tmv);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

bool fileExists(const std::string &path) {
    std::error_code ec;
    return fs::exists(u8pathOf(path), ec);
}

std::string ensureDir(const std::string &dir) {
    if (dir.empty() || dir == ".") return "";
    std::error_code ec;
    fs::create_directories(u8pathOf(dir), ec);
    std::error_code ec2;
    if (!fs::exists(u8pathOf(dir), ec2))
        return L("无法创建目录: ", "cannot create dir: ") + dir;
    return "";
}

static std::string normalizeDate(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '-' || c == '.' || c == '_' || c == '/') o += '/';
        else o += c;
    }
    return o;
}

std::string buildDefaultBaseName(const Config &cfg, const std::string &dateStr, int index,
                                 const std::string &modeName) {
    std::string t = cfg.nameTemplate.empty() ? std::string("函数 {date} {n}") : cfg.nameTemplate;
    auto repl = [&](const std::string &from, const std::string &to) {
        std::size_t p = 0;
        while ((p = t.find(from, p)) != std::string::npos) {
            t.replace(p, from.size(), to);
            p += to.size();
        }
    };
    repl("{date}", dateStr);
    repl("{n}", std::to_string(index));
    repl("{mode}", modeName);
    repl("{time}", nowTimeString());
    // 文件名中不允许出现 '/' 和 '\\'
    std::string cleaned;
    for (char c : t) {
        if (c == '/' || c == '\\') cleaned += '-';
        else cleaned += c;
    }
    while (!cleaned.empty() && (cleaned.back() == ' ' || cleaned.back() == '.')) cleaned.pop_back();
    if (cleaned.empty()) cleaned = "EasyMath";
    return cleaned;
}

int maxExistingIndex(const std::string &dir, const std::string &baseName, const std::string &dateStr,
                     const std::string &ext) {
    int best = 0;
    std::string want = normalizeDate(dateStr);
    std::string prefix = baseName + " ";
    for (const std::string &name : listDir(dir)) {
        if (name.size() <= prefix.size() + ext.size()) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.compare(name.size() - ext.size(), ext.size(), ext) != 0) continue;
        std::string middle = name.substr(prefix.size(), name.size() - prefix.size() - ext.size());
        std::size_t sp = middle.find_last_of(' ');
        if (sp == std::string::npos) continue;
        std::string datePart = middle.substr(0, sp);
        std::string numPart = middle.substr(sp + 1);
        if (numPart.empty()) continue;
        bool allDigits = true;
        for (char c : numPart)
            if (!std::isdigit(static_cast<unsigned char>(c))) allDigits = false;
        if (!allDigits) continue;
        if (normalizeDate(datePart) != want) continue;
        try {
            int n = std::stoi(numPart);
            if (n > best) best = n;
        } catch (...) {
        }
    }
    return best;
}

std::string uniqueBaseName(const std::string &dir, const std::string &baseName, const std::string &ext,
                           bool overwrite, bool &overwrote, bool &renamed) {
    overwrote = false;
    renamed = false;
    if (!fileExists(joinPath(dir, baseName + ext))) return baseName;
    if (overwrite) {
        overwrote = true;
        return baseName;
    }
    for (int i = 1; i < 100000; ++i) {
        std::string cand = baseName + "(" + std::to_string(i) + ")";
        if (!fileExists(joinPath(dir, cand + ext))) {
            renamed = true;
            return cand;
        }
    }
    renamed = true;
    return baseName + "(" + std::to_string(static_cast<long long>(std::time(nullptr))) + ")";
}

SaveOutcome saveFunction(const Config &cfg, const std::string &baseName, const std::string &markdown,
                         const std::string &title, const std::string &modeName, bool writeMd,
                         const std::string &extraHtml, const std::string &svg) {
    SaveOutcome res;
    std::string dir = cfg.saveDir.empty() ? "." : cfg.saveDir;
    std::string derr = ensureDir(dir);
    if (!derr.empty()) {
        res.message = derr;
        return res;
    }
    std::string base = baseName.empty() ? baseName : sanitizeName(baseName);
    if (base.empty()) {
        std::string dateStr = todayDateString("-");
        std::string tmpl = cfg.nameTemplate.empty() ? std::string("函数 {date} {n}") : cfg.nameTemplate;
        auto repl = [&](const std::string &from, const std::string &to) {
            std::size_t p2 = 0;
            while ((p2 = tmpl.find(from, p2)) != std::string::npos) {
                tmpl.replace(p2, from.size(), to);
                p2 += to.size();
            }
        };
        repl("{date}", dateStr);
        repl("{mode}", modeName);
        repl("{time}", nowTimeString());
        std::size_t np = tmpl.find("{n}");
        std::string pre, post;
        if (np == std::string::npos) {
            pre = tmpl;
            post.clear();
        } else {
            pre = tmpl.substr(0, np);
            post = tmpl.substr(np + 3);
        }
        pre = sanitizeChars(pre);
        post = sanitizeChars(post);
        int idx = maxIndexPrefixed(dir, pre, post, ".html");
        if (np == std::string::npos) base = sanitizeName(pre);
        else base = sanitizeName(pre + std::to_string(idx + 1) + post);
    }
    bool ow = false, rn = false;
    std::string unique = uniqueBaseName(dir, base, ".html", cfg.overwrite, ow, rn);
    res.overwrote = ow;
    res.renamed = rn;
    res.htmlPath = joinPath(dir, unique + ".html");
    std::string html = renderHtml(markdown, cfg, title,
                                  L("由 EasyMath 生成 · ", "Generated by EasyMath · ") + nowTimeString(),
                                  extraHtml);
    std::ofstream out(u8pathOf(res.htmlPath), std::ios::binary);
    if (!out) {
        res.message = L("无法写入文件: ", "cannot write file: ") + res.htmlPath;
        return res;
    }
    out << html;
    out.close();
    if (writeMd) {
        res.mdPath = joinPath(dir, unique + ".md");
        std::ofstream mout(u8pathOf(res.mdPath), std::ios::binary);
        if (mout) {
            mout << markdown;
            mout.close();
        } else {
            res.mdPath.clear();
        }
    }
    if (!svg.empty()) {
        res.svgPath = joinPath(dir, unique + ".svg");
        std::ofstream sout(u8pathOf(res.svgPath), std::ios::binary);
        if (sout) {
            sout << svg;
            sout.close();
        } else {
            res.svgPath.clear();
        }
    }
    res.ok = true;
    return res;
}

} // namespace em

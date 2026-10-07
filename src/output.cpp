// EasyMath - 输出渲染实现
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#include "output.hpp"

#include "i18n.hpp"

#include <algorithm>
#include <string>
#include <cctype>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <fstream>

namespace em {

std::string Section::plainText() const {
    std::string s;
    for (const auto &l : plainLines) {
        s += l;
        s += "\n";
    }
    return s;
}

std::string Section::latexText() const {
    std::string s;
    for (const auto &l : latexLines) {
        s += l;
        s += "\n";
    }
    return s;
}

Section &Report::section(Chan c, const std::string &title) {
    for (auto &s : sections) {
        if (s.chan == c) {
            if (!title.empty()) s.title = title;
            return s;
        }
    }
    Section s;
    s.chan = c;
    s.title = title;
    sections.push_back(s);
    return sections.back();
}

void Report::line(Chan c, const std::string &plain, const std::string &latex) {
    Section &s = section(c, "");
    s.plainLines.push_back(plain);
    s.latexLines.push_back(latex.empty() ? plain : latex);
}

void Report::note(const std::string &plain) { line(Chan::Note, plain, plain); }

bool Report::has(Chan c) const {
    for (const auto &s : sections)
        if (s.chan == c) return true;
    return false;
}

std::string renderTerminal(const Report &r, const Config &cfg) {
    std::string out;
    for (const auto &s : r.sections) {
        if (!channelEnabled(cfg, s.chan)) continue;
        if (!s.title.empty()) out += "[" + s.title + "]\n";
        for (const auto &l : s.plainLines) out += l + "\n";
    }
    return out;
}

std::string renderMarkdown(const Report &r, const Config &cfg) {
    std::string md;
    if (!r.title.empty()) md += "# " + r.title + "\n\n";
    for (const auto &s : r.sections) {
        if (!channelEnabled(cfg, s.chan)) continue;
        if (!s.title.empty()) md += "## " + s.title + "\n\n";
        for (std::size_t i = 0; i < s.plainLines.size(); ++i) {
            const std::string &p = s.plainLines[i];
            const std::string &lx = (i < s.latexLines.size()) ? s.latexLines[i] : "";
            if (!lx.empty() && lx != p) {
                md += "$$" + lx + "$$\n\n";
                md += "```\n" + p + "\n```\n\n";
            } else {
                md += p + "\n\n";
            }
        }
    }
    return md;
}

std::string htmlEscape(const std::string &s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

static std::string inlineMd(const std::string &s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i + 1 < s.size() && s[i] == '*' && s[i + 1] == '*') {
            std::size_t e = s.find("**", i + 2);
            if (e != std::string::npos) {
                o += "<strong>" + htmlEscape(s.substr(i + 2, e - i - 2)) + "</strong>";
                i = e + 1;
                continue;
            }
        }
        if (s[i] == '`') {
            std::size_t e = s.find('`', i + 1);
            if (e != std::string::npos) {
                o += "<code>" + htmlEscape(s.substr(i + 1, e - i - 1)) + "</code>";
                i = e;
                continue;
            }
        }
        o += htmlEscape(std::string(1, s[i]));
    }
    return o;
}

std::string markdownToHtml(const std::string &md) {
    std::string html;
    std::istringstream in(md);
    std::string line;
    bool inCode = false;
    bool inList = false;
    bool inMath = false;
    auto closeList = [&]() {
        if (inList) {
            html += "</ul>\n";
            inList = false;
        }
    };
    while (std::getline(in, line)) {
        if (inMath) {
            html += htmlEscape(line) + "\n";
            if (line.rfind("$$", 0) == 0 || line.find("$$") != std::string::npos) {
                html += "</div>\n";
                inMath = false;
            }
            continue;
        }
        if (line.rfind("$$", 0) == 0) {
            closeList();
            html += "<div class=\"math\">" + htmlEscape(line) + "\n";
            if (line.size() > 3 && line.find("$$", 2) != std::string::npos) {
                html += "</div>\n";
            } else {
                inMath = true;
            }
            continue;
        }
        if (line.rfind("```", 0) == 0) {
            closeList();
            if (!inCode) {
                html += "<pre class=\"plain\">";
                inCode = true;
            } else {
                html += "</pre>\n";
                inCode = false;
            }
            continue;
        }
        if (inCode) {
            html += htmlEscape(line) + "\n";
            continue;
        }
        if (line.rfind("### ", 0) == 0) {
            closeList();
            html += "<h3>" + inlineMd(line.substr(4)) + "</h3>\n";
            continue;
        }
        if (line.rfind("## ", 0) == 0) {
            closeList();
            html += "<h2>" + inlineMd(line.substr(3)) + "</h2>\n";
            continue;
        }
        if (line.rfind("# ", 0) == 0) {
            closeList();
            html += "<h1>" + inlineMd(line.substr(2)) + "</h1>\n";
            continue;
        }
        if (line.rfind("- ", 0) == 0) {
            if (!inList) {
                html += "<ul>\n";
                inList = true;
            }
            html += "<li>" + inlineMd(line.substr(2)) + "</li>\n";
            continue;
        }
        if (line.empty()) {
            closeList();
            continue;
        }
        closeList();
        html += "<p>" + inlineMd(line) + "</p>\n";
    }
    closeList();
    if (inCode) html += "</pre>\n";
    if (inMath) html += "</div>\n";
    return html;
}

// ============ 自包含 HTML: 内联 KaTeX(含字体) ============
// 目的: 保存的报告发给别人/离线打开时公式仍然正常。
// 资源来自安装目录 share/EasyMath/katex(与 Android 端同一份), 找不到就退回 CDN 版本。

namespace {

std::string exeDir() {
#if defined(_WIN32)
    char buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    std::string p(buf, n);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t sz = sizeof(buf);
    std::string p;
    if (_NSGetExecutablePath(buf, &sz) == 0) p = buf;
#else
    char buf[4096];
    ssize_t rn = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::string p = (rn > 0) ? std::string(buf, static_cast<std::size_t>(rn)) : std::string();
#endif
    std::size_t slash = p.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string(".") : p.substr(0, slash);
}

bool readBinary(const std::string &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return !out.empty();
}

std::string b64(const std::string &bytes) {
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < bytes.size()) {
        unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) |
                     (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                     static_cast<unsigned char>(bytes[i + 2]);
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63];
        i += 3;
    }
    if (i + 1 == bytes.size()) {
        unsigned v = static_cast<unsigned char>(bytes[i]) << 16;
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == bytes.size()) {
        unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) |
                     (static_cast<unsigned char>(bytes[i + 1]) << 8);
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '=';
    }
    return o;
}

// 找 katex 资源目录(安装目录 / 可执行文件旁边 / 环境变量)
bool katexDir(std::string &dir) {
    const char *env = std::getenv("EASYMATH_DATA_DIR");
    std::vector<std::string> cands;
    if (env && *env) {
        cands.push_back(std::string(env) + "/katex");
        cands.push_back(std::string(env));
    }
    std::string e = exeDir();
    cands.push_back(e + "/katex");
    cands.push_back(e + "/../share/EasyMath/katex");
    cands.push_back(e + "/../share/easymath/katex");
    for (const auto &c : cands) {
        std::string probe = c + "/katex.min.css";
        std::ifstream f(probe, std::ios::binary);
        if (f.good()) { dir = c; return true; }
    }
    return false;
}

// 把 css 里 url(fonts/x.woff2) 换成 data URI(只保留 woff2, 现代浏览器都支持)
std::string inlineFonts(const std::string &css, const std::string &dir) {
    std::string out;
    std::size_t i = 0;
    const std::string key = "url(fonts/";
    while (true) {
        std::size_t p = css.find(key, i);
        if (p == std::string::npos) { out += css.substr(i); break; }
        out += css.substr(i, p - i);
        std::size_t q = css.find(')', p);
        if (q == std::string::npos) { out += css.substr(p); break; }
        std::string name = css.substr(p + key.size(), q - p - key.size());
        std::string data;
        if (name.size() > 6 && name.compare(name.size() - 6, 6, ".woff2") == 0 &&
            readBinary(dir + "/fonts/" + name, data)) {
            out += "url(data:font/woff2;base64," + b64(data) + ")";
            // 跳过后面同一 @font-face 里的 woff/ttf 备用项
            std::size_t r = q + 1;
            while (true) {
                std::size_t np = css.find("url(fonts/", r);
                if (np == std::string::npos) break;
                std::size_t comma = css.find_last_of(",", np);
                std::size_t close = css.find(')', np);
                if (comma == std::string::npos || close == std::string::npos) break;
                bool onlySpaces = true;
                for (std::size_t k = comma + 1; k < np; ++k)
                    if (!std::isspace(static_cast<unsigned char>(css[k]))) { onlySpaces = false; break; }
                if (!onlySpaces) break;
                r = close + 1;
            }
            i = r;
        } else {
            out += css.substr(p, q - p + 1);
            i = q + 1;
        }
    }
    return out;
}

// 生成自包含的 <style>/<script> 片段; 资源缺失时返回空串(调用方退回 CDN)
std::string katexAssetsInline() {
    std::string dir;
    if (!katexDir(dir)) return std::string();
    std::string css, kjs, ajs;
    if (!readBinary(dir + "/katex.min.css", css)) return std::string();
    if (!readBinary(dir + "/katex.min.js", kjs)) return std::string();
    readBinary(dir + "/auto-render.min.js", ajs);
    std::string o;
    o += "<style>\n" + inlineFonts(css, dir) + "\n</style>\n";
    o += "<script>\n" + kjs + "\n</script>\n";
    if (!ajs.empty()) o += "<script>\n" + ajs + "\n</script>\n";
    // 必须等 DOM 就绪: 这段脚本在 <head> 里, 此时 document.body 还不存在
    o += "<script>\ndocument.addEventListener('DOMContentLoaded',function(){try{"
         "renderMathInElement(document.body,{delimiters:["
         "{left:'$$',right:'$$',display:true},{left:'\\[',right:'\\]',display:true},"
         "{left:'$',right:'$',display:false}],throwOnError:false});}catch(e){}});\n</script>\n";
    return o;
}

} // namespace

std::string renderHtml(const std::string &markdown, const Config &cfg, const std::string &title,
                       const std::string &headingPath,
                       const std::string &extraHtml) {
    std::string body = markdownToHtml(markdown);
    std::ostringstream o;
    o << "<!DOCTYPE html>\n";
    o << "<html lang=\"" << currentLangCode() << "\">\n<head>\n";
    o << "<meta charset=\"utf-8\">\n";
    o << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    o << "<title>" << htmlEscape(title) << "</title>\n";
    if (cfg.mathjax) {
        // 优先内联 KaTeX(自包含, 离线可用); 找不到资源再退回 MathJax CDN
        std::string inlined = katexAssetsInline();
        if (!inlined.empty()) {
            o << inlined;
        } else {
            o << "<script>\nwindow.MathJax = {tex: {inlineMath: [['$','$']], displayMath: [['$$','$$']]}};\n"
                 "</script>\n";
            o << "<script async src=\"https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js\"></script>\n";
        }
    }
    o << "<style>\n"
         "body{font-family:-apple-system,'Segoe UI','Noto Sans CJK SC',sans-serif;max-width:52em;"
         "margin:2em auto;padding:0 1em;line-height:1.6;color:#222}\n"
         "h1{border-bottom:2px solid #ddd;padding-bottom:.3em}\n"
         ".math{overflow-x:auto;margin:1em 0}\n"
         "pre.plain{background:#f6f8fa;padding:.6em .8em;border-radius:6px;font-size:.95em}\n"
         "code{background:#f0f0f0;padding:0 .2em;border-radius:3px}\n"
         ".meta{color:#666;font-size:.9em}\n"
         "</style>\n</head>\n<body>\n";
    if (!headingPath.empty()) o << "<p class=\"meta\">" << htmlEscape(headingPath) << "</p>\n";
    o << body;
    o << "\n<script type=\"text/markdown\" id=\"markdown-source\">\n";
    o << markdown;
    if (!extraHtml.empty()) o << "\n" << extraHtml << "\n";
    o << "\n</script>\n</body>\n</html>\n";
    return o.str();
}

} // namespace em

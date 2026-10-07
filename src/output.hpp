// EasyMath - 输出模型与渲染 (纯文本 / LaTeX / Markdown / HTML)
#pragma once

#include "config.hpp"

#include <string>
#include <vector>

namespace em {

struct Section {
    Chan chan;
    std::string title;
    std::vector<std::string> plainLines;
    std::vector<std::string> latexLines; // 与 plainLines 对应, 可为空串

    std::string plainText() const;
    std::string latexText() const;
};

struct Report {
    std::string title;
    std::vector<Section> sections;
    // 原样插入 HTML 的片段(如字形模式的 <svg> 预览); 终端/Markdown 不显示
    std::string extraHtml;

    Section &section(Chan c, const std::string &title = "");
    void line(Chan c, const std::string &plain, const std::string &latex = "");
    void note(const std::string &plain);
    bool has(Chan c) const;
};

std::string renderTerminal(const Report &r, const Config &cfg);
std::string renderMarkdown(const Report &r, const Config &cfg);
std::string renderHtml(const std::string &markdown, const Config &cfg, const std::string &title,
                       const std::string &headingPath = "", const std::string &extraHtml = "");
std::string markdownToHtml(const std::string &md);

} // namespace em

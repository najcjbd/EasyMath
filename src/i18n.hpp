// EasyMath - 双语 (中文 / English) 支持
#pragma once

#include <string>

namespace em {

enum class Lang { ZH, EN };

void setLang(const std::string &code); // "zh" / "en"
Lang currentLang();
const char *currentLangCode();

// L(中文, English): 按当前语言返回
std::string L(const std::string &zh, const std::string &en);

} // namespace em

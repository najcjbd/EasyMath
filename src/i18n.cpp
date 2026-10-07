#include "i18n.hpp"

#include <algorithm>
#include <cctype>

namespace em {

static Lang g_lang = Lang::ZH;

void setLang(const std::string &code) {
    std::string c = code;
    std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (c.rfind("en", 0) == 0) g_lang = Lang::EN;
    else g_lang = Lang::ZH;
}

Lang currentLang() { return g_lang; }

const char *currentLangCode() { return g_lang == Lang::EN ? "en" : "zh"; }

std::string L(const std::string &zh, const std::string &en) { return g_lang == Lang::EN ? en : zh; }

} // namespace em

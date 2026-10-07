// EasyMath - Unicode / 数学输入归一化层
// 负责: UTF-8 编解码、上下标、全角字符、Unicode 数学符号、中文数学词、数学字母变体
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace em {

using cp_t = char32_t;

// ---------- UTF-8 ----------
std::vector<cp_t> utf8_decode(const std::string &s);
std::string utf8_encode(cp_t c);
std::string utf8_encode(const std::vector<cp_t> &v);
std::size_t utf8_length(const std::string &s); // 码点个数, 非法字节计 1
std::string utf8_substr_cp(const std::string &s, std::size_t from, std::size_t count);

// ---------- 字符分类 ----------
bool is_ascii_digit(cp_t c);
bool is_ascii_alpha(cp_t c);
bool is_ascii_alnum(cp_t c);
bool is_space_cp(cp_t c);
bool is_ident_start(cp_t c); // 可作为标识符起始 (ASCII 字母 / 希腊字母)
bool is_ident_cont(cp_t c);

// 默认分隔符判定（与数学无关的标点, 例如 , . @ # $ ; : 、 ， 等）
// '.' 夹在两个数字之间时视为小数点, 不作为分隔符
bool is_default_separator(cp_t c, cp_t prev, cp_t next);
bool is_separator_string(const std::string &s, char32_t extra = 0);

// ---------- 上下标 ----------
bool map_superscript(cp_t c, std::string &out); // 上标字符 -> ASCII 片段
bool map_subscript(cp_t c, std::string &out);   // 下标字符 -> ASCII 片段

// 数学字母变体 (U+1D400..U+1D7FF 粗体/斜体/花体/双线体/无衬线/等宽, 及希腊变体)
bool math_alphanumeric_to_ascii(cp_t c, std::string &out);
// 希腊字母 -> ASCII 名称 (alpha, beta, pi, theta ...)
bool greek_to_name(cp_t c, std::string &out);

// ---------- 归一化 ----------
// 把任意数学输入转成规范 ASCII 形式:
//   x⁴        -> x^(4)
//   x₁        -> x_1
//   √6        -> sqrt(6)      (由 sqrt + 隐式调用处理)
//   2×3       -> 2*3
//   x + alpha -> x + alpha
//   根号6      -> sqrt(6)
//   45度       -> 45°
std::string normalize_math(const std::string &in);

// 把整串中的中文数学词替换成 ASCII 记号
std::string replace_math_words(const std::string &in);

// 角度单位后缀判定
bool has_degree_suffix(const std::string &normalized);

} // namespace em

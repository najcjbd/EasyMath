// EasyMath - 极简 ZIP 读取器(只读, stored/deflate, 带 CRC 校验)
// 用途: 读用户导入的字体包(.zip)。刻意不带目录遍历/写功能, 避免不必要的攻击面。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace em {

uint32_t crc32Of(const uint8_t *p, std::size_t n, uint32_t seed = 0);

// ZIP 名字解码: 有 UTF-8 标志位或字节本身是合法 UTF-8 就直接用, 否则按 GBK 转(中文包常见)
std::string decodeZipName(const std::string &raw, bool utf8Flag);
// 字节串 -> 小写 hex(安卓侧拿它按 GBK 解码名字)
std::string hexOf(const std::string &s);

struct ZipEntry {
    std::string name;      // 原始名字字节(UTF-8 或本地编码)
    std::string nameAscii; // 只留 ASCII 可打印字符, 便于显示与匹配
    std::string nameRawHex; // 原始名字字节(hex): 给安卓端用平台字符集(GBK)自行解码
    std::size_t size = 0;      // 解压后字节数
    std::size_t compSize = 0;  // 压缩后字节数
    std::size_t localOffset = 0;
    uint16_t method = 0;  // 0=stored, 8=deflate
    uint32_t crc = 0;
    bool utf8 = false;
};

class ZipArchive {
public:
    bool openPath(const std::string &path, std::string &err);
    bool openMemory(const std::vector<uint8_t> &data, std::string &err);

    const std::vector<ZipEntry> &entries() const { return entries_; }
    const uint8_t *raw() const { return data_.data(); }
    std::size_t rawSize() const { return data_.size(); }

    bool extract(std::size_t index, std::vector<uint8_t> &out, std::string &err) const;
    // 找后缀(如 ".ttf")与找子串(如 "regular"), 都按小写比较 nameAscii
    std::vector<std::size_t> findSuffix(const std::string &suffix) const;
    std::vector<std::size_t> findSubstr(const std::string &sub) const;

private:
    std::vector<uint8_t> data_;
    std::vector<ZipEntry> entries_;
};

bool readWholeFile(const std::string &path, std::vector<uint8_t> &out, std::string &err);

} // namespace em

// EasyMath - ZIP 读取器实现
#include "zipfile.hpp"

#include "inflate.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace em {
namespace {

uint32_t rd32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t rd16(const uint8_t *p) { return uint16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8)); }

std::string asciiOnly(const std::string &s) {
    std::string o;
    for (unsigned char c : s) o += (c >= 0x20 && c < 0x7F) ? char(c) : ' ';
    // 压掉连续空格
    std::string r;
    bool sp = false;
    for (char c : o) {
        if (c == ' ') {
            if (!sp) r += c;
            sp = true;
        } else {
            r += c;
            sp = false;
        }
    }
    return r;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

// ---------- ZIP 文件名解码 ----------
// ZIP 规范: 通用标志位 bit 11 表示名字是 UTF-8。但很多中文工具**不设这个位**却写的是
// UTF-8 字节(本项目的字体包就是这样), 所以规则是: 有标志位直接用; 没标志位先按 UTF-8 校验,
// 合法就用, 不合法再按 GBK 转; 都不行就保留原样(纯 ASCII 显示)。
static bool isValidUtf8(const std::string &s) {
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t n = 0;
        if (c < 0x80) n = 1;
        else if ((c & 0xE0) == 0xC0) n = 2;
        else if ((c & 0xF0) == 0xE0) n = 3;
        else if ((c & 0xF8) == 0xF0) n = 4;
        else return false;
        if (i + n > s.size()) return false;
        for (std::size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        if (n == 2 && c < 0xC2) return false;   // 过长编码
        i += n;
    }
    return true;
}

// 注意: Android(Bionic) 没有 iconv, 必须排除, 否则安卓端编译不过(踩过一次)
#if (defined(__linux__) || defined(__GLIBC__) || defined(__APPLE__)) && !defined(__ANDROID__)
#include <iconv.h>
static std::string gbkToUtf8(const std::string &in) {
    iconv_t cd = iconv_open("UTF-8", "GBK");
    if (cd == reinterpret_cast<iconv_t>(-1)) return in;
    std::string out(in.size() * 4 + 8, '\0');
    char *ip = const_cast<char *>(in.data());
    std::size_t il = in.size();
    char *op = &out[0];
    std::size_t ol = out.size();
    std::size_t r = iconv(cd, &ip, &il, &op, &ol);
    iconv_close(cd);
    if (r == static_cast<std::size_t>(-1)) return in;
    out.resize(out.size() - ol);
    return out;
}
#else
static std::string gbkToUtf8(const std::string &in) { return in; }
#endif

std::string hexOf(const std::string &s) {
    static const char *kHex = "0123456789abcdef";
    std::string o;
    o.reserve(s.size() * 2);
    for (unsigned char c : s) {
        o += kHex[c >> 4];
        o += kHex[c & 0xF];
    }
    return o;
}

std::string decodeZipName(const std::string &raw, bool utf8Flag) {
    if (utf8Flag && isValidUtf8(raw)) return raw;
    bool ascii = true;
    for (unsigned char c : raw)
        if (c >= 0x80) { ascii = false; break; }
    if (ascii) return raw;
    if (isValidUtf8(raw)) return raw;      // 没标志位但其实是 UTF-8(常见)
    std::string g = gbkToUtf8(raw);
    if (g != raw && isValidUtf8(g)) return g;
    return raw;                            // 兜底
}


uint32_t crc32Of(const uint8_t *p, std::size_t n, uint32_t seed) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = seed ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool readWholeFile(const std::string &path, std::vector<uint8_t> &out, std::string &err) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "打不开文件: " + path;
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        std::fclose(f);
        err = "读文件长度失败: " + path;
        return false;
    }
    out.resize(static_cast<std::size_t>(sz));
    std::size_t got = sz ? std::fread(out.data(), 1, static_cast<std::size_t>(sz), f) : 0;
    std::fclose(f);
    if (got != static_cast<std::size_t>(sz)) {
        err = "读文件不完整: " + path;
        return false;
    }
    return true;
}

bool ZipArchive::openMemory(const std::vector<uint8_t> &data, std::string &err) {
    data_ = data;
    entries_.clear();
    if (data_.size() < 22) {
        err = "不是有效的 ZIP(太短)";
        return false;
    }
    // 从尾部往前找 EOCD(0x06054b50), 注释最长 65535
    std::size_t limit = data_.size() > 65557 ? data_.size() - 65557 : 0;
    std::size_t eocd = std::string::npos;
    for (std::size_t i = data_.size() - 22 + 1; i-- > limit;) {
        if (rd32(&data_[i]) == 0x06054b50u) {
            eocd = i;
            break;
        }
        if (i == 0) break;
    }
    if (eocd == std::string::npos) {
        err = "不是有效的 ZIP(找不到中央目录)";
        return false;
    }
    uint16_t total = rd16(&data_[eocd + 10]);
    uint32_t cdSize = rd32(&data_[eocd + 12]);
    uint32_t cdOff = rd32(&data_[eocd + 16]);
    if (total == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) {
        err = "这个字体包是 ZIP64 格式, 暂不支持(请改用普通 zip 或直接指定 .ttf)";
        return false;
    }
    if (std::size_t(cdOff) + cdSize > data_.size()) {
        err = "ZIP 中央目录越界";
        return false;
    }
    std::size_t p = cdOff;
    for (uint16_t i = 0; i < total; ++i) {
        if (p + 46 > data_.size() || rd32(&data_[p]) != 0x02014b50u) {
            err = "ZIP 中央目录项损坏";
            return false;
        }
        ZipEntry e;
        uint16_t flags = rd16(&data_[p + 8]);
        e.method = rd16(&data_[p + 10]);
        e.crc = rd32(&data_[p + 16]);
        e.compSize = rd32(&data_[p + 20]);
        e.size = rd32(&data_[p + 24]);
        uint16_t nameLen = rd16(&data_[p + 28]);
        uint16_t extraLen = rd16(&data_[p + 30]);
        uint16_t commentLen = rd16(&data_[p + 32]);
        e.localOffset = rd32(&data_[p + 42]);
        e.utf8 = (flags & 0x0800) != 0;
        if (p + 46 + nameLen > data_.size()) {
            err = "ZIP 中央目录项名字越界";
            return false;
        }
        std::string rawName(reinterpret_cast<const char *>(&data_[p + 46]), nameLen);
        e.nameRawHex = hexOf(rawName);
        e.name = decodeZipName(rawName, e.utf8);
        e.nameAscii = asciiOnly(e.name);
        if (flags & 0x0001) {
            err = "字体包里有加密条目, 暂不支持";
            return false;
        }
        if (e.compSize == 0xFFFFFFFFu || e.size == 0xFFFFFFFFu || e.localOffset == 0xFFFFFFFFu) {
            err = "这个字体包是 ZIP64 格式, 暂不支持";
            return false;
        }
        entries_.push_back(std::move(e));
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

bool ZipArchive::openPath(const std::string &path, std::string &err) {
    std::vector<uint8_t> data;
    if (!readWholeFile(path, data, err)) return false;
    return openMemory(data, err);
}

bool ZipArchive::extract(std::size_t index, std::vector<uint8_t> &out, std::string &err) const {
    out.clear();
    if (index >= entries_.size()) {
        err = "条目序号越界";
        return false;
    }
    const ZipEntry &e = entries_[index];
    if (e.localOffset + 30 > data_.size() || rd32(&data_[e.localOffset]) != 0x04034b50u) {
        err = "ZIP 局部头损坏: " + e.nameAscii;
        return false;
    }
    uint16_t nameLen = rd16(&data_[e.localOffset + 26]);
    uint16_t extraLen = rd16(&data_[e.localOffset + 28]);
    std::size_t start = e.localOffset + 30 + nameLen + extraLen;
    if (start + e.compSize > data_.size()) {
        err = "ZIP 条目数据越界: " + e.nameAscii;
        return false;
    }
    const uint8_t *src = &data_[start];
    if (e.method == 0) {
        out.assign(src, src + e.compSize);
    } else if (e.method == 8) {
        std::string ierr;
        if (!inflateRaw(src, e.compSize, out, ierr, e.size ? e.size + 64 : 0)) {
            err = "解压失败(" + e.nameAscii + "): " + ierr;
            return false;
        }
    } else {
        err = "不支持的压缩方式(" + std::to_string(e.method) + "): " + e.nameAscii;
        return false;
    }
    if (out.size() != e.size) {
        err = "解压后大小不符(" + e.nameAscii + "): " + std::to_string(out.size()) + " != " +
              std::to_string(e.size);
        return false;
    }
    uint32_t got = crc32Of(out.data(), out.size());
    if (got != e.crc) {
        err = "CRC 校验失败: " + e.nameAscii;
        return false;
    }
    return true;
}

std::vector<std::size_t> ZipArchive::findSuffix(const std::string &suffix) const {
    std::string s = lower(suffix);
    std::vector<std::size_t> v;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        std::string n = lower(entries_[i].nameAscii);
        std::string n2 = lower(entries_[i].name);   // 解码后的真实名字(可能含中文)
        bool hit = n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0;
        if (!hit && n2.size() >= s.size() && n2.compare(n2.size() - s.size(), s.size(), s) == 0) hit = true;
        if (hit) v.push_back(i);
    }
    return v;
}

std::vector<std::size_t> ZipArchive::findSubstr(const std::string &sub) const {
    std::string s = lower(sub);
    std::vector<std::size_t> v;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        // ASCII 匹配用 nameAscii; 中文等非 ASCII 用解码后的 name(lower 对 UTF-8 字节无害)
        if (lower(entries_[i].nameAscii).find(s) != std::string::npos ||
            lower(entries_[i].name).find(s) != std::string::npos)
            v.push_back(i);
    }
    return v;
}

} // namespace em

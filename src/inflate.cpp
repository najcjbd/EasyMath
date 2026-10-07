// EasyMath - 自包含 DEFLATE 解压实现
#include "inflate.hpp"

#include <cstring>

namespace em {
namespace {

constexpr std::size_t kDefaultMaxOut = std::size_t(256) << 20;

// 长度码 257..285 的基准值与附加位
const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19, 23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
// 距离码 0..29 的基准值与附加位
const uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
                                33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
                                1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5,  5,  6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// 位读取: DEFLATE 是 LSB-first
struct Bits {
    const uint8_t *p = nullptr;
    std::size_t n = 0;
    std::size_t pos = 0;
    uint64_t buf = 0;
    int cnt = 0;
    bool bad = false;

    bool fill(int k) {
        while (cnt < k) {
            if (pos >= n) return false;
            buf |= static_cast<uint64_t>(p[pos++]) << cnt;
            cnt += 8;
        }
        return true;
    }
    uint32_t get(int k) {
        if (k == 0) return 0;
        if (!fill(k)) {
            bad = true;
            return 0;
        }
        uint32_t v = static_cast<uint32_t>(buf & ((uint64_t(1) << k) - 1));
        buf >>= k;
        cnt -= k;
        return v;
    }
    void alignByte() {
        int r = cnt & 7;
        buf >>= r;
        cnt -= r;
    }
};

// 规范 Huffman 码表
struct Huff {
    uint16_t count[16] = {0};
    std::vector<uint16_t> sym;

    bool build(const uint8_t *len, int n) {
        for (int i = 0; i < 16; ++i) count[i] = 0;
        for (int i = 0; i < n; ++i) count[len[i]]++;
        if (count[0] == n) { // 全 0: 合法但不可解码
            sym.clear();
            return true;
        }
        int left = 1;
        for (int l = 1; l <= 15; ++l) {
            left <<= 1;
            left -= count[l];
            if (left < 0) return false; // 过完备
        }
        uint16_t off[16] = {0};
        for (int l = 1; l < 15; ++l) off[l + 1] = static_cast<uint16_t>(off[l] + count[l]);
        sym.assign(static_cast<std::size_t>(n), 0);
        for (int i = 0; i < n; ++i)
            if (len[i]) sym[off[len[i]]++] = static_cast<uint16_t>(i);
        return true;
    }

    int decode(Bits &b) const {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l <= 15; ++l) {
            code |= static_cast<int>(b.get(1));
            if (b.bad) return -1;
            int c = count[l];
            if (code - first < c) {
                std::size_t k = static_cast<std::size_t>(index + (code - first));
                if (k >= sym.size()) return -1;
                return sym[k];
            }
            index += c;
            first = (first + c) << 1;
            code <<= 1;
        }
        return -1;
    }
};

void buildFixed(Huff &lit, Huff &dist) {
    uint8_t l[288];
    for (int i = 0; i < 144; ++i) l[i] = 8;
    for (int i = 144; i < 256; ++i) l[i] = 9;
    for (int i = 256; i < 280; ++i) l[i] = 7;
    for (int i = 280; i < 288; ++i) l[i] = 8;
    lit.build(l, 288);
    uint8_t d[30];
    for (int i = 0; i < 30; ++i) d[i] = 5;
    dist.build(d, 30);
}

bool readDynamic(Bits &b, Huff &lit, Huff &dist, std::string &err) {
    static const int order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint32_t hlit = b.get(5) + 257;
    uint32_t hdist = b.get(5) + 1;
    uint32_t hclen = b.get(4) + 4;
    if (b.bad) {
        err = "动态块头被截断";
        return false;
    }
    if (hlit > 286 || hdist > 30) {
        err = "动态块码表长度非法";
        return false;
    }
    uint8_t clLen[19] = {0};
    for (uint32_t i = 0; i < hclen; ++i) clLen[order[i]] = static_cast<uint8_t>(b.get(3));
    if (b.bad) {
        err = "动态块码长表被截断";
        return false;
    }
    Huff cl;
    if (!cl.build(clLen, 19)) {
        err = "码长码表非法";
        return false;
    }
    const int n = static_cast<int>(hlit + hdist);
    std::vector<uint8_t> all(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n;) {
        int s = cl.decode(b);
        if (s < 0) {
            err = "码长表解码失败";
            return false;
        }
        if (s < 16) {
            all[static_cast<std::size_t>(i++)] = static_cast<uint8_t>(s);
            continue;
        }
        uint32_t rep = 0;
        uint8_t val = 0;
        if (s == 16) {
            if (i == 0) {
                err = "码长重复但前面没有可重复的码长";
                return false;
            }
            val = all[static_cast<std::size_t>(i - 1)];
            rep = 3 + b.get(2);
        } else if (s == 17) {
            rep = 3 + b.get(3);
        } else {
            rep = 11 + b.get(7);
        }
        if (b.bad) {
            err = "码长重复段被截断";
            return false;
        }
        if (i + static_cast<int>(rep) > n) {
            err = "码长重复超出范围";
            return false;
        }
        for (uint32_t k = 0; k < rep; ++k) all[static_cast<std::size_t>(i++)] = val;
    }
    if (!lit.build(all.data(), static_cast<int>(hlit))) {
        err = "字面量码表非法";
        return false;
    }
    if (!dist.build(all.data() + hlit, static_cast<int>(hdist))) {
        err = "距离码表非法";
        return false;
    }
    return true;
}

bool decodeBlock(Bits &b, const Huff &lit, const Huff &dist, std::vector<uint8_t> &out,
                 std::size_t maxOut, std::string &err) {
    for (;;) {
        int sym = lit.decode(b);
        if (sym < 0) {
            err = "字面量/长度码非法";
            return false;
        }
        if (sym < 256) {
            if (out.size() >= maxOut) {
                err = "解压结果超过上限";
                return false;
            }
            out.push_back(static_cast<uint8_t>(sym));
            continue;
        }
        if (sym == 256) return true; // 块结束
        int li = sym - 257;
        if (li >= 29) {
            err = "长度码超出范围";
            return false;
        }
        uint32_t len = uint32_t(kLenBase[li]) + b.get(kLenExtra[li]);
        int ds = dist.decode(b);
        if (ds < 0 || ds >= 30) {
            err = "距离码非法";
            return false;
        }
        uint32_t d = uint32_t(kDistBase[ds]) + b.get(kDistExtra[ds]);
        if (b.bad) {
            err = "长度/距离附加位被截断";
            return false;
        }
        if (d > out.size()) {
            err = "距离超出已解压数据";
            return false;
        }
        if (out.size() + len > maxOut) {
            err = "解压结果超过上限";
            return false;
        }
        std::size_t start = out.size() - d;
        for (uint32_t i = 0; i < len; ++i) {
            uint8_t byte = out[start + i];
            out.push_back(byte);
        }
    }
}

} // namespace

bool inflateRaw(const uint8_t *in, std::size_t n, std::vector<uint8_t> &out, std::string &err,
                std::size_t maxOut) {
    out.clear();
    if (!in && n) {
        err = "输入为空";
        return false;
    }
    if (!maxOut) maxOut = kDefaultMaxOut;
    Bits b;
    b.p = in;
    b.n = n;
    bool final = false;
    while (!final) {
        final = b.get(1) != 0;
        uint32_t type = b.get(2);
        if (b.bad) {
            err = "数据在块头处被截断";
            return false;
        }
        if (type == 0) { // 未压缩块
            b.alignByte();
            uint32_t len = b.get(16);
            uint32_t nlen = b.get(16);
            if (b.bad) {
                err = "未压缩块长度被截断";
                return false;
            }
            if ((len ^ 0xFFFFu) != nlen) {
                err = "未压缩块长度校验失败";
                return false;
            }
            if (out.size() + len > maxOut) {
                err = "解压结果超过上限";
                return false;
            }
            for (uint32_t i = 0; i < len; ++i) {
                uint32_t v = b.get(8);
                if (b.bad) {
                    err = "未压缩块数据被截断";
                    return false;
                }
                out.push_back(static_cast<uint8_t>(v));
            }
        } else if (type == 1 || type == 2) {
            Huff lit, dist;
            if (type == 1) {
                buildFixed(lit, dist);
            } else if (!readDynamic(b, lit, dist, err)) {
                return false;
            }
            if (!decodeBlock(b, lit, dist, out, maxOut, err)) return false;
        } else {
            err = "非法的块类型";
            return false;
        }
    }
    return true;
}

bool inflateZlib(const uint8_t *in, std::size_t n, std::vector<uint8_t> &out, std::string &err,
                 std::size_t maxOut) {
    if (n < 6) {
        err = "zlib 数据太短";
        return false;
    }
    uint32_t cmf = in[0], flg = in[1];
    if ((cmf & 0x0F) != 8) {
        err = "zlib 压缩方法不是 deflate";
        return false;
    }
    if (((cmf << 8) | flg) % 31 != 0) {
        err = "zlib 头校验失败";
        return false;
    }
    if (flg & 0x20) {
        err = "zlib 预设字典暂不支持";
        return false;
    }
    if (!inflateRaw(in + 2, n - 6, out, err, maxOut)) return false;
    // adler32 校验
    uint32_t a = 1, bb = 0;
    for (uint8_t c : out) {
        a = (a + c) % 65521;
        bb = (bb + a) % 65521;
    }
    uint32_t want = (uint32_t(in[n - 4]) << 24) | (uint32_t(in[n - 3]) << 16) |
                    (uint32_t(in[n - 2]) << 8) | uint32_t(in[n - 1]);
    if (((bb << 16) | a) != want) {
        err = "adler32 校验失败";
        return false;
    }
    return true;
}

} // namespace em

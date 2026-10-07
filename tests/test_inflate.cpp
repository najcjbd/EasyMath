// EasyMath - inflate / zip 对拍测试
// 这里链接系统 zlib 只用来"造"压缩数据与算 CRC, 产品代码(src/inflate.cpp)不依赖 zlib。
#include "../src/inflate.hpp"
#include "../src/zipfile.hpp"

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace em;

static int g_pass = 0, g_fail = 0;
static void ok(const char *name) {
    ++g_pass;
    (void)name;
}
static void bad(const char *name, const std::string &why) {
    ++g_fail;
    std::printf("FAIL: %s —— %s\n", name, why.c_str());
}
static void expect(bool cond, const char *name, const std::string &why = "") {
    if (cond) ok(name); else bad(name, why);
}

// 用 zlib 压缩(裸 deflate)
static std::vector<uint8_t> deflateRaw(const std::vector<uint8_t> &in, int level, int strategy,
                                       bool multiBlock) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    deflateInit2(&zs, level, Z_DEFLATED, -15, 8, strategy);
    std::vector<uint8_t> out(in.size() + in.size() / 2 + 128);
    zs.next_in = const_cast<Bytef *>(in.data());
    zs.avail_in = static_cast<uInt>(in.size());
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    if (multiBlock) {
        // 分块喂入 + Z_SYNC_FLUSH, 逼出多个块(含 stored/dynamic 混合)
        std::size_t chunk = in.size() / 3 + 1;
        std::size_t pos = 0;
        while (pos < in.size()) {
            std::size_t n = std::min(chunk, in.size() - pos);
            zs.next_in = const_cast<Bytef *>(in.data() + pos);
            zs.avail_in = static_cast<uInt>(n);
            deflate(&zs, Z_SYNC_FLUSH);
            pos += n;
        }
        deflate(&zs, Z_FINISH);
    } else {
        deflate(&zs, Z_FINISH);
    }
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return out;
}

static void testRoundTrip(const char *name, const std::vector<uint8_t> &data, int level,
                          int strategy, bool multiBlock) {
    std::vector<uint8_t> comp = deflateRaw(data, level, strategy, multiBlock);
    std::vector<uint8_t> back;
    std::string err;
    if (!inflateRaw(comp.data(), comp.size(), back, err)) {
        bad(name, "inflate 失败: " + err);
        return;
    }
    if (back != data) {
        bad(name, "解压结果不一致: " + std::to_string(back.size()) + " vs " +
                      std::to_string(data.size()));
        return;
    }
    ok(name);
}

static std::vector<uint8_t> randomData(std::mt19937 &rng, std::size_t n, int kind) {
    std::vector<uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (kind == 0) v[i] = static_cast<uint8_t>(rng() & 0xFF);
        else if (kind == 1) v[i] = static_cast<uint8_t>(rng() % 4);
        else if (kind == 2) v[i] = static_cast<uint8_t>((i / 97) & 0xFF);
        else v[i] = 'A' + static_cast<uint8_t>(i % 3);
    }
    return v;
}

// ---- 手工拼一个 zip(供 ZipArchive 测试) ----
static void put32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(uint8_t(x & 0xFF)); v.push_back(uint8_t((x >> 8) & 0xFF));
    v.push_back(uint8_t((x >> 16) & 0xFF)); v.push_back(uint8_t((x >> 24) & 0xFF));
}
static void put16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(uint8_t(x & 0xFF)); v.push_back(uint8_t((x >> 8) & 0xFF));
}

struct BuiltEntry { std::string name; std::vector<uint8_t> raw, comp; uint32_t crc; uint16_t method; };

static std::vector<uint8_t> buildZip(const std::vector<BuiltEntry> &ents) {
    std::vector<uint8_t> out;
    struct Loc { std::size_t off; };
    std::vector<std::size_t> offsets;
    for (const auto &e : ents) {
        offsets.push_back(out.size());
        put32(out, 0x04034b50u);
        put16(out, 20);
        put16(out, 0x0800); // UTF-8 名字
        put16(out, e.method);
        put16(out, 0);
        put16(out, 0);
        put32(out, e.crc);
        put32(out, uint32_t(e.comp.size()));
        put32(out, uint32_t(e.raw.size()));
        put16(out, uint16_t(e.name.size()));
        put16(out, 0);
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), e.comp.begin(), e.comp.end());
    }
    std::size_t cdStart = out.size();
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const auto &e = ents[i];
        put32(out, 0x02014b50u);
        put16(out, 20); put16(out, 20);
        put16(out, 0x0800);
        put16(out, e.method);
        put16(out, 0); put16(out, 0);
        put32(out, e.crc);
        put32(out, uint32_t(e.comp.size()));
        put32(out, uint32_t(e.raw.size()));
        put16(out, uint16_t(e.name.size()));
        put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0);
        put32(out, 0);
        put32(out, uint32_t(offsets[i]));
        out.insert(out.end(), e.name.begin(), e.name.end());
    }
    std::size_t cdSize = out.size() - cdStart;
    put32(out, 0x06054b50u);
    put16(out, 0); put16(out, 0);
    put16(out, uint16_t(ents.size())); put16(out, uint16_t(ents.size()));
    put32(out, uint32_t(cdSize));
    put32(out, uint32_t(cdStart));
    put16(out, 0);
    return out;
}

int main() {
    std::mt19937 rng(12345);
    const int kinds[] = {0, 1, 2, 3};
    const std::size_t sizes[] = {0, 1, 2, 15, 100, 1000, 70000, 300000};
    for (std::size_t sz : sizes) {
        for (int kind : kinds) {
            auto data = randomData(rng, sz, kind);
            for (int level : {0, 1, 6, 9}) {
                std::string nm = std::string("往返 size=") + std::to_string(sz) + " kind=" +
                                 std::to_string(kind) + " level=" + std::to_string(level);
                testRoundTrip(nm.c_str(), data, level, Z_DEFAULT_STRATEGY, false);
                testRoundTrip((nm + " 多块").c_str(), data, level, Z_DEFAULT_STRATEGY, true);
            }
        }
    }
    // 各种策略(huffman-only / rle / fixed 会走不同的块类型)
    {
        auto data = randomData(rng, 50000, 3);
        testRoundTrip("策略: huffman only", data, 6, Z_HUFFMAN_ONLY, false);
        testRoundTrip("策略: RLE", data, 6, Z_RLE, false);
        testRoundTrip("策略: fixed", data, 6, Z_FIXED, false);
        testRoundTrip("策略: filtered", data, 6, Z_FILTERED, false);
        testRoundTrip("策略: fixed 多块", data, 6, Z_FIXED, true);
    }
    // 长距离重复(触发最大长度/最大距离码)
    {
        std::vector<uint8_t> data;
        for (int i = 0; i < 40; ++i) data.insert(data.end(), 258, uint8_t('x' + (i % 5)));
        testRoundTrip("长重复 258", data, 9, Z_DEFAULT_STRATEGY, false);
        std::vector<uint8_t> d2(70000, 7);
        testRoundTrip("全同 70000", d2, 9, Z_DEFAULT_STRATEGY, false);
    }
    // zlib 容器
    {
        auto data = randomData(rng, 123456, 0);
        uLongf clen = compressBound(static_cast<uLong>(data.size()));
        std::vector<uint8_t> comp(clen);
        compress2(comp.data(), &clen, data.data(), static_cast<uLong>(data.size()), 6);
        comp.resize(clen);
        std::vector<uint8_t> back;
        std::string err;
        expect(inflateZlib(comp.data(), comp.size(), back, err) && back == data, "zlib 容器往返",
               err);
        // 坏 adler32
        auto bad1 = comp;
        bad1[bad1.size() - 1] ^= 0xFF;
        std::vector<uint8_t> dummy;
        expect(!inflateZlib(bad1.data(), bad1.size(), dummy, err), "zlib: 坏校验和要报错");
    }
    // 错误输入
    {
        std::vector<uint8_t> out;
        std::string err;
        expect(!inflateRaw(nullptr, 0, out, err), "空输入要报错");
        uint8_t junk[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        expect(!inflateRaw(junk, sizeof(junk), out, err), "垃圾数据要报错");
        auto data = randomData(rng, 5000, 0);
        auto comp = deflateRaw(data, 9, Z_DEFAULT_STRATEGY, false);
        comp.resize(comp.size() / 2);
        expect(!inflateRaw(comp.data(), comp.size(), out, err), "截断数据要报错");
        // 解压上限
        std::vector<uint8_t> big(1 << 20, 0);
        auto cbig = deflateRaw(big, 9, Z_DEFAULT_STRATEGY, false);
        expect(!inflateRaw(cbig.data(), cbig.size(), out, err, 1024), "超过上限要报错");
    }

    // ---- ZIP ----
    {
        std::vector<BuiltEntry> ents;
        auto raw1 = randomData(rng, 60000, 3);
        BuiltEntry e1{"vivo Sans/fonts/vivoSans-Regular.ttf", raw1,
                      deflateRaw(raw1, 6, Z_DEFAULT_STRATEGY, false),
                      crc32(0, raw1.data(), uInt(raw1.size())), 8};
        auto raw2 = randomData(rng, 100, 0);
        BuiltEntry e2{"stored.bin", raw2, raw2, crc32(0, raw2.data(), uInt(raw2.size())), 0};
        ents.push_back(e1);
        ents.push_back(e2);
        auto zip = buildZip(ents);
        ZipArchive z;
        std::string err;
        expect(z.openMemory(zip, err), "zip: 打开内存包", err);
        expect(z.entries().size() == 2, "zip: 条目数");
        expect(z.entries()[0].nameAscii == e1.name, "zip: 名字读取",
               z.entries()[0].nameAscii);
        expect(z.entries()[0].utf8, "zip: UTF-8 标志");
        std::vector<uint8_t> got;
        expect(z.extract(0, got, err) && got == raw1, "zip: 解压 deflate 条目", err);
        expect(z.extract(1, got, err) && got == raw2, "zip: 解压 stored 条目", err);
        auto hit = z.findSuffix(".ttf");
        expect(hit.size() == 1 && hit[0] == 0, "zip: 按后缀找 ttf");
        auto hit2 = z.findSubstr("regular");
        expect(hit2.size() == 1 && hit2[0] == 0, "zip: 按子串找字体");
        // CRC 破坏检测(stored 条目)
        auto zipBad = zip;
        // 找到 stored 条目数据位置: 局部头 + 名字之后
        for (std::size_t i = 0; i + 1 < zipBad.size(); ++i) {
            if (zipBad[i] == 's' && i + 4 <= zipBad.size() &&
                std::memcmp(&zipBad[i], "stored.bin", 10) == 0) {
                zipBad[i + 10] ^= 0xFF;
                break;
            }
        }
        ZipArchive z2;
        expect(z2.openMemory(zipBad, err), "zip: 破坏后仍能打开");
        std::vector<uint8_t> tmp;
        expect(!z2.extract(1, tmp, err), "zip: CRC 不符要报错");
        // 不支持的压缩方式
        std::vector<BuiltEntry> ents2;
        ents2.push_back({"x.bin", raw2, raw2, 0, 12});
        auto zip2 = buildZip(ents2);
        ZipArchive z3;
        expect(z3.openMemory(zip2, err), "zip: 打开(未知压缩方式)");
        expect(!z3.extract(0, tmp, err), "zip: 不支持的压缩方式要报错");
        // 不是 zip
        std::vector<uint8_t> notzip(100, 0xAB);
        ZipArchive z4;
        expect(!z4.openMemory(notzip, err), "zip: 非 zip 数据要报错");
    }

    std::printf("inflate/zip 测试: 通过 %d 项, 失败 %d 项\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

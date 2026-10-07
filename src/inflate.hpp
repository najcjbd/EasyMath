// EasyMath - 自包含 DEFLATE 解压 (RFC 1951)
// 为什么自己写: Windows/安卓两端都要求零外部依赖(mingw 下没有 libz),
// 而字体包是 .zip(deflate), 所以带一份按规范实现的一次性 inflate。
// 正确性由 tests/test_inflate.cpp 拿 zlib 压缩的随机数据逐字节对拍。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace em {

// 解压 raw DEFLATE 数据。out 先清空; maxOut=0 时用默认上限(256MB)。
// 失败返回 false 并填 err(中文/英文随语言, 只用于诊断)。
bool inflateRaw(const uint8_t *in, std::size_t n, std::vector<uint8_t> &out, std::string &err,
                std::size_t maxOut = 0);

// zlib 容器 = 2 字节头 + raw DEFLATE + 4 字节 adler32
bool inflateZlib(const uint8_t *in, std::size_t n, std::vector<uint8_t> &out, std::string &err,
                 std::size_t maxOut = 0);

} // namespace em

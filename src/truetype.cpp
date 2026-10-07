// EasyMath - TrueType 解析实现
#include "truetype.hpp"

#include <algorithm>
#include <cstring>
#include <functional>

namespace em {
static std::string ttNameById(const std::vector<uint8_t> &data, uint16_t wantId);

namespace {

int16_t rdI16(const uint8_t *p) { return int16_t(uint16_t(p[0]) << 8 | uint16_t(p[1])); }
uint16_t rdU16(const uint8_t *p) { return uint16_t(uint16_t(p[0]) << 8 | uint16_t(p[1])); }
uint32_t rdU32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

bool inRange(std::size_t off, std::size_t need, std::size_t total) {
    return off <= total && need <= total - off;
}

} // namespace


// ==================== 可变字体(fvar) ====================
namespace {
double fixed16(int32_t v) { return double(v) / 65536.0; }
} // namespace


// ==================== gvar: 元组元数据 ====================
// 布局(OpenType 规范): 头 -> 每字形偏移数组 -> 字形变体数据
//   字形变体数据: tupleVariationCount(2, 低 12 位是元组数, bit15=共享点数, bit14=?)
//                 dataOffset(2, 序列化数据相对本字形数据的起点)
//                 元组头[count]: variationDataSize(2), tupleIndex(2)[, peak/中间区间坐标]
//   tupleIndex: bit15=内嵌峰值, bit14=有中间区域, bit13=私有点数, 低 12 位=共享元组下标

void TrueTypeFont::normalizeVarCoords(const std::vector<VarAxis> &axes, const std::vector<double> &user,
                                      std::vector<double> &out) {
    out.assign(axes.size(), 0.0);
    for (std::size_t i = 0; i < axes.size() && i < user.size(); ++i) {
        double c = user[i], d = axes[i].defV;
        double v = 0;
        if (c > d && axes[i].maxV > d) v = (c - d) / (axes[i].maxV - d);
        else if (c < d && d > axes[i].minV) v = (c - d) / (d - axes[i].minV);
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        out[i] = v;
    }
}

double TrueTypeFont::tupleScalar(const GvarTuple &t, const std::vector<double> &coords) {
    double s = 1.0;
    std::size_t n = t.peak.size();
    for (std::size_t i = 0; i < n; ++i) {
        double pk = t.peak[i];
        if (pk == 0) continue;                  // 该轴不参与约束
        double c = i < coords.size() ? coords[i] : 0.0;
        double st = i < t.start.size() ? t.start[i] : pk;
        double en = i < t.end.size() ? t.end[i] : pk;
        double f = 1.0;
        if (c == pk) f = 1.0;
        else if (c <= st || c >= en) f = 0.0;
        else if (c < pk) f = (pk - st) == 0 ? 0.0 : (c - st) / (pk - st);
        else f = (en - pk) == 0 ? 0.0 : (en - c) / (en - pk);
        s *= f;
        if (s == 0) break;
    }
    return s;
}


int TrueTypeFont::glyphPointCount(int glyph) const {
    if (glyph < 0 || glyph >= numGlyphs_) return -1;
    std::size_t g = 0, e = 0;
    if (indexToLoc_) {
        if (!inRange(locaOff_ + std::size_t(glyph) * 4, 8, data_.size())) return -1;
        g = rdU32(&data_[locaOff_ + std::size_t(glyph) * 4]);
        e = rdU32(&data_[locaOff_ + std::size_t(glyph) * 4 + 4]);
    } else {
        if (!inRange(locaOff_ + std::size_t(glyph) * 2, 4, data_.size())) return -1;
        g = std::size_t(rdU16(&data_[locaOff_ + std::size_t(glyph) * 2])) * 2;
        e = std::size_t(rdU16(&data_[locaOff_ + std::size_t(glyph) * 2 + 2])) * 2;
    }
    if (e <= g || !inRange(glyfOff_ + g, 10, data_.size())) return -1;
    const uint8_t *h = &data_[glyfOff_ + g];
    int nc = rdI16(h);                       // numberOfContours
    if (nc < 0) return -1;                   // 复合字形: 由组件拼出来, 点数不直接对应
    if (nc == 0) return 0;
    if (!inRange(glyfOff_ + g + 10, std::size_t(nc) * 2, data_.size())) return -1;
    return int(rdU16(h + 10 + std::size_t(nc - 1) * 2)) + 1;   // 最后一个 endPt + 1
}


bool TrueTypeFont::glyphPoints(int glyph, std::vector<double> &xs, std::vector<double> &ys,
                               std::vector<int> &endPts) const {
    std::vector<char> on;
    return glyphPointsEx(glyph, xs, ys, on, endPts);
}

bool TrueTypeFont::glyphPointsEx(int glyph, std::vector<double> &xs, std::vector<double> &ys,
                                 std::vector<char> &onCurve, std::vector<int> &endPts) const {
    xs.clear(); ys.clear(); endPts.clear();
    if (glyph < 0 || glyph >= numGlyphs_) return false;
    std::size_t g = 0, e = 0;
    if (indexToLoc_) {
        if (!inRange(locaOff_ + std::size_t(glyph) * 4, 8, data_.size())) return false;
        g = rdU32(&data_[locaOff_ + std::size_t(glyph) * 4]);
        e = rdU32(&data_[locaOff_ + std::size_t(glyph) * 4 + 4]);
    } else {
        if (!inRange(locaOff_ + std::size_t(glyph) * 2, 4, data_.size())) return false;
        g = std::size_t(rdU16(&data_[locaOff_ + std::size_t(glyph) * 2])) * 2;
        e = std::size_t(rdU16(&data_[locaOff_ + std::size_t(glyph) * 2 + 2])) * 2;
    }
    if (e <= g || !inRange(glyfOff_ + g, 10, data_.size())) return false;
    std::size_t base = glyfOff_ + g;
    std::size_t avail = e - g;
    int nc = rdI16(&data_[base]);
    if (nc <= 0) return false;                       // 复合/空字形
    std::size_t p = base + 10;
    if (!inRange(p, std::size_t(nc) * 2, data_.size())) return false;
    for (int i = 0; i < nc; ++i) endPts.push_back(int(rdU16(&data_[p + std::size_t(i) * 2])));
    p += std::size_t(nc) * 2;
    if (!inRange(p, 2, data_.size())) return false;
    std::size_t ilen = rdU16(&data_[p]);
    p += 2 + ilen;
    std::size_t n = endPts.empty() ? 0 : std::size_t(endPts.back()) + 1;
    if (n == 0) return true;
    // flags(游程压缩)
    std::vector<uint8_t> flags;
    flags.reserve(n);
    while (flags.size() < n) {
        if (!inRange(p, 1, data_.size())) return false;
        uint8_t f = data_[p++];
        flags.push_back(f);
        if (f & 0x08) {                              // 重复
            if (!inRange(p, 1, data_.size())) return false;
            uint8_t rep = data_[p++];
            for (uint8_t k = 0; k < rep && flags.size() < n; ++k) flags.push_back(f);
        }
    }
    onCurve.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) onCurve[i] = (flags[i] & 0x01) ? 1 : 0;
    // x 坐标
    xs.assign(n, 0.0);
    double cur = 0;
    for (std::size_t i = 0; i < n; ++i) {
        uint8_t f = flags[i];
        if (f & 0x02) {
            if (!inRange(p, 1, data_.size())) return false;
            // 短向量的值字节是**无符号**的, 符号由标志位给(踩过: 读成 int8_t 会把 254 变成 -2)
            uint8_t v = data_[p++];
            cur += (f & 0x10) ? double(v) : -double(v);
        } else if (!(f & 0x10)) {
            if (!inRange(p, 2, data_.size())) return false;
            cur += double(rdI16(&data_[p]));
            p += 2;
        }
        xs[i] = cur;
    }
    // y 坐标
    ys.assign(n, 0.0);
    cur = 0;
    for (std::size_t i = 0; i < n; ++i) {
        uint8_t f = flags[i];
        if (f & 0x04) {
            if (!inRange(p, 1, data_.size())) return false;
            uint8_t v = data_[p++];
            cur += (f & 0x20) ? double(v) : -double(v);
        } else if (!(f & 0x20)) {
            if (!inRange(p, 2, data_.size())) return false;
            cur += double(rdI16(&data_[p]));
            p += 2;
        }
        ys[i] = cur;
    }
    (void)avail;
    return true;
}



void TrueTypeFont::inferUnreferencedDeltas(const std::vector<double> &xs, const std::vector<double> &ys,
                                           const std::vector<int> &endPts,
                                           const std::vector<int> &referenced,
                                           const std::vector<double> &dx, const std::vector<double> &dy,
                                           std::vector<double> &outDx, std::vector<double> &outDy) {
    const std::size_t n = xs.size();
    outDx.assign(n, 0.0);
    outDy.assign(n, 0.0);
    std::vector<char> isRef(n, 0);
    for (std::size_t k = 0; k < referenced.size(); ++k) {
        int i = referenced[k];
        if (i < 0 || std::size_t(i) >= n) continue;
        isRef[std::size_t(i)] = 1;
        outDx[std::size_t(i)] = k < dx.size() ? dx[k] : 0.0;
        outDy[std::size_t(i)] = k < dy.size() ? dy[k] : 0.0;
    }
    if (!n) return;
    std::size_t start = 0;
    for (std::size_t ci = 0; ci < endPts.size(); ++ci) {
        std::size_t last = (endPts[ci] >= 0 && std::size_t(endPts[ci]) < n) ? std::size_t(endPts[ci]) : n - 1;
        if (start > last) break;
        std::vector<std::size_t> refs;
        for (std::size_t i = start; i <= last; ++i)
            if (isRef[i]) refs.push_back(i);
        if (refs.size() == 1) {
            // 整条轮廓跟着唯一被引用的点平移
            for (std::size_t i = start; i <= last; ++i) {
                if (i == refs[0]) continue;
                outDx[i] = outDx[refs[0]];
                outDy[i] = outDy[refs[0]];
            }
        } else if (refs.size() >= 2) {
            // 两个端点增量**一次性取值**(避免循环里读到被改写的值), 区间含端点一侧由 [a,b] 限定
            auto fill = [&](std::size_t i0, std::size_t i1, std::size_t a, std::size_t b) {
                if (a > b || b >= n) return;
                const double d0x = outDx[i0], d1x = outDx[i1], d0y = outDy[i0], d1y = outDy[i1];
                auto one = [&](const std::vector<double> &coords, std::vector<double> &out, double d0,
                               double d1, std::size_t i) {
                    double c0 = coords[i0], c1 = coords[i1], ci2 = coords[i];
                    double v;
                    if (d0 == d1 || c0 == c1) v = d0;
                    else v = d0 + (d1 - d0) * ((ci2 - c0) / (c1 - c0));
                    double lo = std::min(d0, d1), hi = std::max(d0, d1);
                    out[i] = (v < lo) ? lo : ((v > hi) ? hi : v);
                };
                for (std::size_t i = a; i <= b; ++i) {
                    if (i == i0 || i == i1 || isRef[i]) continue;
                    one(xs, outDx, d0x, d1x, i);
                    one(ys, outDy, d0y, d1y, i);
                }
            };
            // 相邻引用点之间的缺口
            for (std::size_t k = 0; k + 1 < refs.size(); ++k)
                if (refs[k] + 1 <= refs[k + 1] - 1) fill(refs[k], refs[k + 1], refs[k] + 1, refs[k + 1] - 1);
            // 环形缺口: 显式分尾段与头段两段(不用取模绕圈, 也不会 size_t 下溢)
            if (refs.back() + 1 <= last) fill(refs.back(), refs.front(), refs.back() + 1, last);
            if (refs.front() > start) fill(refs.back(), refs.front(), start, refs.front() - 1);
        }
        start = last + 1;
    }
}



bool TrueTypeFont::outlineVaried(int glyph, const std::vector<double> &axisUserCoords,
                                 std::vector<TtContour> &out, std::string &err) const {
    out.clear();
    std::vector<double> xs, ys;
    std::vector<char> on;
    std::vector<int> endPts;
    if (!glyphPointsEx(glyph, xs, ys, on, endPts)) {
        err = "该字形取不到点(可能是复合字形)";
        return false;
    }
    bool sparse = false;
    if (!variedGlyphPoints(glyph, axisUserCoords, xs, ys, endPts, sparse)) {
        err = "变体点计算失败";
        return false;
    }
    if (sparse && !axisUserCoords.empty()) {
        // 有稀疏点数元组: 退回默认实例(在调用方提示), 这里仍给出默认轮廓
        std::vector<double> dummy;
        if (!glyphPoints(glyph, xs, ys, endPts)) {
            err = "退回默认实例失败";
            return false;
        }
    }
    std::size_t start = 0;
    for (std::size_t ci = 0; ci < endPts.size(); ++ci) {
        std::size_t last = std::size_t(endPts[ci]);
        if (last >= xs.size() || start > last) break;
        const std::size_t n = last - start + 1;
        auto P = [&](std::size_t k) {   // 轮廓内第 k 个点(0..n-1)
            TtPoint p;
            p.x = xs[start + k];
            p.y = ys[start + k];
            p.on = on[start + k] != 0;
            return p;
        };
        // 找一个在曲线上的起点; 全为控制点时用相邻中点作为隐含起点
        std::size_t first = n;
        for (std::size_t k = 0; k < n; ++k)
            if (P(k).on) { first = k; break; }
        TtContour c;
        TtPoint prevOn;
        bool haveOff = false;
        TtPoint prevOff;
        std::size_t begin;
        if (first == n) {   // 整条都是控制点
            TtPoint a = P(n - 1), b = P(0);
            prevOn.x = (a.x + b.x) / 2;
            prevOn.y = (a.y + b.y) / 2;
            prevOn.on = true;
            begin = 0;
        } else {
            prevOn = P(first);
            begin = first + 1;
        }
        for (std::size_t k = 0; k < n; ++k) {
            std::size_t idx = (begin + k) % n;
            TtPoint cur = P(idx);
            if (cur.on) {
                TtSeg sg;
                if (haveOff) {
                    sg.kind = TtKind::Quad;
                    sg.p0 = prevOn;
                    sg.c = prevOff;
                    sg.p1 = cur;
                    haveOff = false;
                } else {
                    sg.kind = TtKind::Line;
                    sg.p0 = prevOn;
                    sg.p1 = cur;
                }
                if (sg.p0.x != sg.p1.x || sg.p0.y != sg.p1.y || sg.kind == TtKind::Quad)
                    c.segs.push_back(sg);
                prevOn = cur;
            } else {
                if (haveOff) {
                    TtPoint mid;
                    mid.x = (prevOff.x + cur.x) / 2;
                    mid.y = (prevOff.y + cur.y) / 2;
                    mid.on = true;
                    TtSeg sg;
                    sg.kind = TtKind::Quad;
                    sg.p0 = prevOn;
                    sg.c = prevOff;
                    sg.p1 = mid;
                    c.segs.push_back(sg);
                    prevOn = mid;
                }
                prevOff = cur;
                haveOff = true;
            }
        }
        // 收尾回到起点
        if (haveOff) {
            TtPoint st = (first == n) ? prevOn : P(first);
            TtSeg sg;
            sg.kind = TtKind::Quad;
            sg.p0 = prevOn;
            sg.c = prevOff;
            sg.p1 = st;
            c.segs.push_back(sg);
        } else {
            TtPoint st = (first == n) ? prevOn : P(first);
            TtSeg sg;
            sg.kind = TtKind::Line;
            sg.p0 = prevOn;
            sg.p1 = st;
            if (sg.p0.x != sg.p1.x || sg.p0.y != sg.p1.y) c.segs.push_back(sg);
        }
        out.push_back(c);
        start = last + 1;
    }
    return !out.empty();
}

bool TrueTypeFont::variedGlyphPoints(int glyph, const std::vector<double> &axisUserCoords,
                                     std::vector<double> &outXs, std::vector<double> &outYs,
                                     std::vector<int> &endPts, bool &sparse) const {
    sparse = false;
    if (!glyphPoints(glyph, outXs, outYs, endPts)) return false;
    std::vector<VarAxis> axes;
    std::vector<VarInstance> insts;
    if (!variations(axes, insts)) return true;      // 非可变字体: 原样返回
    std::vector<double> coords;
    normalizeVarCoords(axes, axisUserCoords, coords);
    std::vector<GvarTuple> tups;
    if (!glyphGvarTuples(glyph, tups)) return true;  // 该字形没有变体数据
    const std::size_t n = outXs.size();
    for (const auto &t : tups) {
        double sc = tupleScalar(t, coords);
        if (sc == 0) continue;
        if (t.dx.empty() && t.dy.empty()) continue;
        if (!t.points.empty()) { sparse = true; continue; }   // 稀疏点数需要 IUP, 这轮不猜
        if (t.dx.size() != n + 4 || t.dy.size() != n + 4) { sparse = true; continue; }
        for (std::size_t i = 0; i < n; ++i) {
            outXs[i] += sc * t.dx[i];
            outYs[i] += sc * t.dy[i];
        }
    }
    return true;
}


// ==================== GPOS 'kern' 成对字距 ====================
namespace {
// ValueRecord 里 xAdvance 的字节偏移(按 valueFormat 逐位累加)
int xAdvanceOffset(unsigned vf) {
    int off = 0;
    for (int bit = 0; bit < 16; ++bit) {
        if (!(vf & (1u << bit))) continue;
        if (bit == 2) return off;            // 0x0004 = xAdvance
        off += 2;                            // 其余字段各占 2 字节(设备表偏移也算 2)
    }
    return -1;
}
int valueRecordSize(unsigned vf) {
    int n = 0;
    for (int bit = 0; bit < 16; ++bit)
        if (vf & (1u << bit)) ++n;
    return n * 2;
}
} // namespace

bool TrueTypeFont::gposKernUsable() const {
    if (kernParsed_) return kernOk_;
    kernParsed_ = true;
    std::size_t len = 0;
    std::size_t gp = tableOff("GPOS", &len);
    if (!gp || len < 12) return false;
    const uint8_t *b = data_.data() + gp;
    auto u16 = [&](std::size_t i) { return uint16_t((uint16_t(b[i]) << 8) | b[i + 1]); };
    std::size_t featOff = u16(6);
    if (!featOff || featOff + 2 > len) return false;
    std::size_t fc = u16(featOff);
    for (std::size_t i = 0; i < fc; ++i) {
        std::size_t r = featOff + 2 + i * 6;
        if (r + 6 > len) break;
        if (std::memcmp(b + r, "kern", 4) == 0) {
            kernOk_ = true;
            break;
        }
    }
    return kernOk_;
}

int TrueTypeFont::pairKern(int left, int right) const {
    if (!gposKernUsable()) return 0;
    uint64_t key = (uint64_t(uint32_t(left)) << 32) | uint32_t(right);
    auto it = kernCache_.find(key);
    if (it != kernCache_.end()) return it->second;
    std::size_t len = 0;
    std::size_t gp = tableOff("GPOS", &len);
    const uint8_t *b = data_.data() + gp;
    auto u16 = [&](std::size_t i) { return uint16_t((uint16_t(b[i]) << 8) | b[i + 1]); };
    auto i16 = [&](std::size_t i) { return int16_t((uint16_t(b[i]) << 8) | b[i + 1]); };
    std::size_t featOff = u16(6), lookOff = u16(8);
    if (!featOff || !lookOff) { kernCache_[key] = 0; return 0; }
    // 找 kern 特性的 lookup 下标
    std::vector<std::size_t> lookups;
    {
        std::size_t fc = u16(featOff);
        for (std::size_t i = 0; i < fc; ++i) {
            std::size_t r = featOff + 2 + i * 6;
            if (r + 6 > len) break;
            if (std::memcmp(b + r, "kern", 4) != 0) continue;
            std::size_t fo = featOff + u16(r + 4);
            if (fo + 4 > len) break;
            std::size_t n = u16(fo + 2);
            for (std::size_t k = 0; k < n; ++k) {
                if (fo + 4 + k * 2 + 2 > len) break;
                lookups.push_back(u16(fo + 4 + k * 2));
            }
            break;
        }
    }
    // 覆盖率查表
    auto covIndex = [&](std::size_t cov, int gid) -> int {
        if (!cov || cov + 4 > len) return -1;
        std::size_t fmt = u16(cov);
        if (fmt == 1) {
            std::size_t n = u16(cov + 2);
            for (std::size_t i = 0; i < n; ++i) {
                if (cov + 4 + i * 2 + 2 > len) break;
                if (int(u16(cov + 4 + i * 2)) == gid) return int(i);
            }
        } else if (fmt == 2) {
            std::size_t n = u16(cov + 2);
            for (std::size_t i = 0; i < n; ++i) {
                std::size_t r = cov + 4 + i * 6;
                if (r + 6 > len) break;
                int s = u16(r), e = u16(r + 2);
                if (gid >= s && gid <= e) return int(u16(r + 4)) + (gid - s);
            }
        }
        return -1;
    };
    // 类别查表(NULL/0 表示全部为 0 类)
    auto classOf = [&](std::size_t cd, int gid) -> int {
        if (!cd || cd + 4 > len) return 0;
        std::size_t fmt = u16(cd);
        if (fmt == 1) {
            int start = u16(cd + 2);
            std::size_t n = u16(cd + 4);
            if (gid < start || std::size_t(gid - start) >= n) return 0;
            return u16(cd + 6 + std::size_t(gid - start) * 2);
        }
        if (fmt == 2) {
            std::size_t n = u16(cd + 2);
            for (std::size_t i = 0; i < n; ++i) {
                std::size_t r = cd + 4 + i * 6;
                if (r + 6 > len) break;
                int s = u16(r), e = u16(r + 2);
                if (gid >= s && gid <= e) return u16(r + 4);
            }
        }
        return 0;
    };
    int total = 0;
    for (std::size_t li : lookups) {
        if (lookOff + 2 + li * 2 + 2 > len) continue;
        std::size_t lo = lookOff + u16(lookOff + 2 + li * 2);
        if (lo + 6 > len) continue;
        std::size_t ltype = u16(lo), sc = u16(lo + 4);
        if (ltype != 2) continue;
        for (std::size_t si = 0; si < sc; ++si) {
            if (lo + 6 + si * 2 + 2 > len) break;
            std::size_t so = lo + u16(lo + 6 + si * 2);
            if (so + 6 > len) break;
            std::size_t fmt = u16(so), cov = so + u16(so + 2);
            unsigned vf1 = u16(so + 4), vf2 = u16(so + 6);
            int advOff = xAdvanceOffset(vf1);
            int covIdx = covIndex(cov, left);
            if (covIdx < 0) continue;                 // 左字形不在覆盖表 -> 该子表不适用
            if (fmt == 1) {
                std::size_t psc = u16(so + 8);
                if (covIdx >= int(psc)) continue;
                std::size_t ps = so + u16(so + 10 + std::size_t(covIdx) * 2);
                if (ps + 2 > len) continue;
                std::size_t n = u16(ps);
                int recSize = 2 + valueRecordSize(vf1) + valueRecordSize(vf2);
                for (std::size_t k = 0; k < n; ++k) {
                    std::size_t r = ps + 2 + k * std::size_t(recSize);
                    if (r + std::size_t(recSize) > len) break;
                    if (int(u16(r)) != right) continue;   // 已按字形号排序, 这里线性找够用
                    if (advOff >= 0) total += i16(r + 2 + std::size_t(advOff));
                    break;
                }
                break;   // 同一 lookup 内命中一个子表即止
            }
            if (fmt == 2) {
                std::size_t cd1 = u16(so + 8) ? so + u16(so + 8) : 0;
                std::size_t cd2 = u16(so + 10) ? so + u16(so + 10) : 0;
                std::size_t c1 = u16(so + 12), c2 = u16(so + 14);
                int k1 = classOf(cd1, left), k2 = classOf(cd2, right);
                if (k1 < 0 || k2 < 0 || std::size_t(k1) >= c1 || std::size_t(k2) >= c2) continue;
                int vrec = valueRecordSize(vf1) + valueRecordSize(vf2);
                std::size_t base = so + 16 + (std::size_t(k1) * c2 + std::size_t(k2)) * std::size_t(vrec);
                if (base + std::size_t(vrec) > len) continue;
                if (advOff >= 0) total += i16(base + std::size_t(advOff));
                break;
            }
        }
    }
    kernCache_[key] = total;
    return total;
}


// ==================== GSUB type 4: 连字 ====================
bool TrueTypeFont::hasLiga() const {
    if (ligaParsed_) return ligaOk_;
    ligaParsed_ = true;
    std::size_t len = 0;
    std::size_t gs = tableOff("GSUB", &len);
    if (!gs || len < 12) return false;
    const uint8_t *b = data_.data() + gs;
    auto u16 = [&](std::size_t i) { return uint16_t((uint16_t(b[i]) << 8) | b[i + 1]); };
    std::size_t featOff = u16(6);
    if (!featOff || featOff + 2 > len) return false;
    std::size_t fc = u16(featOff);
    for (std::size_t i = 0; i < fc; ++i) {
        std::size_t r = featOff + 2 + i * 6;
        if (r + 6 > len) break;
        if (std::memcmp(b + r, "liga", 4) == 0 || std::memcmp(b + r, "calt", 4) == 0 ||
            std::memcmp(b + r, "dlig", 4) == 0) { ligaOk_ = true; break; }
    }
    return ligaOk_;
}

int TrueTypeFont::ligature(int first, int second, std::string *whichFeature) const {
    if (!hasLiga()) return 0;
    static const char *kTags[] = {"liga", "calt", "dlig"};
    std::size_t len = 0;
    std::size_t gs = tableOff("GSUB", &len);
    const uint8_t *b = data_.data() + gs;
    auto u16 = [&](std::size_t i) { return uint16_t((uint16_t(b[i]) << 8) | b[i + 1]); };
    std::size_t featOff = u16(6), lookOff = u16(8);
    if (!featOff || !lookOff) return 0;
    for (const char *tag : kTags) {
    std::vector<std::size_t> lookups;
    {
        std::size_t fc = u16(featOff);
        for (std::size_t i = 0; i < fc; ++i) {
            std::size_t r = featOff + 2 + i * 6;
            if (r + 6 > len) break;
            if (std::memcmp(b + r, tag, 4) != 0) continue;
            std::size_t fo = featOff + u16(r + 4);
            if (fo + 4 > len) break;
            std::size_t n = u16(fo + 2);
            for (std::size_t k = 0; k < n; ++k) {
                if (fo + 4 + k * 2 + 2 > len) break;
                lookups.push_back(u16(fo + 4 + k * 2));
            }
            break;
        }
    }
    for (std::size_t li : lookups) {
        if (lookOff + 2 + li * 2 + 2 > len) continue;
        std::size_t lo = lookOff + u16(lookOff + 2 + li * 2);
        if (lo + 6 > len) continue;
        std::size_t ltype = u16(lo), sc = u16(lo + 4);
        if (ltype != 4) continue;                    // 4 = Ligature Substitution
        for (std::size_t si = 0; si < sc; ++si) {
            if (lo + 6 + si * 2 + 2 > len) break;
            std::size_t so = lo + u16(lo + 6 + si * 2);
            if (so + 6 > len) break;
            if (u16(so) != 1) continue;              // 只支持格式 1
            std::size_t cov = so + u16(so + 2);
            // 覆盖表: first 必须命中, 命中下标即 LigatureSet 下标
            int covIdx = -1;
            std::size_t lsCount = u16(so + 4);
            if (cov && cov + 4 <= len) {
                std::size_t fmt = u16(cov);
                if (fmt == 1) {
                    std::size_t n = u16(cov + 2);
                    for (std::size_t i = 0; i < n; ++i) {
                        if (cov + 4 + i * 2 + 2 > len) break;
                        if (int(u16(cov + 4 + i * 2)) == first) { covIdx = int(i); break; }
                    }
                } else if (fmt == 2) {
                    std::size_t n = u16(cov + 2);
                    for (std::size_t i = 0; i < n; ++i) {
                        std::size_t r = cov + 4 + i * 6;
                        if (r + 6 > len) break;
                        int s0 = u16(r), e0 = u16(r + 2);
                        if (first >= s0 && first <= e0) {
                            covIdx = int(u16(r + 4)) + (first - s0);
                            break;
                        }
                    }
                }
            }
            if (covIdx < 0 || std::size_t(covIdx) >= lsCount) continue;
            std::size_t lsSet = so + u16(so + 6 + std::size_t(covIdx) * 2);
            if (lsSet + 2 > len) continue;
            std::size_t ln = u16(lsSet);
            for (std::size_t k = 0; k < ln; ++k) {
                if (lsSet + 2 + k * 2 + 2 > len) break;
                std::size_t lg = lsSet + u16(lsSet + 2 + k * 2);
                if (lg + 4 > len) continue;
                int ligGlyph = u16(lg);
                std::size_t comps = u16(lg + 2);
                if (comps < 2) continue;
                if (comps == 2) {
                    if (lg + 6 > len) continue;
                    if (int(u16(lg + 4)) == second) {
                        if (whichFeature) *whichFeature = tag;
                        return ligGlyph;
                    }
                } else {
                    // 更长的连字: 前两个字匹配也算(此处只用于诊断/前缀判断)
                    if (lg + 6 <= len && int(u16(lg + 4)) == second) {
                        if (whichFeature) *whichFeature = tag;
                        return ligGlyph;
                    }
                }
            }
        }
    }
    } // tag
    return 0;
}


// ==================== CFF(OTF) 结构读取(B 阶段第一步) ====================
namespace {
struct CffIdx {
    std::size_t count = 0;
    std::size_t dataStart = 0;   // 第 0 条数据的绝对偏移
    std::size_t end = 0;         // 整个 INDEX 结束的绝对偏移
    std::vector<std::size_t> offs;   // count+1 个, 相对 dataStart
    long firstLen = -1;
};
// 读一个 CFF INDEX(起始处是 count); 失败时 ok=false
bool readIndex(const std::vector<uint8_t> &d, std::size_t at, CffIdx &ix) {
    if (at + 2 > d.size()) return false;
    ix.count = (std::size_t(d[at]) << 8) | d[at + 1];
    if (ix.count == 0) {
        ix.dataStart = at + 2;
        ix.end = at + 2;
        ix.offs = {0};
        ix.firstLen = 0;
        return true;
    }
    if (at + 3 > d.size()) return false;
    std::size_t offSize = d[at + 2];
    if (offSize < 1 || offSize > 4) return false;
    std::size_t base = at + 3;
    if (base + (ix.count + 1) * offSize > d.size()) return false;
    ix.offs.clear();
    for (std::size_t i = 0; i <= ix.count; ++i) {
        std::size_t v = 0;
        for (std::size_t b = 0; b < offSize; ++b) v = (v << 8) | d[base + i * offSize + b];
        ix.offs.push_back(v);
    }
    if (ix.offs[0] != 1) return false;                 // 规范要求从 1 开始
    ix.dataStart = base + (ix.count + 1) * offSize - 1;
    ix.end = ix.dataStart + ix.offs[ix.count];
    if (ix.end > d.size()) return false;
    ix.firstLen = long(ix.offs[1] - ix.offs[0]);
    return true;
}
// 极简 DICT 解析: 只需要少数操作符(op2 用 1200+x 表示)
template <typename F>
void parseDict(const std::vector<uint8_t> &d, std::size_t at, std::size_t end, F onOp) {
    std::vector<double> st;
    std::size_t i = at;
    while (i < end && i < d.size()) {
        uint8_t b = d[i];
        if (b <= 21) {
            int op = (b == 12) ? (1200 + (i + 1 < d.size() ? d[i + 1] : 0)) : int(b);
            onOp(op, st);
            st.clear();
            i += (b == 12) ? 2 : 1;
        } else if (b == 28) {
            if (i + 3 > d.size()) break;
            st.push_back(double(int16_t((uint16_t(d[i + 1]) << 8) | d[i + 2])));
            i += 3;
        } else if (b == 29) {
            if (i + 5 > d.size()) break;
            int32_t v = int32_t((uint32_t(d[i + 1]) << 24) | (uint32_t(d[i + 2]) << 16) |
                                (uint32_t(d[i + 3]) << 8) | d[i + 4]);
            st.push_back(double(v));
            i += 5;
        } else if (b == 30) {                       // 实数: 跳过
            i += 1;
            while (i < d.size()) {
                uint8_t b2 = d[i++];
                if ((b2 & 0x0F) == 0x0F || (b2 >> 4) == 0x0F) break;
            }
            st.push_back(0);
        } else if (b == 255) {                      // 16.16 定点数(DICT 里会出现, 漏掉会让扫描失步)
            if (i + 5 > d.size()) break;
            int32_t v = int32_t((uint32_t(d[i + 1]) << 24) | (uint32_t(d[i + 2]) << 16) |
                                (uint32_t(d[i + 3]) << 8) | d[i + 4]);
            st.push_back(double(v) / 65536.0);
            i += 5;
        } else if (b >= 32 && b <= 246) {
            st.push_back(double(int(b) - 139));
            i += 1;
        } else if (b >= 247 && b <= 250) {
            if (i + 2 > d.size()) break;
            st.push_back(double((int(b) - 247) * 256 + d[i + 1] + 108));
            i += 2;
        } else if (b >= 251 && b <= 254) {
            if (i + 2 > d.size()) break;
            st.push_back(double(-(int(b) - 251) * 256 - d[i + 1] - 108));
            i += 2;
        } else {
            i += 1;                                  // 其它(保留位)忽略
        }
    }
}
} // namespace

bool cffReadInfo(const std::vector<uint8_t> &font, CffInfo &out, std::string &err) {
    out = CffInfo();
    if (font.size() < 12) { err = "文件太小"; return false; }
    auto u16 = [&](std::size_t i) { return uint16_t((uint16_t(font[i]) << 8) | font[i + 1]); };
    auto u32 = [&](std::size_t i) {
        return uint32_t((uint32_t(font[i]) << 24) | (uint32_t(font[i + 1]) << 16) |
                        (uint32_t(font[i + 2]) << 8) | font[i + 3]);
    };
    std::size_t num = u16(4), cff = 0, cffLen = 0;
    bool haveCff2 = false;
    for (std::size_t i = 0; i < num; ++i) {
        std::size_t r = 12 + i * 16;
        if (r + 16 > font.size()) break;
        if (std::memcmp(&font[r], "CFF ", 4) == 0) { cff = u32(r + 8); cffLen = u32(r + 12); }
        if (std::memcmp(&font[r], "CFF2", 4) == 0) { cff = u32(r + 8); cffLen = u32(r + 12); haveCff2 = true; }
    }
    if (!cff) { err = "这不是 CFF/OTF 字体(没有 CFF 表)"; return false; }
    if (cff + 4 > font.size()) { err = "CFF 表越界"; return false; }
    out.off = cff;
    out.len = cffLen;
    out.isCff2 = haveCff2;
    out.major = font[cff];
    out.minor = font[cff + 1];
    out.hdrSize = font[cff + 2];
    if (out.major != 1) { err = "只支持 CFF 1.0(遇到 " + std::to_string(out.major) + "." + std::to_string(out.minor) + ")"; return false; }
    std::size_t p = cff + out.hdrSize;
    CffIdx nameIx, topIx, strIx, gsubIx;
    if (!readIndex(font, p, nameIx)) { err = "Name INDEX 损坏"; return false; }
    p = nameIx.end;
    if (!readIndex(font, p, topIx)) { err = "TopDICT INDEX 损坏"; return false; }
    p = topIx.end;
    if (!readIndex(font, p, strIx)) { err = "String INDEX 损坏"; return false; }
    p = strIx.end;
    if (!readIndex(font, p, gsubIx)) { err = "GlobalSubr INDEX 损坏"; return false; }
    out.nameCount = int(nameIx.count);
    out.topDictCount = int(topIx.count);
    out.stringCount = int(strIx.count);
    out.gsubrCount = int(gsubIx.count);
    if (topIx.count == 0) { err = "TopDICT 为空"; return false; }
    // 只解析第一个 TopDICT(FDArray/FDSelect 属于 CID 字体)
    // 注意: dataStart 已经含了规范里的 "-1"(见 readIndex), 取条目时**不能再减 1**,
    // 否则整个 DICT 扫描前移一字节, 末尾的 Private 操作符会掉出边界(踩过)。
    std::size_t tdStart = topIx.dataStart + topIx.offs[0];
    std::size_t tdEnd = topIx.dataStart + topIx.offs[1];
    bool haveCs = false, havePriv = false;
    parseDict(font, tdStart, tdEnd, [&](int op, const std::vector<double> &st) {
        auto arg = [&](std::size_t k) { return k < st.size() ? long(st[k]) : -1L; };
        if (op == 17) { out.charStringsOff = arg(st.size() >= 1 ? 0 : 0); haveCs = true; }
        else if (op == 18) { out.privateSize = arg(0); out.privateOff = arg(1); havePriv = true; }
        else if (op == 15) out.charsetOff = arg(0);
        else if (op == 1236) out.fdArrayOff = arg(0);
        else if (op == 1237) out.fdSelectOff = arg(0);
    });
    if (!haveCs || out.charStringsOff < 0) { err = "TopDICT 里没有 CharStrings"; return false; }
    CffIdx csIx;
    if (!readIndex(font, cff + std::size_t(out.charStringsOff), csIx)) { err = "CharStrings INDEX 损坏"; return false; }
    out.charStringsCount = long(csIx.count);
    if (havePriv && out.privateOff > 0 && out.privateSize > 0) {
        parseDict(font, cff + std::size_t(out.privateOff),
                  cff + std::size_t(out.privateOff) + std::size_t(out.privateSize),
                  [&](int op, const std::vector<double> &st) {
                      if (op == 19 && !st.empty()) out.subrsOff = long(st[0]);
                  });
    }
    out.ok = true;
    return true;
}


// ==================== B2: Type2 charstring 解释 ====================

// ---- CFF/OTF 的 cmap 查询(format 4 与 12) ----
bool cffCmapLookup(const std::vector<uint8_t> &font, uint32_t cp, int &gid, std::string &err) {
    gid = 0;
    if (font.size() < 12) { err = "文件太小"; return false; }
    auto u16 = [&](std::size_t i) { return uint16_t((uint16_t(font[i]) << 8) | font[i + 1]); };
    auto u32 = [&](std::size_t i) {
        return uint32_t((uint32_t(font[i]) << 24) | (uint32_t(font[i + 1]) << 16) |
                        (uint32_t(font[i + 2]) << 8) | font[i + 3]);
    };
    std::size_t num = u16(4), cmap = 0, cmapLen = 0;
    for (std::size_t i = 0; i < num; ++i) {
        std::size_t r = 12 + i * 16;
        if (r + 16 > font.size()) break;
        if (std::memcmp(&font[r], "cmap", 4) == 0) { cmap = u32(r + 8); cmapLen = u32(r + 12); }
    }
    if (!cmap || cmap + 4 > font.size()) { err = "没有 cmap 表"; return false; }
    std::size_t nsub = u16(cmap + 2);
    long best4 = -1, best12 = -1;
    for (std::size_t i = 0; i < nsub; ++i) {
        std::size_t r = cmap + 4 + i * 8;
        if (r + 8 > font.size()) break;
        std::size_t off = u32(r + 4);
        if (cmap + off + 2 > font.size()) continue;
        std::size_t fmt = u16(cmap + off);
        if (fmt == 4 && best4 < 0) best4 = long(cmap + off);
        if (fmt == 12 && best12 < 0) best12 = long(cmap + off);
    }
    if (best12 >= 0) {                       // 优先 format 12(覆盖全平面)
        std::size_t t = std::size_t(best12);
        std::size_t ngroups = u32(t + 12);
        std::size_t lo = 0, hi = ngroups;
        while (lo < hi) {
            std::size_t mid = (lo + hi) / 2;
            std::size_t g = t + 16 + mid * 12;
            if (g + 12 > font.size()) break;
            uint32_t s = u32(g), e = u32(g + 4);
            if (cp < s) hi = mid;
            else if (cp > e) lo = mid + 1;
            else { gid = int(u32(g + 8) + (cp - s)); return true; }
        }
    }
    if (best4 >= 0) {
        std::size_t t = std::size_t(best4);
        std::size_t segX2 = u16(t + 6);
        std::size_t segs = segX2 / 2;
        std::size_t endBase = t + 14, startBase = endBase + segX2 + 2, deltaBase = startBase + segX2,
                   rangeBase = deltaBase + segX2;
        for (std::size_t i = 0; i < segs; ++i) {
            if (endBase + i * 2 + 2 > font.size()) break;
            uint32_t end = u16(endBase + i * 2);
            if (cp > end) continue;
            uint32_t start = u16(startBase + i * 2);
            if (cp < start) break;
            int delta = int16_t(u16(deltaBase + i * 2));
            uint32_t ro = u16(rangeBase + i * 2);
            if (ro == 0) { gid = int((cp + uint32_t(delta)) & 0xFFFF); return gid != 0; }
            std::size_t g = rangeBase + i * 2 + ro + (cp - start) * 2;
            if (g + 2 > font.size()) break;
            uint32_t v = u16(g);
            if (v == 0) { gid = 0; return false; }
            gid = int((v + uint32_t(delta)) & 0xFFFF);
            return gid != 0;
        }
    }
    err = "cmap 里没有这个字符";
    return false;
}

bool cffGlyphPath(const std::vector<uint8_t> &font, int gid, CffPath &out, std::string &err) {
    out = CffPath();
    CffInfo ci;
    std::string e0;
    if (!cffReadInfo(font, ci, e0)) { err = e0; return false; }
    CffIdx cs;
    if (!readIndex(font, ci.off + std::size_t(ci.charStringsOff), cs)) { err = "CharStrings INDEX 损坏"; return false; }
    if (gid < 0 || std::size_t(gid) >= cs.count) { err = "字形号越界"; return false; }
    auto item = [&](const CffIdx &ix, std::size_t k) {
        std::size_t a = ix.dataStart + ix.offs[k];
        std::size_t b = ix.dataStart + ix.offs[k + 1];
        return std::vector<uint8_t>(font.begin() + a, font.begin() + b);
    };
    // 局部子程序
    std::vector<std::vector<uint8_t>> local;
    if (ci.privateOff > 0 && ci.subrsOff >= 0) {
        CffIdx lix;
        if (readIndex(font, ci.off + std::size_t(ci.privateOff) + std::size_t(ci.subrsOff), lix)) {
            for (std::size_t k = 0; k < lix.count; ++k) local.push_back(item(lix, k));
        }
    }
    auto bias = [](std::size_t n) { return n < 1240 ? 107 : (n < 33900 ? 1131 : 32768); };
    const int lbias = bias(local.size());
    double x = 0, y = 0;
    double x0 = 0, y0 = 0;                 // 当前轮廓起点
    int nStems = 0;
    int depth = 0;
    // 采样三次贝塞尔, 记录包围盒点
    auto cubic = [&](double x1, double y1, double x2, double y2, double x3, double y3) {
        for (int i = 1; i <= 8; ++i) {
            double t = i / 8.0, u = 1 - t;
            double px = u*u*u*x + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x3;
            double py = u*u*u*y + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y3;
            out.pts.push_back({px, py});
        }
        x = x3; y = y3;
        out.ends.push_back({x, y});
    };
    std::function<void(const std::vector<uint8_t> &)> run = [&](const std::vector<uint8_t> &cs2) {
        if (++depth > 10) return;
        std::vector<double> st;
        std::size_t i = 0;
        while (i < cs2.size()) {
            uint8_t b = cs2[i];
            if (b >= 32 || b == 28) {
                if (b == 28) { st.push_back(double(int16_t((uint16_t(cs2[i+1])<<8) | cs2[i+2]))); i += 3; }
                else if (b < 247) { st.push_back(double(int(b) - 139)); i += 1; }
                else if (b < 251) { st.push_back(double((int(b)-247)*256 + cs2[i+1] + 108)); i += 2; }
                else if (b < 255) { st.push_back(double(-(int(b)-251)*256 - cs2[i+1] - 108)); i += 2; }
                else { st.push_back(double(int32_t((uint32_t(cs2[i+1])<<24)|(uint32_t(cs2[i+2])<<16)|
                                                   (uint32_t(cs2[i+3])<<8)|cs2[i+4])) / 65536.0); i += 5; }
                continue;
            }
            i += 1;
            auto pop = [&]() { double v = st.empty() ? 0 : st.back(); if (!st.empty()) st.pop_back(); return v; };
            switch (b) {
                case 1: case 3: case 18: case 23:
                    nStems += int(st.size()) / 2; st.clear(); break;
                case 19: case 20:
                    nStems += int(st.size()) / 2; st.clear();
                    i += std::size_t((nStems + 7) / 8); break;      // 跳过掩码字节
                case 21: {                                            // rmoveto(dx dy)
                    double dy = pop(), dx = pop();
                    x += dx; y += dy; x0 = x; y0 = y;
                    out.pts.push_back({x, y}); out.ends.push_back({x, y});
                    st.clear(); break;
                }
                case 22: { double dx = pop(); x += dx; x0 = x; y0 = y;
                    out.pts.push_back({x, y}); out.ends.push_back({x, y}); st.clear(); break; }
                case 4: { double dy = pop(); y += dy; x0 = x; y0 = y;
                    out.pts.push_back({x, y}); out.ends.push_back({x, y}); st.clear(); break; }
                case 5:
                    for (std::size_t k = 0; k + 1 < st.size(); k += 2) {
                        x += st[k]; y += st[k+1]; out.pts.push_back({x, y}); out.ends.push_back({x, y});
                    }
                    st.clear(); break;
                case 6: case 7: {
                    bool horiz = (b == 6);
                    for (double v : st) {
                        if (horiz) x += v; else y += v;
                        out.pts.push_back({x, y}); out.ends.push_back({x, y});
                        horiz = !horiz;
                    }
                    st.clear(); break;
                }
                case 8:
                    for (std::size_t k = 0; k + 5 < st.size(); k += 6)
                        cubic(x + st[k], y + st[k+1], x + st[k] + st[k+2], y + st[k+1] + st[k+3],
                              x + st[k] + st[k+2] + st[k+4], y + st[k+1] + st[k+3] + st[k+5]);
                    st.clear(); break;
                case 24: {   // rcurveline
                    std::size_t k = 0;
                    for (; k + 5 < st.size() && st.size() - k > 2; k += 6)
                        cubic(x + st[k], y + st[k+1], x + st[k] + st[k+2], y + st[k+1] + st[k+3],
                              x + st[k] + st[k+2] + st[k+4], y + st[k+1] + st[k+3] + st[k+5]);
                    if (k + 1 < st.size()) { x += st[k]; y += st[k+1]; out.pts.push_back({x, y}); out.ends.push_back({x, y}); }
                    st.clear(); break;
                }
                case 26: case 27: {   // vvcurveto / hhcurveto
                    bool horiz = (b == 27);
                    std::size_t k = 0;
                    double d1 = 0;
                    if (st.size() % 4 == 1) { d1 = st[0]; k = 1; }       // 首个可选位移
                    for (; k + 3 < st.size(); k += 4) {
                        double c1a = st[k], c1b = st[k+1], c2a = st[k+2], c2b = st[k+3];
                        if (horiz) {          // hhcurveto: (dx1 dx2 dy2 dx3)
                            cubic(x + c1a, y, x + c1a + c2a, y + c2b, x + c1a + c2a + c2b, y + 0);
                        } else {              // vvcurveto: (dy1 dx2 dy2 dy3)
                            cubic(x, y + c1a, x + c2a, y + c1a + c2b, x + 0, y + c1a + c2b + 0);
                        }
                    }
                    (void)d1;
                    st.clear(); break;
                }
                case 30: case 31: {   // vhcurveto / hvcurveto
                    bool horiz = (b == 31);
                    std::size_t k = 0;
                    while (k + 3 < st.size()) {
                        if (horiz) cubic(x + st[k], y, x + st[k] + st[k+1], y + st[k+2], x + st[k] + st[k+1] + st[k+3], y + st[k+2]);
                        else cubic(x, y + st[k], x + st[k+1], y + st[k] + st[k+2], x + st[k+1], y + st[k] + st[k+2] + st[k+3]);
                        horiz = !horiz; k += 4;
                    }
                    st.clear(); break;
                }
                case 10: case 29: {
                    int idx = int(pop()) + (b == 10 ? lbias : 0);
                    ++out.subrCalls;
                    if (b == 10 && idx >= 0 && std::size_t(idx) < local.size()) run(local[std::size_t(idx)]);
                    st.clear(); break;
                }
                case 11: case 14:
                    --depth; return;
                default:
                    st.clear(); break;
            }
        }
        --depth;
    };
    run(item(cs, std::size_t(gid)));
    if (out.pts.empty()) { err = "这个字形没有可用的轮廓点"; return false; }
    return true;
}

bool TrueTypeFont::hasGvar() const {
    std::size_t len = 0;
    return tableOff("gvar", &len) != 0 && len >= 20;
}

bool TrueTypeFont::glyphGvarTuples(int glyph, std::vector<GvarTuple> &out) const {
    out.clear();
    std::size_t gvarLen = 0;
    std::size_t gvar = tableOff("gvar", &gvarLen);
    if (!gvar || gvarLen < 20) return false;
    const uint8_t *base = data_.data() + gvar;
    auto be16 = [&](std::size_t i) { return uint16_t((uint16_t(base[i]) << 8) | base[i + 1]); };
    auto be32 = [&](std::size_t i) {
        return uint32_t((uint32_t(base[i]) << 24) | (uint32_t(base[i + 1]) << 16) |
                        (uint32_t(base[i + 2]) << 8) | uint32_t(base[i + 3]));
    };
    std::size_t axisCount = be16(4);
    std::size_t sharedCount = be16(6);
    std::size_t sharedOff = be32(8);
    std::size_t glyphCount = be16(12);
    std::size_t flags = be16(14);
    std::size_t dataArrayOff = be32(16);
    if (glyph < 0 || std::size_t(glyph) >= glyphCount) return false;
    const std::size_t entrySize = (flags & 1) ? 4 : 2;
    std::size_t offBase = 20;   // 偏移数组紧跟头部
    std::size_t p0 = offBase + std::size_t(glyph) * entrySize;
    std::size_t p1 = p0 + entrySize;
    if (p1 + entrySize > gvarLen) return false;
    auto readOff = [&](std::size_t p) -> std::size_t {
        if (entrySize == 4) return be32(p);
        return 2 * std::size_t(be16(p));   // 短偏移要乘 2
    };
    std::size_t o0 = readOff(p0), o1 = readOff(p1);
    if (o1 <= o0) return false;                      // 该字形没有变体数据
    std::size_t gd = dataArrayOff + o0;              // 字形变体数据起点
    std::size_t gdLen = o1 - o0;
    if (gd + 4 > gvarLen || gdLen < 4) return false;
    const uint8_t *g = data_.data() + gvar + gd;
    auto g16 = [&](std::size_t i) { return uint16_t((uint16_t(g[i]) << 8) | g[i + 1]); };
    std::size_t tvc = g16(0);
    std::size_t tupleCount = tvc & 0x0FFF;
    bool sharedPoints = (tvc & 0x8000) != 0;
    std::vector<int> sharedPts;
    std::size_t serOff = g16(2);
    std::size_t dataCursor = serOff;   // 序列化数据游标(相对 gd)
    std::size_t pos = 4;
    for (std::size_t t = 0; t < tupleCount; ++t) {
        if (pos + 4 > gdLen) return false;
        GvarTuple gt;
        gt.dataSize = g16(pos);
        std::size_t ti = g16(pos + 2);
        pos += 4;
        gt.embeddedPeak = (ti & 0x8000) != 0;
        gt.intermediate = (ti & 0x4000) != 0;
        gt.sharedIndex = gt.embeddedPeak ? -1 : int(ti & 0x0FFF);
        auto readTuple = [&](std::vector<double> &dst) -> bool {
            if (pos + axisCount * 2 > gdLen) return false;
            dst.clear();
            for (std::size_t a = 0; a < axisCount; ++a) {
                int16_t v = int16_t((uint16_t(g[pos]) << 8) | g[pos + 1]);
                pos += 2;
                dst.push_back(double(v) / 16384.0);   // F2Dot14
            }
            return true;
        };
        if (gt.embeddedPeak) {
            if (!readTuple(gt.peak)) return false;
        } else {
            // 共享元组: 在 sharedTuples 区域按轴数取
            if (gt.sharedIndex < 0 || std::size_t(gt.sharedIndex) >= sharedCount) return false;
            std::size_t sp = sharedOff + std::size_t(gt.sharedIndex) * axisCount * 2;
            if (sp + axisCount * 2 > gvarLen) return false;
            gt.peak.clear();
            for (std::size_t a = 0; a < axisCount; ++a) {
                int16_t v = int16_t((uint16_t(base[sp]) << 8) | base[sp + 1]);
                sp += 2;
                gt.peak.push_back(double(v) / 16384.0);
            }
        }
        if (gt.intermediate) {
            if (!readTuple(gt.start) || !readTuple(gt.end)) return false;
        } else {
            gt.start = gt.peak;
            gt.end = gt.peak;
        }
        gt.privatePoints = (ti & 0x2000) != 0;
        gt.dataOffset = dataCursor;   // 该元组序列化数据的起点(相对 gd)
        // 序列化数据: 点数表 + X 增量 + Y 增量。
        // 点数约定在真实字体上不容易一口咬定(是否含 4 个幻影点、复合字形等), 所以用**数据自证**:
        // 每个元组都声明了 variationDataSize, 我对候选点数逐个试, 谁**恰好**消费完声明的字节数就用谁。
        std::size_t cur = gd + dataCursor;   // 序列化数据游标
        std::size_t tupleStart = cur;
        bool pointsHere = gt.privatePoints || !sharedPoints || t == 0;
        if (pointsHere) {
            if (cur + 1 > gvarLen) { out.push_back(gt); continue; }
            std::size_t first = g[cur - gd];
            ++cur;
            std::size_t count = first;
            bool wide = false;
            if (first & 0x80) {
                if (cur + 1 > gvarLen) { out.push_back(gt); continue; }
                count = ((first & 0x7F) << 8) | g[cur - gd];
                ++cur;
                wide = true;
            }
            gt.points.clear();
            bool listOk = true;
            if (count > 0) {
                if (cur + count * (wide ? 2 : 1) > gvarLen) listOk = false;
                for (std::size_t k = 0; k < count && listOk; ++k) {
                    std::size_t v;
                    if (wide) {
                        v = (std::size_t(g[cur - gd]) << 8) | g[cur - gd + 1];
                        cur += 2;
                    } else {
                        v = g[cur - gd];
                        ++cur;
                    }
                    gt.points.push_back(int(v));
                }
            }
            if (!listOk) { out.push_back(gt); continue; }
            if (t == 0 && sharedPoints && !gt.privatePoints) sharedPts = gt.points;
        } else {
            gt.points = sharedPts;
        }
        // 候选点数: 显式点数表就只有一个答案; 全点情形按常见约定与 glyf 读出的点数组合试
        std::vector<std::size_t> cands;
        if (!gt.points.empty()) {
            cands.push_back(gt.points.size());
        } else {
            int pc = glyphPointCount(glyph);
            if (pc < 0) {   // 复合字形等: 这轮不做点增量, 只保留元数据
                out.push_back(gt);
                continue;
            }
            // 点数约定(是否含 4 个幻影点、复合字形等)在真实字体上难以一口咬定, 所以直接在
            // glyf 读出的点数附近搜: 谁能恰好消费完声明的 variationDataSize, 谁就是答案。
            cands.push_back(std::size_t(pc) + 4);   // 规范约定: 字形点数 + 4 个幻影点(优先)
            for (std::size_t n = std::size_t(pc > 4 ? pc - 4 : 0); n <= std::size_t(pc) + 4; ++n)
                if (n > 0) cands.push_back(n);
        }
        std::size_t afterPoints = cur;          // 点数表结束处
        std::size_t after = cur;
        bool decoded = false;
        std::vector<std::size_t> hits;
        std::size_t bestAfter = cur;
        std::vector<double> bestDx, bestDy;
        for (std::size_t nPts : cands) {
            std::size_t c = cur;
            std::vector<double> dxv, dyv;
            bool okRun = true;
            for (int axis = 0; axis < 2 && okRun; ++axis) {
                std::vector<double> &dst = axis ? dyv : dxv;
                std::size_t got = 0;
                while (got < nPts) {
                    if (c + 1 > gvarLen) { okRun = false; break; }
                    unsigned char ctrl = g[c - gd];
                    ++c;
                    // 关键: 游程长度是 (ctrl & 0x3F) + 1 —— 不是 ctrl & 0x3F。
                    // 之前按后者读, 每个游程少 1 个值, 于是怎么都对不上 dataSize。
                    std::size_t run = (ctrl & 0x3F) + 1;
                    if (ctrl & 0x80) {
                        for (std::size_t k = 0; k < run; ++k) dst.push_back(0);
                    } else if (ctrl & 0x40) {
                        if (c + run * 2 > gvarLen) { okRun = false; break; }
                        for (std::size_t k = 0; k < run; ++k) {
                            int16_t v = int16_t((uint16_t(g[c - gd]) << 8) | g[c - gd + 1]);
                            c += 2;
                            dst.push_back(double(v));
                        }
                    } else {
                        if (c + run > gvarLen) { okRun = false; break; }
                        for (std::size_t k = 0; k < run; ++k) {
                            int8_t v = int8_t(g[c - gd]);
                            ++c;
                            dst.push_back(double(v));
                        }
                    }
                    got += run;
                }
            }
            if (!okRun) continue;
            // 关键校验: 必须恰好消费完该元组声明的字节数。
            // 两种记账都试: 点数表计入本元组长度, 或点数表不计入(共享点数常如此)
            bool sizeOk = (c - tupleStart == gt.dataSize) || (c - afterPoints == gt.dataSize);
            if (!sizeOk) continue;
            hits.push_back(nPts);
            bestAfter = c;
            bestDx = dxv;
            bestDy = dyv;
        }
        // 规范约定(pc+4)优先; 否则必须唯一命中才放行
        bool preferHit = !hits.empty() && hits[0] == std::size_t(glyphPointCount(glyph)) + 4;
        if (preferHit || hits.size() == 1) {
            gt.dx = bestDx;
            gt.dy = bestDy;
            after = bestAfter;
            decoded = true;
        }
        if (decoded) dataCursor = after - gd;
        out.push_back(gt);
    }
    return !out.empty();
}

bool TrueTypeFont::variations(std::vector<VarAxis> &axes, std::vector<VarInstance> &insts) const {
    axes.clear();
    insts.clear();
    std::size_t len = 0;
    std::size_t off = tableOff("fvar", &len);
    if (!off || len < 16) return false;
    const uint8_t *p = data_.data() + off;
    // 注意: OpenType 表里都是**大端序**(我第一版写成小端, 读出来全是垃圾 -> 误判成非可变字体)
    auto rd16 = [&](std::size_t i) { return uint16_t((uint16_t(p[i]) << 8) | p[i + 1]); };
    auto rd32 = [&](std::size_t i) {
        return int32_t((uint32_t(p[i]) << 24) | (uint32_t(p[i + 1]) << 16) | (uint32_t(p[i + 2]) << 8) |
                       uint32_t(p[i + 3]));
    };
    std::size_t dataOff = rd16(4);
    std::size_t axisCount = rd16(8);
    std::size_t axisSize = rd16(10);
    std::size_t instCount = rd16(12);
    std::size_t instSize = rd16(14);
    if (axisCount == 0 || axisSize < 20 || dataOff + axisCount * axisSize > len) return false;
    for (std::size_t i = 0; i < axisCount; ++i) {
        std::size_t a = dataOff + i * axisSize;
        VarAxis ax;
        ax.tag.assign(reinterpret_cast<const char *>(p + a), 4);
        ax.minV = fixed16(rd32(a + 4));
        ax.defV = fixed16(rd32(a + 8));
        ax.maxV = fixed16(rd32(a + 12));
        ax.name = ttNameById(data_, rd16(a + 18));
        axes.push_back(ax);
    }
    if (instSize >= 4 + axisCount * 4 && instCount > 0) {
        std::size_t instBase = dataOff + axisCount * axisSize;
        if (instBase + instCount * instSize <= len) {
            for (std::size_t i = 0; i < instCount; ++i) {
                std::size_t a = instBase + i * instSize;
                VarInstance in;
                in.name = ttNameById(data_, rd16(a));
                for (std::size_t k = 0; k < axisCount; ++k)
                    in.coords.push_back(fixed16(rd32(a + 4 + k * 4)));
                insts.push_back(in);
            }
        }
    }
    return !axes.empty();
}

bool TrueTypeFont::hasVariations() const {
    std::vector<VarAxis> a;
    std::vector<VarInstance> i;
    return variations(a, i);
}

std::size_t TrueTypeFont::tableOff(const char *tag, std::size_t *len) const {
    if (data_.size() < 12) return 0;
    uint16_t num = rdU16(&data_[4]);
    for (uint16_t i = 0; i < num; ++i) {
        std::size_t rec = 12 + std::size_t(i) * 16;
        if (!inRange(rec, 16, data_.size())) return 0;
        if (std::memcmp(&data_[rec], tag, 4) == 0) {
            uint32_t off = rdU32(&data_[rec + 8]);
            uint32_t l = rdU32(&data_[rec + 12]);
            if (!inRange(off, l, data_.size())) return 0;
            if (len) *len = l;
            return off;
        }
    }
    return 0;
}

static std::string ttNameById(const std::vector<uint8_t> &data, uint16_t wantId) {
    if (data.size() < 12) return "";
    uint16_t num = rdU16(&data[4]);
    std::size_t nameOff = 0, nameLen = 0;
    for (uint16_t i = 0; i < num; ++i) {
        std::size_t rec = 12 + std::size_t(i) * 16;
        if (!inRange(rec, 16, data.size())) return "";
        if (std::memcmp(&data[rec], "name", 4) == 0) {
            nameOff = rdU32(&data[rec + 8]);
            nameLen = rdU32(&data[rec + 12]);
            break;
        }
    }
    if (!nameOff || !inRange(nameOff, nameLen, data.size()) || nameLen < 6) return "";
    const uint8_t *t = &data[nameOff];
    uint16_t count = rdU16(t + 2);
    uint16_t strOff = rdU16(t + 4);
    for (uint16_t i = 0; i < count; ++i) {
        std::size_t rec = 6 + std::size_t(i) * 12;
        if (!inRange(nameOff + rec, 12, data.size())) break;
        const uint8_t *r = t + rec;
        uint16_t plat = rdU16(r), enc = rdU16(r + 2), lang = rdU16(r + 4), id = rdU16(r + 6);
        uint16_t len = rdU16(r + 8), off = rdU16(r + 10);
        if (id != wantId) continue; // 1 = family
        std::size_t abs = nameOff + strOff + off;
        if (!inRange(abs, len, data.size())) continue;
        std::string out;
        if (plat == 3 || (plat == 0)) { // UTF-16BE
            for (std::size_t k = 0; k + 1 < len; k += 2) {
                uint32_t c = uint32_t(rdU16(&data[abs + k]));
                if (c < 0x80) out += char(c);
                else if (c < 0x800) {
                    out += char(0xC0 | (c >> 6));
                    out += char(0x80 | (c & 0x3F));
                } else {
                    out += char(0xE0 | (c >> 12));
                    out += char(0x80 | ((c >> 6) & 0x3F));
                    out += char(0x80 | (c & 0x3F));
                }
            }
        } else {
            for (std::size_t k = 0; k < len; ++k) out += char(data[abs + k]);
        }
        if (!out.empty()) return out;
        (void)enc; (void)lang;
    }
    return "";
}

std::string ttFamilyName(const std::vector<uint8_t> &data) { return ttNameById(data, 1); }

bool TrueTypeFont::load(std::vector<uint8_t> d, std::string &err) {
    data_ = std::move(d);
    ok_ = false;
    family_.clear();
    if (data_.size() < 12) {
        err = "字体文件太短";
        return false;
    }
    uint32_t tag = rdU32(&data_[0]);
    if (tag == 0x4F54544Fu) { // 'OTTO' = CFF 轮廓
        err = "这是 CFF/OTF 轮廓字体, 暂不支持(请用 TrueType/glyf 的 .ttf)";
        return false;
    }
    if (tag != 0x00010000u && tag != 0x74727565u) { // 1.0 或 'true'
        err = "不是 TrueType 字体(sfnt 版本不支持)";
        return false;
    }
    std::size_t len = 0;
    std::size_t head = tableOff("head", &len);
    if (!head || len < 54) {
        err = "字体缺少 head 表";
        return false;
    }
    upem_ = rdU16(&data_[head + 18]);
    if (upem_ <= 0) upem_ = 1000;
    indexToLoc_ = rdI16(&data_[head + 50]);
    std::size_t maxp = tableOff("maxp", &len);
    if (!maxp || len < 6) {
        err = "字体缺少 maxp 表";
        return false;
    }
    numGlyphs_ = rdU16(&data_[maxp + 4]);
    if (numGlyphs_ <= 0) {
        err = "字体没有任何字形";
        return false;
    }
    std::size_t hhea = tableOff("hhea", &len);
    if (hhea && len >= 36) {
        ascent_ = rdI16(&data_[hhea + 4]);
        descent_ = rdI16(&data_[hhea + 6]);
        lineGap_ = rdI16(&data_[hhea + 8]);
        numHMetrics_ = rdU16(&data_[hhea + 34]);
    }
    locaOff_ = tableOff("loca", &locaLen_);
    glyfOff_ = tableOff("glyf", &glyfLen_);
    if (!locaOff_ || !glyfOff_) {
        err = "字体缺少 glyf/loca 表(可能是 CFF 字体)";
        return false;
    }
    std::size_t want = std::size_t(numGlyphs_ + 1) * (indexToLoc_ ? 4 : 2);
    if (locaLen_ < want) {
        err = "loca 表长度不足";
        return false;
    }
    hmtxOff_ = tableOff("hmtx", &hmtxLen_);
    if (numHMetrics_ <= 0) numHMetrics_ = numGlyphs_;
    cmapSub_ = 0;
    if (!parseCmap(err)) return false;
    family_ = ttFamilyName(data_);
    ok_ = true;
    return true;
}

bool TrueTypeFont::parseCmap(std::string &err) {
    std::size_t len = 0;
    std::size_t cmap = tableOff("cmap", &len);
    if (!cmap || len < 4) {
        err = "字体缺少 cmap 表";
        return false;
    }
    uint16_t n = rdU16(&data_[cmap + 2]);
    int bestScore = -1;
    for (uint16_t i = 0; i < n; ++i) {
        std::size_t rec = cmap + 4 + std::size_t(i) * 8;
        if (!inRange(rec, 8, data_.size())) break;
        uint16_t plat = rdU16(&data_[rec]);
        uint16_t enc = rdU16(&data_[rec + 2]);
        uint32_t off = rdU32(&data_[rec + 4]);
        std::size_t sub = cmap + off;
        if (!inRange(sub, 2, data_.size())) continue;
        int fmt = rdU16(&data_[sub]);
        if (fmt != 0 && fmt != 4 && fmt != 6 && fmt != 12) continue;
        if (!inRange(sub, fmt == 12 ? 16 : (fmt == 4 ? 14 : 4), data_.size())) continue;
        int score = 0;
        bool sym = false;
        if (plat == 3 && enc == 10 && fmt == 12) score = 100;
        else if ((plat == 0) && fmt == 12) score = 90;
        else if (plat == 3 && enc == 1 && fmt == 4) score = 80;
        else if (plat == 0 && fmt == 4) score = 70;
        else if (plat == 1 && fmt == 0) score = 40;
        else if (plat == 3 && enc == 0) { score = 10; sym = true; }   // 符号字体
        else if (fmt == 12) score = 60;
        else if (fmt == 4) score = 50;
        else if (fmt == 6) score = 30;
        if (score > bestScore) {
            bestScore = score;
            cmapSub_ = sub;
            cmapFormat_ = fmt;
            cmapSymbol_ = sym;
        }
    }
    if (!cmapSub_) {
        err = "字体没有可用的 Unicode cmap 子表";
        return false;
    }
    return true;
}

int TrueTypeFont::glyphIndex(uint32_t cp) const {
    if (!cmapSub_) return 0;
    if (cmapSymbol_) cp = 0xF000u | (cp & 0xFFu);
    const uint8_t *t = &data_[cmapSub_];
    if (cmapFormat_ == 0) {
        if (cp > 255) return 0;
        return t[6 + cp];
    }
    if (cmapFormat_ == 6) {
        uint16_t first = rdU16(t + 6);
        uint16_t count = rdU16(t + 8);
        if (cp < first || cp >= uint32_t(first) + count) return 0;
        return rdU16(t + 10 + 2 * (cp - first));
    }
    if (cmapFormat_ == 4) {
        uint16_t segX2 = rdU16(t + 6);
        int segs = segX2 / 2;
        const uint8_t *endC = t + 14;
        const uint8_t *startC = endC + segX2 + 2;
        const uint8_t *idDelta = startC + segX2;
        const uint8_t *idRange = idDelta + segX2;
        // endCode 递增, 二分找第一个 endCode >= cp
        int lo = 0, hi = segs - 1, found = -1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (rdU16(endC + 2 * mid) >= cp) {
                found = mid;
                hi = mid - 1;
            } else {
                lo = mid + 1;
            }
        }
        if (found < 0) return 0;
        uint16_t sc = rdU16(startC + 2 * found);
        if (cp < sc) return 0;
        uint16_t ro = rdU16(idRange + 2 * found);
        int16_t delta = rdI16(idDelta + 2 * found);
        if (ro == 0) return uint16_t(cp + delta);
        // 指针算术: 从 idRange[found] 出发, 跳过 ro 字节再走 (cp-sc)*2
        const uint8_t *addr = idRange + 2 * found + ro + 2 * (cp - sc);
        if (addr + 2 > data_.data() + data_.size()) return 0;
        uint16_t g = rdU16(addr);
        if (g == 0) return 0;
        return uint16_t(g + delta);
    }
    if (cmapFormat_ == 12) {
        uint32_t nGroups = rdU32(t + 12);
        const uint8_t *g = t + 16;
        int lo = 0, hi = int(nGroups) - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const uint8_t *e = g + std::size_t(mid) * 12;
            uint32_t s = rdU32(e), en = rdU32(e + 4), sg = rdU32(e + 8);
            if (cp < s) hi = mid - 1;
            else if (cp > en) lo = mid + 1;
            else return int(sg + (cp - s));
        }
        return 0;
    }
    return 0;
}

int TrueTypeFont::advance(int glyph) const {
    if (!hmtxOff_ || glyph < 0 || glyph >= numGlyphs_) return upem_ / 2;
    int idx = std::min(glyph, numHMetrics_ - 1);
    if (idx < 0) idx = 0;
    std::size_t off = hmtxOff_ + std::size_t(idx) * 4;
    if (!inRange(off, 2, data_.size())) return upem_ / 2;
    return rdU16(&data_[off]);
}

TtBox TrueTypeFont::glyphBox(int glyph) const {
    TtBox b;
    if (glyph < 0 || glyph >= numGlyphs_) return b;
    // 注意: loca 里 0 是合法偏移(字形数据就在 glyf 开头), 空字形要看 start==end
    std::size_t g = 0, e = 0;
    if (indexToLoc_) {
        if (!inRange(locaOff_ + std::size_t(glyph) * 4, 8, data_.size())) return b;
        g = rdU32(&data_[locaOff_ + std::size_t(glyph) * 4]);
        e = rdU32(&data_[locaOff_ + std::size_t(glyph) * 4 + 4]);
    } else {
        if (!inRange(locaOff_ + std::size_t(glyph) * 2, 4, data_.size())) return b;
        g = std::size_t(rdU16(&data_[locaOff_ + std::size_t(glyph) * 2])) * 2;
        e = std::size_t(rdU16(&data_[locaOff_ + std::size_t(glyph) * 2 + 2])) * 2;
    }
    if (e <= g) return b; // 空字形
    if (!inRange(glyfOff_ + g, 10, data_.size())) return b;
    const uint8_t *h = &data_[glyfOff_ + g];
    b.x0 = rdI16(h + 2);
    b.y0 = rdI16(h + 4);
    b.x1 = rdI16(h + 6);
    b.y1 = rdI16(h + 8);
    b.valid = true;
    return b;
}

namespace {

struct GlyfView {
    const uint8_t *p = nullptr;
    std::size_t n = 0;
    std::size_t pos = 0;
    bool ok = true;
    uint8_t u8() {
        if (pos + 1 > n) { ok = false; return 0; }
        return p[pos++];
    }
    uint16_t u16() {
        if (pos + 2 > n) { ok = false; return 0; }
        uint16_t v = rdU16(p + pos);
        pos += 2;
        return v;
    }
    int16_t i16() { return int16_t(u16()); }
    void skip(std::size_t k) {
        if (pos + k > n) { ok = false; pos = n; return; }
        pos += k;
    }
};

// 把 TrueType 点序列(带 on/off 标志)转成线段/二次贝塞尔
void pointsToContour(const std::vector<TtPoint> &pts, TtContour &out) {
    std::size_t n = pts.size();
    if (n == 0) return;
    // 找起点: 第一个 on 点; 全是 off 点时取首尾中点
    std::size_t start = n;
    for (std::size_t i = 0; i < n; ++i)
        if (pts[i].on) {
            start = i;
            break;
        }
    TtPoint cur;
    std::size_t i;
    if (start == n) {
        cur.x = (pts[0].x + pts[n - 1].x) / 2;
        cur.y = (pts[0].y + pts[n - 1].y) / 2;
        i = 0;
    } else {
        cur = pts[start];
        i = start + 1;
    }
    TtPoint pending;
    bool hasPending = false;
    for (std::size_t k = 0; k < n; ++k, ++i) {
        if (i >= n) i = 0;
        const TtPoint &q = pts[i];
        if (q.on) {
            TtSeg s;
            if (hasPending) {
                s.kind = TtKind::Quad;
                s.p0 = cur;
                s.c = pending;
                s.p1 = q;
                hasPending = false;
            } else {
                s.kind = TtKind::Line;
                s.p0 = cur;
                s.p1 = q;
            }
            out.segs.push_back(s);
            cur = q;
        } else {
            if (hasPending) {
                // 两个连续控制点: 插入隐含的中点
                TtSeg s;
                s.kind = TtKind::Quad;
                s.p0 = cur;
                s.c = pending;
                s.p1.x = (pending.x + q.x) / 2;
                s.p1.y = (pending.y + q.y) / 2;
                out.segs.push_back(s);
                cur = s.p1;
            }
            pending = q;
            hasPending = true;
        }
    }
    // 收尾: 回到起点
    if (hasPending) {
        TtSeg s;
        s.kind = TtKind::Quad;
        s.p0 = cur;
        s.c = pending;
        s.p1 = (start == n) ? TtPoint{(pts[0].x + pts[n - 1].x) / 2,
                                      (pts[0].y + pts[n - 1].y) / 2, true}
                            : pts[start];
        out.segs.push_back(s);
    }
}

} // namespace

bool TrueTypeFont::outline(int glyph, std::vector<TtContour> &out, std::string &err) const {
    out.clear();
    if (!ok_) {
        err = "字体未加载";
        return false;
    }
    if (glyph < 0 || glyph >= numGlyphs_) {
        err = "字形序号越界";
        return false;
    }
    std::size_t start = 0, end = 0;
    if (indexToLoc_) {
        if (!inRange(locaOff_ + std::size_t(glyph) * 4, 8, data_.size())) {
            err = "loca 越界";
            return false;
        }
        start = rdU32(&data_[locaOff_ + std::size_t(glyph) * 4]);
        end = rdU32(&data_[locaOff_ + std::size_t(glyph) * 4 + 4]);
    } else {
        if (!inRange(locaOff_ + std::size_t(glyph) * 2, 4, data_.size())) {
            err = "loca 越界";
            return false;
        }
        start = std::size_t(rdU16(&data_[locaOff_ + std::size_t(glyph) * 2])) * 2;
        end = std::size_t(rdU16(&data_[locaOff_ + std::size_t(glyph) * 2 + 2])) * 2;
    }
    if (end <= start) return true; // 空字形(如空格)
    if (end > glyfLen_ || !inRange(glyfOff_ + start, end - start, data_.size())) {
        err = "glyf 数据越界";
        return false;
    }
    const uint8_t *g = &data_[glyfOff_ + start];
    std::size_t gn = end - start;
    if (gn < 10) {
        err = "字形数据太短";
        return false;
    }
    int16_t numContours = rdI16(g);
    if (numContours >= 0) {
        GlyfView v{g, gn, 10, true};
        std::vector<uint16_t> ends(static_cast<std::size_t>(numContours));
        for (int i = 0; i < numContours; ++i) ends[static_cast<std::size_t>(i)] = v.u16();
        uint16_t insLen = v.u16();
        v.skip(insLen);
        if (!v.ok) {
            err = "简单字形头损坏";
            return false;
        }
        int total = numContours ? int(ends.back()) + 1 : 0;
        std::vector<uint8_t> flags;
        flags.reserve(static_cast<std::size_t>(total));
        while (int(flags.size()) < total) {
            uint8_t f = v.u8();
            if (!v.ok) break;
            flags.push_back(f);
            if (f & 0x08) { // REPEAT
                uint8_t rep = v.u8();
                for (uint8_t k = 0; k < rep && int(flags.size()) < total; ++k) flags.push_back(f);
            }
        }
        if (!v.ok || int(flags.size()) < total) {
            err = "简单字形标志位损坏";
            return false;
        }
        std::vector<int32_t> xs(static_cast<std::size_t>(total)), ys(static_cast<std::size_t>(total));
        int32_t acc = 0;
        for (int i = 0; i < total; ++i) {
            uint8_t f = flags[static_cast<std::size_t>(i)];
            if (f & 0x02) {
                uint8_t d = v.u8();
                acc += (f & 0x10) ? int32_t(d) : -int32_t(d);
            } else if (!(f & 0x10)) {
                acc += v.i16();
            }
            xs[static_cast<std::size_t>(i)] = acc;
        }
        acc = 0;
        for (int i = 0; i < total; ++i) {
            uint8_t f = flags[static_cast<std::size_t>(i)];
            if (f & 0x04) {
                uint8_t d = v.u8();
                acc += (f & 0x20) ? int32_t(d) : -int32_t(d);
            } else if (!(f & 0x20)) {
                acc += v.i16();
            }
            ys[static_cast<std::size_t>(i)] = acc;
        }
        if (!v.ok) {
            err = "简单字形坐标损坏";
            return false;
        }
        std::vector<TtPoint> pts(static_cast<std::size_t>(total));
        for (int i = 0; i < total; ++i)
            pts[static_cast<std::size_t>(i)] = TtPoint{double(xs[static_cast<std::size_t>(i)]),
                                                       double(ys[static_cast<std::size_t>(i)]),
                                                       (flags[static_cast<std::size_t>(i)] & 0x01) != 0};
        int from = 0;
        for (int c = 0; c < numContours; ++c) {
            int to = int(ends[static_cast<std::size_t>(c)]);
            if (to < from) {
                err = "轮廓端点乱序";
                return false;
            }
            std::vector<TtPoint> sub(pts.begin() + from, pts.begin() + to + 1);
            TtContour ctr;
            pointsToContour(sub, ctr);
            if (!ctr.segs.empty()) out.push_back(std::move(ctr));
            from = to + 1;
        }
        return true;
    }

    // 复合字形
    if (numContours != -1) {
        err = "未知的字形类型";
        return false;
    }
    struct Frame {
        double a = 1, b = 0, c = 0, d = 1, dx = 0, dy = 0;
    };
    std::vector<Frame> stack;
    stack.push_back(Frame{});
    int depth = 0;
    std::size_t pos = 10;
    for (;;) {
        if (++depth > 8) {
            err = "复合字形嵌套太深";
            return false;
        }
        bool haveMore = true;
        while (haveMore) {
            if (pos + 4 > gn) {
                err = "复合字形数据损坏";
                return false;
            }
            uint16_t flags = rdU16(g + pos);
            uint16_t comp = rdU16(g + pos + 2);
            pos += 4;
            int32_t arg1 = 0, arg2 = 0;
            if (flags & 0x0001) { // words
                if (pos + 4 > gn) { err = "复合字形参数损坏"; return false; }
                arg1 = rdI16(g + pos);
                arg2 = rdI16(g + pos + 2);
                pos += 4;
            } else {
                if (pos + 2 > gn) { err = "复合字形参数损坏"; return false; }
                arg1 = int8_t(g[pos]);
                arg2 = int8_t(g[pos + 1]);
                pos += 2;
            }
            double a = 1, b = 0, c = 0, d = 1;
            if (flags & 0x0008) {
                if (pos + 2 > gn) { err = "复合字形缩放损坏"; return false; }
                a = d = double(rdI16(g + pos)) / 16384.0;
                pos += 2;
            } else if (flags & 0x0040) {
                if (pos + 4 > gn) { err = "复合字形缩放损坏"; return false; }
                a = double(rdI16(g + pos)) / 16384.0;
                d = double(rdI16(g + pos + 2)) / 16384.0;
                pos += 4;
            } else if (flags & 0x0080) {
                if (pos + 8 > gn) { err = "复合字形变换损坏"; return false; }
                a = double(rdI16(g + pos)) / 16384.0;
                b = double(rdI16(g + pos + 2)) / 16384.0;
                c = double(rdI16(g + pos + 4)) / 16384.0;
                d = double(rdI16(g + pos + 6)) / 16384.0;
                pos += 8;
            }
            const Frame &f = stack.back();
            Frame nf;
            // 先应用组件自身的 2x2, 再套父变换
            nf.a = f.a * a + f.c * b;
            nf.b = f.b * a + f.d * b;
            nf.c = f.a * c + f.c * d;
            nf.d = f.b * c + f.d * d;
            double lx = 0, ly = 0;
            if (flags & 0x0002) { // ARGS_ARE_XY_VALUES
                lx = double(arg1);
                ly = double(arg2);
            }
            nf.dx = f.a * lx + f.c * ly + f.dx;
            nf.dy = f.b * lx + f.d * ly + f.dy;
            std::vector<TtContour> sub;
            std::string serr;
            if (!outline(comp, sub, serr)) {
                err = "复合字形引用的字形解析失败: " + serr;
                return false;
            }
            if (!(flags & 0x0002) && !sub.empty() && !out.empty()) {
                // 点匹配: 把组件第 arg1 个点对齐到父轮廓第 arg2 个点
                auto firstPoint = [](const std::vector<TtContour> &cs, int idx, TtPoint &p) {
                    int k = 0;
                    for (const auto &ct : cs)
                        for (const auto &s : ct.segs) {
                            if (k++ == idx) {
                                p = s.p0;
                                return true;
                            }
                        }
                    return false;
                };
                TtPoint cp, pp;
                if (firstPoint(sub, arg1, cp) && firstPoint(out, arg2, pp)) {
                    double tx = cp.x, ty = cp.y;
                    double wx = nf.a * tx + nf.c * ty + nf.dx;
                    double wy = nf.b * tx + nf.d * ty + nf.dy;
                    nf.dx += pp.x - wx;
                    nf.dy += pp.y - wy;
                }
            }
            for (auto &ct : sub) {
                for (auto &s : ct.segs) {
                    auto tf = [&](TtPoint &p) {
                        double x = nf.a * p.x + nf.c * p.y + nf.dx;
                        double y = nf.b * p.x + nf.d * p.y + nf.dy;
                        p.x = x;
                        p.y = y;
                    };
                    tf(s.p0);
                    if (s.kind == TtKind::Quad) tf(s.c);
                    tf(s.p1);
                }
                out.push_back(std::move(ct));
            }
            if (!(flags & 0x0020)) haveMore = false;
        }
        break;
    }
    return true;
}

} // namespace em

// EasyMath - 字形(文字→函数)测试
// 校验: 保真度(拟合结果必须贴住字体原始轮廓)、函数数不比字体自带分段多、
//       轮廓闭合/连续、精确系数确实精确、显式 y=f(x) 落在轮廓上、锚点写法解析。
// 没有字体文件时跳过(不算失败)。
#include "../src/glyph.hpp"
#include "../src/truetype.hpp"
#include "../src/zipfile.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace em;

static int g_pass = 0, g_fail = 0;
static void ok() { ++g_pass; }
static void bad(const std::string &n, const std::string &w) {
    ++g_fail;
    std::printf("FAIL: %s —— %s\n", n.c_str(), w.c_str());
}
static void expect(bool c, const std::string &n, const std::string &w = "") {
    if (c) ok(); else bad(n, w);
}

struct P { double x, y; };
static double segDist(P a, P b, P p) {
    double dx = b.x - a.x, dy = b.y - a.y, L = dx * dx + dy * dy;
    double t = L > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / L : 0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
}

// 原始解析轮廓 -> 折线(闭合)
static void analyticPolylines(const std::vector<TtContour> &cs, double scale, double ox, double oy,
                              std::vector<std::vector<P>> &out) {
    for (const auto &ct : cs) {
        std::vector<P> v;
        for (const auto &sg : ct.segs) {
            P a{sg.p0.x * scale + ox, sg.p0.y * scale + oy};
            P b{sg.p1.x * scale + ox, sg.p1.y * scale + oy};
            if (sg.kind == TtKind::Line) {
                v.push_back(a);
            } else {
                P c{sg.c.x * scale + ox, sg.c.y * scale + oy};
                for (int k = 0; k < 16; ++k) {
                    double t = double(k) / 16, u = 1 - t;
                    v.push_back(P{u * u * a.x + 2 * u * t * c.x + t * t * b.x,
                                  u * u * a.y + 2 * u * t * c.y + t * t * b.y});
                }
            }
        }
        if (!v.empty()) {
            v.push_back(v.front());
            out.push_back(v);
        }
    }
}

static double distToAll(const std::vector<std::vector<P>> &polys, P p) {
    double best = 1e18;
    for (const auto &v : polys)
        for (std::size_t j = 0; j + 1 < v.size(); ++j) best = std::min(best, segDist(v[j], v[j + 1], p));
    return best;
}

static uint32_t utf8First(const char *s) {
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
    if (p[0] < 0x80) return p[0];
    if ((p[0] >> 5) == 6) return uint32_t((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    if ((p[0] >> 4) == 14) return uint32_t((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return uint32_t((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
}

int main(int argc, char **argv) {
    std::string path = argc > 1 ? argv[1] : "/root/Math/vivo_Sans.zip";
    FILE *f0 = std::fopen(path.c_str(), "rb");
    if (!f0) {
        std::printf("字形测试: 跳过(没找到 %s)\n", path.c_str());
        return 0;
    }
    std::fclose(f0);
    std::vector<uint8_t> fontData;
    std::string err;
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".zip") == 0) {
        ZipArchive z;
        if (!z.openPath(path, err)) { std::printf("字形测试: 字体包打不开(%s)\n", err.c_str()); return 1; }
        auto ttfs = z.findSuffix(".ttf");
        if (ttfs.empty()) { std::printf("字形测试: 跳过(包里没有 ttf)\n"); return 0; }
        std::size_t pick = ttfs.front();
        std::size_t best = 0;
        for (std::size_t i : ttfs) {
            const std::string &n = z.entries()[i].nameAscii;
            if (n.find("Regular") != std::string::npos && z.entries()[i].size > best) {
                best = z.entries()[i].size;
                pick = i;
            }
        }
        if (!z.extract(pick, fontData, err)) { std::printf("字形测试: 解压失败(%s)\n", err.c_str()); return 1; }
    } else if (!readWholeFile(path, fontData, err)) {
        std::printf("字形测试: 读文件失败(%s)\n", err.c_str());
        return 1;
    }
    TrueTypeFont font;
    if (!font.load(fontData, err)) { std::printf("字形测试: 字体解析失败(%s)\n", err.c_str()); return 1; }

    const double size = 1000.0;
    const double scale = size / double(font.unitsPerEm());
    const double tol = 0.5;
    const char *samples[] = {"A", "B", "O", "g", "5", "@", "\u4e2d", "\u6c38", "\u4e00", "\u53e3",
                             "\u00b2", "\u221a", "\u6f22", "\u03c0"};
    for (const char *ch : samples) {
        uint32_t cp = utf8First(ch);
        int gid = font.glyphIndex(cp);
        if (!gid) continue;
        std::vector<TtContour> cs;
        std::string e2;
        expect(font.outline(gid, cs, e2), std::string("取原始轮廓 ") + ch, e2);
        int srcSegs = 0;
        for (const auto &c : cs) srcSegs += int(c.segs.size());
        GlyphShape sh;
        if (!shapeFromGlyph(font, cp, size, 0, 0, tol, sh, e2)) {
            bad(std::string("还原 ") + ch, e2);
            continue;
        }
        ok();
        // 1) 函数数不超过字体自带分段
        expect(sh.segCount() <= srcSegs + 1,
               std::string("函数数不增 ") + ch,
               std::to_string(sh.segCount()) + " > " + std::to_string(srcSegs));
        // 2) 每段连续 + 每条轮廓闭合
        bool cont = true, closed = true;
        for (const auto &c : sh.contours) {
            if (c.segs.empty()) cont = false;
            for (std::size_t k = 1; k < c.segs.size(); ++k)
                if (std::fabs(c.segs[k].x0 - c.segs[k - 1].x1) > 1e-6 ||
                    std::fabs(c.segs[k].y0 - c.segs[k - 1].y1) > 1e-6)
                    cont = false;
            if (!c.segs.empty() &&
                (std::fabs(c.segs.front().x0 - c.segs.back().x1) > 1e-6 ||
                 std::fabs(c.segs.front().y0 - c.segs.back().y1) > 1e-6))
                closed = false;
        }
        expect(cont, std::string("轮廓连续 ") + ch, "相邻段端点不接");
        expect(closed, std::string("轮廓闭合 ") + ch, "首尾没接上");
        // 3) 保真度: 在容差内贴住原始轮廓
        std::vector<std::vector<P>> polys;
        analyticPolylines(cs, scale, 0, 0, polys);
        double worst = 0;
        for (const auto &c : sh.contours)
            for (const auto &s : c.segs)
                for (int k = 0; k <= 24; ++k) {
                    double t = double(k) / 24;
                    P p{((s.cx[3] * t + s.cx[2]) * t + s.cx[1]) * t + s.cx[0],
                        ((s.cy[3] * t + s.cy[2]) * t + s.cy[1]) * t + s.cy[0]};
                    worst = std::max(worst, distToAll(polys, p));
                }
        expect(worst <= tol * 1.6, std::string("保真度 ") + ch,
               "最大偏差 " + std::to_string(worst) + " > " + std::to_string(tol * 1.6));
        // 4) exact 段的系数确实精确
        for (const auto &c : sh.contours)
            for (const auto &s : c.segs) {
                if (!s.exact) continue;
                for (int k = 0; k <= s.degree; ++k) {
                    double rx = double(s.rx[k].n) / double(s.rx[k].d);
                    double ry = double(s.ry[k].n) / double(s.ry[k].d);
                    if (std::fabs(rx - s.cx[k]) > 1e-6 || std::fabs(ry - s.cy[k]) > 1e-6) {
                        bad(std::string("精确系数自洽 ") + ch, "系数对不上");
                        break;
                    }
                }
            }
        ok();
        // 5) 显式 y=f(x) 必须落在轮廓上
        for (const auto &ef : sh.explicitFns) {
            if (!(ef.xb > ef.xa)) {
                bad(std::string("显式区间 ") + ch, "区间不合法");
                continue;
            }
            double w2 = 0;
            for (int k = 0; k <= 20; ++k) {
                double x = ef.xa + (ef.xb - ef.xa) * k / 20.0;
                double y = ((ef.c[3] * x + ef.c[2]) * x + ef.c[1]) * x + ef.c[0];
                w2 = std::max(w2, distToAll(polys, P{x, y}));
            }
            expect(w2 <= tol * 2.5, std::string("显式函数贴合 ") + ch,
                   "最大偏差 " + std::to_string(w2));
        }
    }

    // "图 = 函数": 预览路径反解回来必须与函数一致(偏差只应来自小数位舍入)
    for (const char *ch : samples) {
        uint32_t cp = utf8First(ch);
        if (!font.glyphIndex(cp)) continue;
        GlyphShape sh;
        std::string e2;
        if (!shapeFromGlyph(font, cp, size, 0, 0, tol, sh, e2)) continue;
        GlyphSelfCheck c3 = glyphSelfCheck(sh, 3);
        expect(c3.ok, std::string("自检可算 ") + ch, c3.detail);
        expect(c3.svgVsCurve <= 1e-2, std::string("自检: 预览↔函数(3 位小数) ") + ch,
               "最大偏差 " + std::to_string(c3.svgVsCurve));
        // 文本误差本来就受打印小数位限制: 3 位小数 -> <= 5e-4, 8 位 -> <= 5e-9
        expect(c3.textLoss <= 1.5e-3, std::string("自检: 文本按打印精度舍入(3 位) ") + ch,
               "相对误差 " + std::to_string(c3.textLoss));
        GlyphSelfCheck c8 = glyphSelfCheck(sh, 3, 8);
        expect(c8.textLoss <= 1.5e-8, std::string("自检: 8 位小数文本更精确 ") + ch,
               "相对误差 " + std::to_string(c8.textLoss));
        expect(c8.ok && c8.svgVsCurve < c3.svgVsCurve + 1e-12,
               std::string("自检: 小数位越多图越准 ") + ch,
               std::to_string(c3.svgVsCurve) + " -> " + std::to_string(c8.svgVsCurve));
    }

    // 锚点写法解析
    {
        struct Case { const char *in; bool ok; double x, y; bool mir; };
        const Case cases[] = {
            {"(1,2)", true, 1, 2, false},      {"( 1 , 2 )", true, 1, 2, false},
            {"(1 2)", true, 1, 2, false},      {"1 2", true, 1, 2, false},
            {"1,2", true, 1, 2, false},        {"1, 2", true, 1, 2, false},
            {"(1.5,-2)", true, 1.5, -2, false}, {"-(3,4)", false, 0, 0, false},
            {":(10,20)", true, 10, 20, true},  {":(10 20)", true, 10, 20, true},
            {"", true, 0, 0, false},           {"(1)", false, 0, 0, false},
            {"(1,2,3)", false, 0, 0, false},   {"abc", false, 0, 0, false},
            {"(1,2)(3,4)", false, 0, 0, false},
        };
        for (const auto &c : cases) {
            AnchorSpec a = parseAnchor(c.in);
            bool good = (a.ok == c.ok) && (!a.ok || (std::fabs(a.x - c.x) < 1e-12 &&
                                                     std::fabs(a.y - c.y) < 1e-12 &&
                                                     a.mirrorX == c.mir));
            expect(good, std::string("锚点解析 ") + c.in,
                   std::string("ok=") + (a.ok ? "1" : "0") + " x=" + std::to_string(a.x) +
                           " y=" + std::to_string(a.y) + " mirror=" + (a.mirrorX ? "1" : "0"));
        }
    }

    // 手绘笔画: 一条平滑曲线应当被拟合成更少的函数, 且贴住折线
    {
        std::vector<std::vector<std::pair<double, double>>> strokes;
        std::vector<std::pair<double, double>> st;
        for (int k = 0; k <= 200; ++k) {
            double t = double(k) / 200;
            st.push_back({100 * t, 50 * std::sin(3.14159 * t)});
        }
        strokes.push_back(st);
        GlyphShape sh;
        std::string e3;
        expect(shapeFromStrokes(strokes, 0.5, sh, e3), "手绘: 解析", e3);
        expect(!sh.contours.empty() && sh.segCount() < 200, "手绘: 函数数变少",
               std::to_string(sh.segCount()));
        std::vector<std::vector<P>> poly;
        std::vector<P> v;
        for (auto &p : st) v.push_back(P{p.first, p.second});
        poly.push_back(v);
        double worst = 0;
        for (const auto &c : sh.contours)
            for (const auto &s : c.segs)
                for (int k = 0; k <= 24; ++k) {
                    double t = double(k) / 24;
                    P p{((s.cx[3] * t + s.cx[2]) * t + s.cx[1]) * t + s.cx[0],
                        ((s.cy[3] * t + s.cy[2]) * t + s.cy[1]) * t + s.cy[0]};
                    worst = std::max(worst, distToAll(poly, p));
                }
        expect(worst <= 1.6, "手绘: 保真度", "最大偏差 " + std::to_string(worst));
    }

    std::printf("字形测试: 通过 %d 项, 失败 %d 项\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

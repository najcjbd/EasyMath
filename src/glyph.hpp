// EasyMath - 字形轮廓 → 函数
// 目标: 用"尽可能少的函数"正确还原文字/手绘, 且不留无意义的杂线。
//
// 表示: 每个闭合轮廓由若干段参数方程组成
//   x(t) = a3 t^3 + a2 t^2 + a1 t + a0
//   y(t) = b3 t^3 + b2 t^2 + b1 t + b0        t ∈ [0,1]
// 若某段在 x 上单调, 再补一条显式 y = f(x) (x ∈ [x0,x1])。
// 系数优先用精确有理数(能吸附且验证通过时), 否则标为近似。
#pragma once

#include "truetype.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace em {

// 精确有理数(小分母, 字形坐标本身是整数, 够用)
struct GRat {
    int64_t n = 0, d = 1;
    bool operator==(const GRat &o) const { return n == o.n && d == o.d; }
};

// 一段参数方程(多项式, 最高 3 次)
struct GlyphSeg {
    double cx[4] = {0, 0, 0, 0}; // x(t) 系数, 下标=次数
    double cy[4] = {0, 0, 0, 0};
    int degree = 3;      // 1=直线 2=二次 3=三次
    bool exact = false;  // 系数是否精确(否则是拟合近似)
    GRat rx[4], ry[4];   // exact=true 时的精确系数
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // 端点(t=0 / t=1)
    double len = 0;      // 近似弧长(用于统计)
};

// 显式函数 y = f(x), 定义在 [xa, xb]
struct ExplicitFunc {
    double c[4] = {0, 0, 0, 0}; // y = c0 + c1 x + c2 x^2 + c3 x^3
    int degree = 1;
    double xa = 0, xb = 0;
    bool exact = false;
    GRat rc[4];
};

struct GlyphContour {
    std::vector<GlyphSeg> segs;
};

struct GlyphShape {
    std::vector<GlyphContour> contours;   // 一条轮廓 = 一个闭合回路
    std::vector<ExplicitFunc> explicitFns;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0; // 包围盒
    int srcSegs = 0;                       // 原始段数(线+二次)
    int srcContours = 0;
    bool empty() const { return contours.empty(); }
    int segCount() const {
        int n = 0;
        for (const auto &c : contours) n += int(c.segs.size());
        return n;
    }
};

// 把一段 TrueType 轮廓变成"最少函数":
//   tol         拟合容差(字体单位, 相对 upem=1000 的字形)
//   mergeCollinear 是否把共线直线并成一条
//   cornerCos   拐角判定(相邻方向夹角余弦阈值, 小于它就当成尖角切开)
GlyphContour fitContour(const TtContour &src, double tol, bool mergeCollinear, double cornerCos,
                        double scale = 1.0, double dx = 0, double dy = 0);

// 取一个码位的形状(已按 size 缩放, 平移到 (ox,oy), y 向上, 原点=锚点)
bool shapeFromGlyph(const TrueTypeFont &font, uint32_t cp, double size, double ox, double oy,
                    double tol, GlyphShape &out, std::string &err,
                    const std::vector<double> &axisCoords = {}, bool *sparseOut = nullptr,
                    int forceGid = -1);

// 取"手绘笔画中心线"的形状: 输入是若干条折线(画布坐标, y 向下), 输出同样的函数表示
bool shapeFromStrokes(const std::vector<std::vector<std::pair<double, double>>> &strokes,
                      double tol, GlyphShape &out, std::string &err);

// SVG path(内部把 y 翻过来: y -> flipBase - y)
std::string svgPath(const GlyphShape &shape, double flipBase, int decimals);
// 完整 <svg> 预览(自包含, 不依赖外部资源)
std::string shapeSvg(const GlyphShape &shape, int decimals, double pad);

// 变换: 左右镜像(以 C 为轴: x -> C - x) / 平移
void mirrorShapeX(GlyphShape &shape, double c);
void translateShape(GlyphShape &shape, double dx, double dy);

// 一段参数方程的纯文本, 例如 "x = 10 + 250t - 40t^2, y = 0 + 300t"
std::string segPlain(const GlyphSeg &s, int decimals, const char *varName = "t");
// LaTeX 版本
std::string segLatex(const GlyphSeg &s, int decimals, const char *varName = "t");
// 显式函数纯文本, 例如 "y = 2x^2 - 3x + 1, x ∈ [0, 100]"
std::string explicitPlain(const ExplicitFunc &f, int decimals);
std::string explicitLatex(const ExplicitFunc &f, int decimals);

// ---- "图 = 函数" 自检 ----
// 思路: 预览 SVG 是另一条输出路径, 一旦画图那侧写错(控制点换算/命令用错),
// 光看系数是看不出来的。所以这里把 svgPath() 生成的路径**重新解析回折线**,
// 与"函数系数采样"逐点比对; 同时把系数按打印精度格式化后读回来, 检查文本没丢精度。
struct GlyphSelfCheck {
    double svgVsCurve = 0; // 预览图 vs 函数曲线 的最大偏差(含打印舍入)
    double textLoss = 0;   // 文本系数 vs 原系数 的最大偏差(相对)
    int segs = 0;          // 参与自检的段数
    bool ok = false;
    std::string detail;
};
GlyphSelfCheck glyphSelfCheck(const GlyphShape &shape, int svgDecimals, int textDecimals = -1);

// 一个字形/图形的统计行, 例如 "函数 9 个(原始 42 段, 省了 79%)"
std::string shapeStats(const GlyphShape &shape);

// 解析"左下角坐标"输入: 接受 (x,x) / (x , x) / (x x) / x x / x,x, 前缀 ':' 表示左右镜像
struct AnchorSpec {
    bool ok = false;
    double x = 0, y = 0;
    bool mirrorX = false;
    std::string err;
};
AnchorSpec parseAnchor(const std::string &text);

// 画布高度(y 向上, 用于把 y 翻到 SVG 坐标)
double shapeTop(const GlyphShape &shape);

} // namespace em

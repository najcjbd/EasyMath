// EasyMath - 字形轮廓 → 函数(拟合与输出)
#include "glyph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace em {
namespace {

struct V2 {
    double x = 0, y = 0;
};
V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator*(V2 a, double k) { return {a.x * k, a.y * k}; }
double dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
double len2(V2 a) { return dot(a, a); }
double vlen(V2 a) { return std::sqrt(len2(a)); }
V2 norm(V2 a) {
    double l = vlen(a);
    return l > 1e-12 ? V2{a.x / l, a.y / l} : V2{0, 0};
}

// ---------- 有理数(小分母) ----------
int64_t gcd64(int64_t a, int64_t b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a ? a : 1;
}
GRat ratNorm(int64_t n, int64_t d) {
    if (d < 0) {
        n = -n;
        d = -d;
    }
    int64_t g = gcd64(n ? n : 1, d ? d : 1);
    return GRat{n / g, d / g};
}
// 连分数逼近, 分母上限 maxDen; 失败(d==0 或偏差大)返回 false
bool ratApprox(double v, int64_t maxDen, GRat &out, double eps) {
    if (!std::isfinite(v)) return false;
    bool neg = v < 0;
    double a = std::fabs(v);
    int64_t h0 = 0, h1 = 1, k0 = 1, k1 = 0;
    double x = a;
    for (int i = 0; i < 64; ++i) {
        double fl = std::floor(x);
        if (fl > 9e15) break;
        int64_t ai = int64_t(fl);
        int64_t h2 = ai * h1 + h0, k2 = ai * k1 + k0;
        if (k2 > maxDen || h2 < 0) break;
        h0 = h1; h1 = h2; k0 = k1; k1 = k2;
        double approx = double(h1) / double(k1 ? k1 : 1);
        if (std::fabs(approx - a) <= eps) {
            out = ratNorm(neg ? -h1 : h1, k1);
            return true;
        }
        double frac = x - fl;
        if (frac < 1e-12) break;
        x = 1.0 / frac;
    }
    if (k1 > 0) {
        out = ratNorm(neg ? -h1 : h1, k1);
        return std::fabs(double(out.n) / double(out.d) - v) <= eps;
    }
    return false;
}

// ---------- 折线化(自适应采样) ----------
V2 quadAt(V2 p0, V2 c, V2 p1, double t) {
    double u = 1 - t;
    return p0 * (u * u) + c * (2 * u * t) + p1 * (t * t);
}

// 一条三次贝塞尔(用结构体包住数组, 因为数组不能直接放进 vector)
struct Bez {
    V2 p[4];
};

// ---------- Schneider 曲线拟合 ----------
void chordLengthParam(const std::vector<V2> &d, int first, int last, std::vector<double> &u) {
    u.assign(std::size_t(last - first + 1), 0.0);
    for (int i = first + 1; i <= last; ++i) u[std::size_t(i - first)] = u[std::size_t(i - first - 1)] + vlen(d[std::size_t(i)] - d[std::size_t(i - 1)]);
    double total = u.back();
    if (total <= 0) {
        for (std::size_t i = 0; i < u.size(); ++i) u[i] = double(i) / double(u.size() - 1 ? u.size() - 1 : 1);
        return;
    }
    for (auto &x : u) x /= total;
}

V2 bezierAt(const V2 b[4], double t) {
    double u = 1 - t;
    return b[0] * (u * u * u) + b[1] * (3 * u * u * t) + b[2] * (3 * u * t * t) + b[3] * (t * t * t);
}

void generateBezier(const std::vector<V2> &d, int first, int last, const std::vector<double> &u,
                    V2 tHat1, V2 tHat2, V2 out[4]) {
    int n = last - first + 1;
    std::vector<V2> A0, A1;
    A0.resize(static_cast<std::size_t>(n));
    A1.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        double t = u[std::size_t(i)], omt = 1 - t;
        A0[std::size_t(i)] = tHat1 * (3 * t * omt * omt);
        A1[std::size_t(i)] = tHat2 * (3 * t * t * omt);
    }
    double c00 = 0, c01 = 0, c11 = 0, x0 = 0, x1 = 0;
    for (int i = 0; i < n; ++i) {
        c00 += dot(A0[std::size_t(i)], A0[std::size_t(i)]);
        c01 += dot(A0[std::size_t(i)], A1[std::size_t(i)]);
        c11 += dot(A1[std::size_t(i)], A1[std::size_t(i)]);
        double t = u[std::size_t(i)], omt = 1 - t;
        V2 tmp = d[std::size_t(first + i)] -
                 (d[std::size_t(first)] * (omt * omt * omt) +
                  d[std::size_t(first)] * (3 * omt * omt * t) + d[std::size_t(last)] * (3 * omt * t * t) +
                  d[std::size_t(last)] * (t * t * t));
        x0 += dot(A0[std::size_t(i)], tmp);
        x1 += dot(A1[std::size_t(i)], tmp);
    }
    double detC = c00 * c11 - c01 * c01;
    double alphaL = 0, alphaR = 0;
    if (std::fabs(detC) > 1e-12) {
        alphaL = (c11 * x0 - c01 * x1) / detC;
        alphaR = (c00 * x1 - c01 * x0) / detC;
    }
    double segLen = vlen(d[std::size_t(last)] - d[std::size_t(first)]);
    if (!(alphaL > 1e-6) || !(alphaR > 1e-6) || alphaL > segLen * 3 || alphaR > segLen * 3) {
        alphaL = alphaR = segLen / 3.0;
    }
    out[0] = d[std::size_t(first)];
    out[3] = d[std::size_t(last)];
    out[1] = out[0] + tHat1 * alphaL;
    out[2] = out[3] + tHat2 * alphaR;
}

double maxErrorOf(const std::vector<V2> &d, int first, int last, const V2 b[4],
                  const std::vector<double> &u, int &splitPoint) {
    splitPoint = (first + last) / 2;
    double maxD = 0;
    for (int i = first + 1; i < last; ++i) {
        V2 p = bezierAt(b, u[std::size_t(i - first)]);
        double dist = len2(p - d[std::size_t(i)]);
        if (dist >= maxD) {
            maxD = dist;
            splitPoint = i;
        }
    }
    return maxD;
}

void reparameterize(const std::vector<V2> &d, int first, int last, const std::vector<double> &u,
                    const V2 b[4], std::vector<double> &out) {
    out.resize(u.size());
    for (int i = first; i <= last; ++i) {
        double t = u[std::size_t(i - first)];
        double ux = 1 - t, uy = 1 - t;
        V2 q0 = (b[1] - b[0]) * 3.0;
        V2 q1 = (b[2] - b[1]) * 3.0;
        V2 q2 = (b[3] - b[2]) * 3.0;
        V2 q = q0 * (ux * ux) + q1 * (2 * ux * t) + q2 * (t * t);
        V2 qq0 = (q1 - q0) * 2.0;
        V2 qq1 = (q2 - q1) * 2.0;
        V2 qq = qq0 * uy + qq1 * t;
        V2 p = bezierAt(b, t);
        V2 diff = p - d[std::size_t(i)];
        double num = dot(diff, q);
        double den = dot(q, q) + dot(diff, qq);
        out[std::size_t(i - first)] = std::fabs(den) < 1e-12 ? t : t - num / den;
    }
}

void fitCubic(const std::vector<V2> &d, int first, int last, V2 tHat1, V2 tHat2, double error,
              int depth, std::vector<Bez> &out) {
    int n = last - first + 1;
    if (n < 2) return;
    V2 bez[4];
    if (n == 2) {
        double dist = vlen(d[std::size_t(last)] - d[std::size_t(first)]) / 3.0;
        bez[0] = d[std::size_t(first)];
        bez[3] = d[std::size_t(last)];
        bez[1] = bez[0] + tHat1 * dist;
        bez[2] = bez[3] + tHat2 * dist;
        out.push_back(Bez{{bez[0], bez[1], bez[2], bez[3]}});
        return;
    }
    std::vector<double> u;
    chordLengthParam(d, first, last, u);
    generateBezier(d, first, last, u, tHat1, tHat2, bez);
    int split = 0;
    double maxErr = maxErrorOf(d, first, last, bez, u, split);
    if (std::getenv("EASY_DEBUG_FIT")) {
        std::fprintf(stderr, "[fit] n=%d first=%d last=%d maxErr=%.6f err^2=%.6f split=%d\n", n, first,
                     last, maxErr, error * error, split);
        if (n <= 4) {
            std::fprintf(stderr, "       d[first]=(%.3f,%.3f) d[last]=(%.3f,%.3f) t1=(%.4f,%.4f) t2=(%.4f,%.4f)\n",
                         d[first].x, d[first].y, d[last].x, d[last].y, tHat1.x, tHat1.y, tHat2.x, tHat2.y);
            std::fprintf(stderr, "       bez=(%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f) u=[", bez[0].x, bez[0].y,
                         bez[1].x, bez[1].y, bez[2].x, bez[2].y, bez[3].x, bez[3].y);
            for (double uu : u) std::fprintf(stderr, "%.4f ", uu);
            std::fprintf(stderr, "]\n");
            for (int ii = first + 1; ii < last; ++ii) {
                V2 pp = bezierAt(bez, u[std::size_t(ii - first)]);
                std::fprintf(stderr, "       点%d 数据=(%.3f,%.3f) 拟合=(%.3f,%.3f) 距离=%.4f\n", ii,
                             d[std::size_t(ii)].x, d[std::size_t(ii)].y, pp.x, pp.y,
                             std::sqrt(len2(pp - d[std::size_t(ii)])));
            }
        }
    }
    if (maxErr < error * error) {
        out.push_back(Bez{{bez[0], bez[1], bez[2], bez[3]}});
        return;
    }
    // Newton 重新参数化最多 4 轮
    if (maxErr < error * error * 16.0) {
        std::vector<double> up;
        for (int it = 0; it < 4; ++it) {
            reparameterize(d, first, last, u, bez, up);
            u = up;
            generateBezier(d, first, last, u, tHat1, tHat2, bez);
            maxErr = maxErrorOf(d, first, last, bez, u, split);
            if (maxErr < error * error) {
                out.push_back(Bez{{bez[0], bez[1], bez[2], bez[3]}});
                return;
            }
        }
    }
    if (depth > 24) {
        out.push_back(Bez{{bez[0], bez[1], bez[2], bez[3]}});
        return;
    }
    if (split <= first) split = first + 1;
    if (split >= last) split = last - 1;
    // 分裂点处取"中心切线"(Schneider): 两个反向单位切线的平均, 指向后方。
    // 左半段的末端切线 = 中心切线; 右半段的起始切线 = 它的反向。
    V2 vBack1 = d[std::size_t(split - 1)] - d[std::size_t(split)];
    V2 vBack2 = d[std::size_t(split)] - d[std::size_t(split + 1)];
    V2 centerTangent = norm(vBack1 + vBack2);
    if (vlen(centerTangent) < 1e-12) centerTangent = norm(vBack1);
    fitCubic(d, first, split, tHat1, centerTangent, error, depth + 1, out);
    fitCubic(d, split, last, centerTangent * -1.0, tHat2, error, depth + 1, out);
}

// ---------- 数字/多项式文本 ----------
std::string fmtNum(double v, int decimals) {
    if (std::fabs(v) < 5e-13) v = 0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}
std::string ratPlain(const GRat &r) {
    if (r.d == 1) return std::to_string(r.n);
    return std::to_string(r.n) + "/" + std::to_string(r.d);
}
double ratVal(const GRat &r) { return double(r.n) / double(r.d); }

std::string polyPlain(const double c[4], const GRat r[4], int degree, bool exact, int decimals,
                      const char *var) {
    std::string out;
    for (int k = degree; k >= 0; --k) {
        double v = c[k];
        if (exact) v = ratVal(r[k]);
        if (std::fabs(v) < 5e-13) continue;
        bool first = out.empty();
        double av = std::fabs(v);
        std::string term;
        if (k == 0) {
            term = exact ? ratPlain(r[0]) : fmtNum(av, decimals);
        } else {
            bool needParen = false;
            std::string coef;
            if (exact) {
                GRat a = r[k];
                if (a.n < 0) a.n = -a.n;
                if (a.d == 1) coef = (a.n == 1) ? "" : std::to_string(a.n);
                else {
                    coef = ratPlain(a);
                    needParen = true;
                }
            } else if (std::fabs(av - 1.0) > 5e-13) {
                coef = fmtNum(av, decimals);
            }
            term = needParen ? "(" + coef + ")" : coef;
            term += var;
            if (k > 1) term += "^" + std::to_string(k);
        }
        if (first) out += (v < 0 ? "-" : "") + term;
        else {
            out += v < 0 ? " - " : " + ";
            out += term;
        }
    }
    if (out.empty()) out = "0";
    return out;
}

std::string polyLatex(const double c[4], const GRat r[4], int degree, bool exact, int decimals,
                      const char *var) {
    std::string out;
    for (int k = degree; k >= 0; --k) {
        double v = c[k];
        if (exact) v = ratVal(r[k]);
        if (std::fabs(v) < 5e-13) continue;
        double av = std::fabs(v);
        std::string term;
        if (k == 0) {
            if (exact) {
                term = (r[0].d == 1) ? std::to_string(std::llabs(r[0].n))
                                     : ("\\frac{" + std::to_string(std::llabs(r[0].n)) + "}{" +
                                        std::to_string(r[0].d) + "}");
            } else {
                term = fmtNum(av, decimals);
            }
        } else {
            std::string coef;
            if (exact) {
                GRat a = r[k];
                if (a.n < 0) a.n = -a.n;
                if (a.d == 1) coef = (a.n == 1) ? "" : std::to_string(a.n);
                else
                    coef = (a.n == 1)
                                   ? ("\\frac{1}{" + std::to_string(a.d) + "}")
                                   : ("\\frac{" + std::to_string(a.n) + "}{" + std::to_string(a.d) + "}");
            } else if (std::fabs(av - 1.0) > 5e-13) {
                coef = fmtNum(av, decimals);
            }
            term = coef + var;
            if (k > 1) term += "^{" + std::to_string(k) + "}";
        }
        if (out.empty()) out += (v < 0 ? "-" : "") + term;
        else {
            out += v < 0 ? " - " : " + ";
            out += term;
        }
    }
    if (out.empty()) out = "0";
    return out;
}

// ---------- 贝塞尔/多项式互转 + 用参考折线复验 ----------
V2 evalPoly(const double cx[4], const double cy[4], double t) {
    return V2{((cx[3] * t + cx[2]) * t + cx[1]) * t + cx[0],
              ((cy[3] * t + cy[2]) * t + cy[1]) * t + cy[0]};
}

double distToPolyline(const std::vector<V2> &poly, V2 p) {
    double best = 1e300;
    for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
        V2 a = poly[i], b = poly[i + 1];
        V2 d = b - a;
        double L2 = len2(d);
        double t = L2 > 1e-18 ? dot(p - a, d) / L2 : 0.0;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        V2 q = a + d * t;
        double dd = len2(p - q);
        if (dd < best) best = dd;
    }
    return best > 0 ? std::sqrt(best) : 0.0;
}

double maxDeviationOf(const GlyphSeg &s, const std::vector<V2> &ref) {
    double worst = 0;
    const int N = 24;
    for (int k = 0; k <= N; ++k) {
        V2 p = evalPoly(s.cx, s.cy, double(k) / N);
        double d = distToPolyline(ref, p);
        if (d > worst) worst = d;
    }
    return worst;
}

GRat ratAdd(GRat a, GRat b) { return ratNorm(a.n * b.d + b.n * a.d, a.d * b.d); }
GRat ratSub(GRat a, GRat b) { return ratNorm(a.n * b.d - b.n * a.d, a.d * b.d); }

// 三次贝塞尔 -> 多项式段。先按有理数吸附, 吸附后必须仍在容差内才认, 否则保留小数。
GlyphSeg segFromBezierChecked(const V2 b[4], const std::vector<V2> &ref, double tol) {
    GlyphSeg s;
    for (int axis = 0; axis < 2; ++axis) {
        double p0 = axis ? b[0].y : b[0].x;
        double p1 = axis ? b[1].y : b[1].x;
        double p2 = axis ? b[2].y : b[2].x;
        double p3 = axis ? b[3].y : b[3].x;
        double *c = axis ? s.cy : s.cx;
        c[0] = p0;
        c[1] = 3.0 * (p1 - p0);
        c[2] = 3.0 * (p2 - 2 * p1 + p0);
        c[3] = p3 - 3 * p2 + 3 * p1 - p0;
    }
    s.x0 = b[0].x; s.y0 = b[0].y; s.x1 = b[3].x; s.y1 = b[3].y;
    double eps = 1e-9 * std::max(1.0, std::fabs(s.x1) + std::fabs(s.y1));
    if (std::fabs(s.cx[3]) < eps && std::fabs(s.cy[3]) < eps)
        s.degree = (std::fabs(s.cx[2]) < eps && std::fabs(s.cy[2]) < eps) ? 1 : 2;
    else
        s.degree = 3;
    s.len = 0;
    for (int k = 0; k + 1 <= 24; ++k) {
        V2 p = evalPoly(s.cx, s.cy, double(k) / 24);
        V2 q = evalPoly(s.cx, s.cy, double(k + 1) / 24);
        s.len += vlen(q - p);
    }
    bool allExact = true;
    for (int axis = 0; axis < 2 && allExact; ++axis) {
        const double *c = axis ? s.cy : s.cx;
        GRat *r = axis ? s.ry : s.rx;
        for (int k = 0; k <= s.degree; ++k) {
            GRat g;
            if (!ratApprox(c[k], 4096, g, 1e-9 * std::max(1.0, std::fabs(c[k])))) {
                allExact = false;
                break;
            }
            r[k] = g;
        }
        if (allExact)
            for (int k = s.degree + 1; k < 4; ++k) r[k] = GRat{0, 1};
    }
    s.exact = false;
    if (allExact) {
        GlyphSeg t = s;
        for (int k = 0; k <= 3; ++k) {
            t.cx[k] = ratVal(s.rx[k]);
            t.cy[k] = ratVal(s.ry[k]);
        }
        if (ref.empty() || maxDeviationOf(t, ref) <= std::max(tol, 1e-9) * 1.000001) {
            s = t;
            s.exact = true;
        }
    }
    return s;
}

// 二次贝塞尔 -> 三次(精确: 控制点是 2/3 组合, 分母只有 3)
void quadToCubic(V2 p0, V2 c, V2 p1, V2 out[4]) {
    out[0] = p0;
    out[1] = p0 + (c - p0) * (2.0 / 3.0);
    out[2] = p1 + (c - p1) * (2.0 / 3.0);
    out[3] = p1;
}

struct Piece {
    bool line = true;
    V2 p0, c, p1;
    V2 startDir() const { return line ? norm(p1 - p0) : norm(c - p0); }
    V2 endDir() const { return line ? norm(p1 - p0) : norm(p1 - c); }
};

void samplePiece(const Piece &pc, int sub, std::vector<V2> &out) {
    if (pc.line) {
        out.push_back(pc.p1);
        return;
    }
    for (int k = 1; k <= sub; ++k) out.push_back(quadAt(pc.p0, pc.c, pc.p1, double(k) / sub));
}

double polylineLen(const std::vector<V2> &v) {
    double l = 0;
    for (std::size_t i = 0; i + 1 < v.size(); ++i) l += vlen(v[i + 1] - v[i]);
    return l;
}


// 解 (d+1) 元线性方程组(最小二乘的正规方程), 失败返回 false
bool solveSmall(double A[4][4], double b[4], int n, double x[4]) {
    for (int i = 0; i < n; ++i) {
        int piv = i;
        for (int k = i + 1; k < n; ++k)
            if (std::fabs(A[k][i]) > std::fabs(A[piv][i])) piv = k;
        if (std::fabs(A[piv][i]) < 1e-12) return false;
        if (piv != i) {
            for (int j = 0; j < n; ++j) std::swap(A[i][j], A[piv][j]);
            std::swap(b[i], b[piv]);
        }
        for (int k = i + 1; k < n; ++k) {
            double f = A[k][i] / A[i][i];
            for (int j = i; j < n; ++j) A[k][j] -= f * A[i][j];
            b[k] -= f * b[i];
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        double v = b[i];
        for (int j = i + 1; j < n; ++j) v -= A[i][j] * x[j];
        x[i] = v / A[i][i];
    }
    return true;
}

// 用最小二乘拟合 y = c0 + c1 x + ... + c_deg x^deg, 返回最大偏差
bool fitPolynomial(const std::vector<V2> &pts, int deg, double c[4], double &maxDev) {
    double A[4][4] = {{0}};
    double b[4] = {0};
    int n = deg + 1;
    for (const auto &p : pts) {
        double pw[7];
        pw[0] = 1;
        for (int k = 1; k < 7; ++k) pw[k] = pw[k - 1] * p.x;
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) A[i][j] += pw[i + j];
            b[i] += pw[i] * p.y;
        }
    }
    double x[4] = {0};
    if (!solveSmall(A, b, n, x)) return false;
    for (int i = 0; i < 4; ++i) c[i] = 0;
    for (int i = 0; i < n; ++i) c[i] = x[i];
    maxDev = 0;
    for (const auto &p : pts) {
        double y = ((c[3] * p.x + c[2]) * p.x + c[1]) * p.x + c[0];
        maxDev = std::max(maxDev, std::fabs(y - p.y));
    }
    return true;
}

// 采样参数段在 [t0,t1] 上的点(含端点)
void sampleSeg(const GlyphSeg &s, double t0, double t1, int n, std::vector<V2> &out) {
    for (int k = 0; k <= n; ++k) {
        double t = t0 + (t1 - t0) * double(k) / n;
        out.push_back(evalPoly(s.cx, s.cy, t));
    }
}

// 对一条轮廓求"极大 x 单调段"上的显式函数
void explicitForContour(const GlyphContour &ct, double tol, std::vector<ExplicitFunc> &out) {
    std::size_t n = ct.segs.size();
    if (n == 0) return;
    // 先找出 x 单调的连续段(段内 dx/dt 不变号)
    // x 的走向: +1 递增, -1 递减, 0 表示 x 不变(竖段)或中途变向 —— 都不能当函数
    auto xdir = [](const GlyphSeg &s) -> int {
        double d1 = s.cx[1], d2 = 2 * s.cx[2], d3 = 3 * s.cx[3];
        int dir = 0;
        for (int k = 0; k <= 8; ++k) {
            double t = double(k) / 8;
            double d = (d3 * t + d2) * t + d1;
            if (d > 1e-9) {
                if (dir < 0) return 0;
                dir = 1;
            } else if (d < -1e-9) {
                if (dir > 0) return 0;
                dir = -1;
            }
        }
        return dir;
    };
    std::size_t i = 0;
    while (i < n) {
        int d0 = xdir(ct.segs[i]);
        if (d0 == 0) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j + 1 < n && xdir(ct.segs[j + 1]) == d0 &&
               std::fabs(ct.segs[j + 1].x0 - ct.segs[j].x1) < 1e-9)
            ++j;
        // 采样这一段
        std::vector<V2> pts;
        for (std::size_t k = i; k <= j; ++k) sampleSeg(ct.segs[k], 0, 1, k == j ? 16 : 12, pts);
        // 去掉重复 x
        std::vector<V2> clean;
        for (const auto &p : pts)
            if (clean.empty() || std::fabs(clean.back().x - p.x) > 1e-9) clean.push_back(p);
        if (clean.size() >= 3) {
            double xa = clean.front().x, xb = clean.back().x;
            double span = std::fabs(xb - xa);
            if (span > 1e-6) {
                for (int deg = 1; deg <= 3; ++deg) {
                    double c[4] = {0, 0, 0, 0};
                    double dev = 0;
                    if (!fitPolynomial(clean, deg, c, dev)) continue;
                    if (dev <= tol) {
                        ExplicitFunc f;
                        for (int k = 0; k < 4; ++k) f.c[k] = c[k];
                        f.degree = deg;
                        // 区间一律按从小到大印(多项式本身与方向无关)
                        f.xa = std::min(xa, xb);
                        f.xb = std::max(xa, xb);
                        // 有理数吸附并复验
                        bool allExact = true;
                        for (int k = 0; k <= deg; ++k) {
                            GRat g;
                            if (!ratApprox(c[k], 4096, g, 1e-9 * std::max(1.0, std::fabs(c[k])))) {
                                allExact = false;
                                break;
                            }
                            f.rc[k] = g;
                        }
                        if (allExact) {
                            double dev2 = 0;
                            for (const auto &p : clean) {
                                double y = ((ratVal(f.rc[3]) * p.x + ratVal(f.rc[2])) * p.x +
                                            ratVal(f.rc[1])) * p.x + ratVal(f.rc[0]);
                                dev2 = std::max(dev2, std::fabs(y - p.y));
                            }
                            if (dev2 <= tol) f.exact = true;
                            else allExact = false;
                        }
                        if (!allExact) f.exact = false;
                        out.push_back(f);
                        break;
                    }
                }
            }
        }
        i = j + 1;
    }
}

} // namespace

GlyphContour fitContour(const TtContour &src, double tol, bool mergeCollinear, double cornerCos,
                        double scale, double dx, double dy) {
    GlyphContour out;
    if (tol <= 0) tol = 0.5;
    // 1) 解析段(缩放/平移)
    std::vector<Piece> pieces;
    for (const auto &sg : src.segs) {
        Piece pc;
        pc.line = (sg.kind == TtKind::Line);
        pc.p0 = V2{sg.p0.x * scale + dx, sg.p0.y * scale + dy};
        pc.p1 = V2{sg.p1.x * scale + dx, sg.p1.y * scale + dy};
        if (!pc.line) pc.c = V2{sg.c.x * scale + dx, sg.c.y * scale + dy};
        if (pc.line && vlen(pc.p1 - pc.p0) < 1e-12) continue;
        pieces.push_back(pc);
    }
    if (pieces.empty()) return out;
    // 2) 尖角(只在段接头上切, 保证不从曲线中间切)
    std::vector<bool> cornerAt(pieces.size(), false);
    for (std::size_t j = 1; j < pieces.size(); ++j)
        if (dot(pieces[j - 1].endDir(), pieces[j].startDir()) < cornerCos) cornerAt[j] = true;
    if (pieces.size() > 1 && dot(pieces.back().endDir(), pieces.front().startDir()) < cornerCos)
        cornerAt[0] = true;
    std::vector<std::size_t> starts;
    for (std::size_t j = 0; j < pieces.size(); ++j)
        if (cornerAt[j]) starts.push_back(j);
    // 必须从 0 开始分段, 否则"闭环起点不是尖角"时, 开头那几段会整段漏掉(轮廓就不闭合了)
    if (starts.empty() || starts.front() != 0) starts.insert(starts.begin(), 0);

    // 3) 每个"平滑 run": 先拟合 -> 用原始解析轮廓验证 -> 过不了就退回字体自带分段(精确)
    for (std::size_t k = 0; k < starts.size(); ++k) {
        std::size_t i0 = starts[k];
        std::size_t i1 = (k + 1 < starts.size()) ? starts[k + 1] - 1 : pieces.size() - 1;
        std::vector<V2> ref;
        ref.push_back(pieces[i0].p0);
        for (std::size_t j = i0; j <= i1; ++j) samplePiece(pieces[j], 16, ref);
        bool ok = ref.size() >= 2;
        std::vector<GlyphSeg> cand;
        if (ok) {
            std::vector<Bez> curves;
            V2 t1 = pieces[i0].startDir();
            V2 t2 = pieces[i1].endDir() * -1.0;
            fitCubic(ref, 0, int(ref.size()) - 1, t1, t2, tol, 0, curves);
            ok = !curves.empty();
            double refLen = polylineLen(ref);
            double candLen = 0;
            for (auto &cv : curves) {
                GlyphSeg s = segFromBezierChecked(cv.p, ref, tol);
                if (maxDeviationOf(s, ref) > tol) ok = false;
                candLen += s.len;
                cand.push_back(s);
            }
            if (refLen > 1e-9 && (candLen < refLen * 0.9 || candLen > refLen * 1.15)) ok = false;
            // 只在"函数确实变少"时采用拟合, 否则用字体自带分段(精确且不会更差)
            if (cand.size() >= i1 - i0 + 1) ok = false;
        }
        if (ok && !cand.empty()) {
            for (auto &s : cand) out.segs.push_back(s);
        } else {
            for (std::size_t j = i0; j <= i1; ++j) {
                const Piece &pc = pieces[j];
                V2 b[4];
                if (pc.line) {
                    b[0] = pc.p0;
                    b[1] = pc.p0 + (pc.p1 - pc.p0) * (1.0 / 3.0);
                    b[2] = pc.p0 + (pc.p1 - pc.p0) * (2.0 / 3.0);
                    b[3] = pc.p1;
                } else {
                    quadToCubic(pc.p0, pc.c, pc.p1, b);
                }
                std::vector<V2> local{b[0], b[3]};
                GlyphSeg s = segFromBezierChecked(b, local, tol);
                s.exact = true;
                out.segs.push_back(s);
            }
        }
    }
    // 4) 极短段(杂线)去掉; 去掉后如果闭环出现极小缺口, 用一条极短直线补回去
    double minLen = std::max(0.01, tol * 0.25);
    bool wasClosed = pieces.size() > 1 && vlen(pieces.front().p0 - pieces.back().p1) < 1e-6;
    std::vector<GlyphSeg> keep;
    for (auto &s : out.segs)
        if (s.len >= minLen) keep.push_back(s);
    if (keep.empty() && !out.segs.empty()) keep.push_back(out.segs.front());
    out.segs.swap(keep);
    if (wasClosed && !out.segs.empty()) {
        GlyphSeg &last = out.segs.back();
        const GlyphSeg &first = out.segs.front();
        double gap = std::hypot(last.x1 - first.x0, last.y1 - first.y0);
        if (gap > 1e-9 && gap <= std::max(4 * minLen, 1.0) && out.segs.size() > 1) {
            // 把最后一段改成"从它自己的起点直接连回起点"的直线(缺口 <= 0.05% em, 肉眼不可见)
            last.degree = 1;
            last.cx[0] = last.x0;
            last.cy[0] = last.y0;
            last.cx[1] = first.x0 - last.x0;
            last.cy[1] = first.y0 - last.y0;
            last.cx[2] = last.cx[3] = 0;
            last.cy[2] = last.cy[3] = 0;
            last.x1 = first.x0;
            last.y1 = first.y0;
            last.len = gap;
            last.exact = false;
        }
    }

    // 5) 共线直线合并(保留有理数信息, 能精确就精确)
    if (mergeCollinear && out.segs.size() > 1) {
        std::vector<GlyphSeg> merged;
        for (const auto &s : out.segs) {
            if (!merged.empty()) {
                GlyphSeg &p = merged.back();
                if (p.degree == 1 && s.degree == 1) {
                    V2 d1{p.x1 - p.x0, p.y1 - p.y0};
                    V2 d2{s.x1 - s.x0, s.y1 - s.y0};
                    double cross = d1.x * d2.y - d1.y * d2.x;
                    double l1 = vlen(d1), l2 = vlen(d2);
                    if (l1 > 1e-9 && l2 > 1e-9 && std::fabs(cross) / (l1 * l2) < 1e-6 &&
                        dot(d1, d2) > 0) {
                        double nx = s.x1, ny = s.y1;
                        bool ex = p.exact && s.exact;
                        GRat endX{0, 1}, endY{0, 1};
                        if (ex) {
                            endX = ratAdd(s.rx[0], s.rx[1]);
                            endY = ratAdd(s.ry[0], s.ry[1]);
                        }
                        p.x1 = nx;
                        p.y1 = ny;
                        p.cx[0] = p.x0;
                        p.cy[0] = p.y0;
                        p.cx[1] = p.x1 - p.x0;
                        p.cy[1] = p.y1 - p.y0;
                        p.cx[2] = p.cx[3] = 0;
                        p.cy[2] = p.cy[3] = 0;
                        p.len = l1 + l2;
                        p.exact = ex;
                        if (ex) {
                            p.rx[0] = p.rx[0];
                            p.ry[0] = p.ry[0];
                            p.rx[1] = ratSub(endX, p.rx[0]);
                            p.ry[1] = ratSub(endY, p.ry[0]);
                            p.rx[2] = p.rx[3] = GRat{0, 1};
                            p.ry[2] = p.ry[3] = GRat{0, 1};
                        }
                        continue;
                    }
                }
            }
            merged.push_back(s);
        }
        out.segs.swap(merged);
    }
    return out;
}


double shapeTop(const GlyphShape &shape) { return shape.y1; }

bool shapeFromGlyph(const TrueTypeFont &font, uint32_t cp, double size, double ox, double oy,
                    double tol, GlyphShape &out, std::string &err,
                    const std::vector<double> &axisCoords, bool *sparseOut, int forceGid) {
    out = GlyphShape();
    int gid = forceGid >= 0 ? forceGid : font.glyphIndex(cp);   // 连字替换时直接给字形号
    if (gid == 0) {
        err = "字体里没有这个字";
        return false;
    }
    std::vector<TtContour> cs;
    if (axisCoords.empty()) {
        if (!font.outline(gid, cs, err)) return false;
    } else {
        // 可变字体: 按轴坐标插值出点再组段(默认实例下与 outline() 完全一致, 有测试守着)
        if (!font.outlineVaried(gid, axisCoords, cs, err)) {
            if (!font.outline(gid, cs, err)) return false;   // 退化时用默认实例
        }
    }
    double scale = size / double(font.unitsPerEm());
    out.srcSegs = 0;
    out.srcContours = int(cs.size());
    for (const auto &c : cs) out.srcSegs += int(c.segs.size());
    for (const auto &c : cs) {
        // 直接缩放到输出坐标系再拟合: 吸附出来的有理数就是最终坐标, 不会因为后面再做缩放/平移而失真
        GlyphContour gc = fitContour(c, tol * (scale > 0 ? scale : 1.0), true, 0.35, scale, ox, oy);
        if (gc.segs.empty()) continue;
        out.contours.push_back(std::move(gc));
    }
    if (sparseOut) *sparseOut = false;
    if (out.contours.empty()) {
        err = "这个字是空白字形";
        return false;
    }
    // 显式 y = f(x)(x 单调段)
    out.explicitFns.clear();
    for (const auto &c : out.contours)
        explicitForContour(c, std::max(0.05, tol * (size / double(font.unitsPerEm()))), out.explicitFns);
    // 包围盒
    out.x0 = out.y0 = 1e18;
    out.x1 = out.y1 = -1e18;
    for (const auto &c : out.contours)
        for (const auto &s : c.segs) {
            for (const auto &p : {std::pair<double, double>{s.x0, s.y0}, {s.x1, s.y1}}) {
                out.x0 = std::min(out.x0, p.first);
                out.y0 = std::min(out.y0, p.second);
                out.x1 = std::max(out.x1, p.first);
                out.y1 = std::max(out.y1, p.second);
            }
        }
    return true;
}

bool shapeFromStrokes(const std::vector<std::vector<std::pair<double, double>>> &strokes,
                      double tol, GlyphShape &out, std::string &err) {
    out = GlyphShape();
    int srcSegs = 0;
    for (const auto &st : strokes) {
        if (st.size() < 2) continue;
        std::vector<V2> pts;
        for (const auto &p : st) pts.push_back(V2{p.first, p.second});
        ++srcSegs;
        std::vector<Bez> curves;
        V2 t1 = norm(pts[1] - pts[0]);
        V2 t2 = norm(pts[pts.size() - 2] - pts[pts.size() - 1]);
        fitCubic(pts, 0, int(pts.size()) - 1, t1, t2, std::max(0.05, tol), 0, curves);
        GlyphContour gc;
        bool ok = !curves.empty();
        std::vector<GlyphSeg> cand;
        for (auto &c : curves) {
            GlyphSeg sg = segFromBezierChecked(c.p, pts, tol);
            if (maxDeviationOf(sg, pts) > tol) ok = false;
            cand.push_back(sg);
        }
        if (ok) {
            gc.segs = cand;
        } else {
            // 拟合不达标: 退回折线本身(精确)
            for (std::size_t j = 0; j + 1 < pts.size(); ++j) {
                V2 b[4] = {pts[j], pts[j] + (pts[j + 1] - pts[j]) * (1.0 / 3.0),
                           pts[j] + (pts[j + 1] - pts[j]) * (2.0 / 3.0), pts[j + 1]};
                std::vector<V2> local{b[0], b[3]};
                gc.segs.push_back(segFromBezierChecked(b, local, tol));
            }
        }
        if (!gc.segs.empty()) out.contours.push_back(std::move(gc));
    }
    if (out.contours.empty()) {
        err = "没有有效的笔画";
        return false;
    }
    out.srcSegs = srcSegs;
    out.srcContours = int(out.contours.size());
    out.x0 = out.y0 = 1e18;
    out.x1 = out.y1 = -1e18;
    for (const auto &c : out.contours)
        for (const auto &s : c.segs) {
            for (const auto &p : {std::pair<double, double>{s.x0, s.y0}, {s.x1, s.y1}}) {
                out.x0 = std::min(out.x0, p.first);
                out.y0 = std::min(out.y0, p.second);
                out.x1 = std::max(out.x1, p.first);
                out.y1 = std::max(out.y1, p.second);
            }
        }
    return true;
}

std::string svgPath(const GlyphShape &shape, double flipBase, int decimals) {
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(decimals);
    auto Y = [&](double y) { return flipBase - y; };
    for (const auto &c : shape.contours) {
        if (c.segs.empty()) continue;
        o << "M " << c.segs.front().x0 << " " << Y(c.segs.front().y0);
        for (const auto &s : c.segs) {
            if (s.degree == 1) {
                o << " L " << s.x1 << " " << Y(s.y1);
            } else if (s.degree == 2) {
                // 二次: 控制点 = p0 + c1/2
                double qx = s.x0 + s.cx[1] / 2.0;
                double qy = s.y0 + s.cy[1] / 2.0;
                o << " Q " << qx << " " << Y(qy) << " " << s.x1 << " " << Y(s.y1);
            } else {
                double c1x = s.x0 + s.cx[1] / 3.0;
                double c1y = s.y0 + s.cy[1] / 3.0;
                double c2x = s.x1 - (s.cx[1] + 2 * s.cx[2] + 3 * s.cx[3]) / 3.0;
                double c2y = s.y1 - (s.cy[1] + 2 * s.cy[2] + 3 * s.cy[3]) / 3.0;
                o << " C " << c1x << " " << Y(c1y) << " " << c2x << " " << Y(c2y) << " " << s.x1
                  << " " << Y(s.y1);
            }
        }
        o << " Z";
    }
    return o.str();
}

std::string shapeSvg(const GlyphShape &shape, int decimals, double pad) {
    if (shape.contours.empty()) return std::string();
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(3);
    double w = shape.x1 - shape.x0, h = shape.y1 - shape.y0;
    double vx = shape.x0 - pad, vy = shape.y0 - pad, vw = w + 2 * pad, vh = h + 2 * pad;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << vx << " " << vy << " " << vw
      << " " << vh << "\" width=\"" << int(vw) << "\" height=\"" << int(vh)
      << "\" role=\"img\" aria-label=\"glyph\">\n";
    o << "<path fill=\"none\" stroke=\"#1f6feb\" stroke-width=\"" << std::max(1.0, std::max(w, h) / 300.0)
      << "\" stroke-linejoin=\"round\" stroke-linecap=\"round\" d=\"";
    o << svgPath(shape, shape.y0 + shape.y1, decimals);
    o << "\"/>\n</svg>\n";
    return o.str();
}

// 变换之后重新吸附: exact 的含义 = "系数可以写成小分母有理数"
void reSnapSeg(GlyphSeg &s) {
    bool allExact = true;
    for (int axis = 0; axis < 2 && allExact; ++axis) {
        const double *c = axis ? s.cy : s.cx;
        GRat *r = axis ? s.ry : s.rx;
        for (int k = 0; k <= 3; ++k) {
            if (k > s.degree && std::fabs(c[k]) < 1e-12) {
                r[k] = GRat{0, 1};
                continue;
            }
            GRat g;
            if (!ratApprox(c[k], 4096, g, 1e-9 * std::max(1.0, std::fabs(c[k])))) {
                allExact = false;
                break;
            }
            r[k] = g;
        }
    }
    s.exact = allExact;
}

void reSnapExplicit(ExplicitFunc &f) {
    bool allExact = true;
    GRat g[4];
    for (int k = 0; k <= f.degree; ++k) {
        if (!ratApprox(f.c[k], 4096, g[k], 1e-9 * std::max(1.0, std::fabs(f.c[k])))) {
            allExact = false;
            break;
        }
    }
    for (int k = 0; k < 4; ++k) f.rc[k] = (allExact && k <= f.degree) ? g[k] : GRat{0, 1};
    f.exact = allExact;
}

void mirrorShapeX(GlyphShape &shape, double c) {
    GRat cr;
    (void)cr;
    for (auto &ct : shape.contours) {
        for (auto &s : ct.segs) {
            s.cx[0] = c - s.cx[0];
            s.cx[1] = -s.cx[1];
            s.cx[2] = -s.cx[2];
            s.cx[3] = -s.cx[3];
            s.x0 = c - s.x0;
            s.x1 = c - s.x1;
            (void)cr;
            reSnapSeg(s);
        }
    }
    for (auto &f : shape.explicitFns) {
        f.c[0] = f.c[0] + f.c[1] * c + f.c[2] * c * c + f.c[3] * c * c * c; // 近似: 变号后重算
        f.c[1] = -(f.c[1] + 2 * f.c[2] * c + 3 * f.c[3] * c * c);
        f.c[2] = -(f.c[2] + 3 * f.c[3] * c);
        f.c[3] = -f.c[3];
        double a = f.xa, b = f.xb;
        f.xa = c - b;
        f.xb = c - a;
        reSnapExplicit(f);
    }
    double x0 = shape.x0, x1 = shape.x1;
    shape.x0 = c - x1;
    shape.x1 = c - x0;
}

void translateShape(GlyphShape &shape, double dx, double dy) {
    GRat rx, ry;
    (void)rx;
    (void)ry;
    for (auto &ct : shape.contours)
        for (auto &s : ct.segs) {
            s.cx[0] += dx;
            s.cy[0] += dy;
            s.x0 += dx;
            s.y0 += dy;
            s.x1 += dx;
            s.y1 += dy;
            (void)rx;
            (void)ry;
            reSnapSeg(s);
        }
    for (auto &f : shape.explicitFns) {
        // y = f(x - dx) + dy
        double a = -dx;
        double c0 = f.c[0], c1 = f.c[1], c2 = f.c[2], c3 = f.c[3];
        f.c[0] = c0 + c1 * a + c2 * a * a + c3 * a * a * a + dy;
        f.c[1] = c1 + 2 * c2 * a + 3 * c3 * a * a;
        f.c[2] = c2 + 3 * c3 * a;
        f.c[3] = c3;
        f.xa += dx;
        f.xb += dx;
        reSnapExplicit(f);
    }
    shape.x0 += dx;
    shape.x1 += dx;
    shape.y0 += dy;
    shape.y1 += dy;
}

std::string segPlain(const GlyphSeg &s, int decimals, const char *var) {
    std::string x = polyPlain(s.cx, s.rx, s.degree, s.exact, decimals, var);
    std::string y = polyPlain(s.cy, s.ry, s.degree, s.exact, decimals, var);
    return std::string("x(") + var + ") = " + x + ", y(" + var + ") = " + y;
}

std::string segLatex(const GlyphSeg &s, int decimals, const char *var) {
    std::string x = polyLatex(s.cx, s.rx, s.degree, s.exact, decimals, var);
    std::string y = polyLatex(s.cy, s.ry, s.degree, s.exact, decimals, var);
    return std::string("x(") + var + ") = " + x + ",\\quad y(" + var + ") = " + y;
}

std::string explicitPlain(const ExplicitFunc &f, int decimals) {
    std::string body = polyPlain(f.c, f.rc, f.degree, f.exact, decimals, "x");
    return "y = " + body + ",  x ∈ [" + fmtNum(f.xa, decimals) + ", " + fmtNum(f.xb, decimals) + "]";
}

std::string explicitLatex(const ExplicitFunc &f, int decimals) {
    std::string body = polyLatex(f.c, f.rc, f.degree, f.exact, decimals, "x");
    return "y = " + body + ",\\quad x \\in [" + fmtNum(f.xa, decimals) + ",\\ " +
           fmtNum(f.xb, decimals) + "]";
}

// 把 svgPath() 的输出反解回多项式段(只认它自己会生成的 M/L/Q/C/Z)。
// 这是"图 = 函数"的关键一环: 反解出来的是控制点 -> 多项式, 与打印的函数同一形式,
// 于是可以逐段比较, 而不是拿折线近似去比(那样量到的是采样误差, 不是画图误差)。
static bool svgPathToSegments(const std::string &d, std::vector<GlyphSeg> &out, double flip) {
    out.clear();
    std::size_t i = 0;
    auto skipWs = [&]() {
        while (i < d.size() && (d[i] == ' ' || d[i] == ',' || d[i] == '\n' || d[i] == '\t')) ++i;
    };
    auto num = [&](double &v) -> bool {
        skipWs();
        const char *start = d.c_str() + i;
        char *end = nullptr;
        v = std::strtod(start, &end);
        if (end == start) return false;
        i += std::size_t(end - start);
        return true;
    };
    double x = 0, y = 0; // SVG 坐标(已翻转)
    auto mk = [&](double p0x, double p0y, double p1x, double p1y, const double *ctrl, int nctrl) {
        GlyphSeg g;
        double ax = p0x, ay = flip - p0y, bx = p1x, by = flip - p1y; // 翻回数学坐标
        if (nctrl == 0) {
            g.degree = 1;
            g.cx[0] = ax; g.cy[0] = ay;
            g.cx[1] = bx - ax; g.cy[1] = by - ay;
        } else if (nctrl == 1) {
            double qx = ctrl[0], qy = flip - ctrl[1];
            g.degree = 2;
            g.cx[0] = ax; g.cy[0] = ay;
            g.cx[1] = 2 * (qx - ax); g.cy[1] = 2 * (qy - ay);
            g.cx[2] = ax - 2 * qx + bx; g.cy[2] = ay - 2 * qy + by;
        } else {
            double c1x = ctrl[0], c1y = flip - ctrl[1];
            double c2x = ctrl[2], c2y = flip - ctrl[3];
            g.degree = 3;
            g.cx[0] = ax; g.cy[0] = ay;
            g.cx[1] = 3 * (c1x - ax); g.cy[1] = 3 * (c1y - ay);
            g.cx[2] = 3 * (ax - 2 * c1x + c2x); g.cy[2] = 3 * (ay - 2 * c1y + c2y);
            g.cx[3] = bx - 3 * c2x + 3 * c1x - ax; g.cy[3] = by - 3 * c2y + 3 * c1y - ay;
            if (std::fabs(g.cx[3]) < 1e-12 && std::fabs(g.cy[3]) < 1e-12) g.degree = 2;
        }
        g.x0 = ax; g.y0 = ay; g.x1 = bx; g.y1 = by;
        out.push_back(g);
    };
    while (i < d.size()) {
        skipWs();
        if (i >= d.size()) break;
        char c = d[i];
        ++i;
        if (c == 'M') {
            double a, b;
            if (!num(a) || !num(b)) return false;
            x = a; y = b;
        } else if (c == 'L') {
            double a, b;
            if (!num(a) || !num(b)) return false;
            mk(x, y, a, b, nullptr, 0);
            x = a; y = b;
        } else if (c == 'Q') {
            double p[4];
            for (int k = 0; k < 4; ++k)
                if (!num(p[k])) return false;
            mk(x, y, p[2], p[3], p, 1);
            x = p[2]; y = p[3];
        } else if (c == 'C') {
            double p[6];
            for (int k = 0; k < 6; ++k)
                if (!num(p[k])) return false;
            mk(x, y, p[4], p[5], p, 2);
            x = p[4]; y = p[5];
        } else if (c == 'Z') {
            // 闭环: 不做额外段(自检按段比对, 闭合由轮廓本身保证)
        } else {
            return false;
        }
    }
    return !out.empty();
}

GlyphSelfCheck glyphSelfCheck(const GlyphShape &shape, int svgDecimals, int textDecimals) {
    if (textDecimals < 0) textDecimals = svgDecimals;
    GlyphSelfCheck r;
    if (shape.contours.empty()) {
        r.detail = "空图形";
        return r;
    }
    const double flip = shape.y0 + shape.y1;
    std::string d = svgPath(shape, flip, svgDecimals);
    std::vector<GlyphSeg> parsed;
    if (!svgPathToSegments(d, parsed, flip)) {
        r.detail = "预览路径解析失败(说明 svgPath 输出的命令不合法)";
        return r;
    }
    // 展平原曲线(顺序应与 svgPath 生成命令的顺序一致)
    std::vector<const GlyphSeg *> src;
    for (const auto &c : shape.contours)
        for (const auto &sg : c.segs) src.push_back(&sg);
    if (src.size() != parsed.size()) {
        r.detail = "段数对不上: 函数 " + std::to_string(src.size()) + " 段, 预览 " +
                   std::to_string(parsed.size()) + " 段";
        return r;
    }
    double worstSvg = 0, worstText = 0;
    for (std::size_t k = 0; k < src.size(); ++k) {
        const GlyphSeg &a = *src[k];
        const GlyphSeg &b = parsed[k];
        const int K = 32;
        for (int j = 0; j <= K; ++j) {
            double t = double(j) / K;
            double ax = ((a.cx[3] * t + a.cx[2]) * t + a.cx[1]) * t + a.cx[0];
            double ay = ((a.cy[3] * t + a.cy[2]) * t + a.cy[1]) * t + a.cy[0];
            double bx = ((b.cx[3] * t + b.cx[2]) * t + b.cx[1]) * t + b.cx[0];
            double by = ((b.cy[3] * t + b.cy[2]) * t + b.cy[1]) * t + b.cy[0];
            double dev = std::hypot(ax - bx, ay - by);
            if (dev > worstSvg) worstSvg = dev;
        }
        for (int axis = 0; axis < 2; ++axis) {
            const double *cc = axis ? a.cy : a.cx;
            for (int j = 0; j <= a.degree; ++j) {
                std::string tok = a.exact ? std::to_string(cc[j]) : fmtNum(cc[j], textDecimals);
                double back = std::strtod(tok.c_str(), nullptr);
                double rel = std::fabs(back - cc[j]) / std::max(1.0, std::fabs(cc[j]));
                if (rel > worstText) worstText = rel;
            }
        }
    }
    r.svgVsCurve = worstSvg;
    r.textLoss = worstText;
    r.segs = int(src.size());
    r.ok = true;
    return r;
}

std::string shapeStats(const GlyphShape &shape) {
    int n = shape.segCount();
    std::ostringstream o;
    o << "函数 " << n << " 个";
    if (shape.srcSegs > 0) {
        o << "(原始 " << shape.srcSegs << " 段";
        if (shape.srcSegs > 0) {
            int saved = 100 - int(100.0 * n / shape.srcSegs);
            o << ", 省 " << saved << "%";
        }
        o << ")";
    }
    if (!shape.explicitFns.empty()) o << ", 其中显式 y=f(x) " << shape.explicitFns.size() << " 条";
    return o.str();
}

AnchorSpec parseAnchor(const std::string &text) {
    AnchorSpec a;
    std::string s = text;
    // 允许前后空白
    auto trim = [](std::string v) {
        std::size_t b = v.find_first_not_of(" \t\r\n");
        std::size_t e = v.find_last_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string();
        return v.substr(b, e - b + 1);
    };
    s = trim(s);
    if (s.empty()) {
        a.ok = true;
        a.x = a.y = 0;
        return a;
    }
    if (s[0] == ':') {
        a.mirrorX = true;
        s = trim(s.substr(1));
    }
    // 去掉括号
    while (!s.empty() && (s.front() == '(' || s.front() == '[')) s = trim(s.substr(1));
    while (!s.empty() && (s.back() == ')' || s.back() == ']')) s = trim(s.substr(0, s.size() - 1));
    // 分隔符: 逗号 / 空白 (也接受中文逗号 U+FF0C)
    std::string zhComma = "\xEF\xBC\x8C";
    for (std::size_t pos = s.find(zhComma); pos != std::string::npos; pos = s.find(zhComma, pos))
        s.replace(pos, zhComma.size(), ",");
    for (char &c : s)
        if (c == ',' || c == '\t' || c == ';' || c == '\xEF') c = ' ';
    std::istringstream is(s);
    double x = 0, y = 0;
    if (!(is >> x)) {
        a.err = "坐标要写成 (x,x) / (x x) / x,x 这样的两个数";
        return a;
    }
    if (!(is >> y)) {
        a.err = "只给了一个数, 需要左下角的两个坐标";
        return a;
    }
    std::string extra;
    if (is >> extra) {
        a.err = "坐标后面多了东西: " + extra;
        return a;
    }
    a.ok = true;
    a.x = x;
    a.y = y;
    return a;
}

} // namespace em

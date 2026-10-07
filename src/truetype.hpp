// EasyMath - TrueType(glyf) 字体解析: 只为"把文字还原成函数"服务
// 支持: 简单字形 + 复合字形、cmap 0/4/6/12、hmtx 推进宽度、name 家族名。
// 不支持: CFF/OTF 轮廓(会明确报错)、可变字体的非默认实例(用默认实例)、GSUB 连字。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <map>
#include <vector>

namespace em {

// ---- CFF(OTF) 结构信息: B 阶段第一步(只读取结构, 还不解释 charstring) ----
struct CffInfo {
    bool ok = false;
    bool isCff2 = false;
    std::size_t off = 0, len = 0;
    int major = 0, minor = 0, hdrSize = 0;
    int nameCount = 0, topDictCount = 0, stringCount = 0, gsubrCount = 0;
    long charStringsOff = -1, charStringsCount = -1;
    long privateSize = -1, privateOff = -1, subrsOff = -1;
    long charsetOff = -1, fdArrayOff = -1, fdSelectOff = -1;   // CID 字体用
};
// 从整个字体文件字节里读 CFF 结构; 不是 CFF 字体返回 false(并在 err 说明)
bool cffReadInfo(const std::vector<uint8_t> &font, CffInfo &out, std::string &err);

// ---- B2: CFF Type2 charstring 解释(输出路径点; 曲线按折线采样记录包围盒) ----
struct CffPath {
    std::vector<std::pair<double, double>> pts;   // 所有命中点(含曲线采样点)
    std::vector<std::pair<double, double>> ends;  // 路径端点(moveto/lineto/curveto 落点)
    std::size_t subrCalls = 0;
};
bool cffGlyphPath(const std::vector<uint8_t> &font, int gid, CffPath &out, std::string &err);
// CFF/OTF 字体的 cmap 查询(独立于 TrueTypeFont::load —— 它会拒绝 CFF 轮廓)。
// 支持 format 4(BMP)与 12(全平面); 找不到返回 false 并说明。
bool cffCmapLookup(const std::vector<uint8_t> &font, uint32_t cp, int &gid, std::string &err);

// 字体单位下的点(y 向上)
struct TtPoint {
    double x = 0, y = 0;
    bool on = true;
};

enum class TtKind { Line, Quad };

// 一段轮廓: Line 用 p0/p1; Quad 用 p0(起点)/c(控制点)/p1(终点)
struct TtSeg {
    TtKind kind = TtKind::Line;
    TtPoint p0, c, p1;
};

struct TtContour {
    std::vector<TtSeg> segs;
};

struct TtBox {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool valid = false;
};

class TrueTypeFont {
public:
    bool load(std::vector<uint8_t> data, std::string &err);

    bool ok() const { return ok_; }
    int unitsPerEm() const { return upem_; }
    int numGlyphs() const { return numGlyphs_; }
    int ascent() const { return ascent_; }
    int descent() const { return descent_; }
    int lineGap() const { return lineGap_; }
    int advance(int glyph) const;              // 水平推进(字体单位)
    int glyphIndex(uint32_t cp) const;         // 0 表示缺字(.notdef)
    bool hasGlyph(uint32_t cp) const { return glyphIndex(cp) != 0; }
    bool outline(int glyph, std::vector<TtContour> &out, std::string &err) const;
    TtBox glyphBox(int glyph) const;
    const std::string &familyName() const { return family_; }

    // ---- 可变字体(fvar): 轴与命名实例(第一阶段: 解析与报告; 轮廓插值见 gvar 阶段) ----
    struct VarAxis {
        std::string tag;        // 例: wght
        double minV = 0, defV = 0, maxV = 0;
        std::string name;       // 轴的名字(来自 name 表, 可能为空)
    };
    struct VarInstance {
        std::string name;                    // 命名实例(例: Regular/Bold)
        std::vector<double> coords;          // 与轴一一对应
    };
    bool hasVariations() const;

    // ---- gvar 元组元数据(第二阶段第一步: 只解析"哪些元组、区域是什么") ----
    // peak/start/end 是与轴一一对应的 F2Dot14 归一化坐标(用户坐标经轴映射后再归一化)。
    struct GvarTuple {
        std::vector<double> peak;    // 峰值坐标
        std::vector<double> start;   // 中间区域起点(没有中间区域时与 peak 相同)
        std::vector<double> end;     // 中间区域终点
        bool intermediate = false;   // 是否带中间区域
        bool embeddedPeak = false;   // 峰值坐标是内嵌在元组头里, 还是引用共享元组
        int sharedIndex = -1;        // 引用共享元组的下标(-1 = 内嵌)
        std::size_t dataSize = 0;    // 该元组的序列化数据字节数
        std::size_t dataOffset = 0;  // 相对"字形变体数据"起点的偏移
        bool privatePoints = false;  // 自带点数表(否则用共享点数或全文)
        // ---- 第二阶段第二步: 点增量(未做 IUP 推断) ----
        std::vector<int> points;     // 受影响的点序号; 空 = 全部点
        std::vector<double> dx, dy;  // 与受影响点一一对应的增量(字体单位)
    };
    bool hasGvar() const;
    // GPOS 'kern' 成对字距(字体单位, xAdvance 调整; 负=更紧)。没有 GPOS/kern 时返回 0。
    int pairKern(int left, int right) const;
    // GSUB 连字: 按优先级 liga -> calt -> dlig 找, 返回连字字形号(没有则 0);
    // whichFeature 可选, 回填命中的特性标签(便于诊断与测试)
    int ligature(int first, int second, std::string *whichFeature = nullptr) const;
    bool hasLiga() const;   // 存在 liga/calt/dlig 之一即算可用
    bool hasKern() const { return gposKernUsable(); }
    // 简单字形的原始点数(不含 4 个幻影点); 复合字形/空字形返回 -1
    int glyphPointCount(int glyph) const;
    // 简单字形的原始点坐标与轮廓边界(endPts 是每个轮廓最后一个点的下标)
    bool glyphPoints(int glyph, std::vector<double> &xs, std::vector<double> &ys,
                     std::vector<int> &endPts) const;
    // 同上, 但额外给出每个点是否在曲线上(on-curve), 供自行组段使用
    bool glyphPointsEx(int glyph, std::vector<double> &xs, std::vector<double> &ys,
                       std::vector<char> &onCurve, std::vector<int> &endPts) const;
    // 变体轮廓: 按轴坐标插值出点后组段(默认实例下应与 outline() 完全一致)
    bool outlineVaried(int glyph, const std::vector<double> &axisUserCoords,
                       std::vector<TtContour> &out, std::string &err) const;
    // 按轴的用户坐标取"变体点坐标"(可变字体插值)。
    // 只处理"全点引用"的元组(不需要 IUP); 若某元组是稀疏点数(需要 IUP 推断)则
    // 置 sparse=true 并**保持默认实例坐标**(宁可退回默认, 不给错的轮廓)。
    bool variedGlyphPoints(int glyph, const std::vector<double> &axisUserCoords,
                           std::vector<double> &outXs, std::vector<double> &outYs,
                           std::vector<int> &endPts, bool &sparse) const;
    // IUP: 未引用点的增量按轮廓端点线性推断(x/y 各自插值并按两端增量夹住)。
    // referenced 是受影响的点下标(升序), dx/dy 与之对应; 结果写进 outDx/outDy(长度 = 点数)。
    static void inferUnreferencedDeltas(const std::vector<double> &xs, const std::vector<double> &ys,
                                        const std::vector<int> &endPts,
                                        const std::vector<int> &referenced,
                                        const std::vector<double> &dx, const std::vector<double> &dy,
                                        std::vector<double> &outDx, std::vector<double> &outDy);
    // 把用户坐标按轴映射成归一化坐标(-1..1): 默认值->0, 最大->1, 最小->-1
    static void normalizeVarCoords(const std::vector<VarAxis> &axes, const std::vector<double> &user,
                                  std::vector<double> &out);
    // 某元组在给定归一化坐标下的权重(0..1); 任一带峰值约束的轴落在区域外则为 0
    static double tupleScalar(const GvarTuple &t, const std::vector<double> &coords);
    // 取某个字形的元组元数据; 没有 gvar 或该字形无变体时返回 false
    bool glyphGvarTuples(int glyph, std::vector<GvarTuple> &out) const;
    bool variations(std::vector<VarAxis> &axes, std::vector<VarInstance> &insts) const;
    // 用于诊断: 支持到的 cmap 表格式
    int cmapFormat() const { return cmapFormat_; }

private:
    std::size_t tableOff(const char *tag, std::size_t *len) const;
    bool parseCmap(std::string &err);

    std::vector<uint8_t> data_;
    bool ok_ = false;
    int upem_ = 1000;
    int numGlyphs_ = 0;
    int ascent_ = 800, descent_ = -200, lineGap_ = 0;
    int indexToLoc_ = 0;
    int numHMetrics_ = 0;
    bool gposKernUsable() const;   // 首次调用时解析并缓存 kern lookups
    mutable bool kernParsed_ = false;
    mutable bool kernOk_ = false;
    mutable std::map<uint64_t, int> kernCache_;
    mutable bool ligaParsed_ = false;
    mutable bool ligaOk_ = false;
    std::size_t locaOff_ = 0, locaLen_ = 0;
    std::size_t glyfOff_ = 0, glyfLen_ = 0;
    std::size_t hmtxOff_ = 0, hmtxLen_ = 0;
    std::size_t cmapSub_ = 0;   // 选中的子表偏移
    int cmapFormat_ = -1;
    bool cmapSymbol_ = false;   // (3,0) 符号字体: 需要 0xF000 偏移
    std::string family_;
};

// 从字体字节里读出家族名(失败返回空)
std::string ttFamilyName(const std::vector<uint8_t> &data);

} // namespace em

/**
 * HostFontBackend.cpp
 *
 * 本体（吉里吉里Z）のフォントサービス（FontServiceIntf、tp_stub 経由）を
 * richtext::FontBackend として実装する。
 *
 * 前提とする本体側の性質:
 *  - face はレジストリで共有される。ピクセルサイズ・変形・可変軸座標は
 *    face の状態なので、**取得のたびにサイズを渡す**（本 I/F はそうなっている）
 *  - 可変軸だけは「自分専用の設定を保ちたい」ので、可変軸を持つフォントは
 *    TVPFontAcquireFaceInstance で専用 face を開く（バイト列は共有のまま）
 *  - アウトラインはフォントユニット・y-up（richtext の FontOutlineSink と同じ規約）
 */

// richtext のヘッダを ncbind (windows.h) より先に入れる（main.cpp と同じ理由）
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "HostFontBackend.hpp"

#include "ncbind.hpp"

#include <string>
#include <vector>

namespace krkr_richtext {

namespace {

ttstr utf8ToTtstr(const std::string& s)
{
    if (s.empty()) return ttstr();
    tjs_int len = TVPUtf8ToWideCharString(s.c_str(), nullptr);
    if (len <= 0) return ttstr();
    std::vector<tjs_char> buf(static_cast<size_t>(len) + 1, 0);
    len = TVPUtf8ToWideCharString(s.c_str(), buf.data());
    if (len <= 0) return ttstr();
    buf[static_cast<size_t>(len)] = 0;
    return ttstr(buf.data());
}

std::string ttstrToUtf8(const ttstr& s)
{
    if (s.IsEmpty()) return std::string();
    tjs_int len = TVPWideCharToUtf8String(s.c_str(), nullptr);
    if (len <= 0) return std::string();
    std::vector<char> buf(static_cast<size_t>(len) + 1, 0);
    len = TVPWideCharToUtf8String(s.c_str(), buf.data());
    if (len <= 0) return std::string();
    return std::string(buf.data(), static_cast<size_t>(len));
}

int toPixelSize(float size)
{
    int px = static_cast<int>(size + 0.5f);
    return px > 0 ? px : 1;
}

/**
 * richtext の FontOutlineSink を本体の iTVPFontOutlineSink に橋渡し
 */
class OutlineSinkAdapter : public iTVPFontOutlineSink
{
public:
    explicit OutlineSinkAdapter(richtext::FontOutlineSink& sink) : Sink(sink) {}
    void TJS_INTF_METHOD MoveTo(float x, float y) override { Sink.moveTo(x, y); }
    void TJS_INTF_METHOD LineTo(float x, float y) override { Sink.lineTo(x, y); }
    void TJS_INTF_METHOD QuadTo(float cx, float cy, float x, float y) override {
        Sink.quadTo(cx, cy, x, y);
    }
    void TJS_INTF_METHOD CubicTo(float c1x, float c1y, float c2x, float c2y,
                                 float x, float y) override {
        Sink.cubicTo(c1x, c1y, c2x, c2y, x, y);
    }
    void TJS_INTF_METHOD ClosePath() override { Sink.close(); }

private:
    richtext::FontOutlineSink& Sink;
};

//------------------------------------------------------------------------------
// FontBackendFace 実装
//------------------------------------------------------------------------------

class HostFace : public richtext::FontBackendFace
{
public:
    HostFace(tTVPFontFaceHandle handle, std::string key)
        : Handle(handle), Key(std::move(key)) {}

    ~HostFace() override
    {
        if (Handle) TVPFontReleaseFace(Handle);
    }

    /// 開けなければ nullptr
    static std::shared_ptr<HostFace> open(const std::string& keyU8)
    {
        const ttstr name = utf8ToTtstr(keyU8);
        tTVPFontFaceHandle h = TVPFontAcquireFace(name);
        if (!h) return nullptr;

        // 可変軸を持つフォントは専用 face に切り替える。軸座標は face の状態
        // なので、共有 face に設定すると本体 drawText や他プラグインの描画まで
        // 変わってしまう（richtext は wght 別に face を登録して使う）
        if (TVPFontGetVarAxes(h, nullptr, 0) > 0) {
            TVPFontReleaseFace(h);
            h = TVPFontAcquireFaceInstance(name, nullptr, 0);
            if (!h) return nullptr;
        }

        auto face = std::make_shared<HostFace>(h, keyU8);
        face->resolveInfo(name);
        return face;
    }

    // --- メタデータ ---
    const std::string& familyName() const override { return Family; }
    const std::string& styleName() const override { return Style; }
    bool isColorFont() const override { return Color; }
    bool isScalable() const override { return Scalable; }
    richtext::FontFaceMetrics faceMetrics() const override { return Metrics; }

    const void* fontData() const override { return Data; }
    size_t fontDataSize() const override { return static_cast<size_t>(DataSize); }
    int faceIndex() const override { return FaceIndex; }

    // --- グリフ ---
    bool getGlyphMetrics(uint32_t glyphId, float pixelSize, bool bold, bool italic,
                         bool unhinted, richtext::FontGlyphMetrics& out) const override
    {
        tTVPFontGlyphMetrics m = {};
        const tjs_int mode = unhinted ? TVP_FONT_METRICS_UNHINTED : TVP_FONT_METRICS_HINTED;
        if (!TVPFontGetGlyphMetricsEx(Handle, glyphId, toPixelSize(pixelSize),
                                      bold, italic, mode, &m)) {
            return false;
        }
        assign(m, out);
        return true;
    }

    bool getGlyphMetricsUnscaled(uint32_t glyphId, bool bold, bool italic,
                                 richtext::FontGlyphMetrics& out) const override
    {
        tTVPFontGlyphMetrics m = {};
        if (!TVPFontGetGlyphMetricsEx(Handle, glyphId, 0, bold, italic,
                                      TVP_FONT_METRICS_UNSCALED, &m)) {
            return false;
        }
        assign(m, out);
        return true;
    }

    bool getGlyphOutline(uint32_t glyphId, bool bold, bool italic,
                         richtext::FontOutlineSink& sink) const override
    {
        OutlineSinkAdapter adapter(sink);
        return TVPFontGetGlyphOutline(Handle, glyphId, bold, italic, &adapter);
    }

    bool getGlyphBitmap(uint32_t glyphId, float pixelSize, bool color,
                        bool bold, bool italic,
                        richtext::FontGlyphBitmapView& out) override
    {
        tTVPFontGlyphBitmap b = {};
        if (!TVPFontGetGlyphBitmap(Handle, glyphId, toPixelSize(pixelSize), color,
                                   bold, italic, &b)) {
            return false;
        }
        if (b.Width <= 0 || b.Height <= 0 || !b.Buffer) return false;
        out.format = (b.Format == TVP_FONT_BITMAP_BGRA)
                         ? richtext::FontBitmapFormat::BGRA
                         : richtext::FontBitmapFormat::Gray;
        out.left = static_cast<int>(b.Left);
        out.top = static_cast<int>(b.Top);
        out.width = static_cast<int>(b.Width);
        out.rows = static_cast<int>(b.Height);
        out.pitch = static_cast<int>(b.Pitch);
        out.buffer = b.Buffer;
        return true;
    }

    uint32_t getGlyphIndex(char32_t codepoint) const override
    {
        return TVPFontGetGlyphIndex(Handle, static_cast<tjs_uint32>(codepoint));
    }

    // --- バリアブルフォント ---
    bool isVariableFont() const override { return !Axes.empty(); }

    std::vector<richtext::FontVarCoord> getAxes() const override
    {
        std::vector<richtext::FontVarCoord> out;
        out.reserve(Axes.size());
        for (const auto& a : Axes) out.push_back({a.Tag, a.DefaultValue});
        return out;
    }

    bool setVariations(const std::vector<richtext::FontVarCoord>& coords) override
    {
        if (coords.empty()) return false;
        std::vector<tTVPFontVarCoord> v;
        v.reserve(coords.size());
        for (const auto& c : coords) v.push_back(tTVPFontVarCoord{c.tag, c.value});
        return TVPFontSetVariations(Handle, v.data(), static_cast<tjs_int>(v.size()));
    }

    bool getAxisRange(uint32_t tag, float& minValue, float& defaultValue,
                      float& maxValue) const override
    {
        for (const auto& a : Axes) {
            if (a.Tag != tag) continue;
            minValue = a.MinValue;
            defaultValue = a.DefaultValue;
            maxValue = a.MaxValue;
            return true;
        }
        return false;
    }

    bool getGlyphMask(uint32_t glyphId, const richtext::FontRenderParams& params,
                      richtext::FontGlyphMask& out) override
    {
        tTVPFontRenderParams rp = {};
        rp.Transform[0] = params.transform.xx;
        rp.Transform[1] = params.transform.xy;
        rp.Transform[2] = params.transform.dx;
        rp.Transform[3] = params.transform.yx;
        rp.Transform[4] = params.transform.yy;
        rp.Transform[5] = params.transform.dy;
        rp.Bold = params.bold;
        rp.Italic = params.italic;
        rp.StrokeWidth = params.strokeWidth;
        rp.Join = (params.join == richtext::FontStrokeJoin::Miter) ? TVP_FONT_JOIN_MITER
                : (params.join == richtext::FontStrokeJoin::Bevel) ? TVP_FONT_JOIN_BEVEL
                                                                   : TVP_FONT_JOIN_ROUND;
        rp.Cap = (params.cap == richtext::FontStrokeCap::Butt)   ? TVP_FONT_CAP_BUTT
               : (params.cap == richtext::FontStrokeCap::Square) ? TVP_FONT_CAP_SQUARE
                                                                 : TVP_FONT_CAP_ROUND;
        rp.MiterLimit = params.miterLimit;

        tTVPFontGlyphMask mask = {};
        if (!TVPFontRenderGlyphMask(Handle, glyphId, &rp, &mask)) return false;
        out.left = static_cast<int>(mask.Left);
        out.top = static_cast<int>(mask.Top);
        out.width = static_cast<int>(mask.Width);
        out.rows = static_cast<int>(mask.Height);
        out.pitch = static_cast<int>(mask.Pitch);
        out.buffer = mask.Buffer;
        return true;
    }

    bool getColorLayers(uint32_t glyphId, float pixelSize,
                        std::vector<richtext::FontColorLayer>& out,
                        richtext::FontColorGlyphBox* box) override
    {
        out.clear();
        LayerSink sink(out);
        float clip[4] = {0, 0, 0, 0};
        const tjs_int n = TVPFontGetColorLayers(Handle, glyphId, toPixelSize(pixelSize),
                                                &sink, clip);
        if (n <= 0) return false;
        if (box) {
            box->xMin = clip[0];
            box->yMin = clip[1];
            box->xMax = clip[2];
            box->yMax = clip[3];
            // 4 要素すべて 0 = クリップボックス無し
            box->valid = !(clip[0] == 0.f && clip[1] == 0.f && clip[2] == 0.f && clip[3] == 0.f);
        }
        return true;
    }

private:
    /// 本体のカラーレイヤー通知を richtext の配列に積む
    class LayerSink : public iTVPFontColorLayerSink
    {
    public:
        explicit LayerSink(std::vector<richtext::FontColorLayer>& out) : Out(out) {}
        void TJS_INTF_METHOD Layer(const tTVPFontColorLayer& l) override
        {
            richtext::FontColorLayer dst;
            dst.glyphId = l.GlyphId;
            for (int i = 0; i < 6; i++) dst.transform[i] = l.Transform[i];
            dst.paint.kind = (l.PaintKind == TVP_FONT_PAINT_LINEAR)
                                 ? richtext::FontPaintKind::LinearGradient
                             : (l.PaintKind == TVP_FONT_PAINT_RADIAL)
                                 ? richtext::FontPaintKind::RadialGradient
                                 : richtext::FontPaintKind::Solid;
            dst.paint.r = l.R; dst.paint.g = l.G; dst.paint.b = l.B; dst.paint.a = l.A;
            dst.paint.x0 = l.X0; dst.paint.y0 = l.Y0;
            dst.paint.x1 = l.X1; dst.paint.y1 = l.Y1;
            dst.paint.r0 = l.R0; dst.paint.r1 = l.R1;
            for (tjs_int i = 0; i < l.StopCount; i++) {
                const tTVPFontColorStop& s = l.Stops[i];
                dst.paint.stops.push_back({s.Offset, s.R, s.G, s.B, s.A});
            }
            Out.push_back(std::move(dst));
        }
    private:
        std::vector<richtext::FontColorLayer>& Out;
    };

    static void assign(const tTVPFontGlyphMetrics& src, richtext::FontGlyphMetrics& dst)
    {
        dst.advanceX = src.AdvanceX;
        dst.advanceY = src.AdvanceY;
        dst.bearingX = src.BearingX;
        dst.bearingY = src.BearingY;
        dst.width = src.Width;
        dst.height = src.Height;
    }

    void resolveInfo(const ttstr& name)
    {
        tTVPFontFaceInfo info;
        if (TVPFontGetFaceInfo(name, &info)) {
            Family = ttstrToUtf8(info.Family);
            Style = ttstrToUtf8(info.Subfamily);
            Color = info.Color;
            Scalable = info.Scalable;
        }

        // 可変軸
        const tjs_int axisCount = TVPFontGetVarAxes(Handle, nullptr, 0);
        if (axisCount > 0) {
            Axes.resize(static_cast<size_t>(axisCount));
            TVPFontGetVarAxes(Handle, Axes.data(), axisCount);
        }

        // SFNT バイト列（minikin が自前の hb_face を作るのに使う）
        const tjs_uint8* data = nullptr;
        tjs_uint64 size = 0;
        tjs_int faceIndex = 0;
        if (TVPFontGetFaceData(Handle, &data, &size, &faceIndex)) {
            Data = data;
            DataSize = size;
            FaceIndex = static_cast<int>(faceIndex);
        }

        // フォントユニットのラインメトリクス。
        // 本体 API はスケール済みの値しか返さないので、ピクセルサイズを
        // unitsPerEm に張って 1:1 スケールで引く（ascender/descender は
        // フォントユニットでは整数なので丸め誤差なく取れる）。
        tTVPFontLineMetrics lm = {};
        if (TVPFontGetLineMetrics(Handle, 16, &lm) && lm.UnitsPerEm > 0) {
            Metrics.unitsPerEm = lm.UnitsPerEm;
            const tjs_int upem = static_cast<tjs_int>(lm.UnitsPerEm + 0.5f);
            tTVPFontLineMetrics unit = {};
            if (upem > 0 && TVPFontGetLineMetrics(Handle, upem, &unit)) {
                Metrics.ascenderUnits = unit.Ascent;
                Metrics.descenderUnits = -unit.Descent;   // FT 慣習（下向きが負）
                Metrics.heightUnits = unit.Ascent + unit.Descent + unit.LineGap;
            }
        }
    }

    tTVPFontFaceHandle Handle = nullptr;
    std::string Key;
    std::string Family;
    std::string Style;
    bool Color = false;
    bool Scalable = true;
    richtext::FontFaceMetrics Metrics;
    std::vector<tTVPFontVarAxis> Axes;
    const tjs_uint8* Data = nullptr;
    tjs_uint64 DataSize = 0;
    int FaceIndex = 0;
};

//------------------------------------------------------------------------------
// FontBackend 実装
//------------------------------------------------------------------------------

class HostFontBackend : public richtext::FontBackend
{
public:
    std::shared_ptr<richtext::FontBackendFace> openFace(const std::string& key,
                                                        int /*index*/) override
    {
        // index は本体側が名前解決の一部として決める（fonts.json の faceIndex や
        // GDI フォントの TTC 索引）ので、ここでは渡さない
        return HostFace::open(key);
    }
};

} // namespace

std::shared_ptr<richtext::FontBackend> createHostFontBackend()
{
    // 「本体が face を開けるか」はフォント名を決めないと試せない（本体の
    // 名前解決は fonts.json / ストレージ / GDI 名を順に見るので、空文字列や
    // 適当な名前では判定にならない）。ここでは常にバックエンドを返し、
    // 実際に開けるかは openFace() の結果で分かるようにしている。
    // 本体が glyphware 無効ビルドなら openFace が nullptr を返し続ける。
    return std::make_shared<HostFontBackend>();
}

} // namespace krkr_richtext

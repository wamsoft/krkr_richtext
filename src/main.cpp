// richtext.hppをncbind.hppの前にインクルードして、
// minikinヘッダとwindows.hのコンフリクトを回避
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "richtext.hpp"

// ncbind.hppをrichtext.hppの後にインクルード
#include "ncbind.hpp"

#include <thorvg.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

using namespace richtext;

// ncbind用の型定義
typedef float REAL;

// ============================================================================
// ログ出力
// ============================================================================

void message_log(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    char msg[1024];
    _vsnprintf_s(msg, 1024, _TRUNCATE, format, args);
    TVPAddLog(ttstr(msg));
    va_end(args);
}

void error_log(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    char msg[1024];
    _vsnprintf_s(msg, 1024, _TRUNCATE, format, args);
    TVPAddImportantLog(ttstr(msg));
    va_end(args);
}

// ============================================================================
// ユーティリティ: TJS文字列 ⇔ UTF-16 変換
// ============================================================================

/**
 * tjs_char* (UTF-16) を std::u16string に変換
 */
static std::u16string tjsToU16(const tjs_char* str)
{
    if (!str) return std::u16string();
    return std::u16string(reinterpret_cast<const char16_t*>(str));
}

/**
 * std::u16string を ttstr に変換
 */
static ttstr u16ToTjs(const std::u16string& str)
{
    return ttstr(reinterpret_cast<const tjs_char*>(str.c_str()));
}

/**
 * tjs_char* を std::string (UTF-8/ANSI) に変換
 */
static std::string tjsToNarrow(const tjs_char* str)
{
    if (!str) return std::string();
    tTJSString s(str);
    tjs_int len = s.GetNarrowStrLen();
    std::string result(len, '\0');
    s.ToNarrowStr(&result[0], len + 1);
    return result;
}

/**
 * 配列かどうかの判定
 */
static bool IsArray(const tTJSVariant& var)
{
    if (var.Type() == tvtObject) {
        iTJSDispatch2* obj = var.AsObjectNoAddRef();
        return obj && obj->IsInstanceOf(0, NULL, NULL, TJS_W("Array"), obj) == TJS_S_TRUE;
    }
    return false;
}

// ============================================================================
// TJSラッパークラス: RichTextStyle (TextStyle のラッパー)
// ============================================================================

class RichTextStyle {
public:
    TextStyle style;
    
    RichTextStyle() {}
    
    // フォントコレクション設定（フォント名の配列から）
    void setFonts(tTJSVariant names) {
        if (!IsArray(names)) {
            TVPThrowExceptionMessage(TJS_W("fonts must be an array"));
            return;
        }
        
        ncbPropAccessor arr(names);
        tjs_int count = arr.GetArrayCount();
        std::vector<std::string> fontNames;
        
        for (tjs_int i = 0; i < count; ++i) {
            ttstr name = arr.getStrValue(i);
            fontNames.push_back(tjsToNarrow(name.c_str()));
        }
        
        style.fontCollection = FontManager::instance().createCollection(fontNames);
    }
    
    // プロパティ: fontSize
    void setFontSize(REAL size) { style.fontSize = size; }
    REAL getFontSize() const { return style.fontSize; }
    
    // プロパティ: fontWeight
    void setFontWeight(int weight) { style.fontWeight = static_cast<uint16_t>(weight); }
    int getFontWeight() const { return style.fontWeight; }
    
    // プロパティ: italic
    void setItalic(bool v) { style.italic = v; }
    bool getItalic() const { return style.italic; }
    
    // プロパティ: letterSpacing
    void setLetterSpacing(REAL v) { style.letterSpacing = v; }
    REAL getLetterSpacing() const { return style.letterSpacing; }
    
    // プロパティ: wordSpacing
    void setWordSpacing(REAL v) { style.wordSpacing = v; }
    REAL getWordSpacing() const { return style.wordSpacing; }
    
    // プロパティ: scaleX
    void setScaleX(REAL v) { style.scaleX = v; }
    REAL getScaleX() const { return style.scaleX; }
    
    // プロパティ: skewX
    void setSkewX(REAL v) { style.skewX = v; }
    REAL getSkewX() const { return style.skewX; }
    
    // プロパティ: locale
    void setLocale(const tjs_char* locale) {
        style.localeId = FontManager::instance().registerLocale(tjsToNarrow(locale));
    }
    
    // クローン
    RichTextStyle* clone() const {
        RichTextStyle* c = new RichTextStyle();
        c->style = style;
        return c;
    }
};

// ============================================================================
// TJSラッパークラス: RichTextAppearance (Appearance のラッパー)
// ============================================================================

class RichTextAppearance {
public:
    Appearance appearance;
    
    RichTextAppearance() {}
    
    /**
     * 塗り追加
     * @param color ARGB色値
     * @param offsetX X方向オフセット（省略可）
     * @param offsetY Y方向オフセット（省略可）
     */
    void addFill(tjs_uint32 color, REAL offsetX = 0, REAL offsetY = 0) {
        appearance.addFill(color, offsetX, offsetY);
    }
    
    /**
     * ストローク追加
     * @param color ARGB色値
     * @param width 線幅
     * @param offsetX X方向オフセット（省略可）
     * @param offsetY Y方向オフセット（省略可）
     */
    void addStroke(tjs_uint32 color, REAL width, REAL offsetX = 0, REAL offsetY = 0) {
        appearance.addStroke(color, width, offsetX, offsetY);
    }
    
    /**
     * 影追加（塗りで実装）
     * @param color 影色
     * @param offsetX X方向オフセット
     * @param offsetY Y方向オフセット
     */
    void addShadow(tjs_uint32 color, REAL offsetX, REAL offsetY) {
        // 影は最初に描画されるように先頭に追加する必要があるため
        // Appearanceを再構築
        Appearance newApp;
        newApp.addFill(color, offsetX, offsetY);
        for (const auto& s : appearance.getStyles()) {
            if (s.type == DrawStyle::Type::Fill) {
                newApp.addFill(s);
            } else {
                newApp.addStroke(s);
            }
        }
        appearance = newApp;
    }
    
    /**
     * 全スタイルクリア
     */
    void clear() {
        appearance.clear();
    }
    
    /**
     * スタイルが空かどうか
     */
    bool isEmpty() const {
        return appearance.isEmpty();
    }
    
    /**
     * スタイル数
     */
    int getCount() const {
        return static_cast<int>(appearance.size());
    }
    
    /**
     * クローン
     */
    RichTextAppearance* clone() const {
        RichTextAppearance* c = new RichTextAppearance();
        c->appearance = appearance;
        return c;
    }
};

// ============================================================================
// TJSラッパークラス: RichTextLayout (TextLayout のラッパー)
// ============================================================================

class RichTextLayout {
public:
    TextLayout layout;

    RichTextLayout() {}

    void measure(const tjs_char* text, RichTextStyle* style) {
        if (!style) TVPThrowExceptionMessage(TJS_W("style is required"));
        layout.layout(tjsToU16(text), style->style);
    }

    REAL getWidth() const { return layout.getWidth(); }
    REAL getHeight() const { return layout.getHeight(); }
    REAL getAscent() const { return layout.getAscent(); }
    REAL getDescent() const { return layout.getDescent(); }
    int getGlyphCount() const { return static_cast<int>(layout.getGlyphCount()); }

    RichTextLayout* clone() const {
        RichTextLayout* c = new RichTextLayout();
        c->layout = layout;
        return c;
    }
};

// ============================================================================
// TJSラッパークラス: RichTextParagraphLayout (ParagraphLayout のラッパー)
// ============================================================================

class RichTextParagraphLayout {
public:
    ParagraphLayout layout;

    RichTextParagraphLayout() {}

    void measure(const tjs_char* text, REAL maxWidth, RichTextStyle* style) {
        if (!style) TVPThrowExceptionMessage(TJS_W("style is required"));
        cachedText_ = tjsToU16(text);
        cachedMaxWidth_ = maxWidth;
        cachedStyle_ = &style->style;
        layout.layout(cachedText_, maxWidth, style->style);
    }

    int getLineCount() const { return static_cast<int>(layout.getLineCount()); }
    REAL getTotalHeight() const { return layout.getTotalHeight(); }
    REAL getMaxWidth() const { return layout.getMaxWidth(); }
    int getTotalGlyphCount() const { return static_cast<int>(layout.getTotalGlyphCount()); }

    void setLineSpacing(REAL v) { layout.setLineSpacing(v); }
    REAL getLineSpacing() const { return layout.getLineSpacing(); }

    tTJSVariant getLineInfo(int index) const {
        if (index < 0 || index >= static_cast<int>(layout.getLineCount())) {
            TVPThrowExceptionMessage(TJS_W("line index out of range"));
        }
        const auto& line = layout.getLine(index);
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        tTJSVariant val;
        val = static_cast<int>(line.startIndex);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("startIndex"), nullptr, &val, dict);
        val = static_cast<int>(line.endIndex);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("endIndex"), nullptr, &val, dict);
        val = line.width;
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("width"), nullptr, &val, dict);
        val = line.height();
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("height"), nullptr, &val, dict);
        tTJSVariant result(dict, dict);
        dict->Release();
        return result;
    }

    RichTextParagraphLayout* clone() const {
        RichTextParagraphLayout* c = new RichTextParagraphLayout();
        if (cachedStyle_) {
            c->cachedText_ = cachedText_;
            c->cachedMaxWidth_ = cachedMaxWidth_;
            c->cachedStyle_ = cachedStyle_;
            c->layout.setLineSpacing(layout.getLineSpacing());
            c->layout.setBreakStrategy(layout.getBreakStrategy());
            c->layout.layout(cachedText_, cachedMaxWidth_, *cachedStyle_);
        }
        return c;
    }

private:
    std::u16string cachedText_;
    float cachedMaxWidth_ = 0;
    const TextStyle* cachedStyle_ = nullptr;
};

// rect を [x,t,w,h] の配列にする
tTJSVariant toVariant(const richtext::RectF& rect) {
    tTJSVariant result;
    tTJSVariant x(rect.x);
    tTJSVariant y(rect.y);
    tTJSVariant w(rect.width);
    tTJSVariant h(rect.height);
    tTJSVariant *points[4] = {&x, &y, &w, &h};
    iTJSDispatch2* arr = TJSCreateArrayObject();
    static tjs_uint32 pushHint;
    arr->FuncCall(0, TJS_W("push"), &pushHint, 0, 4, points, arr);
    result = tTJSVariant(arr, arr);
    arr->Release();
    return result;
}

// 列挙型コンバータ
NCB_TYPECONV_CAST_INTEGER(ParagraphLayout::HAlign);
NCB_TYPECONV_CAST_INTEGER(ParagraphLayout::VAlign);

// ============================================================================
// レイヤー拡張: LayerExRichText
// ============================================================================

class LayerExRichText {
protected:
    iTJSDispatch2* _obj;
    
    // プロパティキャッシュ
    tjs_int _width, _height, _pitch;
    tjs_uint32* _buffer;
    
    // テキストレンダラ
    TextRenderer renderer_;
    
public:
    LayerExRichText(iTJSDispatch2* obj) : _obj(obj), _width(0), _height(0), _pitch(0), _buffer(nullptr) {
    }
    
    virtual ~LayerExRichText() {
    }
    
    /**
     * レイヤー情報の更新
     */
    void reset() {
        tTJSVariant val;
        
        // imageWidth
        _obj->PropGet(0, TJS_W("imageWidth"), nullptr, &val, _obj);
        _width = static_cast<tjs_int>(val);
        
        // imageHeight
        _obj->PropGet(0, TJS_W("imageHeight"), nullptr, &val, _obj);
        _height = static_cast<tjs_int>(val);
        
        // mainImageBufferForWrite
        _obj->PropGet(0, TJS_W("mainImageBufferForWrite"), nullptr, &val, _obj);
        _buffer = reinterpret_cast<tjs_uint32*>(static_cast<tjs_intptr_t>(val));
        
        // mainImageBufferPitch
        _obj->PropGet(0, TJS_W("mainImageBufferPitch"), nullptr, &val, _obj);
        _pitch = static_cast<tjs_int>(val);
        
        // レンダラにキャンバスを設定
        renderer_.setCanvas(_buffer, _width, _height, _pitch);
    }
    
    /**
     * 再描画指定
     */
    void redraw(int x, int y, int w, int h) {
        tTJSVariant vars[4] = { x, y, w, h };
        tTJSVariant* varsp[4] = { vars, vars + 1, vars + 2, vars + 3 };
        
        tTJSVariant result;
        _obj->FuncCall(0, TJS_W("update"), nullptr, &result, 4, varsp, _obj);
    }
    
    // ------------------------------------------------------------------
    // 描画メソッド
    // ------------------------------------------------------------------

    /**
     * 1行テキスト描画
     */
    tTJSVariant drawStyleText(const tjs_char* text, REAL x, REAL y,
                               RichTextStyle* style, RichTextAppearance* appearance)
    {
        if (!style || !appearance) {
            TVPThrowExceptionMessage(TJS_W("style and appearance are required"));
        }
        std::u16string u16text = tjsToU16(text);
        richtext::RectF rect = renderer_.drawText(u16text, x, y, style->style, appearance->appearance);
        renderer_.sync();
        redraw(static_cast<int>(rect.x), static_cast<int>(rect.y),
               static_cast<int>(rect.width) + 1, static_cast<int>(rect.height) + 1);
        return toVariant(rect);
    }

    /**
     * パラグラフ描画
     */
    tTJSVariant drawStyleParagraph(const tjs_char* text, REAL x, REAL y, REAL width, REAL height,
                                    int hAlign, int vAlign,
                                    RichTextStyle* style, RichTextAppearance* appearance)
    {
        if (!style || !appearance) {
            TVPThrowExceptionMessage(TJS_W("style and appearance are required"));
        }
        std::u16string u16text = tjsToU16(text);
        richtext::RectF r(x, y, width, height);
        richtext::RectF result = renderer_.drawParagraph(
            u16text, r,
            static_cast<ParagraphLayout::HAlign>(hAlign),
            static_cast<ParagraphLayout::VAlign>(vAlign),
            style->style, appearance->appearance);
        renderer_.sync();
        redraw(static_cast<int>(result.x), static_cast<int>(result.y),
               static_cast<int>(result.width) + 1, static_cast<int>(result.height) + 1);
        return toVariant(result);
    }

    /**
     * タグ付きテキスト描画
     */
    tTJSVariant drawStyleTaggedText(const tjs_char* text, REAL x, REAL y, REAL width, REAL height,
                                     int hAlign, int vAlign,
                                     RichTextStyle* defaultStyle,
                                     RichTextAppearance* defaultAppearance)
    {
        if (!defaultStyle || !defaultAppearance) {
            TVPThrowExceptionMessage(TJS_W("defaultStyle and defaultAppearance are required"));
        }
        std::u16string u16text = tjsToU16(text);
        richtext::RectF r(x, y, width, height);
        TagParser parser;
        auto parseResult = parser.parse(u16text, defaultStyle->style, defaultAppearance->appearance);
        richtext::RectF result = renderer_.drawParagraph(
            parseResult.plainText, r,
            static_cast<ParagraphLayout::HAlign>(hAlign),
            static_cast<ParagraphLayout::VAlign>(vAlign),
            parseResult.styleRuns,
            defaultAppearance->appearance);
        renderer_.sync();
        redraw(static_cast<int>(result.x), static_cast<int>(result.y),
               static_cast<int>(result.width) + 1, static_cast<int>(result.height) + 1);
        return toVariant(result);
    }

    /**
     * ParagraphLayout 描画（逐次表示対応）
     */
    tTJSVariant drawStyleParagraphLayout(RichTextParagraphLayout* paraLayout,
                                          REAL x, REAL y, REAL width, REAL height,
                                          int hAlign, int vAlign,
                                          RichTextStyle* style,
                                          RichTextAppearance* appearance,
                                          int maxGlyphs = -1)
    {
        if (!paraLayout || !style || !appearance) {
            TVPThrowExceptionMessage(TJS_W("paraLayout, style and appearance are required"));
        }
        richtext::RectF r(x, y, width, height);
        richtext::RectF result = renderer_.drawParagraphLayout(
            paraLayout->layout, r,
            static_cast<ParagraphLayout::HAlign>(hAlign),
            static_cast<ParagraphLayout::VAlign>(vAlign),
            style->style, appearance->appearance,
            maxGlyphs);
        renderer_.sync();
        redraw(static_cast<int>(result.x), static_cast<int>(result.y),
               static_cast<int>(result.width) + 1, static_cast<int>(result.height) + 1);
        return toVariant(result);
    }

    /**
     * 矩形描画
     */
    void fillRect(REAL x, REAL y, REAL width, REAL height,
                  tjs_uint32 fillColor, tjs_uint32 strokeColor = 0,
                  REAL strokeWidth = 0)
    {
        renderer_.drawRect(x, y, width, height, fillColor, strokeColor, strokeWidth);
        renderer_.sync();
        redraw(static_cast<int>(x), static_cast<int>(y),
               static_cast<int>(width) + 1, static_cast<int>(height) + 1);
    }

    // ------------------------------------------------------------------
    // 計測メソッド
    // ------------------------------------------------------------------

    /**
     * テキスト計測
     */
    tTJSVariant measureStyleText(const tjs_char* text, RichTextStyle* style)
    {
        if (!style) {
            TVPThrowExceptionMessage(TJS_W("style is required"));
        }
        std::u16string u16text = tjsToU16(text);
        TextLayout layout = renderer_.measureText(u16text, style->style);
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        tTJSVariant val;
        val = layout.getWidth();
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("width"), nullptr, &val, dict);
        val = layout.getHeight();
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("height"), nullptr, &val, dict);
        val = layout.getAscent();
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("ascent"), nullptr, &val, dict);
        val = layout.getDescent();
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("descent"), nullptr, &val, dict);
        tTJSVariant result(dict, dict);
        dict->Release();
        return result;
    }

    /**
     * パラグラフ計測
     */
    tTJSVariant measureStyleParagraph(const tjs_char* text, REAL maxWidth, RichTextStyle* style)
    {
        if (!style) {
            TVPThrowExceptionMessage(TJS_W("style is required"));
        }
        std::u16string u16text = tjsToU16(text);
        ParagraphLayout layout = renderer_.measureParagraph(u16text, maxWidth, style->style);
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        tTJSVariant val;
        val = layout.getMaxWidth();
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("width"), nullptr, &val, dict);
        val = layout.getTotalHeight();
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("height"), nullptr, &val, dict);
        val = static_cast<int>(layout.getLineCount());
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("lineCount"), nullptr, &val, dict);
        tTJSVariant result(dict, dict);
        dict->Release();
        return result;
    }
    
    // ------------------------------------------------------------------
    // キャッシュ制御
    // ------------------------------------------------------------------
    
    void setUseCache(bool v) { renderer_.setUseCache(v); }
    bool getUseCache() const { return renderer_.getUseCache(); }
    
    void clearCache() { renderer_.clearCache(); }
    void setCacheMaxSize(int bytes) { renderer_.setCacheMaxSize(static_cast<size_t>(bytes)); }
    
    // ------------------------------------------------------------------
    // 描画同期
    // ------------------------------------------------------------------
    
    void sync() {
        renderer_.sync();
    }
};

// ============================================================================
// フォント管理クラス (静的メソッド)
// ============================================================================

class RichText {
public:
    /**
     * フォント登録
     * @param path フォントファイルパス
     * @param name 登録名
     * @param index フォントインデックス（OTCの場合）
     * @return 成功時 true
     */
    static bool registerFont(const tjs_char* path, const tjs_char* name, int index = 0) {
        std::string pathStr = tjsToNarrow(path);
        std::string nameStr = tjsToNarrow(name);
        return FontManager::instance().registerFont(pathStr, nameStr, index);
    }
    
    /**
     * フォント解除
     * @param name 登録名
     * @return 成功時 true
     */
    static bool unregisterFont(const tjs_char* name) {
        return FontManager::instance().unregisterFont(tjsToNarrow(name));
    }
    
    /**
     * ロケール登録
     * @param locale ロケール文字列
     * @return ロケールID
     */
    static int registerLocale(const tjs_char* locale) {
        return static_cast<int>(FontManager::instance().registerLocale(tjsToNarrow(locale)));
    }
};

// ============================================================================
// thorvg 初期化・終了
// ============================================================================

static bool tvgInitialized = false;

void initRichText()
{
    if (!tvgInitialized) {
        if (tvg::Initializer::init(4) == tvg::Result::Success) {
            tvgInitialized = true;
            FontManager::instance().initialize();
            message_log("RichText: initialized");
        } else {
            error_log("RichText: failed to initialize thorvg");
        }
    }
}

void deInitRichText()
{
    if (tvgInitialized) {
        FontManager::instance().terminate();
        tvg::Initializer::term();
        tvgInitialized = false;
        message_log("RichText: terminated");
    }
}

// ============================================================================
// ncbind 登録
// ============================================================================

// RichTextStyle サブクラス
NCB_REGISTER_SUBCLASS(RichTextStyle) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(setFonts);
    NCB_PROPERTY(fontSize, getFontSize, setFontSize);
    NCB_PROPERTY(fontWeight, getFontWeight, setFontWeight);
    NCB_PROPERTY(italic, getItalic, setItalic);
    NCB_PROPERTY(letterSpacing, getLetterSpacing, setLetterSpacing);
    NCB_PROPERTY(wordSpacing, getWordSpacing, setWordSpacing);
    NCB_PROPERTY(scaleX, getScaleX, setScaleX);
    NCB_PROPERTY(skewX, getSkewX, setSkewX);
    NCB_METHOD(setLocale);
    NCB_METHOD(clone);
};

// RichTextAppearance サブクラス
NCB_REGISTER_SUBCLASS(RichTextAppearance) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(addFill);
    NCB_METHOD(addStroke);
    NCB_METHOD(addShadow);
    NCB_METHOD(clear);
    NCB_PROPERTY_RO(isEmpty, isEmpty);
    NCB_PROPERTY_RO(count, getCount);
    NCB_METHOD(clone);
};

// RichTextLayout サブクラス
NCB_REGISTER_SUBCLASS(RichTextLayout) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(measure);
    NCB_PROPERTY_RO(width, getWidth);
    NCB_PROPERTY_RO(height, getHeight);
    NCB_PROPERTY_RO(ascent, getAscent);
    NCB_PROPERTY_RO(descent, getDescent);
    NCB_PROPERTY_RO(glyphCount, getGlyphCount);
    NCB_METHOD(clone);
};

// RichTextParagraphLayout サブクラス
NCB_REGISTER_SUBCLASS(RichTextParagraphLayout) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(measure);
    NCB_PROPERTY_RO(lineCount, getLineCount);
    NCB_PROPERTY_RO(totalHeight, getTotalHeight);
    NCB_PROPERTY_RO(maxWidth, getMaxWidth);
    NCB_PROPERTY_RO(totalGlyphCount, getTotalGlyphCount);
    NCB_PROPERTY(lineSpacing, getLineSpacing, setLineSpacing);
    NCB_METHOD(getLineInfo);
    NCB_METHOD(clone);
};

// RichText クラス (静的メソッドと定数)
NCB_REGISTER_CLASS(RichText)
{
    // フォント管理
    NCB_METHOD(registerFont);
    NCB_METHOD(unregisterFont);
    NCB_METHOD(registerLocale);
    
    // 水平アライン
    Variant(TJS_W("HALIGN_LEFT"),   (int)ParagraphLayout::HAlign::Left);
    Variant(TJS_W("HALIGN_CENTER"), (int)ParagraphLayout::HAlign::Center);
    Variant(TJS_W("HALIGN_RIGHT"),  (int)ParagraphLayout::HAlign::Right);
    
    // 垂直アライン
    Variant(TJS_W("VALIGN_TOP"),    (int)ParagraphLayout::VAlign::Top);
    Variant(TJS_W("VALIGN_MIDDLE"), (int)ParagraphLayout::VAlign::Middle);
    Variant(TJS_W("VALIGN_BOTTOM"), (int)ParagraphLayout::VAlign::Bottom);
    
    // サブクラス
    NCB_SUBCLASS(Style, RichTextStyle);
    NCB_SUBCLASS(Appearance, RichTextAppearance);
    NCB_SUBCLASS(Layout, RichTextLayout);
    NCB_SUBCLASS(ParagraphLayout, RichTextParagraphLayout);
}

// LayerExRichText インスタンスフック
NCB_GET_INSTANCE_HOOK(LayerExRichText)
{
    NCB_INSTANCE_GETTER(objthis) {
        ClassT* obj = GetNativeInstance(objthis);
        if (!obj) {
            obj = new ClassT(objthis);
            SetNativeInstance(objthis, obj);
        }
        obj->reset();
        return obj;
    }
    ~NCB_GET_INSTANCE_HOOK_CLASS() {
    }
};

// Layer 拡張としてアタッチ
NCB_ATTACH_CLASS_WITH_HOOK(LayerExRichText, Layer) {
    // 描画メソッド
    NCB_METHOD(drawStyleText);
    NCB_METHOD(drawStyleParagraph);
    NCB_METHOD(drawStyleTaggedText);
    NCB_METHOD(drawStyleParagraphLayout);
    NCB_METHOD(fillRect);

    // 計測メソッド
    NCB_METHOD(measureStyleText);
    NCB_METHOD(measureStyleParagraph);

    // キャッシュ制御
    NCB_PROPERTY(useCache, getUseCache, setUseCache);
    NCB_METHOD(clearCache);
    NCB_METHOD(setCacheMaxSize);

    // 同期
    NCB_METHOD(sync);
}

// 初期化・終了コールバック
NCB_PRE_REGIST_CALLBACK(initRichText);
NCB_POST_UNREGIST_CALLBACK(deInitRichText);
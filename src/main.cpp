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

// RichTextRender (TextRender 互換クラス)
// ncbind.hpp の後・richtext.hpp の後にインクルード
#include "RichTextRender.hpp"

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
    std::string result;
    if (str) {
        tjs_int len = TVPWideCharToUtf8String( str, NULL );
        if( len > 0 ) {
            char* buf = new char[len];
            if( buf ) {
                try {
                    len = TVPWideCharToUtf8String( str, buf );
                    if( len > 0 ) result.assign( buf, len );
                    delete[] buf;
                } catch(...) {
                    delete[] buf;
                    throw;
                }
            }
        }
    }
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
    void setFontSize(float size) { style.fontSize = size; }
    float getFontSize() const { return style.fontSize; }
    
    // プロパティ: fontWeight
    void setFontWeight(int weight) { style.fontWeight = static_cast<uint16_t>(weight); }
    int getFontWeight() const { return style.fontWeight; }
    
    // プロパティ: italic
    void setItalic(bool v) { style.italic = v; }
    bool getItalic() const { return style.italic; }
    
    // プロパティ: letterSpacing
    void setLetterSpacing(float v) { style.letterSpacing = v; }
    float getLetterSpacing() const { return style.letterSpacing; }
    
    // プロパティ: wordSpacing
    void setWordSpacing(float v) { style.wordSpacing = v; }
    float getWordSpacing() const { return style.wordSpacing; }
    
    // プロパティ: scaleX
    void setScaleX(float v) { style.scaleX = v; }
    float getScaleX() const { return style.scaleX; }
    
    // プロパティ: skewX
    void setSkewX(float v) { style.skewX = v; }
    float getSkewX() const { return style.skewX; }
    
    // プロパティ: fontWidth（バリアブルフォントのwdth軸、パーセント）
    void setFontWidth(float v) { style.fontWidth = v; }
    float getFontWidth() const { return style.fontWidth; }
    
    // プロパティ: bidi（双方向テキスト制御）
    void setBidi(int v) { style.bidi = static_cast<minikin::Bidi>(v); }
    int getBidi() const { return static_cast<int>(style.bidi); }
    
    // プロパティ: locale
    void setLocale(const tjs_char* locale) {
        style.localeId = FontManager::instance().getLocaleId(tjsToNarrow(locale));
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
    void addFill(tjs_uint32 color, float offsetX = 0, float offsetY = 0) {
        appearance.addFill(color, offsetX, offsetY);
    }
    
    /**
     * ストローク追加
     * @param color ARGB色値
     * @param width 線幅
     * @param offsetX X方向オフセット（省略可）
     * @param offsetY Y方向オフセット（省略可）
     */
    void addStroke(tjs_uint32 color, float width, float offsetX = 0, float offsetY = 0) {
        appearance.addStroke(color, width, offsetX, offsetY);
    }
    
    /**
     * 影追加（最背面に追加）
     * @param color 影色
     * @param offsetX X方向オフセット
     * @param offsetY Y方向オフセット
     */
    void addShadow(tjs_uint32 color, float offsetX, float offsetY) {
        appearance.addShadow(color, offsetX, offsetY);
    }
    
    /**
     * テキスト色の設定（既存の通常Fillを置換）
     * @param color ARGB色値
     */
    void setColor(tjs_uint32 color) {
        appearance.setColor(color);
    }
    
    /**
     * テキスト色の追加（最前面に追加）
     * @param color ARGB色値
     * @param offsetX X方向オフセット（省略可）
     * @param offsetY Y方向オフセット（省略可）
     */
    void addColor(tjs_uint32 color, float offsetX = 0, float offsetY = 0) {
        appearance.addColor(color, offsetX, offsetY);
    }
    
    /**
     * 縁取りの設定（既存のStrokeを置換）
     * @param color ARGB色値
     * @param width 線幅
     * @param offsetX X方向オフセット（省略可）
     * @param offsetY Y方向オフセット（省略可）
     */
    void setOutline(tjs_uint32 color, float width, float offsetX = 0, float offsetY = 0) {
        appearance.setOutline(color, width, offsetX, offsetY);
    }
    
    /**
     * 縁取りの追加（最背面Strokeに追加）
     * @param color ARGB色値
     * @param width 線幅
     * @param offsetX X方向オフセット（省略可）
     * @param offsetY Y方向オフセット（省略可）
     */
    void addOutline(tjs_uint32 color, float width, float offsetX = 0, float offsetY = 0) {
        appearance.addOutline(color, width, offsetX, offsetY);
    }
    
    /**
     * 影の設定（既存の影Fillを置換）
     * @param color 影色
     * @param offsetX X方向オフセット
     * @param offsetY Y方向オフセット
     */
    void setShadow(tjs_uint32 color, float offsetX, float offsetY) {
        appearance.setShadow(color, offsetX, offsetY);
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
    TextLayout layout_;

    RichTextLayout() {}

    void layout(const tjs_char* text, RichTextStyle* style) {
        if (!style) TVPThrowExceptionMessage(TJS_W("style is required"));
        layout_.layout(tjsToU16(text), style->style);
    }

    float getWidth() const { return layout_.getWidth(); }
    float getHeight() const { return layout_.getHeight(); }
    float getAscent() const { return layout_.getAscent(); }
    float getDescent() const { return layout_.getDescent(); }
    int getGlyphCount() const { return static_cast<int>(layout_.getGlyphCount()); }

    RichTextLayout* clone() const {
        RichTextLayout* c = new RichTextLayout();
        c->layout_ = layout_;
        return c;
    }
};

// ============================================================================
// TJSラッパークラス: RichTextParagraphLayout (ParagraphLayout のラッパー)
// ============================================================================

class RichTextParagraphLayout {
public:
    ParagraphLayout layout_;

    RichTextParagraphLayout() {}

    void layout(const tjs_char* text, float maxWidth, RichTextStyle* style) {
        if (!style) TVPThrowExceptionMessage(TJS_W("style is required"));
        cachedText_ = tjsToU16(text);
        cachedMaxWidth_ = maxWidth;
        cachedStyle_ = &style->style;
        layout_.layout(cachedText_, maxWidth, style->style);
    }

    int getLineCount() const { return static_cast<int>(layout_.getLineCount()); }
    float getTotalHeight() const { return layout_.getTotalHeight(); }
    float getMaxWidth() const { return layout_.getMaxWidth(); }
    int getTotalGlyphCount() const { return static_cast<int>(layout_.getTotalGlyphCount()); }

    void setLineSpacing(float v) { layout_.setLineSpacing(v); }
    float getLineSpacing() const { return layout_.getLineSpacing(); }

    void setBreakStrategy(int v) { layout_.setBreakStrategy(static_cast<ParagraphLayout::BreakStrategy>(v)); }
    int getBreakStrategy() const { return static_cast<int>(layout_.getBreakStrategy()); }

    tTJSVariant getLineInfo(int index) const {
        if (index < 0 || index >= static_cast<int>(layout_.getLineCount())) {
            TVPThrowExceptionMessage(TJS_W("line index out of range"));
        }
        const auto& line = layout_.getLine(index);
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
            c->layout_.setLineSpacing(layout_.getLineSpacing());
            c->layout_.setBreakStrategy(layout_.getBreakStrategy());
            c->layout_.layout(cachedText_, cachedMaxWidth_, *cachedStyle_);
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
NCB_TYPECONV_CAST_INTEGER(ParagraphLayout::BreakStrategy);
NCB_TYPECONV_CAST_INTEGER(minikin::Bidi);

// 前方宣言
class RichTextStyledLayout;

// ============================================================================
// TJSラッパークラス: RichTextTextureAtlas (TextureAtlas のラッパー)
// ============================================================================

/**
 * レイヤーをテクスチャとして使う ITexture 実装
 */
class LayerTexture : public ITexture {
public:
    LayerTexture(iTJSDispatch2* layer) : layer_(layer) {
        updateInfo();
    }

    int getWidth() const override { return width_; }
    int getHeight() const override { return height_; }

    void update(int x, int y, int width, int height,
                const uint32_t* pixels, int pitch) override {
        // レイヤーバッファ情報を更新
        updateInfo();
        if (!buffer_) return;

        // ピクセルコピー
        for (int row = 0; row < height; ++row) {
            int dstY = y + row;
            if (dstY < 0 || dstY >= height_) continue;
            const uint32_t* srcRow = reinterpret_cast<const uint32_t*>(
                reinterpret_cast<const uint8_t*>(pixels) + row * pitch);
            uint32_t* dstRow = reinterpret_cast<uint32_t*>(
                reinterpret_cast<uint8_t*>(buffer_) + dstY * pitch_);
            for (int col = 0; col < width; ++col) {
                int dstX = x + col;
                if (dstX < 0 || dstX >= width_) continue;
                dstRow[dstX] = srcRow[col];
            }
        }
    }

private:
    void updateInfo() {
        tTJSVariant val;
        layer_->PropGet(0, TJS_W("imageWidth"), nullptr, &val, layer_);
        width_ = static_cast<int>(val);
        layer_->PropGet(0, TJS_W("imageHeight"), nullptr, &val, layer_);
        height_ = static_cast<int>(val);
        layer_->PropGet(0, TJS_W("mainImageBufferForWrite"), nullptr, &val, layer_);
        buffer_ = reinterpret_cast<uint32_t*>(static_cast<tjs_intptr_t>(val));
        layer_->PropGet(0, TJS_W("mainImageBufferPitch"), nullptr, &val, layer_);
        pitch_ = static_cast<int>(val);
    }

    iTJSDispatch2* layer_;
    int width_ = 0;
    int height_ = 0;
    uint32_t* buffer_ = nullptr;
    int pitch_ = 0;
};

class RichTextTextureAtlas {
public:
    RichTextTextureAtlas(tTJSVariant layer) {
        iTJSDispatch2* layerObj = layer.AsObjectNoAddRef();
        if (!layerObj || layerObj->IsInstanceOf(0, NULL, NULL, TJS_W("Layer"), layerObj) != TJS_S_TRUE) {
            TVPThrowExceptionMessage(TJS_W("argument must be a Layer"));
        }
        // レイヤーオブジェクトを参照保持
        layerObj_ = layerObj;
        layerObj_->AddRef();
        texture_ = std::make_unique<LayerTexture>(layerObj);
        atlas_ = std::make_unique<TextureAtlas>(texture_.get());
    }

    ~RichTextTextureAtlas() {
        atlas_.reset();
        texture_.reset();
        if (layerObj_) {
            layerObj_->Release();
        }
    }

    void clear() { atlas_->clear(); }

    bool addLayout(RichTextLayout* layout, RichTextAppearance* appearance) {
        if (!layout || !appearance) {
            TVPThrowExceptionMessage(TJS_W("layout and appearance are required"));
        }
        return atlas_->addLayout(layout->layout_, appearance->appearance);
    }

    bool addParagraphLayout(RichTextParagraphLayout* para, RichTextStyle* style, RichTextAppearance* appearance) {
        if (!para || !style || !appearance) {
            TVPThrowExceptionMessage(TJS_W("paraLayout, style and appearance are required"));
        }
        return atlas_->addParagraphLayout(para->layout_, style->style, appearance->appearance);
    }

    bool addStyledLayout(RichTextStyledLayout* layout); // 実装は RichTextStyledLayout 定義後

    void commit() {
        atlas_->commit();
        // レイヤーに更新通知
        tTJSVariant val;
        layerObj_->PropGet(0, TJS_W("imageWidth"), nullptr, &val, layerObj_);
        int w = static_cast<int>(val);
        layerObj_->PropGet(0, TJS_W("imageHeight"), nullptr, &val, layerObj_);
        int h = static_cast<int>(val);
        tTJSVariant vars[4] = { 0, 0, w, h };
        tTJSVariant* varsp[4] = { vars, vars + 1, vars + 2, vars + 3 };
        tTJSVariant result;
        layerObj_->FuncCall(0, TJS_W("update"), nullptr, &result, 4, varsp, layerObj_);
    }

    /**
     * getCopyRects を TJS 配列として返す共通ヘルパー
     */
    static tTJSVariant copyRectsToVariant(const std::vector<richtext::CopyRect>& rects) {
        iTJSDispatch2* arr = TJSCreateArrayObject();
        static tjs_uint32 pushHint;
        for (const auto& r : rects) {
            iTJSDispatch2* dict = TJSCreateDictionaryObject();
            tTJSVariant v;
            v = r.srcX;  dict->PropSet(TJS_MEMBERENSURE, TJS_W("srcX"), nullptr, &v, dict);
            v = r.srcY;  dict->PropSet(TJS_MEMBERENSURE, TJS_W("srcY"), nullptr, &v, dict);
            v = r.srcWidth;  dict->PropSet(TJS_MEMBERENSURE, TJS_W("srcWidth"), nullptr, &v, dict);
            v = r.srcHeight; dict->PropSet(TJS_MEMBERENSURE, TJS_W("srcHeight"), nullptr, &v, dict);
            v = r.dstX;  dict->PropSet(TJS_MEMBERENSURE, TJS_W("dstX"), nullptr, &v, dict);
            v = r.dstY;  dict->PropSet(TJS_MEMBERENSURE, TJS_W("dstY"), nullptr, &v, dict);
            v = r.displayIndex; dict->PropSet(TJS_MEMBERENSURE, TJS_W("displayIndex"), nullptr, &v, dict);
            tTJSVariant dictVar(dict, dict);
            dict->Release();
            tTJSVariant* p = &dictVar;
            arr->FuncCall(0, TJS_W("push"), &pushHint, nullptr, 1, &p, arr);
        }
        tTJSVariant result(arr, arr);
        arr->Release();
        return result;
    }

    // getCopyRects は RawCallback で実装（オーバーロード対応のため後述）

    TextureAtlas& getAtlas() { return *atlas_; }

private:
    iTJSDispatch2* layerObj_;
    std::unique_ptr<LayerTexture> texture_;
    std::unique_ptr<TextureAtlas> atlas_;
};

// ============================================================================
// TJSラッパークラス: RichTextStyledLayout (StyledLayout のラッパー)
// ============================================================================

class RichTextStyledLayout {
public:
    StyledLayout layout_;

    RichTextStyledLayout() {}

    // lineCount
    int getLineCount() const { return static_cast<int>(layout_.getLineCount()); }

    // totalGlyphCount
    int getTotalGlyphCount() const { return static_cast<int>(layout_.getTotalGlyphCount()); }

    // totalCharCount
    int getTotalCharCount() const { return static_cast<int>(layout_.getTotalCharCount()); }

    // maxWidth
    float getMaxWidth() const { return layout_.getMaxWidth(); }

    // maxHeight
    float getMaxHeight() const { return layout_.getMaxHeight(); }

    // isValid
    bool getIsValid() const { return layout_.isValid(); }
};

// RichTextTextureAtlas::addStyledLayout の実装（RichTextStyledLayout 定義後）
bool RichTextTextureAtlas::addStyledLayout(RichTextStyledLayout* layout) {
    if (!layout) {
        TVPThrowExceptionMessage(TJS_W("styledLayout is required"));
    }
    return atlas_->addStyledLayout(layout->layout_);
}

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

    // RichTextコアAPIに合わせたメソッド名・引数で再定義

    // 1行テキスト描画
    tTJSVariant drawText(const tjs_char* text, float x, float y, RichTextStyle* style, RichTextAppearance* appearance) {
        if (!style || !appearance) {
            TVPThrowExceptionMessage(TJS_W("style and appearance are required"));
        }
        std::u16string u16text = tjsToU16(text);
        richtext::RectF rect = renderer_.drawText(u16text, x, y, style->style, appearance->appearance);
        renderer_.sync();
        redraw(static_cast<int>(rect.x), static_cast<int>(rect.y), static_cast<int>(rect.width) + 1, static_cast<int>(rect.height) + 1);
        return toVariant(rect);
    }

    // パラグラフ描画
    tTJSVariant drawParagraph(const tjs_char* text, float x, float y, float width, float height, int hAlign, int vAlign, RichTextStyle* style, RichTextAppearance* appearance) {
        if (!style || !appearance) {
            TVPThrowExceptionMessage(TJS_W("style and appearance are required"));
        }
        std::u16string u16text = tjsToU16(text);
        richtext::RectF r(x, y, width, height);
        richtext::RectF result = renderer_.drawParagraph(u16text, r, static_cast<ParagraphLayout::HAlign>(hAlign), static_cast<ParagraphLayout::VAlign>(vAlign), style->style, appearance->appearance);
        renderer_.sync();
        redraw(static_cast<int>(result.x), static_cast<int>(result.y), static_cast<int>(result.width) + 1, static_cast<int>(result.height) + 1);
        return toVariant(result);
    }

    // タグ付きテキスト描画（drawStyledTextに統一）
    tTJSVariant drawStyledText(const tjs_char* text, float x, float y, float width, float height, int hAlign, int vAlign, 
        const std::map<std::string, TextStyle>& styles,
        const std::map<std::string, Appearance>& appearances,
        float lineSpacing = 0.0f) {
        std::u16string u16text = tjsToU16(text);
        richtext::RectF r(x, y, width, height);
        // styles/appearancesは現状TJSから渡せないのでdefaultのみ
        // 必要ならTJS辞書→std::map変換を追加
        richtext::RectF result = renderer_.drawStyledText(u16text, r, static_cast<ParagraphLayout::HAlign>(hAlign), static_cast<ParagraphLayout::VAlign>(vAlign), styles, appearances, lineSpacing);
        renderer_.sync();
        redraw(static_cast<int>(result.x), static_cast<int>(result.y), static_cast<int>(result.width) + 1, static_cast<int>(result.height) + 1);
        return toVariant(result);
    }
    
    // TextLayout描画
    tTJSVariant drawTextLayout(RichTextLayout* textLayout, float x, float y, RichTextAppearance* appearance, int maxGlyphs = -1) {
        if (!textLayout || !appearance) {
            TVPThrowExceptionMessage(TJS_W("textLayout and appearance are required"));
        }
        richtext::RectF result = renderer_.drawLayout(textLayout->layout_, x, y, appearance->appearance, maxGlyphs);
        renderer_.sync();
        redraw(static_cast<int>(result.x), static_cast<int>(result.y), static_cast<int>(result.width) + 1, static_cast<int>(result.height) + 1);
        return toVariant(result);
    }

    // ParagraphLayout描画
    tTJSVariant drawParagraphLayout(RichTextParagraphLayout* paraLayout, float x, float y, float width, float height, int hAlign, int vAlign, RichTextStyle* style, RichTextAppearance* appearance, int maxGlyphs = -1) {
        if (!paraLayout || !style || !appearance) {
            TVPThrowExceptionMessage(TJS_W("paraLayout, style and appearance are required"));
        }
        richtext::RectF r(x, y, width, height);
        richtext::RectF result = renderer_.drawParagraphLayout(paraLayout->layout_, r, static_cast<ParagraphLayout::HAlign>(hAlign), static_cast<ParagraphLayout::VAlign>(vAlign), style->style, appearance->appearance, maxGlyphs);
        renderer_.sync();
        redraw(static_cast<int>(result.x), static_cast<int>(result.y), static_cast<int>(result.width) + 1, static_cast<int>(result.height) + 1);
        return toVariant(result);
    }

    // StyledLayout描画
    tTJSVariant drawStyledLayout(RichTextStyledLayout* styledLayout, float x, float y, int maxGlyphs = -1) {
        if (!styledLayout) {
            TVPThrowExceptionMessage(TJS_W("styledLayout is required"));
        }
        richtext::RectF result = renderer_.drawStyledLayout(styledLayout->layout_, x, y, maxGlyphs);
        renderer_.sync();
        redraw(static_cast<int>(result.x), static_cast<int>(result.y), static_cast<int>(result.width) + 1, static_cast<int>(result.height) + 1);
        return toVariant(result);
    }

    // 矩形描画
    void drawRect(float x, float y, float width, float height, tjs_uint32 fillColor, tjs_uint32 strokeColor = 0, float strokeWidth = 0) {
        renderer_.drawRect(x, y, width, height, fillColor, strokeColor, strokeWidth);
        renderer_.sync();
        redraw(static_cast<int>(x), static_cast<int>(y), static_cast<int>(width) + 1, static_cast<int>(height) + 1);
    }
    
    // ------------------------------------------------------------------
    // キャッシュ制御
    // ------------------------------------------------------------------
    
    void setUseCache(bool v) { renderer_.setUseCache(v); }
    bool getUseCache() const { return renderer_.getUseCache(); }
    
    void clearCache() { renderer_.clearCache(); }
    void setCacheMaxSize(int bytes) { renderer_.setCacheMaxSize(static_cast<size_t>(bytes)); }
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
     * バリアブルフォント登録
     * @param path フォントファイルパス
     * @param name 登録名
     * @param weight フォントウェイト（100-900）
     * @param italic イタリック
     * @param index フォントインデックス（OTCの場合）
     * @return 成功時 true
     */
    static bool registerVariableFont(const tjs_char* path, const tjs_char* name,
                                      int weight, bool italic = false, int index = 0) {
        std::string pathStr = tjsToNarrow(path);
        std::string nameStr = tjsToNarrow(name);
        return FontManager::instance().registerVariableFont(
            pathStr, nameStr, static_cast<uint16_t>(weight), italic, index);
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
     * 名前付きフォントコレクション登録
     * @param collectionName コレクション名
     * @param fontNames フォント名の配列
     * @return 成功時 true
     */
    static bool registerCollection(const tjs_char* collectionName, tTJSVariant fontNames) {
        if (!IsArray(fontNames)) {
            TVPThrowExceptionMessage(TJS_W("fontNames must be an array"));
            return false;
        }
        
        ncbPropAccessor arr(fontNames);
        tjs_int count = arr.GetArrayCount();
        std::vector<std::string> names;
        
        for (tjs_int i = 0; i < count; ++i) {
            ttstr name = arr.getStrValue(i);
            names.push_back(tjsToNarrow(name.c_str()));
        }
        
        return FontManager::instance().registerCollection(
            tjsToNarrow(collectionName), names);
    }
    
    /**
     * 名前付きフォントコレクション解除
     * @param collectionName コレクション名
     * @return 成功時 true
     */
    static bool unregisterCollection(const tjs_char* collectionName) {
        return FontManager::instance().unregisterCollection(tjsToNarrow(collectionName));
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
// RawCallback helpers (省略可能引数対応)
// ============================================================================

static tjs_error TJS_INTF_METHOD
RichTextAppearance_addFill_RawCallback(tTJSVariant* result, tjs_int numparams,
                                       tTJSVariant** param, RichTextAppearance* objthis)
{
    if (numparams < 1) return TJS_E_BADPARAMCOUNT;
    tjs_uint32 color = static_cast<tjs_uint32>(param[0]->AsInteger());
    float offsetX = (numparams >= 2) ? static_cast<float>(param[1]->AsReal()) : 0;
    float offsetY = (numparams >= 3) ? static_cast<float>(param[2]->AsReal()) : 0;
    objthis->addFill(color, offsetX, offsetY);
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
RichTextAppearance_addStroke_RawCallback(tTJSVariant* result, tjs_int numparams,
                                         tTJSVariant** param, RichTextAppearance* objthis)
{
    if (numparams < 2) return TJS_E_BADPARAMCOUNT;
    tjs_uint32 color = static_cast<tjs_uint32>(param[0]->AsInteger());
    float width = static_cast<float>(param[1]->AsReal());
    float offsetX = (numparams >= 3) ? static_cast<float>(param[2]->AsReal()) : 0;
    float offsetY = (numparams >= 4) ? static_cast<float>(param[3]->AsReal()) : 0;
    objthis->addStroke(color, width, offsetX, offsetY);
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
RichTextAppearance_addColor_RawCallback(tTJSVariant* result, tjs_int numparams,
                                        tTJSVariant** param, RichTextAppearance* objthis)
{
    if (numparams < 1) return TJS_E_BADPARAMCOUNT;
    tjs_uint32 color = static_cast<tjs_uint32>(param[0]->AsInteger());
    float offsetX = (numparams >= 2) ? static_cast<float>(param[1]->AsReal()) : 0;
    float offsetY = (numparams >= 3) ? static_cast<float>(param[2]->AsReal()) : 0;
    objthis->addColor(color, offsetX, offsetY);
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
RichTextAppearance_setOutline_RawCallback(tTJSVariant* result, tjs_int numparams,
                                          tTJSVariant** param, RichTextAppearance* objthis)
{
    if (numparams < 2) return TJS_E_BADPARAMCOUNT;
    tjs_uint32 color = static_cast<tjs_uint32>(param[0]->AsInteger());
    float width = static_cast<float>(param[1]->AsReal());
    float offsetX = (numparams >= 3) ? static_cast<float>(param[2]->AsReal()) : 0;
    float offsetY = (numparams >= 4) ? static_cast<float>(param[3]->AsReal()) : 0;
    objthis->setOutline(color, width, offsetX, offsetY);
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
RichTextAppearance_addOutline_RawCallback(tTJSVariant* result, tjs_int numparams,
                                          tTJSVariant** param, RichTextAppearance* objthis)
{
    if (numparams < 2) return TJS_E_BADPARAMCOUNT;
    tjs_uint32 color = static_cast<tjs_uint32>(param[0]->AsInteger());
    float width = static_cast<float>(param[1]->AsReal());
    float offsetX = (numparams >= 3) ? static_cast<float>(param[2]->AsReal()) : 0;
    float offsetY = (numparams >= 4) ? static_cast<float>(param[3]->AsReal()) : 0;
    objthis->addOutline(color, width, offsetX, offsetY);
    return TJS_S_OK;
}


/**
 * 辞書からスタイル一覧取得
 */
class StylesGetCaller : public tTJSDispatch
{
public:
	StylesGetCaller(std::map<std::string, TextStyle> &styles) : styles(styles) {}
	virtual tjs_error TJS_INTF_METHOD FuncCall( // function invocation
												tjs_uint32 flag,			// calling flag
												const tjs_char * membername,// member name ( NULL for a default member )
												tjs_uint32 *hint,			// hint for the member name (in/out)
												tTJSVariant *result,		// result
												tjs_int numparams,			// number of parameters
												tTJSVariant **param,		// parameters
												iTJSDispatch2 *objthis		// object as "this"
												) {
		if (numparams > 1) {
			tTVInteger flag = param[1]->AsInteger();
            if (!(flag & TJS_HIDDENMEMBER)) {
                RichTextStyle* style = ncbInstanceAdaptor<RichTextStyle>::GetNativeInstance(param[2]->AsObjectNoAddRef());
                if (style) {
                    std::string utf8name = tjsToNarrow(param[0]->GetString());
                    styles[utf8name] = style->style;
                }
			}
		}
		if (result) {
			*result = true;
		}
		return TJS_S_OK;
	}
private:
    std::map<std::string, TextStyle> &styles;
};

class AppearancesGetCaller : public tTJSDispatch
{
public:
    AppearancesGetCaller(std::map<std::string, Appearance> &appearances) : appearances(appearances) {}
    virtual tjs_error TJS_INTF_METHOD FuncCall( // function invocation
                                                tjs_uint32 flag,			// calling flag
                                                const tjs_char * membername,// member name ( NULL for a default member )
                                                tjs_uint32 *hint,			// hint for the member name (in/out)
                                                tTJSVariant *result,		// result
                                                tjs_int numparams,			// number of parameters
                                                tTJSVariant **param,		// parameters
                                                iTJSDispatch2 *objthis		// object as "this"
                                                ) {
        if (numparams > 1) {
            tTVInteger flag = param[1]->AsInteger();
            if (!(flag & TJS_HIDDENMEMBER)) {
                RichTextAppearance* appearance = ncbInstanceAdaptor<RichTextAppearance>::GetNativeInstance(param[2]->AsObjectNoAddRef());
                if (appearance) {
                    std::string utf8name = tjsToNarrow(param[0]->GetString());
                    appearances[utf8name] = appearance->appearance;
                }
            }
        }
        if (result) {
            *result = true;
        }
        return TJS_S_OK;
    }
private:
    std::map<std::string, Appearance> &appearances;
};

/**
 * TJS引数から styles マップを構築（単体 Style or 辞書）
 */
static std::map<std::string, TextStyle> parseStyles(tTJSVariant* param) {
    std::map<std::string, TextStyle> styles;
    RichTextStyle* single = ncbInstanceAdaptor<RichTextStyle>::GetNativeInstance(param->AsObjectNoAddRef());
    if (single) {
        styles["default"] = single->style;
    } else {
        StylesGetCaller *caller = new StylesGetCaller(styles);
        tTJSVariantClosure closure(caller);
        param->AsObjectClosureNoAddRef().EnumMembers(TJS_IGNOREPROP, &closure, NULL);
    }
    return styles;
}

/**
 * TJS引数から appearances マップを構築（単体 Appearance or 辞書）
 */
static std::map<std::string, Appearance> parseAppearances(tTJSVariant* param) {
    std::map<std::string, Appearance> appearances;
    RichTextAppearance* single = ncbInstanceAdaptor<RichTextAppearance>::GetNativeInstance(param->AsObjectNoAddRef());
    if (single) {
        appearances["default"] = single->appearance;
    } else {
        AppearancesGetCaller *caller = new AppearancesGetCaller(appearances);
        tTJSVariantClosure closure(caller);
        param->AsObjectClosureNoAddRef().EnumMembers(TJS_IGNOREPROP, &closure, NULL);
    }
    return appearances;
}

// drawTextLayout RawCallback（省略可能maxGlyphs対応）
static tjs_error TJS_INTF_METHOD
LayerExRichText_drawTextLayout_RawCallback(tTJSVariant* result, tjs_int numparams,
                                           tTJSVariant** param, LayerExRichText* objthis)
{
    if (numparams < 4) return TJS_E_BADPARAMCOUNT;
    RichTextLayout* textLayout = ncbInstanceAdaptor<RichTextLayout>::GetNativeInstance(param[0]->AsObjectNoAddRef());
    float x = static_cast<float>(param[1]->AsReal());
    float y = static_cast<float>(param[2]->AsReal());
    RichTextAppearance* appearance = ncbInstanceAdaptor<RichTextAppearance>::GetNativeInstance(param[3]->AsObjectNoAddRef());
    int maxGlyphs = (numparams >= 5) ? static_cast<int>(param[4]->AsInteger()) : -1;

    if (result) {
        *result = objthis->drawTextLayout(textLayout, x, y, appearance, maxGlyphs);
    } else {
        objthis->drawTextLayout(textLayout, x, y, appearance, maxGlyphs);
    }
    return TJS_S_OK;
}

// drawStyledText RawCallback（省略可能lineSpacing対応）
static tjs_error TJS_INTF_METHOD
LayerExRichText_drawStyledText_RawCallback(tTJSVariant* result, tjs_int numparams,
                                           tTJSVariant** param, LayerExRichText* objthis)
{
    if (numparams < 9) return TJS_E_BADPARAMCOUNT;
    ttstr text = static_cast<ttstr>(*param[0]);
    float x = static_cast<float>(param[1]->AsReal());
    float y = static_cast<float>(param[2]->AsReal());
    float width = static_cast<float>(param[3]->AsReal());
    float height = static_cast<float>(param[4]->AsReal());
    int hAlign = static_cast<int>(param[5]->AsInteger());
    int vAlign = static_cast<int>(param[6]->AsInteger());

    auto styles = parseStyles(param[7]);
    auto appearances = parseAppearances(param[8]);
    float lineSpacing = (numparams >= 10) ? static_cast<float>(param[9]->AsReal()) : 0.0f;

    if (result) {
        *result = objthis->drawStyledText(text.c_str(), x, y, width, height, hAlign, vAlign, styles, appearances, lineSpacing);
    } else {
        objthis->drawStyledText(text.c_str(), x, y, width, height, hAlign, vAlign, styles, appearances, lineSpacing);
    }
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
LayerExRichText_drawParagraphLayout_RawCallback(tTJSVariant* result, tjs_int numparams,
                                                tTJSVariant** param, LayerExRichText* objthis)
{
    if (numparams < 9) return TJS_E_BADPARAMCOUNT;
    RichTextParagraphLayout* paraLayout = ncbInstanceAdaptor<RichTextParagraphLayout>::GetNativeInstance(param[0]->AsObjectNoAddRef());
    float x = static_cast<float>(param[1]->AsReal());
    float y = static_cast<float>(param[2]->AsReal());
    float width = static_cast<float>(param[3]->AsReal());
    float height = static_cast<float>(param[4]->AsReal());
    int hAlign = static_cast<int>(param[5]->AsInteger());
    int vAlign = static_cast<int>(param[6]->AsInteger());
    RichTextStyle* style = ncbInstanceAdaptor<RichTextStyle>::GetNativeInstance(param[7]->AsObjectNoAddRef());
    RichTextAppearance* appearance = ncbInstanceAdaptor<RichTextAppearance>::GetNativeInstance(param[8]->AsObjectNoAddRef());
    int maxGlyphs = (numparams >= 10) ? static_cast<int>(param[9]->AsInteger()) : -1;

    if (result) {
        *result = objthis->drawParagraphLayout(
            paraLayout, x, y, width, height, hAlign, vAlign, style, appearance, maxGlyphs);
    } else {
        objthis->drawParagraphLayout(
            paraLayout, x, y, width, height, hAlign, vAlign, style, appearance, maxGlyphs);
    }
    return TJS_S_OK;
}

// StyledLayout::layout RawCallback（TJS辞書→std::map変換）
static tjs_error TJS_INTF_METHOD
RichTextStyledLayout_layout_RawCallback(tTJSVariant* result, tjs_int numparams,
                                        tTJSVariant** param, RichTextStyledLayout* objthis)
{
    if (numparams < 7) return TJS_E_BADPARAMCOUNT;
    ttstr text = static_cast<ttstr>(*param[0]);
    float maxWidth = static_cast<float>(param[1]->AsReal());
    float maxHeight = static_cast<float>(param[2]->AsReal());
    int hAlign = static_cast<int>(param[3]->AsInteger());
    int vAlign = static_cast<int>(param[4]->AsInteger());

    auto styles = parseStyles(param[5]);
    auto appearances = parseAppearances(param[6]);

    float lineSpacing = (numparams >= 8) ? static_cast<float>(param[7]->AsReal()) : 0.0f;

    objthis->layout_.layout(tjsToU16(text.c_str()), maxWidth, maxHeight,
                           static_cast<ParagraphLayout::HAlign>(hAlign),
                           static_cast<ParagraphLayout::VAlign>(vAlign),
                           styles, appearances, lineSpacing);
    return TJS_S_OK;
}

// TextureAtlas::getCopyRects RawCallback
// 引数パターンで 3 つのオーバーロードを区別:
//   (Layout, x, y, Appearance [, maxGlyphs])          → TextLayout 版
//   (ParagraphLayout, x, y, w, h, hAlign, vAlign, Style, Appearance [, maxGlyphs]) → Paragraph 版
//   (StyledLayout, x, y [, maxGlyphs])                → StyledLayout 版
static tjs_error TJS_INTF_METHOD
RichTextTextureAtlas_getCopyRects_RawCallback(tTJSVariant* result, tjs_int numparams,
                                              tTJSVariant** param, RichTextTextureAtlas* objthis)
{
    if (numparams < 3) return TJS_E_BADPARAMCOUNT;

    // StyledLayout 版: (StyledLayout, x, y [, maxGlyphs])
    RichTextStyledLayout* styledLayout = ncbInstanceAdaptor<RichTextStyledLayout>::GetNativeInstance(param[0]->AsObjectNoAddRef());
    if (styledLayout) {
        float x = static_cast<float>(param[1]->AsReal());
        float y = static_cast<float>(param[2]->AsReal());
        int maxGlyphs = (numparams >= 4) ? static_cast<int>(param[3]->AsInteger()) : -1;
        auto rects = objthis->getAtlas().getCopyRects(styledLayout->layout_, x, y, maxGlyphs);
        if (result) *result = RichTextTextureAtlas::copyRectsToVariant(rects);
        return TJS_S_OK;
    }

    // TextLayout 版: (Layout, x, y, Appearance [, maxGlyphs])
    RichTextLayout* textLayout = ncbInstanceAdaptor<RichTextLayout>::GetNativeInstance(param[0]->AsObjectNoAddRef());
    if (textLayout) {
        if (numparams < 4) return TJS_E_BADPARAMCOUNT;
        float x = static_cast<float>(param[1]->AsReal());
        float y = static_cast<float>(param[2]->AsReal());
        RichTextAppearance* appearance = ncbInstanceAdaptor<RichTextAppearance>::GetNativeInstance(param[3]->AsObjectNoAddRef());
        if (!appearance) {
            TVPThrowExceptionMessage(TJS_W("appearance is required"));
        }
        int maxGlyphs = (numparams >= 5) ? static_cast<int>(param[4]->AsInteger()) : -1;
        auto rects = objthis->getAtlas().getCopyRects(textLayout->layout_, x, y, appearance->appearance, maxGlyphs);
        if (result) *result = RichTextTextureAtlas::copyRectsToVariant(rects);
        return TJS_S_OK;
    }

    // ParagraphLayout 版: (ParagraphLayout, x, y, w, h, hAlign, vAlign, Style, Appearance [, maxGlyphs])
    RichTextParagraphLayout* paraLayout = ncbInstanceAdaptor<RichTextParagraphLayout>::GetNativeInstance(param[0]->AsObjectNoAddRef());
    if (paraLayout) {
        if (numparams < 9) return TJS_E_BADPARAMCOUNT;
        float x = static_cast<float>(param[1]->AsReal());
        float y = static_cast<float>(param[2]->AsReal());
        float w = static_cast<float>(param[3]->AsReal());
        float h = static_cast<float>(param[4]->AsReal());
        int hAlign = static_cast<int>(param[5]->AsInteger());
        int vAlign = static_cast<int>(param[6]->AsInteger());
        RichTextStyle* style = ncbInstanceAdaptor<RichTextStyle>::GetNativeInstance(param[7]->AsObjectNoAddRef());
        RichTextAppearance* appearance = ncbInstanceAdaptor<RichTextAppearance>::GetNativeInstance(param[8]->AsObjectNoAddRef());
        if (!style || !appearance) {
            TVPThrowExceptionMessage(TJS_W("style and appearance are required"));
        }
        int maxGlyphs = (numparams >= 10) ? static_cast<int>(param[9]->AsInteger()) : -1;
        richtext::RectF rect(x, y, w, h);
        auto rects = objthis->getAtlas().getCopyRects(
            paraLayout->layout_, rect,
            static_cast<ParagraphLayout::HAlign>(hAlign),
            static_cast<ParagraphLayout::VAlign>(vAlign),
            style->style, appearance->appearance, maxGlyphs);
        if (result) *result = RichTextTextureAtlas::copyRectsToVariant(rects);
        return TJS_S_OK;
    }

    TVPThrowExceptionMessage(TJS_W("first argument must be Layout, ParagraphLayout, or StyledLayout"));
    return TJS_E_INVALIDPARAM;
}

// drawStyledLayout RawCallback（省略可能maxGlyphs対応）
static tjs_error TJS_INTF_METHOD
LayerExRichText_drawStyledLayout_RawCallback(tTJSVariant* result, tjs_int numparams,
                                             tTJSVariant** param, LayerExRichText* objthis)
{
    if (numparams < 3) return TJS_E_BADPARAMCOUNT;
    RichTextStyledLayout* styledLayout = ncbInstanceAdaptor<RichTextStyledLayout>::GetNativeInstance(param[0]->AsObjectNoAddRef());
    float x = static_cast<float>(param[1]->AsReal());
    float y = static_cast<float>(param[2]->AsReal());
    int maxGlyphs = (numparams >= 4) ? static_cast<int>(param[3]->AsInteger()) : -1;

    if (result) {
        *result = objthis->drawStyledLayout(styledLayout, x, y, maxGlyphs);
    } else {
        objthis->drawStyledLayout(styledLayout, x, y, maxGlyphs);
    }
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
LayerExRichText_drawRect_RawCallback(tTJSVariant* result, tjs_int numparams,
                                     tTJSVariant** param, LayerExRichText* objthis)
{
    if (numparams < 4) return TJS_E_BADPARAMCOUNT;
    float x = static_cast<float>(param[0]->AsReal());
    float y = static_cast<float>(param[1]->AsReal());
    float width = static_cast<float>(param[2]->AsReal());
    float height = static_cast<float>(param[3]->AsReal());
    tjs_uint32 fillColor = (numparams >= 5) ? static_cast<tjs_uint32>(param[4]->AsInteger()) : 0;
    tjs_uint32 strokeColor = (numparams >= 6) ? static_cast<tjs_uint32>(param[5]->AsInteger()) : 0;
    float strokeWidth = (numparams >= 7) ? static_cast<float>(param[6]->AsReal()) : 0;

    objthis->drawRect(x, y, width, height, fillColor, strokeColor, strokeWidth);
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
RichText_registerFont_RawCallback(tTJSVariant* result, tjs_int numparams,
                                  tTJSVariant** param, iTJSDispatch2* objthis)
{
    if (numparams < 2) return TJS_E_BADPARAMCOUNT;
    ttstr path = static_cast<ttstr>(*param[0]);
    ttstr name = static_cast<ttstr>(*param[1]);
    int index = (numparams >= 3) ? static_cast<int>(param[2]->AsInteger()) : 0;
    bool ok = RichText::registerFont(path.c_str(), name.c_str(), index);
    if (result) *result = ok;
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD
RichText_registerVariableFont_RawCallback(tTJSVariant* result, tjs_int numparams,
                                          tTJSVariant** param, iTJSDispatch2* objthis)
{
    if (numparams < 3) return TJS_E_BADPARAMCOUNT;
    ttstr path = static_cast<ttstr>(*param[0]);
    ttstr name = static_cast<ttstr>(*param[1]);
    int weight = static_cast<int>(param[2]->AsInteger());
    bool italic = (numparams >= 4) ? (param[3]->AsInteger() != 0) : false;
    int index = (numparams >= 5) ? static_cast<int>(param[4]->AsInteger()) : 0;
    bool ok = RichText::registerVariableFont(path.c_str(), name.c_str(), weight, italic, index);
    if (result) *result = ok;
    return TJS_S_OK;
}

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

            // 吉里吉里のストレージシステムを使うフォントデータローダーを登録
            FontManager::instance().setFontDataLoader(
                [](const std::string& name) -> FontDataBuffer {
                    ttstr path(name.c_str());
                    IStream* stream = TVPCreateIStream(path, TJS_BS_READ);
                    if (!stream) return nullptr;

                    // ストリームサイズ取得
                    STATSTG stat;
                    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) {
                        stream->Release();
                        return nullptr;
                    }
                    size_t size = static_cast<size_t>(stat.cbSize.QuadPart);

                    // バッファに読み込み
                    auto buffer = std::make_shared<std::vector<uint8_t>>(size);
                    ULONG read = 0;
                    HRESULT hr = stream->Read(buffer->data(), static_cast<ULONG>(size), &read);
                    stream->Release();

                    if (FAILED(hr) || read != size) return nullptr;
                    return buffer;
                });

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
    NCB_PROPERTY(fontWidth, getFontWidth, setFontWidth);
    NCB_PROPERTY(bidi, getBidi, setBidi);
    NCB_METHOD(setLocale);
    NCB_METHOD(clone);
};

// RichTextAppearance サブクラス
NCB_REGISTER_SUBCLASS(RichTextAppearance) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD_RAW_CALLBACK(addFill, RichTextAppearance_addFill_RawCallback, 0);
    NCB_METHOD_RAW_CALLBACK(addStroke, RichTextAppearance_addStroke_RawCallback, 0);
    NCB_METHOD(addShadow);
    NCB_METHOD(setColor);
    NCB_METHOD_RAW_CALLBACK(addColor, RichTextAppearance_addColor_RawCallback, 0);
    NCB_METHOD_RAW_CALLBACK(setOutline, RichTextAppearance_setOutline_RawCallback, 0);
    NCB_METHOD_RAW_CALLBACK(addOutline, RichTextAppearance_addOutline_RawCallback, 0);
    NCB_METHOD(setShadow);
    NCB_METHOD(clear);
    NCB_PROPERTY_RO(isEmpty, isEmpty);
    NCB_PROPERTY_RO(count, getCount);
    NCB_METHOD(clone);
};

// RichTextLayout サブクラス
NCB_REGISTER_SUBCLASS(RichTextLayout) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(layout);
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
    NCB_METHOD(layout);
    NCB_PROPERTY_RO(lineCount, getLineCount);
    NCB_PROPERTY_RO(totalHeight, getTotalHeight);
    NCB_PROPERTY_RO(maxWidth, getMaxWidth);
    NCB_PROPERTY_RO(totalGlyphCount, getTotalGlyphCount);
    NCB_PROPERTY(lineSpacing, getLineSpacing, setLineSpacing);
    NCB_PROPERTY(breakStrategy, getBreakStrategy, setBreakStrategy);
    NCB_METHOD(getLineInfo);
    NCB_METHOD(clone);
};

// RichTextStyledLayout サブクラス
NCB_REGISTER_SUBCLASS(RichTextStyledLayout) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD_RAW_CALLBACK(layout, RichTextStyledLayout_layout_RawCallback, 0);
    NCB_PROPERTY_RO(lineCount, getLineCount);
    NCB_PROPERTY_RO(totalGlyphCount, getTotalGlyphCount);
    NCB_PROPERTY_RO(totalCharCount, getTotalCharCount);
    NCB_PROPERTY_RO(maxWidth, getMaxWidth);
    NCB_PROPERTY_RO(maxHeight, getMaxHeight);
    NCB_PROPERTY_RO(isValid, getIsValid);
};

// RichTextTextureAtlas サブクラス
NCB_REGISTER_SUBCLASS(RichTextTextureAtlas) {
    NCB_CONSTRUCTOR((tTJSVariant));
    NCB_METHOD(clear);
    NCB_METHOD(addLayout);
    NCB_METHOD(addParagraphLayout);
    NCB_METHOD(addStyledLayout);
    NCB_METHOD(commit);
    NCB_METHOD_RAW_CALLBACK(getCopyRects, RichTextTextureAtlas_getCopyRects_RawCallback, 0);
};

// RichText クラス (静的メソッドと定数)
NCB_REGISTER_CLASS(RichText)
{
    // フォント管理
    NCB_METHOD_RAW_CALLBACK(registerFont, RichText_registerFont_RawCallback, TJS_STATICMEMBER);
    NCB_METHOD_RAW_CALLBACK(registerVariableFont, RichText_registerVariableFont_RawCallback, TJS_STATICMEMBER);
    NCB_METHOD(unregisterFont);
    NCB_METHOD(registerCollection);
    NCB_METHOD(unregisterCollection);
    NCB_METHOD(registerLocale);
    
    // 水平アライン
    Variant(TJS_W("HALIGN_LEFT"),    (int)ParagraphLayout::HAlign::Left);
    Variant(TJS_W("HALIGN_CENTER"),  (int)ParagraphLayout::HAlign::Center);
    Variant(TJS_W("HALIGN_RIGHT"),   (int)ParagraphLayout::HAlign::Right);
    Variant(TJS_W("HALIGN_JUSTIFY"), (int)ParagraphLayout::HAlign::Justify);
    
    // 垂直アライン
    Variant(TJS_W("VALIGN_TOP"),    (int)ParagraphLayout::VAlign::Top);
    Variant(TJS_W("VALIGN_MIDDLE"), (int)ParagraphLayout::VAlign::Middle);
    Variant(TJS_W("VALIGN_BOTTOM"), (int)ParagraphLayout::VAlign::Bottom);
    
    // 行分割戦略
    Variant(TJS_W("BREAK_GREEDY"),       (int)ParagraphLayout::BreakStrategy::Greedy);
    Variant(TJS_W("BREAK_HIGH_QUALITY"), (int)ParagraphLayout::BreakStrategy::HighQuality);
    Variant(TJS_W("BREAK_BALANCED"),     (int)ParagraphLayout::BreakStrategy::Balanced);
    
    // 双方向テキスト
    Variant(TJS_W("BIDI_LTR"),         (int)minikin::Bidi::LTR);
    Variant(TJS_W("BIDI_RTL"),         (int)minikin::Bidi::RTL);
    Variant(TJS_W("BIDI_DEFAULT_LTR"), (int)minikin::Bidi::DEFAULT_LTR);
    Variant(TJS_W("BIDI_DEFAULT_RTL"), (int)minikin::Bidi::DEFAULT_RTL);
    
    // サブクラス
    NCB_SUBCLASS(Style, RichTextStyle);
    NCB_SUBCLASS(Appearance, RichTextAppearance);
    NCB_SUBCLASS(Layout, RichTextLayout);
    NCB_SUBCLASS(ParagraphLayout, RichTextParagraphLayout);
    NCB_SUBCLASS(StyledLayout, RichTextStyledLayout);
    NCB_SUBCLASS(TextureAtlas, RichTextTextureAtlas);
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
    NCB_METHOD_DIFFER(drawTextEx, drawText);
    NCB_METHOD(drawParagraph);
    NCB_METHOD_RAW_CALLBACK(drawStyledText, LayerExRichText_drawStyledText_RawCallback, 0);
    NCB_METHOD_RAW_CALLBACK(drawTextLayout, LayerExRichText_drawTextLayout_RawCallback, 0);
    NCB_METHOD_RAW_CALLBACK(drawParagraphLayout, LayerExRichText_drawParagraphLayout_RawCallback, 0);
    NCB_METHOD_RAW_CALLBACK(drawStyledLayout, LayerExRichText_drawStyledLayout_RawCallback, 0);
    NCB_METHOD_RAW_CALLBACK(drawRectEx, LayerExRichText_drawRect_RawCallback, 0);

    // キャッシュ制御
    NCB_PROPERTY(useCache, getUseCache, setUseCache);
    NCB_METHOD(clearCache);
    NCB_METHOD(setCacheMaxSize);
}

// ============================================================================
// RichTextRender TJS バインディング
// ============================================================================

/**
 * RichTextRender の TJS ラッパー
 * ncbind で TJS2 クラスとして公開する
 */
class RichTextRenderWrapper {
public:
    RichTextRenderWrapper() {
        // TJS コールバックはバインド後に設定される
    }

    void setRenderSize(float w, float h) { render_.setRenderSize(w, h); }

    void setDefault(tTJSVariant elm) {
        iTJSDispatch2* dict = elm.AsObjectNoAddRef();
        render_.setDefaultFromDict(dict);
    }

    void setOption(tTJSVariant elm) {
        iTJSDispatch2* dict = elm.AsObjectNoAddRef();
        render_.setOptionFromDict(dict);
    }

    void setFont(tTJSVariant elm) {
        iTJSDispatch2* dict = elm.AsObjectNoAddRef();
        render_.setFontFromDict(dict);
    }

    void resetFont() { render_.resetFont(); }

    void setStyle(tTJSVariant elm) {
        iTJSDispatch2* dict = elm.AsObjectNoAddRef();
        render_.setStyleFromDict(dict);
    }

    void resetStyle() { render_.resetStyle(); }

    void clear() { render_.clear(); }

    void newline() { render_.newline(); }

    void done() { render_.done(); }

    // プロパティ
    bool getRenderOver() const { return render_.getRenderOver(); }
    int getRenderLines() const { return render_.getRenderLines(); }
    int getRenderCount() const { return render_.getRenderCount(); }
    float getRenderDelay() const { return render_.getRenderDelay(); }
    float getRenderLeft() const { return render_.getRenderLeft(); }
    float getRenderTop() const { return render_.getRenderTop(); }
    float getRenderRight() const { return render_.getRenderRight(); }
    float getRenderBottom() const { return render_.getRenderBottom(); }

    ttstr getRenderText() const {
        return ttstr(reinterpret_cast<const tjs_char*>(render_.getRenderText().c_str()));
    }

    float getTimeScale() const { return render_.getTimeScale(); }
    void setTimeScale(float v) { render_.setTimeScale(v); }

    float getFontScale() const { return render_.getFontScale(); }
    void setFontScale(float v) { render_.setFontScale(v); }

    // デフォルトプロパティ
    ttstr getDefaultFace() const {
        return ttstr(reinterpret_cast<const tjs_char*>(render_.getDefaultFace().c_str()));
    }
    void setDefaultFace(const tjs_char* v) {
        render_.setDefaultFace(std::u16string(reinterpret_cast<const char16_t*>(v)));
    }

    float getDefaultFontSize() const { return render_.getDefaultFontSize(); }
    void setDefaultFontSize(float v) { render_.setDefaultFontSize(v); }
    float getDefaultBigFontSize() const { return render_.getDefaultBigFontSize(); }
    void setDefaultBigFontSize(float v) { render_.setDefaultBigFontSize(v); }
    float getDefaultSmallFontSize() const { return render_.getDefaultSmallFontSize(); }
    void setDefaultSmallFontSize(float v) { render_.setDefaultSmallFontSize(v); }
    float getDefaultLineSize() const { return render_.getDefaultLineSize(); }
    void setDefaultLineSize(float v) { render_.setDefaultLineSize(v); }
    float getDefaultLineSpacing() const { return render_.getDefaultLineSpacing(); }
    void setDefaultLineSpacing(float v) { render_.setDefaultLineSpacing(v); }
    float getDefaultPitch() const { return render_.getDefaultPitch(); }
    void setDefaultPitch(float v) { render_.setDefaultPitch(v); }
    int getDefaultAlign() const { return render_.getDefaultAlign(); }
    void setDefaultAlign(int v) { render_.setDefaultAlign(v); }
    int getDefaultValign() const { return render_.getDefaultValign(); }
    void setDefaultValign(int v) { render_.setDefaultValign(v); }
    float getDefaultRubySize() const { return render_.getDefaultRubySize(); }
    void setDefaultRubySize(float v) { render_.setDefaultRubySize(v); }
    tjs_uint32 getDefaultChColor() const { return render_.getDefaultColor(); }
    void setDefaultChColor(tjs_uint32 v) { render_.setDefaultColor(v); }
    bool getDefaultShadow() const { return render_.getDefaultShadow(); }
    void setDefaultShadow(bool v) { render_.setDefaultShadow(v); }
    tjs_uint32 getDefaultShadowColor() const { return render_.getDefaultShadowColor(); }
    void setDefaultShadowColor(tjs_uint32 v) { render_.setDefaultShadowColor(v); }
    bool getDefaultEdge() const { return render_.getDefaultEdge(); }
    void setDefaultEdge(bool v) { render_.setDefaultEdge(v); }
    tjs_uint32 getDefaultEdgeColor() const { return render_.getDefaultEdgeColor(); }
    void setDefaultEdgeColor(tjs_uint32 v) { render_.setDefaultEdgeColor(v); }
    bool getDefaultBold() const { return render_.getDefaultBold(); }
    void setDefaultBold(bool v) { render_.setDefaultBold(v); }
    bool getDefaultItalic() const { return render_.getDefaultItalic(); }
    void setDefaultItalic(bool v) { render_.setDefaultItalic(v); }

    int calcShowCount(float time) const { return render_.calcShowCount(time); }
    float calcLineOffset(int lineno) const { return render_.calcLineOffset(lineno); }

    bool isLinkContains(int link, float x, float y) const {
        return render_.isLinkContains(link, x, y);
    }

    int getLinkOfPosition(float x, float y) const {
        return render_.getLinkOfPosition(x, y);
    }

    // TJS objthis 設定（コールバック用）
    void setTJSObject(iTJSDispatch2* obj) { tjsObj_ = obj; }

    // コールバック設定
    void setupCallbacks() {
        iTJSDispatch2* obj = tjsObj_;

        // onEval コールバック
        render_.setEvalCallback([obj](const std::u16string& name) -> std::u16string {
            if (!obj) return std::u16string();
            tTJSVariant result;
            tTJSVariant param(ttstr(reinterpret_cast<const tjs_char*>(name.c_str())));
            tTJSVariant* params[] = { &param };
            if (TJS_SUCCEEDED(obj->FuncCall(0, TJS_W("onEval"), nullptr, &result, 1, params, obj))) {
                ttstr str = result.GetString();
                return std::u16string(reinterpret_cast<const char16_t*>(str.c_str()));
            }
            return std::u16string();
        });

        // onLabel コールバック
        render_.setLabelResolver([obj](const std::string& label) -> float {
            if (!obj) return 0.0f;
            tTJSVariant result;
            ttstr labelStr(label.c_str());
            tTJSVariant param(labelStr);
            tTJSVariant* params[] = { &param };
            if (TJS_SUCCEEDED(obj->FuncCall(0, TJS_W("onLabel"), nullptr, &result, 1, params, obj))) {
                return static_cast<float>(result.AsReal());
            }
            return 0.0f;
        });

        // onGetGraphSize コールバック
        render_.setGraphSizeCallback([obj](const std::u16string& name, float& w, float& h) -> bool {
            if (!obj) return false;
            tTJSVariant result;
            tTJSVariant param(ttstr(reinterpret_cast<const tjs_char*>(name.c_str())));
            tTJSVariant* params[] = { &param };
            if (TJS_SUCCEEDED(obj->FuncCall(0, TJS_W("onGetGraphSize"), nullptr, &result, 1, params, obj))) {
                if (result.Type() == tvtObject) {
                    iTJSDispatch2* dict = result.AsObjectNoAddRef();
                    tTJSVariant wv, hv;
                    if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("width"), nullptr, &wv, dict)))
                        w = static_cast<float>(wv.AsReal());
                    if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("height"), nullptr, &hv, dict)))
                        h = static_cast<float>(hv.AsReal());
                    return true;
                }
            }
            return false;
        });
    }

    RichTextRender& getRender() { return render_; }

private:
    RichTextRender render_;
    iTJSDispatch2* tjsObj_ = nullptr;
};

// render() RawCallback（省略可能引数対応）
static tjs_error TJS_INTF_METHOD
RichTextRender_render_RawCallback(tTJSVariant* result, tjs_int numparams,
                                   tTJSVariant** param, RichTextRenderWrapper* objthis)
{
    if (numparams < 1) return TJS_E_BADPARAMCOUNT;
    ttstr text = static_cast<ttstr>(*param[0]);
    int autoIndent = (numparams >= 2) ? static_cast<int>(param[1]->AsInteger()) : 0;
    float diff = (numparams >= 3) ? static_cast<float>(param[2]->AsReal()) : 0;
    float all = (numparams >= 4) ? static_cast<float>(param[3]->AsReal()) : 0;
    bool noResetDelay = (numparams >= 5) ? (param[4]->AsInteger() != 0) : false;

    std::u16string u16text(reinterpret_cast<const char16_t*>(text.c_str()));
    objthis->getRender().render(u16text, autoIndent, diff, all, noResetDelay);

    if (result) *result = true;
    return TJS_S_OK;
}

// getCharacters() RawCallback
static tjs_error TJS_INTF_METHOD
RichTextRender_getCharacters_RawCallback(tTJSVariant* result, tjs_int numparams,
                                          tTJSVariant** param, RichTextRenderWrapper* objthis)
{
    int start = (numparams >= 1) ? static_cast<int>(param[0]->AsInteger()) : 0;
    int num = (numparams >= 2) ? static_cast<int>(param[1]->AsInteger()) : 0;

    auto chars = objthis->getRender().getCharacters(start, num);

    // TJS 配列に変換
    iTJSDispatch2* array = TJSCreateArrayObject();
    for (size_t i = 0; i < chars.size(); i++) {
        const auto& ci = chars[i];
        iTJSDispatch2* dict = TJSCreateDictionaryObject();

        // text
        tTJSVariant textVal(ttstr(reinterpret_cast<const tjs_char*>(ci.text.c_str())));
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("text"), nullptr, &textVal, dict);

        // graph
        if (!ci.graph.empty()) {
            tTJSVariant graphVal(ttstr(reinterpret_cast<const tjs_char*>(ci.graph.c_str())));
            dict->PropSet(TJS_MEMBERENSURE, TJS_W("graph"), nullptr, &graphVal, dict);
        }

        // x, y, cw, size
        tTJSVariant xv(ci.x), yv(ci.y), cwv(ci.cw), sv(ci.size);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("x"), nullptr, &xv, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("y"), nullptr, &yv, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("cw"), nullptr, &cwv, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("size"), nullptr, &sv, dict);

        // face
        tTJSVariant faceVal(ttstr(reinterpret_cast<const tjs_char*>(ci.face.c_str())));
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("face"), nullptr, &faceVal, dict);

        // color, bold, italic, shadow, edge
        tTJSVariant colorVal((tjs_int64)ci.color);
        tTJSVariant boldVal((tjs_int)ci.bold);
        tTJSVariant italicVal((tjs_int)ci.italic);
        tTJSVariant shadowVal((tjs_int)ci.shadow);
        tTJSVariant edgeVal((tjs_int)ci.edge);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("color"), nullptr, &colorVal, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("bold"), nullptr, &boldVal, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("italic"), nullptr, &italicVal, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("shadow"), nullptr, &shadowVal, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("edge"), nullptr, &edgeVal, dict);

        // shadowColor, edgeColor
        tTJSVariant scVal((tjs_int64)ci.shadowColor), ecVal((tjs_int64)ci.edgeColor);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("shadowColor"), nullptr, &scVal, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("edgeColor"), nullptr, &ecVal, dict);

        // delay, link, linkName
        tTJSVariant delayVal(ci.delay);
        tTJSVariant linkVal((tjs_int)ci.link);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("delay"), nullptr, &delayVal, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("link"), nullptr, &linkVal, dict);

        if (ci.link >= 0) {
            tTJSVariant lnVal(ttstr(ci.linkName.c_str()));
            dict->PropSet(TJS_MEMBERENSURE, TJS_W("linkName"), nullptr, &lnVal, dict);
        }

        // ruby
        if (!ci.ruby.empty()) {
            iTJSDispatch2* rubyArr = TJSCreateArrayObject();
            for (size_t ri = 0; ri < ci.ruby.size(); ri++) {
                iTJSDispatch2* rubyDict = TJSCreateDictionaryObject();
                tTJSVariant rtxt(ttstr(reinterpret_cast<const tjs_char*>(ci.ruby[ri].text.c_str())));
                tTJSVariant rx(ci.ruby[ri].x), ry(ci.ruby[ri].y), rsz(ci.ruby[ri].size);
                rubyDict->PropSet(TJS_MEMBERENSURE, TJS_W("text"), nullptr, &rtxt, rubyDict);
                rubyDict->PropSet(TJS_MEMBERENSURE, TJS_W("x"), nullptr, &rx, rubyDict);
                rubyDict->PropSet(TJS_MEMBERENSURE, TJS_W("y"), nullptr, &ry, rubyDict);
                rubyDict->PropSet(TJS_MEMBERENSURE, TJS_W("size"), nullptr, &rsz, rubyDict);
                tTJSVariant rubyDictVar(rubyDict, rubyDict);
                rubyArr->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(ri), &rubyDictVar, rubyArr);
                rubyDict->Release();
            }
            tTJSVariant rubyArrVar(rubyArr, rubyArr);
            dict->PropSet(TJS_MEMBERENSURE, TJS_W("ruby"), nullptr, &rubyArrVar, dict);
            rubyArr->Release();
        }

        tTJSVariant dictVar(dict, dict);
        array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i), &dictVar, array);
        dict->Release();
    }

    if (result) {
        *result = tTJSVariant(array, array);
    }
    array->Release();
    return TJS_S_OK;
}

// getKeyWait() RawCallback
static tjs_error TJS_INTF_METHOD
RichTextRender_getKeyWait_RawCallback(tTJSVariant* result, tjs_int numparams,
                                       tTJSVariant** param, RichTextRenderWrapper* objthis)
{
    const auto& keyWaits = objthis->getRender().getKeyWaits();

    iTJSDispatch2* array = TJSCreateArrayObject();
    for (size_t i = 0; i < keyWaits.size(); i++) {
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        tTJSVariant posVal((tjs_int)keyWaits[i].charIndex);
        tTJSVariant timeVal(keyWaits[i].delay);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("pos"), nullptr, &posVal, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("time"), nullptr, &timeVal, dict);
        tTJSVariant dictVar(dict, dict);
        array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i), &dictVar, array);
        dict->Release();
    }

    if (result) {
        *result = tTJSVariant(array, array);
    }
    array->Release();
    return TJS_S_OK;
}

// getLinkNames() RawCallback
static tjs_error TJS_INTF_METHOD
RichTextRender_getLinkNames_RawCallback(tTJSVariant* result, tjs_int numparams,
                                         tTJSVariant** param, RichTextRenderWrapper* objthis)
{
    auto names = objthis->getRender().getLinkNames();
    iTJSDispatch2* array = TJSCreateArrayObject();
    for (size_t i = 0; i < names.size(); i++) {
        tTJSVariant nameVal(ttstr(names[i].c_str()));
        array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i), &nameVal, array);
    }
    if (result) {
        *result = tTJSVariant(array, array);
    }
    array->Release();
    return TJS_S_OK;
}

// getLinkRects() RawCallback
static tjs_error TJS_INTF_METHOD
RichTextRender_getLinkRects_RawCallback(tTJSVariant* result, tjs_int numparams,
                                         tTJSVariant** param, RichTextRenderWrapper* objthis)
{
    if (numparams < 1) return TJS_E_BADPARAMCOUNT;
    int link = static_cast<int>(param[0]->AsInteger());
    auto rects = objthis->getRender().getLinkRects(link);

    iTJSDispatch2* array = TJSCreateArrayObject();
    for (size_t i = 0; i < rects.size(); i++) {
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        tTJSVariant lv(rects[i].left), tv(rects[i].top);
        tTJSVariant wv(rects[i].width()), hv(rects[i].height());
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("left"), nullptr, &lv, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("top"), nullptr, &tv, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("width"), nullptr, &wv, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("height"), nullptr, &hv, dict);
        tTJSVariant dictVar(dict, dict);
        array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i), &dictVar, array);
        dict->Release();
    }
    if (result) {
        *result = tTJSVariant(array, array);
    }
    array->Release();
    return TJS_S_OK;
}

// getLinkCharacters() RawCallback
static tjs_error TJS_INTF_METHOD
RichTextRender_getLinkCharacters_RawCallback(tTJSVariant* result, tjs_int numparams,
                                              tTJSVariant** param, RichTextRenderWrapper* objthis)
{
    if (numparams < 1) return TJS_E_BADPARAMCOUNT;
    int link = static_cast<int>(param[0]->AsInteger());

    // getCharacters と同じフォーマットで返す
    auto chars = objthis->getRender().getLinkCharacters(link);
    iTJSDispatch2* array = TJSCreateArrayObject();
    for (size_t i = 0; i < chars.size(); i++) {
        const auto& ci = chars[i];
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        tTJSVariant textVal(ttstr(reinterpret_cast<const tjs_char*>(ci.text.c_str())));
        tTJSVariant xv(ci.x), yv(ci.y), cwv(ci.cw);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("text"), nullptr, &textVal, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("x"), nullptr, &xv, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("y"), nullptr, &yv, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("cw"), nullptr, &cwv, dict);
        tTJSVariant dictVar(dict, dict);
        array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i), &dictVar, array);
        dict->Release();
    }
    if (result) {
        *result = tTJSVariant(array, array);
    }
    array->Release();
    return TJS_S_OK;
}

// RichTextRender ncbind 登録
NCB_GET_INSTANCE_HOOK(RichTextRenderWrapper)
{
    NCB_INSTANCE_GETTER(objthis) {
        ClassT* obj = GetNativeInstance(objthis);
        if (!obj) {
            obj = new ClassT();
            SetNativeInstance(objthis, obj);
            obj->setTJSObject(objthis);
            obj->setupCallbacks();
        }
        return obj;
    }
    ~NCB_GET_INSTANCE_HOOK_CLASS() {
    }
};

NCB_REGISTER_CLASS_DIFFER(TextRenderBase, RichTextRenderWrapper) {
    NCB_CONSTRUCTOR(());

    // 設定
    NCB_METHOD(setRenderSize);
    NCB_METHOD(setDefault);
    NCB_METHOD(setOption);
    NCB_METHOD(setFont);
    NCB_METHOD(resetFont);
    NCB_METHOD(setStyle);
    NCB_METHOD(resetStyle);

    // レンダリング
    NCB_METHOD(clear);
    NCB_METHOD_RAW_CALLBACK(render, RichTextRender_render_RawCallback, 0);
    NCB_METHOD(newline);
    NCB_METHOD(done);

    // 結果プロパティ
    NCB_PROPERTY_RO(renderOver, getRenderOver);
    NCB_PROPERTY_RO(renderLines, getRenderLines);
    NCB_PROPERTY_RO(renderCount, getRenderCount);
    NCB_PROPERTY_RO(renderDelay, getRenderDelay);
    NCB_PROPERTY_RO(renderLeft, getRenderLeft);
    NCB_PROPERTY_RO(renderTop, getRenderTop);
    NCB_PROPERTY_RO(renderRight, getRenderRight);
    NCB_PROPERTY_RO(renderBottom, getRenderBottom);
    NCB_PROPERTY_RO(renderText, getRenderText);

    NCB_PROPERTY(timeScale, getTimeScale, setTimeScale);
    NCB_PROPERTY(fontScale, getFontScale, setFontScale);

    // デフォルトプロパティ
    NCB_PROPERTY(defaultFace, getDefaultFace, setDefaultFace);
    NCB_PROPERTY(defaultFontSize, getDefaultFontSize, setDefaultFontSize);
    NCB_PROPERTY(defaultBigFontSize, getDefaultBigFontSize, setDefaultBigFontSize);
    NCB_PROPERTY(defaultSmallFontSize, getDefaultSmallFontSize, setDefaultSmallFontSize);
    NCB_PROPERTY(defaultLineSize, getDefaultLineSize, setDefaultLineSize);
    NCB_PROPERTY(defaultLineSpacing, getDefaultLineSpacing, setDefaultLineSpacing);
    NCB_PROPERTY(defaultPitch, getDefaultPitch, setDefaultPitch);
    NCB_PROPERTY(defaultAlign, getDefaultAlign, setDefaultAlign);
    NCB_PROPERTY(defaultValign, getDefaultValign, setDefaultValign);
    NCB_PROPERTY(defaultRubySize, getDefaultRubySize, setDefaultRubySize);
    NCB_PROPERTY(defaultChColor, getDefaultChColor, setDefaultChColor);
    NCB_PROPERTY(defaultShadow, getDefaultShadow, setDefaultShadow);
    NCB_PROPERTY(defaultShadowColor, getDefaultShadowColor, setDefaultShadowColor);
    NCB_PROPERTY(defaultEdge, getDefaultEdge, setDefaultEdge);
    NCB_PROPERTY(defaultEdgeColor, getDefaultEdgeColor, setDefaultEdgeColor);
    NCB_PROPERTY(defaultBold, getDefaultBold, setDefaultBold);
    NCB_PROPERTY(defaultItalic, getDefaultItalic, setDefaultItalic);

    // 結果取得メソッド
    NCB_METHOD_RAW_CALLBACK(getCharacters, RichTextRender_getCharacters_RawCallback, 0);
    NCB_METHOD_RAW_CALLBACK(getKeyWait, RichTextRender_getKeyWait_RawCallback, 0);
    NCB_METHOD(calcShowCount);
    NCB_METHOD(calcLineOffset);

    // リンク
    NCB_METHOD_RAW_CALLBACK(getLinkNames, RichTextRender_getLinkNames_RawCallback, 0);
    NCB_METHOD_RAW_CALLBACK(getLinkRects, RichTextRender_getLinkRects_RawCallback, 0);
    NCB_METHOD_RAW_CALLBACK(getLinkCharacters, RichTextRender_getLinkCharacters_RawCallback, 0);
    NCB_METHOD(isLinkContains);
    NCB_METHOD(getLinkOfPosition);
}

// 初期化・終了コールバック
NCB_PRE_REGIST_CALLBACK(initRichText);
NCB_POST_UNREGIST_CALLBACK(deInitRichText);
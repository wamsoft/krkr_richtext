#ifndef RICH_TEXT_RENDER_HPP
#define RICH_TEXT_RENDER_HPP

#include "richtext.hpp"
#include "EscapeConverter.hpp"

#include <string>
#include <vector>
#include <map>
#include <algorithm>

using namespace richtext;

/**
 * TextRender 互換クラス
 *
 * TextRender のエスケープ記法ベースのテキストレンダリングを
 * richtext ライブラリの StyledLayout で再現する。
 *
 * 使用フロー:
 *   clear() → render() [複数回可] → done()
 *   → getCharacters() / calcShowCount() 等で結果取得
 */
class RichTextRender {
public:
    RichTextRender() {
        resetDefaults();
    }

    // ================================================================
    // 設定
    // ================================================================

    void setRenderSize(float w, float h) {
        width_ = w;
        height_ = h;
    }

    /**
     * デフォルト値の設定
     * TJS 辞書から各プロパティを取得
     */
    void setDefaultFromDict(iTJSDispatch2* dict) {
        if (!dict) return;
        tTJSVariant val;

        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("face"), nullptr, &val, dict)))
            defaultFace_ = tjsToU16(val.GetString());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("fontsize"), nullptr, &val, dict)))
            defaultFontSize_ = static_cast<float>(val.AsReal());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("bigfontsize"), nullptr, &val, dict)))
            bigFontSize_ = static_cast<float>(val.AsReal());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("smallfontsize"), nullptr, &val, dict)))
            smallFontSize_ = static_cast<float>(val.AsReal());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("bold"), nullptr, &val, dict)))
            defaultBold_ = static_cast<bool>(val);
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("italic"), nullptr, &val, dict)))
            defaultItalic_ = static_cast<bool>(val);
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("color"), nullptr, &val, dict)))
            defaultColor_ = static_cast<tjs_uint32>(val.AsInteger());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("shadow"), nullptr, &val, dict)))
            defaultShadow_ = static_cast<bool>(val);
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("shadowcolor"), nullptr, &val, dict)))
            defaultShadowColor_ = static_cast<tjs_uint32>(val.AsInteger());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("edge"), nullptr, &val, dict)))
            defaultEdge_ = static_cast<bool>(val);
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("edgecolor"), nullptr, &val, dict)))
            defaultEdgeColor_ = static_cast<tjs_uint32>(val.AsInteger());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("linespacing"), nullptr, &val, dict)))
            defaultLineSpacing_ = static_cast<float>(val.AsReal());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("pitch"), nullptr, &val, dict)))
            defaultPitch_ = static_cast<float>(val.AsReal());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("linesize"), nullptr, &val, dict)))
            defaultLineSize_ = static_cast<float>(val.AsReal());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("align"), nullptr, &val, dict)))
            defaultAlign_ = static_cast<int>(val.AsInteger());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("valign"), nullptr, &val, dict)))
            defaultValign_ = static_cast<int>(val.AsInteger());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("rubysize"), nullptr, &val, dict)))
            defaultRubySize_ = static_cast<float>(val.AsReal());
    }

    /**
     * オプション設定
     * TJS 辞書から ignore_* 等を取得
     */
    void setOptionFromDict(iTJSDispatch2* dict) {
        if (!dict) return;
        tTJSVariant val;

        auto getBool = [&](const tjs_char* name, bool& out) {
            if (TJS_SUCCEEDED(dict->PropGet(0, name, nullptr, &val, dict)))
                out = static_cast<bool>(val);
        };

        // エスケープ→タグ変換段階の ignore（EscapeConverter 用）
        getBool(TJS_W("ignore_color"), convertOptions_.ignoreColor);
        getBool(TJS_W("ignore_size"), convertOptions_.ignoreSize);
        getBool(TJS_W("ignore_type"), convertOptions_.ignoreType);
        getBool(TJS_W("ignore_face"), convertOptions_.ignoreFace);
        getBool(TJS_W("ignore_ruby"), convertOptions_.ignoreRuby);

        // タグパース段階の ignore（ParseOptions 用）— 二重に無視
        getBool(TJS_W("ignore_color"), parserOptions_.ignoreColor);
        getBool(TJS_W("ignore_size"), parserOptions_.ignoreSize);
        getBool(TJS_W("ignore_type"), parserOptions_.ignoreType);
        getBool(TJS_W("ignore_face"), parserOptions_.ignoreFace);
        getBool(TJS_W("ignore_ruby"), parserOptions_.ignoreRuby);

        // タグパース段階のみの ignore
        bool ignoreDelay = false, ignoreStyle = false;
        getBool(TJS_W("ignore_delay"), ignoreDelay);
        getBool(TJS_W("ignore_style"), ignoreStyle);
        parserOptions_.ignoreDelay = ignoreDelay;
        parserOptions_.ignoreSpacing = ignoreStyle;

        getBool(TJS_W("width_time_scale"), widthTimeScale_);

        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("locale"), nullptr, &val, dict))) {
            ttstr loc = val.GetString();
            if (loc.length() > 0) {
                locale_ = tjsToNarrow(loc.c_str());
            }
        }
    }

    // ================================================================
    // フォント / スタイル操作
    // ================================================================

    void setFontFromDict(iTJSDispatch2* dict) {
        if (!dict) return;
        tTJSVariant val;
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("face"), nullptr, &val, dict)))
            currentFace_ = tjsToU16(val.GetString());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("fontsize"), nullptr, &val, dict)))
            currentFontSize_ = static_cast<float>(val.AsReal());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("bold"), nullptr, &val, dict)))
            currentBold_ = static_cast<bool>(val);
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("italic"), nullptr, &val, dict)))
            currentItalic_ = static_cast<bool>(val);
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("color"), nullptr, &val, dict)))
            currentColor_ = static_cast<tjs_uint32>(val.AsInteger());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("shadow"), nullptr, &val, dict)))
            currentShadow_ = static_cast<bool>(val);
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("shadowcolor"), nullptr, &val, dict)))
            currentShadowColor_ = static_cast<tjs_uint32>(val.AsInteger());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("edge"), nullptr, &val, dict)))
            currentEdge_ = static_cast<bool>(val);
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("edgecolor"), nullptr, &val, dict)))
            currentEdgeColor_ = static_cast<tjs_uint32>(val.AsInteger());
    }

    void resetFont() {
        currentFace_ = defaultFace_;
        currentFontSize_ = defaultFontSize_;
        currentBold_ = defaultBold_;
        currentItalic_ = defaultItalic_;
        currentColor_ = defaultColor_;
        currentShadow_ = defaultShadow_;
        currentShadowColor_ = defaultShadowColor_;
        currentEdge_ = defaultEdge_;
        currentEdgeColor_ = defaultEdgeColor_;
    }

    void setStyleFromDict(iTJSDispatch2* dict) {
        if (!dict) return;
        tTJSVariant val;
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("linespacing"), nullptr, &val, dict)))
            currentLineSpacing_ = static_cast<float>(val.AsReal());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("pitch"), nullptr, &val, dict)))
            currentPitch_ = static_cast<float>(val.AsReal());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("linesize"), nullptr, &val, dict)))
            currentLineSize_ = static_cast<float>(val.AsReal());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("align"), nullptr, &val, dict)))
            currentAlign_ = static_cast<int>(val.AsInteger());
        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("valign"), nullptr, &val, dict)))
            currentValign_ = static_cast<int>(val.AsInteger());
    }

    void resetStyle() {
        currentLineSpacing_ = defaultLineSpacing_;
        currentPitch_ = defaultPitch_;
        currentLineSize_ = defaultLineSize_;
        currentAlign_ = defaultAlign_;
        currentValign_ = defaultValign_;
    }

    // ================================================================
    // レンダリング
    // ================================================================

    void clear() {
        renderEntries_.clear();
        characters_.clear();
        lineOffsets_.clear();
        renderOver_ = false;
        renderText_.clear();
        renderLeft_ = renderTop_ = renderRight_ = renderBottom_ = 0;
        resetFont();
        resetStyle();
    }

    /**
     * テキストをレンダリングキューに追加
     * done() が呼ばれるまで蓄積する
     */
    void render(const std::u16string& text, int autoIndent, float diff, float all, bool noResetDelay) {
        // noResetDelay=false の場合、既存エントリの diff/all を 0 クリア（瞬間表示化）
        if (!noResetDelay) {
            for (auto& e : renderEntries_) {
                e.diff = 0;
                e.all = 0;
            }
        }

        // all がある場合の diff 最小値調整（diff=0 だとスキップ扱いになるため）
        float _diff = diff;
        if (all > 0 && _diff == 0) {
            _diff = 0.001f;
        }

        RenderEntry entry;
        entry.text = text;
        entry.autoIndent = autoIndent;
        entry.diff = _diff;
        entry.all = all;
        entry.fontPreamble = buildFontPreamble();
        renderEntries_.push_back(std::move(entry));
    }

    void newline() {
        RenderEntry entry;
        entry.text = u"\n";
        entry.diff = 0;
        entry.all = 0;
        renderEntries_.push_back(std::move(entry));
    }

    /**
     * レンダリング完了: 蓄積テキストを一括処理
     */
    void done() {
        // 1. 蓄積したテキストをエスケープ変換→タグ付きテキストに結合
        EscapeConverter converter;
        converter.setOptions(convertOptions_);
        if (graphSizeCallback_) converter.setGraphSizeCallback(graphSizeCallback_);

        std::u16string combinedTaggedText;

        for (size_t i = 0; i < renderEntries_.size(); i++) {
            auto& entry = renderEntries_[i];
            std::u16string fullText = entry.fontPreamble + entry.text;
            auto result = converter.convert(fullText, defaultFontSize_, bigFontSize_, smallFontSize_,
                                           entry.diff, entry.all);
            combinedTaggedText += result.taggedText;
            // アライン指定を反映
            if (result.align != -2) currentAlign_ = result.align;
        }

        // 2. StyledLayout でレイアウト（TagParser が全メタデータを生成）
        styledLayout_.setParserOptions(parserOptions_);
        if (evalCallback_) styledLayout_.setEvalCallback(evalCallback_);
        if (labelResolver_) styledLayout_.setLabelResolver(labelResolver_);
        buildStylesAndLayout(combinedTaggedText);

        // 3. タイミング解決
        styledLayout_.resolveAllTimings(timeScale_, widthTimeScale_);

        // 4. 文字情報を構築
        buildCharacterInfo();

        // 5. リンク矩形を構築
        linkRegions_ = styledLayout_.buildLinkRegions();
    }

    // ================================================================
    // 結果取得
    // ================================================================

    bool getRenderOver() const { return renderOver_; }
    int getRenderLines() const { return static_cast<int>(lineOffsets_.size()); }
    int getRenderCount() const { return static_cast<int>(characters_.size()); }
    float getRenderDelay() const { return styledLayout_.getTotalRenderDelay(); }

    float getRenderLeft() const { return renderLeft_; }
    float getRenderTop() const { return renderTop_; }
    float getRenderRight() const { return renderRight_; }
    float getRenderBottom() const { return renderBottom_; }

    const std::u16string& getRenderText() const { return renderText_; }

    float getTimeScale() const { return timeScale_; }
    void setTimeScale(float v) { timeScale_ = v; }

    float getFontScale() const { return fontScale_; }
    void setFontScale(float v) { fontScale_ = v; }

    int calcShowCount(float time) const {
        return styledLayout_.calcShowCount(time);
    }

    float calcLineOffset(int lineno) const {
        if (lineno < 0 || lineno >= static_cast<int>(lineOffsets_.size())) return 0;
        return lineOffsets_[lineno];
    }

    const std::vector<KeyWaitInfo>& getKeyWaits() const {
        return styledLayout_.getKeyWaits();
    }

    // ================================================================
    // 文字情報
    // ================================================================

    struct CharacterInfo {
        std::u16string text;
        std::u16string graph;
        float x = 0, y = 0;
        float cw = 0;
        float size = 0;
        std::u16string face;
        tjs_uint32 color = 0xFFFFFF;
        bool bold = false;
        bool italic = false;
        bool shadow = false;
        bool edge = false;
        tjs_uint32 shadowColor = 0;
        int shadowDiff = 2;
        tjs_uint32 edgeColor = 0;
        float delay = 0;
        int link = -1;
        std::string linkName;
        struct RubyInfo {
            std::u16string text;
            float x = 0, y = 0;
            float size = 0;
        };
        std::vector<RubyInfo> ruby;
    };

    std::vector<CharacterInfo> getCharacters(int start, int num) const {
        std::vector<CharacterInfo> result;
        int end = (num <= 0) ? static_cast<int>(characters_.size()) : std::min(start + num, static_cast<int>(characters_.size()));
        for (int i = start; i < end; i++) {
            result.push_back(characters_[i]);
        }
        return result;
    }

    // ================================================================
    // リンク
    // ================================================================

    int getLinkCount() const { return static_cast<int>(linkRegions_.size()); }

    std::vector<std::string> getLinkNames() const {
        std::vector<std::string> names;
        for (const auto& lr : linkRegions_) {
            names.push_back(lr.name);
        }
        return names;
    }

    std::vector<LinkRect> getLinkRects(int link) const {
        if (link < 0 || link >= static_cast<int>(linkRegions_.size())) return {};
        return linkRegions_[link].rects;
    }

    std::vector<CharacterInfo> getLinkCharacters(int link) const {
        if (link < 0 || link >= static_cast<int>(linkRegions_.size())) return {};
        std::vector<CharacterInfo> result;
        for (int idx : linkRegions_[link].charIndices) {
            if (idx >= 0 && idx < static_cast<int>(characters_.size())) {
                result.push_back(characters_[idx]);
            }
        }
        return result;
    }

    bool isLinkContains(int link, float x, float y) const {
        if (link < 0 || link >= static_cast<int>(linkRegions_.size())) return false;
        return linkRegions_[link].contains(x, y);
    }

    int getLinkOfPosition(float x, float y) const {
        for (int i = 0; i < static_cast<int>(linkRegions_.size()); i++) {
            if (linkRegions_[i].contains(x, y)) return i;
        }
        return -1;
    }

    // ================================================================
    // コールバック設定
    // ================================================================

    void setEvalCallback(EvalCallback cb) { evalCallback_ = std::move(cb); }
    void setGraphSizeCallback(EscapeConverter::GraphSizeCallback cb) { graphSizeCallback_ = std::move(cb); }
    void setLabelResolver(LabelResolver cb) { labelResolver_ = std::move(cb); }

    /** StyledLayout への参照を返す */
    const StyledLayout& getStyledLayout() const { return styledLayout_; }

    // ================================================================
    // デフォルトプロパティ（TJS から参照可能）
    // ================================================================

    const std::u16string& getDefaultFace() const { return defaultFace_; }
    void setDefaultFace(const std::u16string& v) { defaultFace_ = v; }

    float getDefaultFontSize() const { return defaultFontSize_; }
    void setDefaultFontSize(float v) { defaultFontSize_ = v; }
    float getDefaultBigFontSize() const { return bigFontSize_; }
    void setDefaultBigFontSize(float v) { bigFontSize_ = v; }
    float getDefaultSmallFontSize() const { return smallFontSize_; }
    void setDefaultSmallFontSize(float v) { smallFontSize_ = v; }
    float getDefaultLineSize() const { return defaultLineSize_; }
    void setDefaultLineSize(float v) { defaultLineSize_ = v; }
    float getDefaultLineSpacing() const { return defaultLineSpacing_; }
    void setDefaultLineSpacing(float v) { defaultLineSpacing_ = v; }
    float getDefaultPitch() const { return defaultPitch_; }
    void setDefaultPitch(float v) { defaultPitch_ = v; }
    int getDefaultAlign() const { return defaultAlign_; }
    void setDefaultAlign(int v) { defaultAlign_ = v; }
    int getDefaultValign() const { return defaultValign_; }
    void setDefaultValign(int v) { defaultValign_ = v; }
    float getDefaultRubySize() const { return defaultRubySize_; }
    void setDefaultRubySize(float v) { defaultRubySize_ = v; }
    tjs_uint32 getDefaultColor() const { return defaultColor_; }
    void setDefaultColor(tjs_uint32 v) { defaultColor_ = v; }
    bool getDefaultShadow() const { return defaultShadow_; }
    void setDefaultShadow(bool v) { defaultShadow_ = v; }
    tjs_uint32 getDefaultShadowColor() const { return defaultShadowColor_; }
    void setDefaultShadowColor(tjs_uint32 v) { defaultShadowColor_ = v; }
    bool getDefaultEdge() const { return defaultEdge_; }
    void setDefaultEdge(bool v) { defaultEdge_ = v; }
    tjs_uint32 getDefaultEdgeColor() const { return defaultEdgeColor_; }
    void setDefaultEdgeColor(tjs_uint32 v) { defaultEdgeColor_ = v; }
    bool getDefaultBold() const { return defaultBold_; }
    void setDefaultBold(bool v) { defaultBold_ = v; }
    bool getDefaultItalic() const { return defaultItalic_; }
    void setDefaultItalic(bool v) { defaultItalic_ = v; }

private:
    // ================================================================
    // 内部ユーティリティ
    // ================================================================

    static std::u16string tjsToU16(const tjs_char* str) {
        if (!str) return std::u16string();
        return std::u16string(reinterpret_cast<const char16_t*>(str));
    }

    static std::string tjsToNarrow(const tjs_char* str) {
        std::string result;
        if (str) {
            tjs_int len = TVPWideCharToUtf8String(str, NULL);
            if (len > 0) {
                char* buf = new char[len];
                try {
                    len = TVPWideCharToUtf8String(str, buf);
                    if (len > 0) result.assign(buf, len);
                    delete[] buf;
                } catch(...) {
                    delete[] buf;
                    throw;
                }
            }
        }
        return result;
    }

    static std::u16string colorToHex(tjs_uint32 color) {
        const char hexDigits[] = "0123456789abcdef";
        std::u16string result;
        for (int i = 5; i >= 0; i--) {
            result += static_cast<char16_t>(hexDigits[(color >> (i * 4)) & 0xF]);
        }
        return result;
    }

    std::u16string buildFontPreamble() {
        std::u16string preamble;

        if (!currentFace_.empty() && currentFace_ != defaultFace_) {
            preamble += u"%f";
            preamble += currentFace_;
            preamble += u';';
        }

        if (currentFontSize_ != defaultFontSize_ && defaultFontSize_ > 0) {
            float percent = (currentFontSize_ / defaultFontSize_) * 100.0f;
            std::u16string numStr;
            int p = static_cast<int>(percent);
            std::string s = std::to_string(p);
            for (char c : s) numStr += static_cast<char16_t>(c);
            preamble += u'%';
            preamble += numStr;
            preamble += u';';
        }

        if (currentBold_ != defaultBold_)
            preamble += currentBold_ ? u"%b1" : u"%b0";
        if (currentItalic_ != defaultItalic_)
            preamble += currentItalic_ ? u"%i1" : u"%i0";
        if (currentColor_ != defaultColor_) {
            preamble += u'#';
            preamble += colorToHex(currentColor_);
            preamble += u';';
        }
        if (currentShadow_ != defaultShadow_)
            preamble += currentShadow_ ? u"%s1" : u"%s0";
        if (currentEdge_ != defaultEdge_)
            preamble += currentEdge_ ? u"%e1" : u"%e0";

        if (currentPitch_ != defaultPitch_) {
            std::u16string numStr;
            int p = static_cast<int>(currentPitch_);
            std::string s = std::to_string(p);
            for (char c : s) numStr += static_cast<char16_t>(c);
            preamble += u"%p";
            preamble += numStr;
            preamble += u';';
        }

        return preamble;
    }

    TextStyle buildDefaultStyle() {
        TextStyle style;
        if (!defaultFace_.empty()) {
            std::string faceNarrow;
            for (char16_t c : defaultFace_) {
                if (c < 128) faceNarrow += static_cast<char>(c);
            }
            auto collection = FontManager::instance().getCollection(faceNarrow);
            if (collection) {
                style.fontCollection = collection;
            }
        }
        float fontSize = defaultFontSize_;
        if (fontScale_ != 1.0f) fontSize *= fontScale_;
        style.fontSize = fontSize;
        style.fontWeight = defaultBold_ ? 700 : 400;
        style.italic = defaultItalic_;
        style.letterSpacing = defaultPitch_;
        if (!locale_.empty()) {
            style.localeId = FontManager::instance().registerLocale(locale_);
        }
        return style;
    }

    Appearance buildDefaultAppearance() {
        Appearance app;
        uint32_t argb = 0xFF000000 | (defaultColor_ & 0xFFFFFF);
        app.setColor(argb);
        if (defaultShadow_) {
            uint32_t shadowArgb = 0xFF000000 | (defaultShadowColor_ & 0xFFFFFF);
            app.setShadow(shadowArgb, 2.0f, 2.0f);
        }
        if (defaultEdge_) {
            uint32_t edgeArgb = 0xFF000000 | (defaultEdgeColor_ & 0xFFFFFF);
            app.setOutline(edgeArgb, 1.0f);
        }
        return app;
    }

    void buildStylesAndLayout(const std::u16string& taggedText) {
        TextStyle defStyle = buildDefaultStyle();
        Appearance defApp = buildDefaultAppearance();

        std::map<std::string, TextStyle> styles;
        styles["default"] = defStyle;

        std::map<std::string, Appearance> appearances;
        appearances["default"] = defApp;

        ParagraphLayout::HAlign hAlign = ParagraphLayout::HAlign::Left;
        if (currentAlign_ == 0) hAlign = ParagraphLayout::HAlign::Center;
        else if (currentAlign_ == 1) hAlign = ParagraphLayout::HAlign::Right;

        ParagraphLayout::VAlign vAlign = ParagraphLayout::VAlign::Top;
        if (currentValign_ == 0) vAlign = ParagraphLayout::VAlign::Middle;
        else if (currentValign_ == 1) vAlign = ParagraphLayout::VAlign::Bottom;

        float maxWidth = (width_ > 0) ? width_ : 100000.0f;
        float maxHeight = (height_ > 0) ? height_ : 100000.0f;

        styledLayout_.layout(taggedText, maxWidth, maxHeight,
                            hAlign, vAlign, styles, appearances,
                            currentLineSpacing_);

        const auto& para = styledLayout_.getParagraphLayout();
        float totalHeight = para.getTotalHeight();
        float maxW = para.getMaxWidth();

        renderLeft_ = 0;
        renderTop_ = 0;
        renderRight_ = maxW;
        renderBottom_ = totalHeight;

        renderOver_ = false;
        if (width_ > 0 && maxW > width_) renderOver_ = true;
        if (height_ > 0 && totalHeight > height_) renderOver_ = true;
    }


    void buildCharacterInfo() {
        characters_.clear();
        renderText_.clear();
        lineOffsets_.clear();

        const auto& lineLayouts = styledLayout_.getLineLayouts();
        const auto& parsed = styledLayout_.getParsed();
        const auto& plainText = parsed.plainText;
        const auto& spans = parsed.spans;
        const auto& links = parsed.links;
        const auto& graphics = parsed.graphics;

        int globalCharIdx = 0;

        for (size_t li = 0; li < lineLayouts.size(); li++) {
            const auto& line = lineLayouts[li];

            const auto& para = styledLayout_.getParagraphLayout();
            if (li < static_cast<size_t>(para.getLineCount())) {
                const auto& lineInfo = para.getLine(li);
                lineOffsets_.push_back(lineInfo.height());
            }

            for (const auto& seg : line.segments) {
                const auto& glyphs = seg.layout.getGlyphs();
                const auto& span = (seg.spanIdx < spans.size()) ? spans[seg.spanIdx] : spans[0];

                for (const auto& glyph : glyphs) {
                    CharacterInfo ci;

                    size_t charIdx = seg.segStart + glyph.charIndex;
                    if (charIdx < plainText.size()) {
                        ci.text = std::u16string(1, plainText[charIdx]);
                        renderText_ += plainText[charIdx];
                    }

                    ci.x = glyph.x;
                    ci.y = glyph.y + seg.yOffset;
                    ci.cw = glyph.advance;
                    ci.size = span.style.fontSize;
                    ci.bold = (span.style.fontWeight >= 700);
                    ci.italic = span.style.italic;
                    ci.face = defaultFace_;

                    ci.color = defaultColor_;
                    ci.shadow = defaultShadow_;
                    ci.shadowColor = defaultShadowColor_;
                    ci.edge = defaultEdge_;
                    ci.edgeColor = defaultEdgeColor_;

                    // delay
                    const auto& resolvedTimings = styledLayout_.getResolvedTimings();
                    if (globalCharIdx < static_cast<int>(resolvedTimings.size())) {
                        ci.delay = resolvedTimings[globalCharIdx].delay;
                    }

                    // リンク（StyledLayout 経由の LinkInfo を使用）
                    ci.link = -1;
                    for (int lk = 0; lk < static_cast<int>(links.size()); lk++) {
                        if (charIdx >= links[lk].startIndex && charIdx < links[lk].endIndex) {
                            ci.link = lk;
                            ci.linkName = links[lk].name;
                            break;
                        }
                    }

                    // グラフィック文字（StyledLayout 経由の GraphInfo を使用）
                    for (const auto& g : graphics) {
                        if (g.charIndex == static_cast<int>(charIdx)) {
                            ci.graph = g.name;
                            break;
                        }
                    }

                    // ルビ情報
                    if (span.hasRuby && !span.rubyText.empty()) {
                        CharacterInfo::RubyInfo ri;
                        ri.text = span.rubyText;
                        ri.x = glyph.x;
                        ri.y = glyph.y - span.style.fontSize * 0.6f;
                        ri.size = span.style.fontSize * 0.5f;
                        ci.ruby.push_back(ri);
                    }

                    characters_.push_back(std::move(ci));
                    globalCharIdx++;
                }
            }

            if (li < lineLayouts.size() - 1) {
                renderText_ += u'\n';
            }
        }
    }

    void resetDefaults() {
        defaultFontSize_ = 24.0f;
        bigFontSize_ = 36.0f;
        smallFontSize_ = 18.0f;
        defaultLineSize_ = 0;
        defaultLineSpacing_ = 0;
        defaultPitch_ = 0;
        defaultAlign_ = -1;
        defaultValign_ = -1;
        defaultRubySize_ = 12.0f;
        defaultColor_ = 0xFFFFFF;
        defaultShadow_ = false;
        defaultShadowColor_ = 0x000000;
        defaultEdge_ = false;
        defaultEdgeColor_ = 0x000000;
        defaultBold_ = false;
        defaultItalic_ = false;
        timeScale_ = 1.0f;
        fontScale_ = 1.0f;
        widthTimeScale_ = false;
        resetFont();
        resetStyle();
    }

    // ================================================================
    // メンバ変数
    // ================================================================

    float width_ = 0, height_ = 0;

    // デフォルト値
    std::u16string defaultFace_;
    float defaultFontSize_ = 24.0f;
    float bigFontSize_ = 36.0f;
    float smallFontSize_ = 18.0f;
    float defaultLineSize_ = 0;
    float defaultLineSpacing_ = 0;
    float defaultPitch_ = 0;
    int defaultAlign_ = -1;
    int defaultValign_ = -1;
    float defaultRubySize_ = 12.0f;
    tjs_uint32 defaultColor_ = 0xFFFFFF;
    bool defaultShadow_ = false;
    tjs_uint32 defaultShadowColor_ = 0x000000;
    bool defaultEdge_ = false;
    tjs_uint32 defaultEdgeColor_ = 0x000000;
    bool defaultBold_ = false;
    bool defaultItalic_ = false;

    // 現在のフォント状態
    std::u16string currentFace_;
    float currentFontSize_ = 24.0f;
    bool currentBold_ = false;
    bool currentItalic_ = false;
    tjs_uint32 currentColor_ = 0xFFFFFF;
    bool currentShadow_ = false;
    tjs_uint32 currentShadowColor_ = 0x000000;
    bool currentEdge_ = false;
    tjs_uint32 currentEdgeColor_ = 0x000000;

    // 現在のスタイル状態
    float currentLineSpacing_ = 0;
    float currentPitch_ = 0;
    float currentLineSize_ = 0;
    int currentAlign_ = -1;
    int currentValign_ = -1;

    // オプション
    EscapeConverter::ConvertOptions convertOptions_;
    TagParser::ParseOptions parserOptions_;
    bool widthTimeScale_ = false;
    float timeScale_ = 1.0f;
    float fontScale_ = 1.0f;
    std::string locale_;

    // コールバック
    EvalCallback evalCallback_;
    EscapeConverter::GraphSizeCallback graphSizeCallback_;
    LabelResolver labelResolver_;

    // render() で蓄積するエントリ
    struct RenderEntry {
        std::u16string text;
        std::u16string fontPreamble;
        int autoIndent = 0;
        float diff = 0;
        float all = 0;
    };
    std::vector<RenderEntry> renderEntries_;

    // done() 後の結果
    StyledLayout styledLayout_;
    std::vector<CharacterInfo> characters_;
    std::vector<LinkRegion> linkRegions_;
    std::vector<float> lineOffsets_;

    bool renderOver_ = false;
    float renderLeft_ = 0, renderTop_ = 0, renderRight_ = 0, renderBottom_ = 0;
    std::u16string renderText_;
};

#endif // RICH_TEXT_RENDER_HPP

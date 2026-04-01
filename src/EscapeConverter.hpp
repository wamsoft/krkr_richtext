#ifndef ESCAPE_CONVERTER_HPP
#define ESCAPE_CONVERTER_HPP

#include <string>
#include <vector>
#include <functional>

#include "richtext/TimingInfo.hpp"

/**
 * TextRender エスケープ記法 → richtext タグ付きテキスト変換器
 *
 * TextRender の特殊テキスト書式（%codes, #color, [ruby], \escape 等）を
 * 解析し、richtext の TagParser が処理できるタグ付きテキストと、
 * タイミング・リンク・グラフィック等のメタ情報を出力する。
 */
class EscapeConverter {
public:
    /// 変数参照コールバック ($xxx; → 値)
    using EvalCallback = std::function<std::u16string(const std::u16string& name)>;

    /// グラフィック文字サイズ取得コールバック (&xxx; → width, height)
    using GraphSizeCallback = std::function<bool(const std::u16string& name,
                                                  float& outWidth, float& outHeight)>;

    /// 変換オプション（ignore_* 系）
    struct ConvertOptions {
        bool ignoreColor = false;   ///< 色指定を無視
        bool ignoreSize = false;    ///< サイズ指定を無視
        bool ignoreType = false;    ///< フォント種別指定を無視（bold/italic/shadow/edge）
        bool ignoreFace = false;    ///< フォントフェイス指定を無視
        bool ignoreStyle = false;   ///< スタイル指定を無視（align/pitch）
        bool ignoreRuby = false;    ///< ルビ指定を無視
        bool ignoreDelay = false;   ///< 時間指定を無視
    };

    /// グラフィック文字情報
    struct GraphInfo {
        std::u16string name;    ///< 画像名
        int charIndex;          ///< plainText 内の位置
        float width;            ///< 幅
        float height;           ///< 高さ
    };

    /// リンク情報（変換結果用）
    struct LinkInfo {
        std::string name;       ///< リンク名
        size_t startIndex;      ///< plainText 内の開始位置
        size_t endIndex;        ///< plainText 内の終了位置
    };

    /// 変換結果
    struct ConvertResult {
        std::u16string taggedText;                      ///< richtext 用タグ付きテキスト
        std::vector<richtext::TimingEntry> timings;     ///< タイミング情報
        std::vector<richtext::KeyWaitInfo> keyWaits;    ///< キー待ち情報
        std::vector<LinkInfo> links;                    ///< リンク情報
        std::vector<GraphInfo> graphics;                ///< グラフィック文字情報
        int align = -1;                                 ///< テキスト内アライン指定（-1:左, 0:中央, 1:右）
        float pitch = -1.0f;                            ///< テキスト内ピッチ指定（-1 = 未指定）
    };

    EscapeConverter();

    void setOptions(const ConvertOptions& opts) { options_ = opts; }
    void setEvalCallback(EvalCallback cb) { evalCallback_ = std::move(cb); }
    void setGraphSizeCallback(GraphSizeCallback cb) { graphSizeCallback_ = std::move(cb); }

    /**
     * エスケープ記法テキストを richtext タグ付きテキストに変換
     *
     * @param escapeText        エスケープ記法テキスト
     * @param defaultFontSize   デフォルトフォントサイズ
     * @param bigFontSize       大サイズフォントサイズ（%B 用）
     * @param smallFontSize     小サイズフォントサイズ（%S 用）
     * @return 変換結果
     */
    ConvertResult convert(const std::u16string& escapeText,
                          float defaultFontSize,
                          float bigFontSize,
                          float smallFontSize);

private:
    ConvertOptions options_;
    EvalCallback evalCallback_;
    GraphSizeCallback graphSizeCallback_;

    // パース状態
    struct ParseState {
        const char16_t* text;
        size_t len;
        size_t pos;

        // 出力
        std::u16string taggedText;
        size_t plainCharCount;  // タグを除いたプレーンテキスト文字数

        // スタイルスタック管理
        bool inBold = false;
        bool inItalic = false;
        bool inShadow = false;
        bool inEdge = false;
        int openFontTags = 0;       // 開いている <font> タグ数
        int openColorTags = 0;      // 開いている <color> タグ数
        int openShadowTags = 0;     // 開いている <shadow> タグ数
        int openOutlineTags = 0;    // 開いている <outline> タグ数
        int openBoldTags = 0;
        int openItalicTags = 0;

        // リンク状態
        int currentLinkIndex = -1;

        // タイミング
        float currentDelayPercent = 100.0f;
        float currentDelayMs = -1.0f;

        // 結果蓄積
        std::vector<richtext::TimingEntry> timings;
        std::vector<richtext::KeyWaitInfo> keyWaits;
        std::vector<LinkInfo> links;
        std::vector<GraphInfo> graphics;
        int align = -1;
        float pitch = -1.0f;
    };

    // パースユーティリティ
    static char16_t peek(const ParseState& s);
    static char16_t advance(ParseState& s);
    static bool matchStr(const ParseState& s, const char16_t* str, size_t len);

    // セミコロンまでの文字列を読む
    static std::u16string readUntilSemicolon(ParseState& s);

    // セミコロンまでの数値を読む
    static float readNumberUntilSemicolon(ParseState& s);

    // 通常文字の追加（タイミングエントリ付き）
    void addChar(ParseState& s, char16_t ch);
    void addChars(ParseState& s, const std::u16string& str);

    // タグ出力
    void emitTag(ParseState& s, const std::u16string& tag);

    // 全スタイルタグを閉じる（%r リセット用）
    void closeAllStyleTags(ParseState& s);

    // エスケープシーケンスのパース
    void parseBackslash(ParseState& s);
    void parsePercent(ParseState& s, float defaultFontSize,
                      float bigFontSize, float smallFontSize);
    void parseHash(ParseState& s);
    void parseDollar(ParseState& s);
    void parseAmpersand(ParseState& s);
    void parseRuby(ParseState& s);
};

#endif // ESCAPE_CONVERTER_HPP

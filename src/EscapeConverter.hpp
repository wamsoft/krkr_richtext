#ifndef ESCAPE_CONVERTER_HPP
#define ESCAPE_CONVERTER_HPP

#include <string>
#include <vector>
#include <functional>

/**
 * TextRender エスケープ記法 → richtext タグ付きテキスト変換器
 *
 * TextRender の特殊テキスト書式（%codes, #color, [ruby], \escape 等）を
 * 解析し、richtext の TagParser が処理できるタグ付きテキストを出力する。
 *
 * タイミング・リンク・グラフィック等のメタ情報はタグとして出力され、
 * TagParser 側で解釈・記録される。
 */
class EscapeConverter {
public:
    /// グラフィック文字サイズ取得コールバック (&xxx; → width, height)
    using GraphSizeCallback = std::function<bool(const std::u16string& name,
                                                  float& outWidth, float& outHeight)>;

    /// 変換オプション（エスケープ→タグ変換段階での ignore）
    struct ConvertOptions {
        bool ignoreColor = false;   ///< 色指定を無視（タグを出力しない）
        bool ignoreSize = false;    ///< サイズ指定を無視
        bool ignoreType = false;    ///< フォント種別指定を無視（bold/italic/shadow/edge）
        bool ignoreFace = false;    ///< フォントフェイス指定を無視
        bool ignoreRuby = false;    ///< ルビ指定を無視
    };

    /// 変換結果
    struct ConvertResult {
        std::u16string taggedText;  ///< richtext 用タグ付きテキスト
        int align = -2;             ///< アライン指定（-2:未指定, -1:左, 0:中央, 1:右）
    };

    EscapeConverter();

    void setOptions(const ConvertOptions& opts) { options_ = opts; }
    void setGraphSizeCallback(GraphSizeCallback cb) { graphSizeCallback_ = std::move(cb); }

    /**
     * エスケープ記法テキストを richtext タグ付きテキストに変換
     *
     * @param escapeText        エスケープ記法テキスト
     * @param defaultFontSize   デフォルトフォントサイズ
     * @param bigFontSize       大サイズフォントサイズ（%B 用）
     * @param smallFontSize     小サイズフォントサイズ（%S 用）
     * @param diff              1文字あたり基準表示時間（ms）
     * @param all               全体表示時間（ms、0 = 自動）
     * @return 変換結果
     */
    ConvertResult convert(const std::u16string& escapeText,
                          float defaultFontSize,
                          float bigFontSize,
                          float smallFontSize,
                          float diff = 0.0f,
                          float all = 0.0f);

private:
    ConvertOptions options_;
    GraphSizeCallback graphSizeCallback_;

    // パース状態
    struct ParseState {
        const char16_t* text;
        size_t len;
        size_t pos;

        // 出力
        std::u16string taggedText;

        // アライン記録
        int align = -2;  // -2:未指定

        // スタイルスタック管理
        bool inBold = false;
        bool inItalic = false;
        bool inShadow = false;
        bool inEdge = false;
        int openFontTags = 0;
        int openColorTags = 0;
        int openShadowTags = 0;
        int openOutlineTags = 0;
        int openBoldTags = 0;
        int openItalicTags = 0;
    };

    // パースユーティリティ
    static char16_t peek(const ParseState& s);
    static char16_t advance(ParseState& s);

    // セミコロンまでの文字列を読む
    static std::u16string readUntilSemicolon(ParseState& s);

    // セミコロンまでの数値を読む
    static float readNumberUntilSemicolon(ParseState& s);

    // 通常文字の追加（タグ安全エスケープ）
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

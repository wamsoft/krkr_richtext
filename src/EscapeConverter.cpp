#include "EscapeConverter.hpp"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>

// char16_t 配列から std::string (ASCII) に変換
static std::string u16ToAscii(const std::u16string& s) {
    std::string r;
    r.reserve(s.size());
    for (char16_t c : s) {
        if (c < 128) r += static_cast<char>(c);
    }
    return r;
}

// 数値を u16string に変換
static std::u16string toU16String(float v) {
    std::ostringstream oss;
    if (v == static_cast<int>(v)) {
        oss << static_cast<int>(v);
    } else {
        oss << v;
    }
    std::string s = oss.str();
    std::u16string r;
    for (char c : s) r += static_cast<char16_t>(c);
    return r;
}

// ============================================================================
// ユーティリティ
// ============================================================================

char16_t EscapeConverter::peek(const ParseState& s) {
    return (s.pos < s.len) ? s.text[s.pos] : 0;
}

char16_t EscapeConverter::advance(ParseState& s) {
    return (s.pos < s.len) ? s.text[s.pos++] : 0;
}

std::u16string EscapeConverter::readUntilSemicolon(ParseState& s) {
    std::u16string result;
    while (s.pos < s.len) {
        char16_t ch = s.text[s.pos];
        if (ch == u';') {
            s.pos++;
            break;
        }
        result += ch;
        s.pos++;
    }
    return result;
}

float EscapeConverter::readNumberUntilSemicolon(ParseState& s) {
    std::u16string numStr = readUntilSemicolon(s);
    std::string narrow = u16ToAscii(numStr);
    if (narrow.empty()) return 0.0f;
    return static_cast<float>(std::atof(narrow.c_str()));
}

// ============================================================================
// 文字追加（タグ安全エスケープのみ）
// ============================================================================

void EscapeConverter::addChar(ParseState& s, char16_t ch) {
    if (ch == u'<') {
        s.taggedText += u"&lt;";
    } else if (ch == u'>') {
        s.taggedText += u"&gt;";
    } else if (ch == u'&') {
        s.taggedText += u"&amp;";
    } else {
        s.taggedText += ch;
    }
}

void EscapeConverter::addChars(ParseState& s, const std::u16string& str) {
    for (char16_t ch : str) {
        addChar(s, ch);
    }
}

void EscapeConverter::emitTag(ParseState& s, const std::u16string& tag) {
    s.taggedText += tag;
}

// ============================================================================
// スタイルタグ管理
// ============================================================================

void EscapeConverter::closeAllStyleTags(ParseState& s) {
    for (int i = 0; i < s.openItalicTags; i++)
        emitTag(s, u"</i>");
    s.openItalicTags = 0;
    s.inItalic = false;

    for (int i = 0; i < s.openBoldTags; i++)
        emitTag(s, u"</b>");
    s.openBoldTags = 0;
    s.inBold = false;

    for (int i = 0; i < s.openShadowTags; i++)
        emitTag(s, u"</shadow>");
    s.openShadowTags = 0;
    s.inShadow = false;

    for (int i = 0; i < s.openOutlineTags; i++)
        emitTag(s, u"</outline>");
    s.openOutlineTags = 0;
    s.inEdge = false;

    for (int i = 0; i < s.openColorTags; i++)
        emitTag(s, u"</color>");
    s.openColorTags = 0;

    for (int i = 0; i < s.openFontTags; i++)
        emitTag(s, u"</font>");
    s.openFontTags = 0;
}

// ============================================================================
// バックスラッシュエスケープ
// ============================================================================

void EscapeConverter::parseBackslash(ParseState& s) {
    if (s.pos >= s.len) return;
    char16_t ch = advance(s);

    switch (ch) {
    case u'n':
        emitTag(s, u"<br/>");
        break;
    case u't':
        addChar(s, u'\t');
        break;
    case u'i':
    case u'r':
        // インデント（richtext では非対応、無視）
        break;
    case u'w':
        addChar(s, u' ');
        break;
    case u'k':
        // キー入力待ち → タグ出力
        emitTag(s, u"<keywait/>");
        break;
    case u'x':
        // 不可視文字（無視）
        break;
    default:
        addChar(s, ch);
        break;
    }
}

// ============================================================================
// パーセント記法
// ============================================================================

void EscapeConverter::parsePercent(ParseState& s,
                                    float defaultFontSize,
                                    float bigFontSize,
                                    float smallFontSize) {
    if (s.pos >= s.len) return;
    char16_t ch = s.text[s.pos];

    // %f名前; — フォントフェイス
    if (ch == u'f') {
        s.pos++;
        std::u16string face = readUntilSemicolon(s);
        if (!options_.ignoreFace && !face.empty()) {
            std::u16string tag = u"<font face='";
            tag += face;
            tag += u"'>";
            emitTag(s, tag);
            s.openFontTags++;
        }
        return;
    }

    // %bX — ボールド
    if (ch == u'b' && s.pos + 1 < s.len && s.text[s.pos + 1] != u'#') {
        s.pos++;
        char16_t val = (s.pos < s.len) ? advance(s) : u'd';
        if (!options_.ignoreType) {
            if (val == u'1' && !s.inBold) {
                emitTag(s, u"<b>");
                s.openBoldTags++;
                s.inBold = true;
            } else if (val == u'0' && s.inBold) {
                emitTag(s, u"</b>");
                if (s.openBoldTags > 0) s.openBoldTags--;
                s.inBold = false;
            } else if (val != u'0' && val != u'1') {
                if (s.inBold) {
                    emitTag(s, u"</b>");
                    if (s.openBoldTags > 0) s.openBoldTags--;
                    s.inBold = false;
                }
            }
        }
        return;
    }

    // %iX — イタリック
    if (ch == u'i' && s.pos + 1 < s.len) {
        s.pos++;
        char16_t val = (s.pos < s.len) ? advance(s) : u'd';
        if (!options_.ignoreType) {
            if (val == u'1' && !s.inItalic) {
                emitTag(s, u"<i>");
                s.openItalicTags++;
                s.inItalic = true;
            } else if (val == u'0' && s.inItalic) {
                emitTag(s, u"</i>");
                if (s.openItalicTags > 0) s.openItalicTags--;
                s.inItalic = false;
            } else if (val != u'0' && val != u'1') {
                if (s.inItalic) {
                    emitTag(s, u"</i>");
                    if (s.openItalicTags > 0) s.openItalicTags--;
                    s.inItalic = false;
                }
            }
        }
        return;
    }

    // %sX — 影指定
    if (ch == u's') {
        s.pos++;
        if (s.pos < s.len && s.text[s.pos] == u'#') {
            s.pos++;
            std::u16string colorStr = readUntilSemicolon(s);
            if (!options_.ignoreType) {
                std::u16string tag = u"<shadow color='#";
                tag += colorStr;
                tag += u"'>";
                if (s.inShadow) {
                    emitTag(s, u"</shadow>");
                    if (s.openShadowTags > 0) s.openShadowTags--;
                }
                emitTag(s, tag);
                s.openShadowTags++;
                s.inShadow = true;
            }
        } else {
            char16_t val = (s.pos < s.len) ? advance(s) : u'd';
            if (!options_.ignoreType) {
                if (val == u'1' && !s.inShadow) {
                    emitTag(s, u"<shadow>");
                    s.openShadowTags++;
                    s.inShadow = true;
                } else if (val == u'0' && s.inShadow) {
                    emitTag(s, u"</shadow>");
                    if (s.openShadowTags > 0) s.openShadowTags--;
                    s.inShadow = false;
                } else if (val != u'0' && val != u'1') {
                    if (s.inShadow) {
                        emitTag(s, u"</shadow>");
                        if (s.openShadowTags > 0) s.openShadowTags--;
                        s.inShadow = false;
                    }
                }
            }
        }
        return;
    }

    // %eX — エッジ指定
    if (ch == u'e') {
        s.pos++;
        if (s.pos < s.len && s.text[s.pos] == u'#') {
            s.pos++;
            std::u16string colorStr = readUntilSemicolon(s);
            if (!options_.ignoreType) {
                std::u16string tag = u"<outline color='#";
                tag += colorStr;
                tag += u"'>";
                if (s.inEdge) {
                    emitTag(s, u"</outline>");
                    if (s.openOutlineTags > 0) s.openOutlineTags--;
                }
                emitTag(s, tag);
                s.openOutlineTags++;
                s.inEdge = true;
            }
        } else {
            char16_t val = (s.pos < s.len) ? advance(s) : u'd';
            if (!options_.ignoreType) {
                if (val == u'1' && !s.inEdge) {
                    emitTag(s, u"<outline>");
                    s.openOutlineTags++;
                    s.inEdge = true;
                } else if (val == u'0' && s.inEdge) {
                    emitTag(s, u"</outline>");
                    if (s.openOutlineTags > 0) s.openOutlineTags--;
                    s.inEdge = false;
                } else if (val != u'0' && val != u'1') {
                    if (s.inEdge) {
                        emitTag(s, u"</outline>");
                        if (s.openOutlineTags > 0) s.openOutlineTags--;
                        s.inEdge = false;
                    }
                }
            }
        }
        return;
    }

    // %数値; — フォントサイズ（パーセント指定）
    if (ch >= u'0' && ch <= u'9') {
        float percent = readNumberUntilSemicolon(s);
        if (!options_.ignoreSize) {
            float size = defaultFontSize * (percent / 100.0f);
            std::u16string tag = u"<font size='";
            tag += toU16String(size);
            tag += u"'>";
            emitTag(s, tag);
            s.openFontTags++;
        }
        return;
    }

    // %B — 大サイズフォント
    if (ch == u'B') {
        s.pos++;
        if (!options_.ignoreSize) {
            std::u16string tag = u"<font size='";
            tag += toU16String(bigFontSize);
            tag += u"'>";
            emitTag(s, tag);
            s.openFontTags++;
        }
        return;
    }

    // %S — 小サイズフォント
    if (ch == u'S') {
        s.pos++;
        if (!options_.ignoreSize) {
            std::u16string tag = u"<font size='";
            tag += toU16String(smallFontSize);
            tag += u"'>";
            emitTag(s, tag);
            s.openFontTags++;
        }
        return;
    }

    // %r — フォントリセット
    if (ch == u'r') {
        s.pos++;
        closeAllStyleTags(s);
        return;
    }

    // %C — センタリング（記録のみ）
    if (ch == u'C') {
        s.pos++;
        s.align = 0;
        return;
    }

    // %R — 右よせ（記録のみ）
    if (ch == u'R') {
        s.pos++;
        s.align = 1;
        return;
    }

    // %L — 左寄せ（記録のみ）
    if (ch == u'L') {
        s.pos++;
        s.align = -1;
        return;
    }

    // %p数値; — ピッチ → <font spacing> タグ
    if (ch == u'p') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        std::u16string tag = u"<font spacing='";
        tag += toU16String(val);
        tag += u"'>";
        emitTag(s, tag);
        s.openFontTags++;
        return;
    }

    // %d数値; — 文字あたり表示時間（パーセント）→ <delay value='xxx%'> タグ
    if (ch == u'd') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        std::u16string tag = u"<delay value='";
        tag += toU16String(val);
        tag += u"%'/>";
        emitTag(s, tag);
        return;
    }

    // %a数値; — 文字あたり表示時間（ms）→ <delay value='xxx'> タグ
    if (ch == u'a') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        std::u16string tag = u"<delay value='";
        tag += toU16String(val);
        tag += u"'/>";
        emitTag(s, tag);
        return;
    }

    // %w数値; — 時間待ち（パーセント）→ <wait value='xxx%'> タグ
    if (ch == u'w') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        std::u16string tag = u"<wait value='";
        tag += toU16String(val);
        tag += u"%'/>";
        emitTag(s, tag);
        return;
    }

    // %t数値; — 時間待ち（ms）→ <wait value='xxx'> タグ
    if (ch == u't') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        std::u16string tag = u"<wait value='";
        tag += toU16String(val);
        tag += u"'/>";
        emitTag(s, tag);
        return;
    }

    // %D数値; or %D$xxx; — 時間同期 → <sync value='xxx'> タグ
    if (ch == u'D') {
        s.pos++;
        if (s.pos < s.len && s.text[s.pos] == u'$') {
            // %D$label; — ラベル指定
            s.pos++;
            std::u16string label = readUntilSemicolon(s);
            std::u16string tag = u"<sync value='";
            tag += label;
            tag += u"'/>";
            emitTag(s, tag);
        } else {
            // %D数値; — ms 指定
            float val = readNumberUntilSemicolon(s);
            std::u16string tag = u"<sync value='";
            tag += toU16String(val);
            tag += u"'/>";
            emitTag(s, tag);
        }
        return;
    }

    // %lリンク; — リンク開始 / %l; — リンク終了 → <link> タグ
    if (ch == u'l') {
        s.pos++;
        if (s.pos < s.len && s.text[s.pos] == u';') {
            s.pos++;
            emitTag(s, u"</link>");
        } else {
            std::u16string linkName = readUntilSemicolon(s);
            std::u16string tag = u"<link name='";
            tag += linkName;
            tag += u"'>";
            emitTag(s, tag);
        }
        return;
    }

    // %n数値; — スタイル改行
    if (ch == u'n') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        int count = std::max(1, static_cast<int>(val));
        for (int i = 0; i < count; i++) {
            emitTag(s, u"<br/>");
        }
        return;
    }

    // %k0 / %k1 — ワードブレーク（非対応、消費のみ）
    if (ch == u'k') {
        s.pos++;
        if (s.pos < s.len) advance(s);
        return;
    }

    // 不明な % コード
    addChar(s, u'%');
}

// ============================================================================
// #xxxxxx; — 色指定
// ============================================================================

void EscapeConverter::parseHash(ParseState& s) {
    std::u16string colorStr = readUntilSemicolon(s);
    if (!options_.ignoreColor && !colorStr.empty()) {
        std::u16string tag = u"<color value='#";
        tag += colorStr;
        tag += u"'>";
        emitTag(s, tag);
        s.openColorTags++;
    }
}

// ============================================================================
// $xxx; — 変数埋め込み
// ============================================================================

void EscapeConverter::parseDollar(ParseState& s) {
    std::u16string varName = readUntilSemicolon(s);
    if (!varName.empty()) {
        std::u16string tag = u"<eval name='";
        tag += varName;
        tag += u"'/>";
        emitTag(s, tag);
    }
}

// ============================================================================
// &xxx; — グラフィック文字 → <graph> タグ
// ============================================================================

void EscapeConverter::parseAmpersand(ParseState& s) {
    std::u16string graphName = readUntilSemicolon(s);
    if (graphName.empty()) return;

    float w = 0, h = 0;
    if (graphSizeCallback_) {
        graphSizeCallback_(graphName, w, h);
    }

    // <graph> タグを出力（TagParser が U+FFFC 挿入と GraphInfo 記録を行う）
    std::u16string tag = u"<graph name='";
    tag += graphName;
    tag += u"' width='";
    tag += toU16String(w);
    tag += u"' height='";
    tag += toU16String(h);
    tag += u"'/>";
    emitTag(s, tag);
}

// ============================================================================
// [xxxx] / [xxxx,count] — ルビ
// ============================================================================

void EscapeConverter::parseRuby(ParseState& s) {
    if (options_.ignoreRuby) {
        while (s.pos < s.len && s.text[s.pos] != u']') {
            s.pos++;
        }
        if (s.pos < s.len) s.pos++;
        return;
    }

    std::u16string rubyText;
    int rubyCount = 1;

    while (s.pos < s.len) {
        char16_t ch = s.text[s.pos];
        if (ch == u']') {
            s.pos++;
            break;
        }
        if (ch == u',') {
            s.pos++;
            std::u16string countStr;
            while (s.pos < s.len && s.text[s.pos] != u']') {
                countStr += s.text[s.pos];
                s.pos++;
            }
            if (s.pos < s.len) s.pos++;
            std::string narrow = u16ToAscii(countStr);
            rubyCount = std::max(1, std::atoi(narrow.c_str()));
            break;
        }
        rubyText += ch;
        s.pos++;
    }

    if (rubyText.empty()) return;

    std::u16string tag = u"<ruby text='";
    tag += rubyText;
    tag += u"'>";
    emitTag(s, tag);

    int added = 0;
    while (added < rubyCount && s.pos < s.len) {
        char16_t ch = s.text[s.pos];
        if (ch == u'\\' && s.pos + 1 < s.len) {
            s.pos++;
            addChar(s, s.text[s.pos]);
            s.pos++;
        } else {
            addChar(s, ch);
            s.pos++;
        }
        added++;
    }

    emitTag(s, u"</ruby>");
}

// ============================================================================
// メイン変換処理
// ============================================================================

EscapeConverter::EscapeConverter() = default;

EscapeConverter::ConvertResult EscapeConverter::convert(
    const std::u16string& escapeText,
    float defaultFontSize,
    float bigFontSize,
    float smallFontSize,
    float diff,
    float all)
{
    ParseState s;
    s.text = escapeText.c_str();
    s.len = escapeText.size();
    s.pos = 0;
    s.taggedText.reserve(escapeText.size() * 2);

    // 先頭に <start> タグを出力
    {
        std::u16string tag = u"<start diff='";
        tag += toU16String(diff);
        tag += u"' all='";
        tag += toU16String(all);
        tag += u"'/>";
        emitTag(s, tag);
    }

    while (s.pos < s.len) {
        char16_t ch = s.text[s.pos];

        switch (ch) {
        case u'\\':
            s.pos++;
            parseBackslash(s);
            break;

        case u'%':
            s.pos++;
            parsePercent(s, defaultFontSize, bigFontSize, smallFontSize);
            break;

        case u'#':
            s.pos++;
            parseHash(s);
            break;

        case u'$':
            s.pos++;
            parseDollar(s);
            break;

        case u'&':
            s.pos++;
            parseAmpersand(s);
            break;

        case u'[':
            s.pos++;
            parseRuby(s);
            break;

        default:
            s.pos++;
            addChar(s, ch);
            break;
        }
    }

    closeAllStyleTags(s);

    ConvertResult result;
    result.taggedText = std::move(s.taggedText);
    result.align = s.align;
    return result;
}

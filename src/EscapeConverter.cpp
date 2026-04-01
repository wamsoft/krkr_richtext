#include "EscapeConverter.hpp"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>

// UTF-16 文字列リテラルヘルパー
static const char16_t* u16(const char* s, std::u16string& buf) {
    buf.clear();
    while (*s) buf += static_cast<char16_t>(*s++);
    return buf.c_str();
}

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
    // 整数の場合は小数点なし
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
            s.pos++; // セミコロンを消費
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
// 文字追加（タイミングエントリ付き）
// ============================================================================

void EscapeConverter::addChar(ParseState& s, char16_t ch) {
    // タグのエスケープ（richtext タグパーサーが誤解しないよう）
    if (ch == u'<') {
        s.taggedText += u"&lt;";
    } else if (ch == u'>') {
        s.taggedText += u"&gt;";
    } else if (ch == u'&') {
        s.taggedText += u"&amp;";
    } else {
        s.taggedText += ch;
    }

    // タイミングエントリを記録
    richtext::TimingEntry entry;
    entry.type = richtext::TimingEntry::Type::Char;
    entry.charIndex = static_cast<int>(s.plainCharCount);
    entry.delayPercent = s.currentDelayPercent;
    entry.delayMs = s.currentDelayMs;
    s.timings.push_back(entry);

    // リンク中なら文字インデックスを記録
    if (s.currentLinkIndex >= 0 && s.currentLinkIndex < static_cast<int>(s.links.size())) {
        s.links[s.currentLinkIndex].endIndex = s.plainCharCount + 1;
    }

    s.plainCharCount++;
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
    // 開いているタグを逆順に閉じる
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
// バックスラッシュエスケープ (\n, \t, \i, \r, \w, \k, \x, \文字)
// ============================================================================

void EscapeConverter::parseBackslash(ParseState& s) {
    if (s.pos >= s.len) return;
    char16_t ch = advance(s);

    switch (ch) {
    case u'n':
        // 改行
        emitTag(s, u"<br/>");
        break;
    case u't':
        // タブ → スペースとして処理
        addChar(s, u'\t');
        break;
    case u'i':
        // インデント開始（richtext では直接対応なし、無視）
        break;
    case u'r':
        // インデント解除（richtext では直接対応なし、無視）
        break;
    case u'w': {
        // 空白文字分進める
        addChar(s, u' ');
        break;
    }
    case u'k': {
        // キー入力待ち
        richtext::TimingEntry entry;
        entry.type = richtext::TimingEntry::Type::KeyWait;
        entry.charIndex = static_cast<int>(s.plainCharCount);
        s.timings.push_back(entry);
        break;
    }
    case u'x':
        // nul文字相当（表示なし、位置のみ進める）
        break;
    default:
        // エスケープ指定: 特殊機能を無効化してそのまま出力
        addChar(s, ch);
        break;
    }
}

// ============================================================================
// パーセント記法 (%codes)
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
        // ただし %b で次が数字 or デフォルトなら種別指定
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
                // デフォルトに戻す
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

    // %sX — 影指定 (ただし %s# は影色指定)
    if (ch == u's') {
        s.pos++;
        if (s.pos < s.len && s.text[s.pos] == u'#') {
            // %s#xxxxxx; — 影色指定
            s.pos++; // '#' を消費
            std::u16string colorStr = readUntilSemicolon(s);
            if (!options_.ignoreType) {
                std::u16string tag = u"<shadow color='#";
                tag += colorStr;
                tag += u"'>";
                // 既存の shadow を閉じてから新しいものを開く
                if (s.inShadow) {
                    emitTag(s, u"</shadow>");
                    if (s.openShadowTags > 0) s.openShadowTags--;
                }
                emitTag(s, tag);
                s.openShadowTags++;
                s.inShadow = true;
            }
        } else {
            // %sX — 影の on/off
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

    // %eX — エッジ指定 (ただし %e# はエッジ色指定)
    if (ch == u'e') {
        s.pos++;
        if (s.pos < s.len && s.text[s.pos] == u'#') {
            // %e#xxxxxx; — エッジ色指定
            s.pos++; // '#' を消費
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
            // %eX — エッジの on/off
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

    // %C — センタリング
    if (ch == u'C') {
        s.pos++;
        if (!options_.ignoreStyle) {
            s.align = 0;
        }
        return;
    }

    // %R — 右よせ
    if (ch == u'R') {
        s.pos++;
        if (!options_.ignoreStyle) {
            s.align = 1;
        }
        return;
    }

    // %L — 左寄せ
    if (ch == u'L') {
        s.pos++;
        if (!options_.ignoreStyle) {
            s.align = -1;
        }
        return;
    }

    // %p数値; — ピッチ
    if (ch == u'p') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        if (!options_.ignoreStyle) {
            s.pitch = val;
        }
        return;
    }

    // %d数値; — 文字あたり表示時間（パーセント）
    if (ch == u'd') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        if (!options_.ignoreDelay) {
            s.currentDelayPercent = val;
            s.currentDelayMs = -1.0f;
        }
        return;
    }

    // %a数値; — 文字あたり表示時間（ms）
    if (ch == u'a') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        if (!options_.ignoreDelay) {
            s.currentDelayMs = val;
        }
        return;
    }

    // %w数値; — 時間待ち（パーセント）
    if (ch == u'w') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        if (!options_.ignoreDelay) {
            richtext::TimingEntry entry;
            entry.type = richtext::TimingEntry::Type::Wait;
            entry.charIndex = static_cast<int>(s.plainCharCount);
            entry.waitPercent = val;
            s.timings.push_back(entry);
        }
        return;
    }

    // %t数値; — 時間待ち（ms）
    if (ch == u't') {
        s.pos++;
        float val = readNumberUntilSemicolon(s);
        if (!options_.ignoreDelay) {
            richtext::TimingEntry entry;
            entry.type = richtext::TimingEntry::Type::Wait;
            entry.charIndex = static_cast<int>(s.plainCharCount);
            entry.waitMs = val;
            s.timings.push_back(entry);
        }
        return;
    }

    // %D数値; or %D$xxx; — 時間同期
    if (ch == u'D') {
        s.pos++;
        if (!options_.ignoreDelay) {
            if (s.pos < s.len && s.text[s.pos] == u'$') {
                // %D$label;
                s.pos++; // '$' を消費
                std::u16string label = readUntilSemicolon(s);
                richtext::TimingEntry entry;
                entry.type = richtext::TimingEntry::Type::Sync;
                entry.charIndex = static_cast<int>(s.plainCharCount);
                entry.syncLabel = u16ToAscii(label);
                s.timings.push_back(entry);
            } else {
                // %D数値;
                float val = readNumberUntilSemicolon(s);
                richtext::TimingEntry entry;
                entry.type = richtext::TimingEntry::Type::Sync;
                entry.charIndex = static_cast<int>(s.plainCharCount);
                entry.syncMs = val;
                s.timings.push_back(entry);
            }
        } else {
            // ignoreDelay でも構文は消費する
            if (s.pos < s.len && s.text[s.pos] == u'$') {
                s.pos++;
                readUntilSemicolon(s);
            } else {
                readNumberUntilSemicolon(s);
            }
        }
        return;
    }

    // %lリンク; — リンク開始 / %l; — リンク終了
    if (ch == u'l') {
        s.pos++;
        if (s.pos < s.len && s.text[s.pos] == u';') {
            // %l; — リンク終了
            s.pos++; // ';' を消費
            s.currentLinkIndex = -1;
        } else {
            // %lリンク名;
            std::u16string linkName = readUntilSemicolon(s);
            LinkInfo link;
            link.name = u16ToAscii(linkName);
            link.startIndex = s.plainCharCount;
            link.endIndex = s.plainCharCount;
            s.links.push_back(link);
            s.currentLinkIndex = static_cast<int>(s.links.size()) - 1;
        }
        return;
    }

    // %n数値; — スタイル改行（指定個数分）
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

    // 不明な % コード: そのまま '%' と次の文字を出力
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
    if (evalCallback_ && !varName.empty()) {
        std::u16string value = evalCallback_(varName);
        // 変数値をそのまま埋め込む（エスケープ処理なし、値は安全と想定）
        addChars(s, value);
    }
}

// ============================================================================
// &xxx; — グラフィック文字
// ============================================================================

void EscapeConverter::parseAmpersand(ParseState& s) {
    std::u16string graphName = readUntilSemicolon(s);
    if (graphName.empty()) return;

    float w = 0, h = 0;
    if (graphSizeCallback_) {
        graphSizeCallback_(graphName, w, h);
    }

    // U+FFFC (OBJECT REPLACEMENT CHARACTER) をプレースホルダとして挿入
    GraphInfo gi;
    gi.name = graphName;
    gi.charIndex = static_cast<int>(s.plainCharCount);
    gi.width = w;
    gi.height = h;
    s.graphics.push_back(gi);

    addChar(s, u'\uFFFC');
}

// ============================================================================
// [xxxx] / [xxxx,count] — ルビ
// ============================================================================

void EscapeConverter::parseRuby(ParseState& s) {
    if (options_.ignoreRuby) {
        // ルビを無視: ] まで読み飛ばす
        while (s.pos < s.len && s.text[s.pos] != u']') {
            s.pos++;
        }
        if (s.pos < s.len) s.pos++; // ']' を消費
        return;
    }

    // ルビテキストとカウントを読む
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
            // カンマ以降は文字数
            std::u16string countStr;
            while (s.pos < s.len && s.text[s.pos] != u']') {
                countStr += s.text[s.pos];
                s.pos++;
            }
            if (s.pos < s.len) s.pos++; // ']'
            std::string narrow = u16ToAscii(countStr);
            rubyCount = std::max(1, std::atoi(narrow.c_str()));
            break;
        }
        rubyText += ch;
        s.pos++;
    }

    if (rubyText.empty()) return;

    // 次の rubyCount 文字を <ruby> タグで囲む
    std::u16string tag = u"<ruby text='";
    tag += rubyText;
    tag += u"'>";
    emitTag(s, tag);

    int added = 0;
    while (added < rubyCount && s.pos < s.len) {
        char16_t ch = s.text[s.pos];
        // 次の文字がエスケープシーケンスの場合はそのまま通常文字として扱う
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
    float smallFontSize)
{
    ParseState s;
    s.text = escapeText.c_str();
    s.len = escapeText.size();
    s.pos = 0;
    s.plainCharCount = 0;
    s.taggedText.reserve(escapeText.size() * 2);

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

    // 開いているタグを閉じる
    closeAllStyleTags(s);

    // 結果を組み立て
    ConvertResult result;
    result.taggedText = std::move(s.taggedText);
    result.timings = std::move(s.timings);
    result.keyWaits = std::move(s.keyWaits);
    result.links = std::move(s.links);
    result.graphics = std::move(s.graphics);
    result.align = s.align;
    result.pitch = s.pitch;
    return result;
}

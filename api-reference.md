# RichText ライブラリ API リファレンス

## 1. TJS2 API

### 1.1 グローバル関数

#### RichText.loadFont(path, name)

フォントファイルをロードして名前を付けて登録します。

| パラメータ | 型 | 説明 |
|-----------|------|------|
| path | String | フォントファイルのパス |
| name | String | 登録名（他のAPIで参照する際に使用） |

| 戻り値 | 型 | 説明 |
|--------|------|------|
| success | Boolean | 成功時 true |

```tjs
// 使用例
RichText.loadFont("./fonts/NotoSansCJKjp-Regular.otf", "NotoSansCJK");
RichText.loadFont("./fonts/NotoColorEmoji.ttf", "NotoEmoji");
```

#### RichText.unloadFont(name)

登録済みフォントを解除します。

| パラメータ | 型 | 説明 |
|-----------|------|------|
| name | String | 登録名 |

#### RichText.registerLocale(locale)

ロケールを登録し、IDを取得します。行分割ルールの制御に使用。

| パラメータ | 型 | 説明 |
|-----------|------|------|
| locale | String | ロケール文字列 |

| 戻り値 | 型 | 説明 |
|--------|------|------|
| id | Integer | ロケールID |

```tjs
// 使用例
// 日本語・厳格な行分割
var localeJaStrict = RichText.registerLocale("ja_JP-u-lb-strict");
// 日本語・緩い行分割（句読点前での改行を許容）
var localeJaLoose = RichText.registerLocale("ja_JP-u-lb-loose");
// 英語
var localeEn = RichText.registerLocale("en_US");
```

---

### 1.2 FontCollection クラス

複数フォントをまとめたコレクション。フォールバック処理を実現。

#### コンストラクタ

```tjs
var collection = new FontCollection(fontNames);
```

| パラメータ | 型 | 説明 |
|-----------|------|------|
| fontNames | Array | フォント登録名の配列（優先度順） |

```tjs
// 使用例
// 絵文字 → 日本語 → ラテン文字 の順でフォールバック
var collection = new FontCollection([
    "NotoEmoji",
    "NotoSansCJK",
    "NotoSans"
]);
```

---

### 1.3 TextStyle クラス

テキストの論理的なスタイル定義。

#### コンストラクタ

```tjs
var style = new TextStyle();
```

#### プロパティ

| プロパティ | 型 | デフォルト | 説明 |
|-----------|------|---------|------|
| fontCollection | FontCollection | null | 使用するフォントコレクション |
| fontSize | Real | 16.0 | フォントサイズ（ピクセル） |
| fontWeight | Integer | 400 | フォントウェイト（100-900） |
| italic | Boolean | false | イタリック |
| letterSpacing | Real | 0.0 | 字間（em単位、0.1 = 10%） |
| wordSpacing | Real | 0.0 | 語間（em単位） |
| localeId | Integer | 0 | ロケールID |

```tjs
// 使用例
var style = new TextStyle();
style.fontCollection = collection;
style.fontSize = 24;
style.fontWeight = 700;  // Bold
style.letterSpacing = 0.05;  // 5%の字間
style.localeId = localeJaStrict;
```

---

### 1.4 Appearance クラス

描画外観の定義。複数の塗り・ストロークを重ねて装飾効果を実現。

#### コンストラクタ

```tjs
var appearance = new Appearance();
```

#### メソッド

##### addFill(colorOrOption, offsetX, offsetY)

塗りを追加します。

| パラメータ | 型 | デフォルト | 説明 |
|-----------|------|---------|------|
| colorOrOption | Integer/Dictionary | - | ARGB色値または詳細オプション |
| offsetX | Real | 0 | X方向オフセット |
| offsetY | Real | 0 | Y方向オフセット |

```tjs
// シンプルな塗り
appearance.addFill(0xFFFF0000);  // 赤

// オフセット付き（ドロップシャドウ風）
appearance.addFill(0x80000000, 2, 2);  // 半透明黒、右下にオフセット
appearance.addFill(0xFFFFFFFF);  // 白

// グラデーション塗り
appearance.addFill(%[
    type: "linearGradient",
    point1: [0, 0],
    point2: [100, 0],
    color1: 0xFFFF0000,
    color2: 0xFF0000FF
]);
```

##### addStroke(colorOrOption, widthOrOption, offsetX, offsetY)

ストローク（縁取り）を追加します。

| パラメータ | 型 | デフォルト | 説明 |
|-----------|------|---------|------|
| colorOrOption | Integer/Dictionary | - | ARGB色値 |
| widthOrOption | Real/Dictionary | - | 線幅またはオプション |
| offsetX | Real | 0 | X方向オフセット |
| offsetY | Real | 0 | Y方向オフセット |

```tjs
// シンプルなストローク
appearance.addStroke(0xFF000000, 2);  // 黒、幅2

// 詳細オプション付き
appearance.addStroke(0xFF000000, %[
    width: 3,
    cap: "round",      // "flat", "square", "round"
    join: "round",     // "miter", "bevel", "round"
    miterLimit: 4
]);

// 破線
appearance.addStroke(0xFF000000, %[
    width: 2,
    dashStyle: [5, 3],  // 5ピクセル線、3ピクセル空白
    dashOffset: 0
]);
```

##### clear()

すべての描画スタイルをクリアします。

```tjs
// 装飾の典型例: 縁取り文字
var outlineText = new Appearance();
outlineText.addStroke(0xFF000000, 4);  // 太い黒縁
outlineText.addStroke(0xFFFFFFFF, 2);  // 細い白縁
outlineText.addFill(0xFFFF6600);       // オレンジ塗り
```

---

### 1.5 Layer 拡張メソッド

Layer クラスに追加される描画メソッド。

#### drawText(text, x, y, style, appearance)

1行テキストを描画します。

| パラメータ | 型 | 説明 |
|-----------|------|------|
| text | String | 描画するテキスト |
| x | Real | 描画開始X座標 |
| y | Real | 描画開始Y座標（ベースライン） |
| style | TextStyle | テキストスタイル |
| appearance | Appearance | 描画外観 |

| 戻り値 | 型 | 説明 |
|--------|------|------|
| bounds | Dictionary | 描画した領域 {x, y, width, height} |

```tjs
var bounds = layer.drawText("Hello World!", 10, 50, style, appearance);
```

#### drawParagraph(text, rect, hAlign, vAlign, style, appearance)

パラグラフ（複数行テキスト）を描画します。

| パラメータ | 型 | 説明 |
|-----------|------|------|
| text | String | 描画するテキスト |
| rect | Dictionary/Array | 描画領域 {x, y, width, height} または [x, y, w, h] |
| hAlign | String | 水平アライン: "left", "center", "right", "justify" |
| vAlign | String | 垂直アライン: "top", "middle", "bottom" |
| style | TextStyle | テキストスタイル |
| appearance | Appearance | 描画外観 |

```tjs
layer.drawParagraph(
    "長いテキストは自動的に折り返されます。",
    %[x: 10, y: 10, width: 300, height: 200],
    "center",
    "middle",
    style,
    appearance
);
```

#### drawStyledText(text, rect, styles, appearances)

スタイルタグ付きテキストを描画します。

| パラメータ | 型 | 説明 |
|-----------|------|------|
| text | String | スタイルタグ付きテキスト |
| rect | Dictionary | 描画領域 |
| styles | Dictionary | スタイル名 → TextStyle のマップ |
| appearances | Dictionary | スタイル名 → Appearance のマップ |

```tjs
var styles = %[
    "default": normalStyle,
    "bold": boldStyle,
    "red": redStyle
];
var appearances = %[
    "default": normalAppearance,
    "bold": boldAppearance,
    "red": redAppearance
];

layer.drawStyledText(
    "これは<style:bold>太字</style>で、<style:red>赤い文字</style>も使えます。",
    %[x: 10, y: 10, width: 400, height: 300],
    styles,
    appearances
);
```

#### drawTaggedText(text, rect, style, appearance, namedStyles, namedAppearances)

インラインタグ付きテキストを描画します。HTMLライクなタグでスタイルを変更できます。

| パラメータ | 型 | 説明 |
|-----------|------|------|
| text | String | インラインタグ付きテキスト |
| rect | Dictionary | 描画領域 |
| style | TextStyle | デフォルトスタイル |
| appearance | Appearance | デフォルト外観 |
| namedStyles | Dictionary | （省略可）名前付きスタイル |
| namedAppearances | Dictionary | （省略可）名前付き外観 |

```tjs
layer.drawTaggedText(
    "これは<font size=32>大きな</font>文字と" +
    "<color value=0xFFFF0000>赤い</color>文字と" +
    "<b>太字</b>です。",
    %[x: 10, y: 10, width: 400, height: 300],
    defaultStyle,
    defaultAppearance
);
```

---

### 1.6 インラインタグ

#### 対応タグ一覧

| タグ | 説明 | 属性 |
|-----|------|------|
| `<font>` | フォント変更 | `face`, `size`, `weight` |
| `<color>` | 色変更 | `value` (ARGB) または `r`,`g`,`b`,`a` |
| `<b>` | 太字 | - |
| `<i>` | 斜体 | - |
| `<u>` | 下線 | - |
| `<s>` | 取り消し線 | - |
| `<ruby>` | ルビ（振り仮名） | `text` |
| `<sup>` | 上付き文字 | - |
| `<sub>` | 下付き文字 | - |
| `<style>` | 登録済みスタイル参照 | `name` |
| `<br>` | 改行 | - |
| `<sp>` | スペース | `width` (em単位) |

#### `<font>` タグ

フォント属性を変更します。

```tjs
// フォント名・サイズ・ウェイト指定
"<font face='NotoSansCJK' size=24 weight=700>太字24px</font>"

// サイズのみ変更
"<font size=32>大きい文字</font>"
```

| 属性 | 型 | 説明 |
|------|------|------|
| face | String | フォントコレクション名 |
| size | Number | フォントサイズ（ピクセル） |
| weight | Number | フォントウェイト（100-900） |

#### `<color>` タグ

テキスト色を変更します。

```tjs
// ARGB値（16進数）
"<color value=0xFFFF0000>赤</color>"

// #RRGGBB形式
"<color value='#FF0000'>赤</color>"

// RGB個別指定
"<color r=255 g=0 b=0>赤</color>"

// アルファ付き
"<color r=255 g=0 b=0 a=128>半透明の赤</color>"
```

#### `<ruby>` タグ

振り仮名（ルビ）を表示します。

```tjs
"<ruby text='とうきょう'>東京</ruby>に行きます"
"<ruby text='ルビ'>親文字</ruby>"
```

ルビのフォントサイズはデフォルトで親文字の50%です。

#### `<b>`, `<i>`, `<u>`, `<s>` タグ

基本的な書式設定です。

```tjs
"<b>太字</b>"           // Bold
"<i>斜体</i>"           // Italic
"<u>下線</u>"           // Underline
"<s>取り消し線</s>"     // Strikethrough
"<b><i>太字斜体</i></b>" // ネスト可能
```

#### `<sup>`, `<sub>` タグ

上付き・下付き文字です。

```tjs
"H<sub>2</sub>O"      // H₂O
"E=mc<sup>2</sup>"    // E=mc²
```

#### `<outline>` タグ

縁取りを追加します。

```tjs
"<outline color=0xFF000000 width=2>縁取り</outline>"
```

| 属性 | 型 | 説明 |
|------|------|------|
| color | Number | 縁取りの色（ARGB） |
| width | Number | 縁取りの幅（ピクセル） |

#### `<shadow>` タグ

影を追加します。

```tjs
"<shadow color=0x80000000 x=2 y=2>影付き</shadow>"
```

| 属性 | 型 | 説明 |
|------|------|------|
| color | Number | 影の色（ARGB） |
| x | Number | X方向オフセット |
| y | Number | Y方向オフセット |

#### `<style>` タグ

事前登録したスタイルを参照します。

```tjs
var styles = %[
    "emphasis": emphasisStyle,
    "quote": quoteStyle
];
var appearances = %[
    "emphasis": emphasisAppearance,
    "quote": quoteAppearance
];

layer.drawTaggedText(
    "これは<style name='emphasis'>強調</style>です",
    rect, defaultStyle, defaultAppearance,
    styles, appearances
);
```

#### `<br>`, `<sp>` タグ

改行とスペースです。

```tjs
"1行目<br>2行目"           // 改行
"広い<sp width=2>スペース" // 2emのスペース
```

#### エスケープ

特殊文字はエスケープが必要です。

| 記法 | 文字 |
|------|------|
| `&lt;` | `<` |
| `&gt;` | `>` |
| `&amp;` | `&` |
| `&quot;` | `"` |

---

### 1.7 タグ付きテキストからスタイル定義を生成

タグ付きテキストをパースして、独立したスタイル定義（StyleRun配列）を生成できます。
これにより、パース結果をキャッシュしたり、複数回描画したりできます。

#### 基本的な流れ

```tjs
// 1. タグ付きテキストをパース
var result = RichText.parseTaggedText(
    "<font size=24>大きな</font>文字と<b>太字</b>",
    defaultStyle,
    defaultAppearance
);

// 2. パース結果を確認
// result.plainText = "大きな文字と太字"
// result.spans = スタイル区間の配列
// result.styleRuns = minikin用StyleRun配列

// 3. パース結果を使って描画
layer.drawParsedText(result, rect, "left", "top");

// 4. または、スタイルを取り出して個別に使用
for (var i = 0; i < result.spans.count; i++) {
    var span = result.spans[i];
    var text = result.plainText.substr(span.start, span.end - span.start);
    // span.style と span.appearance を使って何かする
}
```

#### スタイル継承

ネストされたタグでは、内側のタグが外側のスタイルを継承します。

```tjs
// 例: <color value=0xFFFF0000><b>赤い太字</b></color>
// 
// 継承関係:
// デフォルト: fontSize=16, color=白, weight=400
//   └─ <color>: fontSize=16（継承）, color=赤, weight=400（継承）
//        └─ <b>: fontSize=16（継承）, color=赤（継承）, weight=700
```

#### 複合装飾の例

```tjs
// ゲームのメッセージウィンドウ風
var text = 
    "<outline color=0xFF000000 width=2>" +
    "キャラクター名：<color value=0xFFFFD700>主人公</color><br>" +
    "</outline>" +
    "「これは<b>重要な</b>セリフです。<br>" +
    "　<shadow color=0x80000000 x=1 y=1>" +
    "<color value=0xFF88CCFF>魔法の呪文</color>" +
    "</shadow>を唱えましょう。」";

layer.drawTaggedText(text, rect, defaultStyle, defaultAppearance);
```

#### measureText(text, style)

テキストのサイズを計測します（描画はしない）。

| パラメータ | 型 | 説明 |

テキストのサイズを計測します（描画はしない）。

| パラメータ | 型 | 説明 |
|-----------|------|------|
| text | String | 計測するテキスト |
| style | TextStyle | テキストスタイル |

| 戻り値 | 型 | 説明 |
|--------|------|------|
| size | Dictionary | {width, height, ascent, descent} |

```tjs
var size = layer.measureText("Hello", style);
// size.width, size.height, size.ascent, size.descent
```

#### measureParagraph(text, maxWidth, style)

パラグラフのサイズを計測します。

| パラメータ | 型 | 説明 |
|-----------|------|------|
| text | String | 計測するテキスト |
| maxWidth | Real | 最大幅 |
| style | TextStyle | テキストスタイル |

| 戻り値 | 型 | 説明 |
|--------|------|------|
| info | Dictionary | {width, height, lineCount, lines[]} |

```tjs
var info = layer.measureParagraph("長いテキスト...", 300, style);
// info.width, info.height, info.lineCount
// info.lines[0].width, info.lines[0].ascent, ...
```

#### getGlyphInfos(text, style)

グリフ単位の情報を取得します（順次表示用）。

| 戻り値 | 型 | 説明 |
|--------|------|------|
| glyphs | Array | グリフ情報の配列 |

各グリフ情報:
```tjs
%[
    charIndex: 0,      // 元テキストでの文字位置
    x: 0.0,            // X座標
    y: 0.0,            // Y座標
    width: 12.0,       // 幅
    height: 24.0,      // 高さ
    advance: 14.0,     // 次の文字までの距離
    isEmoji: false     // 絵文字かどうか
]
```

```tjs
var glyphs = layer.getGlyphInfos("Hello", style);
for (var i = 0; i < glyphs.count; i++) {
    var g = glyphs[i];
    // 1文字ずつ描画（アニメーション用）
    layer.drawGlyph(g, appearance);
}
```

---

### 1.7 タグパーサー API

#### RichText.parseTaggedText(text, defaultStyle, defaultAppearance, namedStyles, namedAppearances)

タグ付きテキストを解析し、スタイル区間に分解します。

| パラメータ | 型 | 説明 |
|-----------|------|------|
| text | String | タグ付きテキスト |
| defaultStyle | TextStyle | デフォルトスタイル |
| defaultAppearance | Appearance | デフォルト外観 |
| namedStyles | Dictionary | （省略可）名前付きスタイル |
| namedAppearances | Dictionary | （省略可）名前付き外観 |

| 戻り値 | 型 | 説明 |
|--------|------|------|
| result | Dictionary | パース結果 |

```tjs
var result = RichText.parseTaggedText(
    "<b>太字</b>と<color value=0xFFFF0000>赤い</color>文字",
    defaultStyle,
    defaultAppearance
);

// result.plainText = "太字と赤い文字"
// result.spans = [
//   %[ start: 0, end: 2, style: ..., appearance: ... ],  // "太字"
//   %[ start: 3, end: 5, style: ..., appearance: ... ]   // "赤い"
// ]
// result.styleRuns = minikin用のStyleRun配列

// パース結果を使って描画
layer.drawParsedText(result, rect, "left", "top");
```

#### RichText.stripTags(text)

タグを除去してプレーンテキストを取得します。

```tjs
var plain = RichText.stripTags("<b>太字</b>と<i>斜体</i>");
// plain = "太字と斜体"
```

#### RichText.escapeText(text)

特殊文字をエスケープします。

```tjs
var escaped = RichText.escapeText("A < B & C > D");
// escaped = "A &lt; B &amp; C &gt; D"
```

#### RichText.unescapeText(text)

エスケープを展開します。

```tjs
var unescaped = RichText.unescapeText("A &lt; B &amp; C &gt; D");
// unescaped = "A < B & C > D"
```

---

## 2. 使用例

### 2.1 基本的なテキスト描画

```tjs
// 初期化
RichText.loadFont("./fonts/NotoSansCJKjp-Regular.otf", "NotoSansCJK");

var collection = new FontCollection(["NotoSansCJK"]);
var style = new TextStyle();
style.fontCollection = collection;
style.fontSize = 24;

var appearance = new Appearance();
appearance.addFill(0xFFFFFFFF);

// 描画
layer.drawText("こんにちは世界！", 10, 50, style, appearance);
```

### 2.2 縁取り付きテキスト

```tjs
var appearance = new Appearance();
// 外側から順に描画（後に追加したものが上に描画される）
appearance.addStroke(0xFF000000, 5);   // 黒い太い縁
appearance.addStroke(0xFFFFFF00, 3);   // 黄色い細い縁
appearance.addFill(0xFFFF0000);        // 赤い塗り

layer.drawText("縁取りテキスト", 10, 50, style, appearance);
```

### 2.3 絵文字を含むテキスト

```tjs
RichText.loadFont("./fonts/NotoColorEmoji.ttf", "NotoEmoji");
RichText.loadFont("./fonts/NotoSansCJKjp-Regular.otf", "NotoSansCJK");

// 絵文字フォントを先に登録してフォールバック
var collection = new FontCollection(["NotoEmoji", "NotoSansCJK"]);

var style = new TextStyle();
style.fontCollection = collection;
style.fontSize = 32;

layer.drawText("Hello 👋 World 🌍", 10, 50, style, appearance);
```

### 2.4 パラグラフ描画

```tjs
var text = "吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。";

layer.drawParagraph(
    text,
    %[x: 20, y: 20, width: 200, height: 400],
    "left",    // 左揃え
    "top",     // 上揃え
    style,
    appearance
);
```

### 2.5 文字送りアニメーション

```tjs
var text = "文字が順番に表示されます";
var glyphs = layer.getGlyphInfos(text, style);
var currentIndex = 0;

function onTimer() {
    if (currentIndex < glyphs.count) {
        layer.drawGlyph(glyphs[currentIndex], appearance);
        currentIndex++;
    }
}
```

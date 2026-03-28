# About

テスト用のフォントデータ置き場です。フォントファイルはリポジトリに含まれていないため、初回セットアップ時にダウンロードが必要です。

## フォントのダウンロード

```bash
# プロジェクトルートから実行
make fontdata

# または直接スクリプトを実行
python3 data/download_fonts.py
```

既にダウンロード済みのフォントはスキップされます。

## フォント一覧

以下のフォントは Google Fonts / Noto Fonts プロジェクトから取得しています:

- https://github.com/notofonts/notofonts.github.io
- https://github.com/notofonts/noto-cjk
- https://github.com/googlefonts/noto-emoji

### 固定ウェイトフォント

| ファイル名 | 言語・用途 | ソース |
|-----------|-----------|--------|
| NotoSans-Regular.ttf | 欧州言語 (Latin, Greek, Cyrillic) | notofonts/latin-greek-cyrillic v2.015 |
| NotoSerif-Regular.ttf | 欧州言語 (Latin, Greek, Cyrillic, Serif) | notofonts/latin-greek-cyrillic v2.015 |
| NotoSansJP-Regular.otf | 日本語 | notofonts/noto-cjk Sans2.004 |
| NotoSansKR-Regular.otf | 韓国語 | notofonts/noto-cjk Sans2.004 |
| NotoSansSC-Regular.otf | 中国語（簡体字） | notofonts/noto-cjk Sans2.004 |
| NotoSansTC-Regular.otf | 中国語（繁体字） | notofonts/noto-cjk Sans2.004 |
| NotoSansArabic-Regular.ttf | アラビア語 (UI向け) | notofonts/arabic v2.013 |
| NotoNaskhArabic-Regular.ttf | アラビア語 (本文向け、Naskh書体) | notofonts/arabic v2.021 |
| **NotoSerifJP-Regular.otf** | **日本語 (Serif / 明朝体)** | **notofonts/noto-cjk Serif2.003** |
| **NotoSerifKR-Regular.otf** | **韓国語 (Serif)** | **notofonts/noto-cjk Serif2.003** |
| **NotoSerifSC-Regular.otf** | **中国語（簡体字、Serif）** | **notofonts/noto-cjk Serif2.003** |
| **NotoSerifTC-Regular.otf** | **中国語（繁体字、Serif）** | **notofonts/noto-cjk Serif2.003** |

### バリアブルフォント

| ファイル名 | 言語・用途 | 軸 |
|-----------|-----------|-----|
| NotoSans-VariableFont.ttf | 欧州言語 (Upright) | wght 100-900, wdth 62.5-100 |
| NotoSans-Italic-VariableFont.ttf | 欧州言語 (Italic) | wght 100-900, wdth 62.5-100 |
| NotoSerif-VariableFont.ttf | 欧州言語 (Serif, Upright) | wght 100-900, wdth 62.5-100 |
| NotoSerif-Italic-VariableFont.ttf | 欧州言語 (Serif, Italic) | wght 100-900, wdth 62.5-100 |
| NotoSansJP-VariableFont.ttf | 日本語 | wght 100-900 |
| **NotoSerifJP-VariableFont.ttf** | **日本語 (Serif / 明朝体)** | **wght 200-900** |

### 絵文字フォント

| ファイル名 | 形式 | 説明 |
|-----------|------|------|
| NotoColorEmoji.ttf | CBDT (ビットマップ) | カラー絵文字 |
| Noto-COLRv1.ttf | COLRv1 (ベクター) | カラー絵文字（ベクターベース） |
| NotoEmoji-Regular.ttf | アウトライン (Variable) | モノクロ絵文字 |

## ライセンス

全フォント: SIL Open Font License (OFL)

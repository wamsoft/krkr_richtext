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

## Google Fonts Noto Sans 多言語フォント

以下のフォントは Google Fonts から取得しています:
- https://github.com/notofonts/notofonts.github.io
- https://github.com/googlefonts/noto-cjk

| ファイル名 | 言語・用途 |
|-----------|-----------|
| NotoSans-Regular.ttf | 欧州言語 (Latin, Greek, Cyrillic) |
| NotoSansJP-Regular.otf | 日本語 |
| NotoSansKR-Regular.otf | 韓国語 |
| NotoSansSC-Regular.otf | 中国語（簡体字） |
| NotoSansTC-Regular.otf | 中国語（繁体字） |
| NotoSansArabic-Regular.ttf | アラビア語 (UI向け) |
| NotoNaskhArabic-Regular.ttf | アラビア語 (本文向け、Naskh書体) |
| NotoColorEmoji.ttf | カラー絵文字 |
| NotoEmoji-Regular.ttf | モノクロ絵文字 |

ライセンス: SIL Open Font License (OFL)

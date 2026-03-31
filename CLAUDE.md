# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 概要

吉里吉里（krkrZ）用の RichText プラグイン。`../richtext` のコアライブラリ（richtext_lib）を使用して TJS2 バインディングを提供する。

## ビルド

事前準備として `VCPKG_ROOT` 環境変数を設定する必要がある。

```bash
# CMake 構成（初回またはCMakeLists.txt変更時）
make prebuild                          # OS自動検出でプリセット選択
make prebuild PRESET=x64-windows       # プリセットを明示指定

# ビルド
make build
make build BUILD_TYPE=Debug            # デバッグビルド
```

プリセット: `x86-windows`, `x64-windows`, `x64-linux`, `arm64-linux`, `x64-osx`, `arm64-osx`, `arm64-android`, `x64-android`

Windows向け吉里吉里プラグイン開発では通常 `x64-windows` または `x86-windows` を使う。

## 依存関係

- `../richtext` — コアライブラリ（richtext_lib 静的ライブラリ）。CMake の `add_subdirectory` で参照。
- `../tp_stub` — 吉里吉里プラグインSDK
- `../ncbind` — 吉里吉里用 C++ バインダー

## ファイル構成

```
src/main.cpp     TJS2 バインディング（ncbind 使用）
```

## TJS バインディング（main.cpp）

`richtext.hpp` を `ncbind.hpp` より先にインクルードすることで minikin ヘッダと `windows.h` のコンフリクトを回避している（ファイル先頭のコメント参照）。

TJS2 の文字列は UTF-16 (`tjs_char*`) なので `tjsToU16()` / `u16ToTjs()` で変換する。

## ビルド成果物

- `richtext.dll` — 吉里吉里プラグイン（`src/main.cpp` + `richtext_lib`）

## 参考ドキュメント

- `manual.tjs` — TJS2 向け API リファレンス（擬似コード形式）
- `実装.md` — 実装進捗
- `../richtext/設計.md` — コアライブラリ設計資料

#!/usr/bin/env python3
"""
Noto フォントを Google Fonts の GitHub リリースからダウンロードして data/ に配置する。

使い方:
    python3 data/download_fonts.py
"""

import io
import os
import sys
import zipfile
import urllib.request
import urllib.error

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = SCRIPT_DIR

# (出力ファイル名, URL, zip内のパス or None(直接ダウンロード))
FONTS = [
    # 欧州言語（固定ウェイト）
    (
        "NotoSans-Regular.ttf",
        "https://github.com/notofonts/latin-greek-cyrillic/releases/download/NotoSans-v2.015/NotoSans-v2.015.zip",
        "NotoSans/unhinted/ttf/NotoSans-Regular.ttf",
    ),
    # 欧州言語（Variable Font: wght 100-900, wdth 62.5-100）
    (
        "NotoSans-VariableFont.ttf",
        "https://github.com/notofonts/latin-greek-cyrillic/releases/download/NotoSans-v2.015/NotoSans-v2.015.zip",
        "NotoSans/unhinted/variable-ttf/NotoSans[wdth,wght].ttf",
    ),
    # 欧州言語 Italic（Variable Font: wght 100-900, wdth 62.5-100）
    (
        "NotoSans-Italic-VariableFont.ttf",
        "https://github.com/notofonts/latin-greek-cyrillic/releases/download/NotoSans-v2.015/NotoSans-v2.015.zip",
        "NotoSans/unhinted/variable-ttf/NotoSans-Italic[wdth,wght].ttf",
    ),
    # 日本語（固定ウェイト）
    (
        "NotoSansJP-Regular.otf",
        "https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/16_NotoSansJP.zip",
        "NotoSansJP-Regular.otf",
    ),
    # 日本語（Variable Font: wght 100-900）
    (
        "NotoSansJP-VariableFont.ttf",
        "https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/02_NotoSansCJK-TTF-VF.zip",
        "Variable/TTF/Subset/NotoSansJP-VF.ttf",
    ),
    # 韓国語
    (
        "NotoSansKR-Regular.otf",
        "https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/17_NotoSansKR.zip",
        "NotoSansKR-Regular.otf",
    ),
    # 中国語（簡体字）
    (
        "NotoSansSC-Regular.otf",
        "https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/18_NotoSansSC.zip",
        "NotoSansSC-Regular.otf",
    ),
    # 中国語（繁体字）
    (
        "NotoSansTC-Regular.otf",
        "https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/19_NotoSansTC.zip",
        "NotoSansTC-Regular.otf",
    ),
    # アラビア語 (UI)
    (
        "NotoSansArabic-Regular.ttf",
        "https://github.com/notofonts/arabic/releases/download/NotoSansArabic-v2.013/NotoSansArabic-v2.013.zip",
        "NotoSansArabic/unhinted/ttf/NotoSansArabic-Regular.ttf",
    ),
    # アラビア語 (Naskh)
    (
        "NotoNaskhArabic-Regular.ttf",
        "https://github.com/notofonts/arabic/releases/download/NotoNaskhArabic-v2.021/NotoNaskhArabic-v2.021.zip",
        "NotoNaskhArabic/unhinted/ttf/NotoNaskhArabic-Regular.ttf",
    ),
    # カラー絵文字 CBDT（直接ダウンロード）
    (
        "NotoColorEmoji.ttf",
        "https://raw.githubusercontent.com/googlefonts/noto-emoji/main/fonts/NotoColorEmoji.ttf",
        None,
    ),
    # カラー絵文字 COLRv1（ベクターベース、直接ダウンロード）
    (
        "Noto-COLRv1.ttf",
        "https://raw.githubusercontent.com/googlefonts/noto-emoji/main/fonts/Noto-COLRv1.ttf",
        None,
    ),
    # モノクロ絵文字（variable font、直接ダウンロード）
    (
        "NotoEmoji-Regular.ttf",
        "https://raw.githubusercontent.com/google/fonts/main/ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf",
        None,
    ),
]


def download_url(url: str) -> bytes:
    """URL からデータをダウンロードする"""
    print(f"  Downloading: {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        return resp.read()


def extract_from_zip(data: bytes, inner_path: str) -> bytes:
    """zip データ内の指定パスのファイルを取得する"""
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        # 完全一致を試す
        names = zf.namelist()
        if inner_path in names:
            return zf.read(inner_path)
        # ファイル名のみで検索（zip内のディレクトリ構造が異なる場合）
        basename = os.path.basename(inner_path)
        candidates = [n for n in names if n.endswith("/" + basename) or n == basename]
        if candidates:
            # 最短パスを優先
            candidates.sort(key=len)
            print(f"  Found in zip: {candidates[0]}")
            return zf.read(candidates[0])
        raise FileNotFoundError(
            f"'{inner_path}' (or '{basename}') not found in zip. "
            f"Available: {[n for n in names if n.endswith('.ttf') or n.endswith('.otf')]}"
        )


def main():
    os.makedirs(DATA_DIR, exist_ok=True)

    success = 0
    failed = 0

    for out_name, url, zip_path in FONTS:
        out_file = os.path.join(DATA_DIR, out_name)
        if os.path.exists(out_file):
            size_mb = os.path.getsize(out_file) / (1024 * 1024)
            print(f"[SKIP] {out_name} (already exists, {size_mb:.1f} MB)")
            success += 1
            continue

        print(f"[GET]  {out_name}")
        try:
            data = download_url(url)
            if zip_path is not None:
                font_data = extract_from_zip(data, zip_path)
            else:
                font_data = data
            with open(out_file, "wb") as f:
                f.write(font_data)
            size_mb = len(font_data) / (1024 * 1024)
            print(f"  -> Saved {out_name} ({size_mb:.1f} MB)")
            success += 1
        except Exception as e:
            print(f"  ERROR: {e}", file=sys.stderr)
            failed += 1

    print(f"\nDone: {success} succeeded, {failed} failed")
    return 1 if failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())

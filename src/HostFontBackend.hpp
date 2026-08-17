#ifndef KRKR_RICHTEXT_HOST_FONT_BACKEND_HPP
#define KRKR_RICHTEXT_HOST_FONT_BACKEND_HPP

#include <memory>

#include "richtext/FontBackend.hpp"

namespace krkr_richtext {

/**
 * 本体（吉里吉里Z）の統一フォントエンジンを richtext の FontBackend として見せる
 *
 * 本体は glyphware を内蔵し、フォントバイト列（StorageCache 共有バッファ）と
 * face を FontServiceIntf 経由でプラグインに公開している。これを使うことで
 * 本体 drawText / Elements / layerExVector / richtext が同じ FT_Face と
 * 同じオンメモリバッファを共有する。
 *
 * @return 本体が face を開けない構成（glyphware 無効ビルド等）では nullptr。
 *         その場合は richtext 側の既定バックエンド（自前 glyphware）に任せる
 */
std::shared_ptr<richtext::FontBackend> createHostFontBackend();

} // namespace krkr_richtext

#endif // KRKR_RICHTEXT_HOST_FONT_BACKEND_HPP

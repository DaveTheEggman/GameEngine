// Raptor::Fonts — :null_service partition
//
// A no-op IFontService (returns null for everything) for tests/headless use.
// Ported from Sedulous.Fonts (NullFontService.bf).

module;
#include "Core/Prelude.h"

export module raptor.fonts:null_service;

import raptor.core;
import raptor.image;
import :interfaces;

using namespace raptor::core;

export namespace raptor::fonts
{
    class NullFontService final : public IFontService
    {
    public:
        [[nodiscard]] CachedFont* GetFont(f32) override { return nullptr; }
        [[nodiscard]] CachedFont* GetFont(WideStringView, f32) override { return nullptr; }
        [[nodiscard]] raptor::image::ImageData* GetAtlasTexture(CachedFont*) override { return nullptr; }
        [[nodiscard]] raptor::image::ImageData* GetAtlasTexture(WideStringView, f32) override { return nullptr; }
        void ReleaseFont(CachedFont*) override {}
        [[nodiscard]] WideStringView DefaultFontFamily() const override { return u""; }
    };
}

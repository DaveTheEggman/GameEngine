// Draconic::Fonts - :null_service partition
//
// A no-op IFontService (returns null for everything) for tests/headless use.
// Ported from Sedulous.Fonts (NullFontService.bf).

module;
#include "Core/Prelude.h"

export module foundation.fonts:null_service;

import foundation.core;
import foundation.image;
import :interfaces;

using namespace foundation::core;

export namespace foundation::fonts
{
    class NullFontService final : public IFontService
    {
    public:
        [[nodiscard]] CachedFont* GetFont(f32) override { return nullptr; }
        [[nodiscard]] CachedFont* GetFont(StringView, f32) override { return nullptr; }
        [[nodiscard]] foundation::image::ImageData* GetAtlasTexture(CachedFont*) override
        {
            return nullptr;
        }
        [[nodiscard]] foundation::image::ImageData* GetAtlasTexture(StringView, f32) override
        {
            return nullptr;
        }
        void ReleaseFont(CachedFont*) override {}
        [[nodiscard]] StringView DefaultFontFamily() const override { return u8""; }
    };
}

// Draconic GUI - :resource_provider partition
//
// IResourceProvider: the seam the CSS engine uses to turn resource-named properties into real
// objects - background-image: url(name) into a Drawable, font-family + font-size into a font.
// The GUI core stays platform/asset-agnostic: an app (or a theme) supplies a concrete provider
// backed by whatever loads its skins and fonts. Names are opaque keys the provider understands.

module;
#include "Core/Prelude.h"

export module draconic.gui:resource_provider;

import draconic.core;    // StringView, f32
import draconic.fonts;   // CachedFont
import :drawable;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;

export namespace draconic::gui
{
    class IResourceProvider
    {
    public:
        virtual ~IResourceProvider() = default;

        // Resolve a named drawable (a skin / background-image). Null if unknown.
        [[nodiscard]] virtual Drawable* GetDrawable(core::StringView name) = 0;

        // Resolve a font by family name + pixel size. Null if unavailable.
        [[nodiscard]] virtual fonts::CachedFont* GetFont(core::StringView family, f32 size) = 0;
    };
}

// Draconic GUI - :resource_provider partition
//
// IResourceProvider: the seam the CSS engine uses to load image assets referenced by
// background-image: url(path). Aligned with Sedulous.UI / draconic.ui's IResourceProvider - it
// returns a raw, provider-owned image (not a framework Drawable), so the provider stays a pure
// asset loader (VFS + draconic.image) with no dependency on the GUI's drawable types; the
// StyleApplier wraps the image into an ImageDrawable. Font resolution is a separate seam
// (IFontProvider). @import / SVG text loading (Sedulous's LoadText) is deferred until we load
// stylesheets from files.

module;
#include "Core/Prelude.h"

export module draconic.gui:resource_provider;

import draconic.core;    // StringView
import draconic.image;   // ImageData

using namespace draconic::core;
namespace core = draconic::core;
namespace image = draconic::image;

export namespace draconic::gui
{
    class IResourceProvider
    {
    public:
        virtual ~IResourceProvider() = default;

        // Load image data for a path (background-image: url(path)). Returns a borrowed pointer
        // owned by the provider (valid until the provider releases it), or null if not found.
        [[nodiscard]] virtual const image::ImageData* LoadImage(core::StringView path) = 0;
    };
}

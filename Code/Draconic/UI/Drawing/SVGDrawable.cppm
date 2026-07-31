// Draconic UI - :svg_drawable partition
//
// Renders SVG content via VGContext path operations (resolution-independent; ideal for icons).
// Thin wrapper around SVGRenderer. Ported from Sedulous.UI/src/Drawing/SVGDrawable.bf.
//
// Divergence (language): Draconic SVGLoader::Load returns Result<SVGDocument> (by value), so the
// document is held BY VALUE (m_document), not a heap pointer + ~delete. FromString returns a
// RefPtr<SVGDrawable> (empty on parse failure) instead of a raw pointer/null.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

export module draconic.ui:svg_drawable;

import draconic.core;   // Color, Rectangle, Float2, Optional, Result, RefPtr, String
import draconic.vg.svg; // SVGDocument, SVGLoader, SVGRenderer
import :drawable;
import :draw_context;

using namespace draconic::core;
namespace vg = draconic::vg;

export namespace draconic::ui
{
    class SVGDrawable : public Drawable
    {
        DRACONIC_OBJECT(SVGDrawable, Drawable)
    public:
        /// Optional tint; when set, overrides all stroke/fill colors in the SVG. Empty = original colors.
        Optional<Color> TintColor;

        explicit SVGDrawable(vg::svg::SVGDocument document) : m_document(Move(document)) {}

        /// Create from an SVG string. Returns an empty RefPtr on parse failure.
        [[nodiscard]] static RefPtr<SVGDrawable> FromString(StringView svgContent)
        {
            Result<vg::svg::SVGDocument> result = vg::svg::SVGLoader::Load(svgContent);
            if (result.HasValue())
            {
                return MakeRef<SVGDrawable>(DefaultAllocator(), Move(result.Value()));
            }
            return {};
        }

        /// Create from an SVG string with a tint applied.
        [[nodiscard]] static RefPtr<SVGDrawable> FromString(StringView svgContent, Color tint)
        {
            Result<vg::svg::SVGDocument> result = vg::svg::SVGLoader::Load(svgContent);
            if (result.HasValue())
            {
                RefPtr<SVGDrawable> d =
                    MakeRef<SVGDrawable>(DefaultAllocator(), Move(result.Value()));
                d->TintColor = tint;
                return d;
            }
            return {};
        }

        [[nodiscard]] Optional<Float2> IntrinsicSize() const override
        {
            if (m_document.width > 0.0f && m_document.height > 0.0f)
            {
                return Float2{m_document.width, m_document.height};
            }
            return {};
        }

        void Draw(UIDrawContext& ctx, const Rectangle& bounds) override
        {
            vg::svg::SVGRenderer::Render(ctx.VG(), m_document, bounds, TintColor);
        }

    private:
        vg::svg::SVGDocument m_document;
    };

    DRACONIC_DEFINE_OBJECT(SVGDrawable, "draconic::ui")
}

// Raptor::VG::SVG — :renderer partition.
//
// SVGRenderer: draws an SVGDocument to a VGContext (standalone, no UI framework).
// Scales the document to fit a bounds rect; an optional tint overrides all
// fill/stroke colors. Ported from Sedulous.VG.SVG/SVGRenderer.bf.

module;
#include "Core/Prelude.h"

export module raptor.vg.svg:renderer;

import raptor.core;
import raptor.vg;
import raptor.fonts;
import :types;

using namespace raptor::core;

export namespace raptor::vg::svg
{
    /// Renders an SVGDocument to a VGContext.
    class SVGRenderer
    {
    public:
        /// Render scaled to fit `bounds`. `tint` (if set) overrides all colors.
        static void Render(raptor::vg::VGContext& vg, const SVGDocument& document, Rect bounds, Optional<Color> tint = {})
        {
            if (document.elements.IsEmpty()) return;

            const f32 scaleX = (document.width > 0.0f) ? bounds.width / document.width : 1.0f;
            const f32 scaleY = (document.height > 0.0f) ? bounds.height / document.height : 1.0f;

            vg.PushState();
            vg.Translate(bounds.x, bounds.y);
            vg.Scale(scaleX, scaleY);

            for (usize i = 0; i < document.elements.Size(); ++i)
                RenderElement(vg, document.elements[i], tint);

            vg.PopState();
        }

        /// Render a single element and its children.
        static void RenderElement(raptor::vg::VGContext& vg, const SVGElement& element, Optional<Color> tint = {})
        {
            if (element.opacity <= 0.0f) return;

            vg.PushState();

            if (element.transform != Mat4::Identity())
                vg.SetTransform(element.transform * vg.GetTransform());

            if (element.opacity < 1.0f)
                vg.PushOpacity(element.opacity);

            if (element.IsGroup())
            {
                for (usize i = 0; i < element.children.Size(); ++i)
                    RenderElement(vg, element.children[i], tint);
            }
            else if (element.type == SVGElementType::Text)
            {
                RenderText(vg, element, tint);
            }
            else if (element.path.HasValue())
            {
                if (element.fillColor.HasValue())
                    vg.FillPath(element.path.Value(), tint.HasValue() ? tint.Value() : element.fillColor.Value());

                if (element.strokeColor.HasValue() && element.strokeWidth > 0.0f)
                    vg.StrokePath(element.path.Value(), tint.HasValue() ? tint.Value() : element.strokeColor.Value(),
                                  StrokeStyle(element.strokeWidth));
            }

            if (element.opacity < 1.0f)
                vg.PopOpacity();

            vg.PopState();
        }

    private:
        static void RenderText(raptor::vg::VGContext& vg, const SVGElement& element, Optional<Color> tint)
        {
            if (element.textContent.AsView().IsEmpty() || vg.FontService() == nullptr)
                return;

            // The current VG transform scales SVG units -> screen pixels. Glyphs are
            // rasterized at a fixed pixel size, so: compute the effective pixel font
            // size, convert position to screen space, and draw without the SVG scale.
            const Mat4 transform = vg.GetTransform();
            const f32 scaleY = Sqrt(transform.m[1][0] * transform.m[1][0] + transform.m[1][1] * transform.m[1][1]);
            const f32 effectiveFontSize = element.fontSize * scaleY;

            raptor::fonts::CachedFont* font = vg.FontService()->GetFont(effectiveFontSize);
            if (font == nullptr) return;

            const Color color = tint.HasValue() ? tint.Value()
                                : (element.fillColor.HasValue() ? element.fillColor.Value() : Color::Black);

            // SVG position -> screen pixels via the current transform.
            const f32 screenX = transform.m[0][0] * element.textX + transform.m[1][0] * element.textY + transform.m[3][0];
            const f32 screenY = transform.m[0][1] * element.textX + transform.m[1][1] * element.textY + transform.m[3][1];

            // text-anchor alignment in screen space.
            const f32 textW = font->font->MeasureString(element.textContent.AsView());
            f32 x = screenX;
            switch (element.textAnchor)
            {
            case SVGTextAnchor::Middle: x -= textW * 0.5f; break;
            case SVGTextAnchor::End:    x -= textW; break;
            case SVGTextAnchor::Start:  break;
            }

            // Draw with identity transform so glyph pixels aren't double-scaled.
            vg.PushState();
            vg.SetTransform(Mat4::Identity());
            vg.DrawText(element.textContent.AsView(), font, Vec2{ x, screenY }, color);
            vg.PopState();
        }
    };
}

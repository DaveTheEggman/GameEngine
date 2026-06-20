// Raptor::VG::SVG — :types partition.
//
// SVG element/document model: SVGElementType, SVGTextAnchor, SVGElement (a parsed
// shape/group/text node with a tessellated Path + style), SVGDocument. Ported
// from Sedulous.VG.SVG (SVGElementType/SVGElement/SVGDocument). Element trees are
// value types (children owned by value); the transform is a Mat4 (mirroring
// Sedulous's Matrix); colors are Color32.

module;
#include "Core/Prelude.h"

export module raptor.vg.svg:types;

import raptor.core;
import raptor.vg;

using namespace raptor::core;

export namespace raptor::vg::svg
{
    /// Types of SVG elements.
    enum class SVGElementType
    {
        Path, Group, Rect, Circle, Ellipse, Line, Polygon, Polyline, Text
    };

    /// Text anchor alignment (maps to the SVG text-anchor attribute).
    enum class SVGTextAnchor { Start, Middle, End };

    /// A parsed SVG element.
    class SVGElement
    {
    public:
        SVGElementType type = SVGElementType::Path;
        Optional<raptor::vg::Path> path;          ///< Tessellatable geometry (shapes/paths).
        Mat4 transform = Mat4::Identity();        ///< Element transform.
        Optional<Color32> fillColor;              ///< Fill color (empty = none/inherit).
        Optional<Color32> strokeColor;            ///< Stroke color (empty = none).
        f32 strokeWidth = 1.0f;
        f32 opacity = 1.0f;
        Array<SVGElement> children;               ///< Children (for group elements).

        // Text-specific fields.
        String textContent;
        f32 textX = 0.0f;
        f32 textY = 0.0f;
        f32 fontSize = 16.0f;
        SVGTextAnchor textAnchor = SVGTextAnchor::Start;
        bool fontBold = false;

        SVGElement() = default;
        explicit SVGElement(SVGElementType inType) : type(inType) {}

        /// Whether this element is a non-empty group.
        [[nodiscard]] bool IsGroup() const { return type == SVGElementType::Group && !children.IsEmpty(); }
    };

    /// A parsed SVG document.
    class SVGDocument
    {
    public:
        f32 width = 0.0f;
        f32 height = 0.0f;
        Array<SVGElement> elements;

        SVGDocument() = default;
        SVGDocument(f32 inWidth, f32 inHeight) : width(inWidth), height(inHeight) {}
    };
}

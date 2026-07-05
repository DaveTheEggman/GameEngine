// Draconic::VG — :vertex partition.
//
// VGVertex: the GPU vertex for vector graphics with analytical-AA coverage.
// Ported from Sedulous.VG/VGVertex.bf. Color is stored as a packed Color32
// (byte RGBA) so the vertex is a compact 24 bytes, matching the renderer's
// unorm8x4 color attribute; the VG API + tessellation compute in float Color
// and pack at the vertex boundary (see [[textures-port]] / Color32).

module;
#include "Core/Prelude.h"

export module draconic.vg:vertex;

import draconic.core;

using namespace draconic::core;

export namespace draconic::vg
{
    /// Vertex structure for vector graphics with analytical AA support.
    struct VGVertex
    {
        Vector2 position;    ///< Position in screen/world coordinates.
        Vector2 texCoord;    ///< Texture coordinates (UV).
        Color32 color;    ///< Vertex color, stored packed RGBA (byte). The API/math
                          ///< work in float Color; conversion happens here at emission.
        f32 coverage = 1.0f; ///< Analytical-AA coverage (0 = transparent fringe, 1 = opaque).

        /// Size in bytes of this vertex structure.
        static constexpr i32 SizeInBytes = 24; // 8 + 8 + 4 + 4

        /// Fixed UV for solid-color drawing.
        static constexpr f32 SolidUV = 0.5f;

        constexpr VGVertex() noexcept = default;

        // Constructors take float Color and pack to Color32 at emission.
        constexpr VGVertex(Vector2 inPosition, Vector2 inTexCoord, Color inColor, f32 inCoverage = 1.0f) noexcept
            : position(inPosition), texCoord(inTexCoord), color(ToColor32(inColor)), coverage(inCoverage) {}

        constexpr VGVertex(f32 x, f32 y, f32 u, f32 v, Color inColor, f32 inCoverage = 1.0f) noexcept
            : position(x, y), texCoord(u, v), color(ToColor32(inColor)), coverage(inCoverage) {}

        /// Create a solid-color vertex (no texture).
        [[nodiscard]] static constexpr VGVertex Solid(Vector2 position, Color color, f32 coverage = 1.0f) noexcept
        {
            return VGVertex(position, Vector2{ SolidUV, SolidUV }, color, coverage);
        }

        /// Create a solid-color vertex (no texture).
        [[nodiscard]] static constexpr VGVertex Solid(f32 x, f32 y, Color color, f32 coverage = 1.0f) noexcept
        {
            return VGVertex(x, y, SolidUV, SolidUV, color, coverage);
        }
    };

    static_assert(sizeof(VGVertex) == VGVertex::SizeInBytes, "VGVertex must stay 24 bytes for the renderer layout");
}

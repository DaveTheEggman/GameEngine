// Raptor::VG — :vertex partition.
//
// VGVertex: the GPU vertex for vector graphics with analytical-AA coverage.
// Ported from Sedulous.VG/VGVertex.bf. Color is stored as a packed Color32
// (byte RGBA) so the vertex is a compact 24 bytes, matching the renderer's
// unorm8x4 color attribute; the VG API + tessellation compute in float Color
// and pack at the vertex boundary (see [[textures-port]] / Color32).

module;
#include "Core/Prelude.h"

export module raptor.vg:vertex;

import raptor.core;

using namespace raptor::core;

export namespace raptor::vg
{
    /// Vertex structure for vector graphics with analytical AA support.
    struct VGVertex
    {
        Vec2 position;    ///< Position in screen/world coordinates.
        Vec2 texCoord;    ///< Texture coordinates (UV).
        Color32 color;    ///< Vertex color (packed RGBA).
        f32 coverage = 1.0f; ///< Analytical-AA coverage (0 = transparent fringe, 1 = opaque).

        /// Size in bytes of this vertex structure.
        static constexpr i32 SizeInBytes = 24; // 8 + 8 + 4 + 4

        /// Fixed UV for solid-color drawing.
        static constexpr f32 SolidUV = 0.5f;

        constexpr VGVertex() noexcept = default;

        constexpr VGVertex(Vec2 inPosition, Vec2 inTexCoord, Color32 inColor, f32 inCoverage = 1.0f) noexcept
            : position(inPosition), texCoord(inTexCoord), color(inColor), coverage(inCoverage) {}

        constexpr VGVertex(f32 x, f32 y, f32 u, f32 v, Color32 inColor, f32 inCoverage = 1.0f) noexcept
            : position(x, y), texCoord(u, v), color(inColor), coverage(inCoverage) {}

        /// Create a solid-color vertex (no texture).
        [[nodiscard]] static constexpr VGVertex Solid(Vec2 position, Color32 color, f32 coverage = 1.0f) noexcept
        {
            return VGVertex(position, Vec2{ SolidUV, SolidUV }, color, coverage);
        }

        /// Create a solid-color vertex (no texture).
        [[nodiscard]] static constexpr VGVertex Solid(f32 x, f32 y, Color32 color, f32 coverage = 1.0f) noexcept
        {
            return VGVertex(x, y, SolidUV, SolidUV, color, coverage);
        }
    };

    static_assert(sizeof(VGVertex) == VGVertex::SizeInBytes, "VGVertex must stay 24 bytes for the renderer layout");
}

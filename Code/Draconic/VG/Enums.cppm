// Draconic::VG - :enums partition.
//
// Small enumerations for vector-graphics draw state. Ported from
// Sedulous.VG (FillRule/VGBlendMode/VGClipMode/VGLineCap/VGLineJoin).

module;
#include "Draconic.Core/Prelude.h"

export module draconic.vg:enums;

export namespace draconic::vg
{
    /// Determines how the interior of a path is calculated.
    enum class FillRule
    {
        EvenOdd, ///< Inside if a ray crosses an odd number of path edges.
        NonZero, ///< Inside if the winding number is non-zero.
    };

    /// Blend mode for draw commands.
    enum class VGBlendMode
    {
        Normal,   ///< Standard alpha blending.
        Additive, ///< Additive blending.
        Multiply, ///< Multiply blending.
        Screen,   ///< Screen blending.
    };

    /// Clipping mode for draw commands.
    enum class VGClipMode
    {
        None,    ///< No clipping.
        Scissor, ///< Scissor rectangle clipping.
        Stencil, ///< Stencil-based path clipping.
    };

    /// Line cap style for stroke endpoints.
    enum class VGLineCap
    {
        Butt,   ///< Flat end at the exact endpoint.
        Round,  ///< Rounded end extending by half the stroke width.
        Square, ///< Square end extending by half the stroke width.
    };

    /// Line join style for stroke corners.
    enum class VGLineJoin
    {
        Miter, ///< Sharp corner (clamped by miter limit).
        Round, ///< Rounded corner.
        Bevel, ///< Beveled (flat cut) corner.
    };

    /// How a command's texels are shaded: straight sampling, or MSDF distance-field decode
    /// (crisp text at any scale). The renderer switches pipelines on this.
    enum class VGDrawMode
    {
        Default,       ///< Sample the texture / vertex color directly.
        DistanceField, ///< Decode an MSDF atlas (median-of-3 + screen-space AA).
    };
}

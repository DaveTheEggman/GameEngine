// Draconic::VG::Renderer - :vertex partition.
//
// VGRenderVertex: the GPU vertex layout (float2 pos, float2 uv, float4 color,
// float coverage) the vg shader expects. Built from the CPU VGVertex; the float
// Color has an sRGB->linear decode applied to RGB (UI/SVG colors are authored
// sRGB and the swapchain re-encodes on write - decoding here avoids double-
// encoding). Ported from Sedulous.VG.Renderer/VGRenderVertex.bf.

module;
#include "Draconic.Core/Prelude.h"

export module draconic.vg.renderer:vertex;

import draconic.core;
import draconic.vg;

using namespace draconic::core;

export namespace draconic::vg::renderer
{
    struct VGRenderVertex
    {
        f32 position[2];
        f32 texCoord[2];
        f32 color[4];
        f32 coverage;

        VGRenderVertex() = default;

        explicit VGRenderVertex(const draconic::vg::VGVertex& v)
        {
            position[0] = v.position.x;
            position[1] = v.position.y;
            texCoord[0] = v.texCoord.x;
            texCoord[1] = v.texCoord.y;
            color[0] = SrgbToLinear(v.color.r);
            color[1] = SrgbToLinear(v.color.g);
            color[2] = SrgbToLinear(v.color.b);
            color[3] = v.color.a;
            coverage = v.coverage;
        }
    };

    static_assert(sizeof(VGRenderVertex) == 36,
                  "VGRenderVertex must be 36 bytes (8+8+16+4) for the shader layout");
}

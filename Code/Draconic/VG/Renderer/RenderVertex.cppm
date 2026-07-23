// Draconic::VG::Renderer - :vertex partition.
//
// VGRenderVertex: the GPU vertex layout (float2 pos, float2 uv, float4 color,
// float coverage) the vg shader expects. Built from the CPU VGVertex; the byte
// Color32 is expanded to float4 with an sRGB->linear decode on RGB (UI/SVG
// colors are authored sRGB and the swapchain re-encodes on write - decoding here
// avoids double-encoding). Ported from Sedulous.VG.Renderer/VGRenderVertex.bf.

module;
#include "Core/Prelude.h"

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
            color[0] = SrgbToLinear(static_cast<f32>(v.color.r) / 255.0f);
            color[1] = SrgbToLinear(static_cast<f32>(v.color.g) / 255.0f);
            color[2] = SrgbToLinear(static_cast<f32>(v.color.b) / 255.0f);
            color[3] = static_cast<f32>(v.color.a) / 255.0f;
            coverage = v.coverage;
        }
    };

    static_assert(sizeof(VGRenderVertex) == 36,
                  "VGRenderVertex must be 36 bytes (8+8+16+4) for the shader layout");
}

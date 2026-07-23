/// Draconic::Render - the `:debug_pass_shaders` partition.
///
/// HLSL source for the debug-draw geometry + screen passes, split out from the pass logic
/// (DebugDrawPass.cppm) so the shader text is isolated and trivial to lift into standalone .hlsl files
/// later. Pass-internal: imported by `:debug_pass`, which registers these with the ShaderSystem.
/// Mirrors the FxaaShaders.cppm pattern.

module;
#include "Core/Prelude.h"

export module draconic.render:debug_pass_shaders;

import draconic.core;

export namespace draconic::render
{

    // World-space geometry: position (world) transformed by the view's ViewProj (push), unlit vertex color.
    // A tiny clip-space depth nudge toward the camera (constant NDC-z bias = kDepthBias, applied as
    // z -= bias*w so it survives the perspective divide) keeps coplanar gizmos (grid/axes on a surface)
    // from shimmering: the debug geometry is projected UNJITTERED but depth-tests against the scene depth,
    // which is rendered with the TAA sub-pixel jitter - so without a bias the depth-test margin oscillates
    // as the jitter walks its sequence each frame. A rasterizer depth bias won't help (it doesn't apply to
    // line primitives on D3D12/Vulkan), so we bias in clip space where it covers both lines and triangles.
    // Harmless for the overlay pipelines (Always depth-compare ignores it).
    [[nodiscard]] inline core::StringView DebugGeomVS() noexcept
    {
        return core::StringView(u8R"(
struct VSIn  { float3 pos : TEXCOORD0; float4 col : TEXCOORD1; };
struct VSOut { float4 pos : SV_Position; float4 col : TEXCOORD0; };
struct GeomPush { row_major float4x4 ViewProj; };
[[vk::push_constant]] GeomPush pc;
static const float kDepthBias = 0.0005;
VSOut main(VSIn i) {
    VSOut o;
    o.pos = mul(float4(i.pos, 1.0), pc.ViewProj);
    o.pos.z -= kDepthBias * o.pos.w;   // pull toward camera (NDC 0=near) to beat TAA-jitter depth noise
    o.col = i.col;
    return o;
}
)");
    }

    [[nodiscard]] inline core::StringView DebugGeomPS() noexcept
    {
        return core::StringView(u8R"(
float4 main(float4 pos : SV_Position, float4 col : TEXCOORD0) : SV_Target { return col; }
)");
    }

    // Screen-space text/rects: pixel coords (top-left origin) -> NDC (top-origin, correct under the RHI's
    // negative viewport), sampled against the R8 font atlas (or its solid block for rects).
    [[nodiscard]] inline core::StringView DebugScreenVS() noexcept
    {
        return core::StringView(u8R"(
struct VSIn  { float3 pos : TEXCOORD0; float2 uv : TEXCOORD1; float4 col : TEXCOORD2; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : TEXCOORD1; };
struct ScreenPush { float2 InvSize; float2 _pad; };
[[vk::push_constant]] ScreenPush pc;
VSOut main(VSIn i) {
    VSOut o;
    float2 ndc = float2(i.pos.x * pc.InvSize.x * 2.0 - 1.0, 1.0 - i.pos.y * pc.InvSize.y * 2.0);
    o.pos = float4(ndc, 0.0, 1.0);
    o.uv = i.uv; o.col = i.col;
    return o;
}
)");
    }

    [[nodiscard]] inline core::StringView DebugScreenPS() noexcept
    {
        return core::StringView(u8R"(
Texture2D    FontAtlas : register(t0, space0);
SamplerState FontSamp  : register(s0, space0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 col : TEXCOORD1) : SV_Target {
    float a = FontAtlas.SampleLevel(FontSamp, uv, 0).r;
    return float4(col.rgb, col.a * a);
}
)");
    }

}

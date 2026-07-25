// draconic.vg.renderer:shaders
//
// The HLSL source for the VG vertex/fragment shaders, published so any consumer (the sample framework,
// the UI-on-runtime host) compiles them via draconic.shaders instead of carrying its own copy. The
// renderer itself stays compiler-agnostic - VGRenderer::Initialize takes already-compiled ShaderModules;
// this partition only exposes the source text (row-major HLSL, SM 6.0, entry point "main").

module;
#include "Core/Prelude.h"

export module draconic.vg.renderer:shaders;

import draconic.core;

export namespace draconic::vg::renderer
{
    /// HLSL source for the VG vertex shader (transforms 2D position by the projection cbuffer, passes
    /// through texcoord / color / coverage). Register b0 = VGUniforms{ float4x4 Projection }.
    [[nodiscard]] inline core::StringView VertexShaderSource() noexcept
    {
        return core::StringView(u8R"(
#pragma pack_matrix(row_major)
cbuffer VGUniforms : register(b0) { float4x4 Projection; float DFPxRange; float DFAtlasW; float DFAtlasH; float _pad; };
struct VSInput { float2 Position:TEXCOORD0; float2 TexCoord:TEXCOORD1; float4 Color:TEXCOORD2; float Coverage:TEXCOORD3; };
struct VSOutput { float4 Position:SV_Position; float2 TexCoord:TEXCOORD0; float4 Color:COLOR0; float Coverage:COVERAGE; };
VSOutput main(VSInput input) {
    VSOutput o;
    o.Position = mul(float4(input.Position, 0.0, 1.0), Projection);
    o.TexCoord = input.TexCoord; o.Color = input.Color; o.Coverage = input.Coverage;
    return o;
}
)");
    }

    /// HLSL source for the VG fragment shader (samples the atlas at t0/s0, modulates by vertex color,
    /// premultiplies coverage into alpha).
    [[nodiscard]] inline core::StringView FragmentShaderSource() noexcept
    {
        return core::StringView(u8R"(
struct PSInput { float4 Position:SV_Position; float2 TexCoord:TEXCOORD0; float4 Color:COLOR0; float Coverage:COVERAGE; };
Texture2D VGTexture : register(t0);
SamplerState VGSampler : register(s0);
float4 main(PSInput input) : SV_Target {
    float4 texColor = VGTexture.Sample(VGSampler, input.TexCoord);
    float4 result = texColor * input.Color;
    result.a *= input.Coverage;
    return result;
}
)");
    }

    /// HLSL source for the distance-field (MSDF) fragment shader: decodes a median-of-3 signed
    /// distance from the atlas and antialiases in SCREEN space (pxRange scaled by fwidth of the
    /// texcoord), so text stays crisp at any scale. Shares VGUniforms (b0) for the DF metadata,
    /// so the bind-group's uniform slot must be visible to the fragment stage.
    [[nodiscard]] inline core::StringView DistanceFieldFragmentShaderSource() noexcept
    {
        return core::StringView(u8R"(
cbuffer VGUniforms : register(b0) { float4x4 Projection; float DFPxRange; float DFAtlasW; float DFAtlasH; float _pad; };
struct PSInput { float4 Position:SV_Position; float2 TexCoord:TEXCOORD0; float4 Color:COLOR0; float Coverage:COVERAGE; };
Texture2D VGTexture : register(t0);
SamplerState VGSampler : register(s0);
float Median(float r, float g, float b) { return max(min(r, g), min(max(r, g), b)); }
float4 main(PSInput input) : SV_Target {
    float3 msd = VGTexture.Sample(VGSampler, input.TexCoord).rgb;
    float sd = Median(msd.r, msd.g, msd.b);
    // Screen-space px range: convert the atlas-space DF spread to screen pixels via the texcoord
    // derivatives, so the antialiased edge is ~1px wide regardless of magnification.
    float2 unitRange = float2(DFPxRange, DFPxRange) / float2(DFAtlasW, DFAtlasH);
    float2 screenTexSize = float2(1.0, 1.0) / max(fwidth(input.TexCoord), float2(1e-6, 1e-6));
    float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    float opacity = clamp(screenPxRange * (sd - 0.5) + 0.5, 0.0, 1.0);
    float4 result = input.Color;
    result.a *= opacity * input.Coverage;
    return result;
}
)");
    }
}

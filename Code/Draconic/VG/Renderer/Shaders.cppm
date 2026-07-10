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
cbuffer VGUniforms : register(b0) { float4x4 Projection; };
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
}

/// Draconic::Render - the `:decal_shaders` partition.
///
/// HLSL source for the decal pass, split out from the pass logic (DecalPass.cppm) so the shader text is
/// isolated and trivial to lift into standalone .hlsl files later. Pass-internal: imported by
/// `:decal_pass`, which registers these with the ShaderSystem. Mirrors the draconic.vg.renderer:shaders pattern.

module;
#include "Core/Prelude.h"

export module draconic.render:decal_shaders;

import draconic.core;

export namespace draconic::render
{

    // Fullscreen triangle that EMITS its clip-space NDC (like SkyPass): the interpolated NDC at a pixel is
    // exactly what the scene geometry used there (same viewport), so unprojecting it is robust - no
    // hand-derived negative-viewport flip. SV_Position is still used to sample depth at the right texel.
    [[nodiscard]] inline core::StringView DecalVS() noexcept
    {
        return core::StringView(u8R"(
struct VSOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    float2 ndc = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;
    VSOut o;
    o.pos = float4(ndc, 0.0, 1.0);
    o.ndc = ndc;
    return o;
}
)");
    }

    [[nodiscard]] inline core::StringView DecalPS() noexcept
    {
        return core::StringView(u8R"(
#pragma pack_matrix(row_major)
Texture2D    SceneDepth : register(t0, space0);
SamplerState DepthSamp  : register(s0, space0);
Texture2D    DecalTex   : register(t0, space2);
SamplerState DecalSamp  : register(s0, space2);
cbuffer DecalUniforms : register(b0, space1) {
    row_major float4x4 World;
    row_major float4x4 InvWorld;
    row_major float4x4 InvViewProj;
    float4   Color;
    float4   Params;      // xy = 1/full-size, z = cos(fadeStart), w = cos(fadeEnd)
};

float4 main(float4 pos : SV_Position, float2 ndc : TEXCOORD0) : SV_Target {
    // Sample the scene depth at THIS framebuffer pixel (SV_Position is the framebuffer position, so
    // pixel/size reads the texel the forward pass wrote here - no flip needed for a same-pixel read).
    float2 uv    = pos.xy * Params.xy;
    float  depth = SceneDepth.SampleLevel(DepthSamp, uv, 0).r;

    // Reconstruct world position from the EMITTED clip NDC (the true clip coord this pixel's geometry
    // used) + the sampled depth. clip -> world via InvViewProj (same as SkyPass).
    float4 h        = mul(float4(ndc, depth, 1.0), InvViewProj);
    float3 worldPos = h.xyz / h.w;

    // Into the decal's unit box; clip outside [-0.5, 0.5].
    float3 local = mul(float4(worldPos, 1.0), InvWorld).xyz;
    if (any(abs(local) > 0.5)) { discard; }

    // Decal UV from the box's local XY (project along local Z). Flip V for top-origin texture space.
    float2 decalUV = float2(local.x + 0.5, 0.5 - local.y);

    // Angle fade: receiver normal from world-pos screen derivatives vs the decal's projection axis
    // (local +Z in world = World row 2). Fade out where the surface faces away from the projection.
    float3 N        = normalize(cross(ddy(worldPos), ddx(worldPos)));
    float3 decalFwd = normalize(float3(World._m20, World._m21, World._m22));
    float  cosA     = dot(N, -decalFwd);
    float  fade     = smoothstep(Params.w, Params.z, cosA);

    float4 c = DecalTex.Sample(DecalSamp, decalUV) * Color;
    c.a *= fade;
    return c;
}
)");
    }

}

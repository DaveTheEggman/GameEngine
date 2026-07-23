/// Draconic::Render - the `:sky_shaders` partition.
///
/// HLSL source for the sky pass, split out from SkyPass.cppm so the shader text is isolated and trivial
/// to lift to standalone .hlsl files later. Pass-internal: imported by `:sky`. SkyCommon() is a shared
/// prefix concatenated with both the VS and PS bodies in the pass.

module;
#include "Core/Prelude.h"

export module draconic.render:sky_shaders;

import draconic.core;

export namespace draconic::render
{

    // Fullscreen-triangle VS: emit far-plane NDC (z=1) + reconstruct the world-space ray via inverse
    // view-proj (row-vector mul). PS samples the env cube along that ray.
    // Sky uniform, shared by VS+PS. PrevViewProj (last frame, unjittered-equivalent via the Jitter unjitter)
    // + Jitter let the sky write a camera-motion velocity so TAA reprojects the background under rotation.
    [[nodiscard]] inline core::StringView SkyCommon() noexcept
    {
        return core::StringView(u8R"(
cbuffer Sky : register(b0, space0) {
    row_major float4x4 InvViewProj;    // inverse of this frame's UNJITTERED view-proj (stable sky ray under TAA)
    row_major float4x4 PrevViewProj;   // last frame's view-proj (motion vectors)
    float4 CamPosIntensity;   // xyz = camera world pos, w = sky intensity
    float4 SunDir;            // xyz = light direction, w = sun angular size (deg)
    float4 SunColor;          // rgb = sun color, w = sun intensity
    float4 Jitter;            // xy = this frame's NDC jitter, zw = last frame's
};
)");
    }

    [[nodiscard]] inline core::StringView SkyVS() noexcept
    {
        return core::StringView(u8R"(
struct VSOut { float4 pos : SV_Position; float3 dir : TEXCOORD0; float2 ndc : TEXCOORD1; };
VSOut main(uint vid : SV_VertexID) {
    float2 uv  = float2((vid << 1) & 2, vid & 2);
    float2 ndc = uv * 2.0 - 1.0;
    VSOut o;
    o.pos = float4(ndc, 1.0, 1.0);                        // far plane (depth = 1)
    o.ndc = ndc;
    // Reconstruct the world ray: at a given screen pixel the interpolated NDC matches what the scene's
    // geometry uses there (both go through the same viewport), so unproject the emitted NDC directly.
    float4 world = mul(float4(ndc, 1.0, 1.0), InvViewProj);  // clip -> world
    o.dir = world.xyz / world.w - CamPosIntensity.xyz;
    return o;
}
)");
    }

    [[nodiscard]] inline core::StringView SkyPS() noexcept
    {
        return core::StringView(u8R"(
TextureCube  EnvMap  : register(t0, space0);
SamplerState EnvSamp : register(s0, space0);
struct PSIn { float4 pos : SV_Position; float3 dir : TEXCOORD0; float2 ndc : TEXCOORD1; };
struct PSOut { float4 color : SV_Target0; float2 velocity : SV_Target1; };
PSOut main(PSIn i) {
    float3 dir = normalize(i.dir);
    float3 c = EnvMap.SampleLevel(EnvSamp, dir, 0.0).rgb * CamPosIntensity.w;
    // Crisp analytic sun disc (screen resolution, round) toward the light, with a soft ~1.5deg edge.
    float3 L     = normalize(-SunDir.xyz);
    float  cd    = dot(dir, L);
    float  inner = cos(radians(max(SunDir.w, 0.1)));
    float  outer = cos(radians(max(SunDir.w, 0.1) + 1.5));
    c += smoothstep(outer, inner, cd) * SunColor.rgb * SunColor.w;

    // Camera-motion velocity: reproject the (infinite) view ray through last frame's view-proj (w=0, a
    // direction) and take the UV delta, in UNJITTERED NDC. The ray is reconstructed through the UNJITTERED
    // InvViewProj (so the background is temporally invariant under a static camera - no per-pixel jitter
    // oscillation for TAA to chase), which makes i.ndc the geometric current NDC directly. The previous
    // term still unjitters (PrevViewProj carries last frame's jitter; +Jitter.zw removes it).
    float4 prevClip = mul(float4(dir, 0.0), PrevViewProj);
    float2 curNDC   = i.ndc;
    float2 prevNDC  = prevClip.xy / prevClip.w + Jitter.zw;
    float2 velocity = (curNDC - prevNDC) * float2(0.5, -0.5);

    PSOut o; o.color = float4(c, 1.0); o.velocity = velocity; return o;
}
)");
    }

}

// draconic.particles.subsystem:particle_shaders
//
// HLSL source for the billboard particle + trail renderers, split out from ParticleRenderer.cppm so the
// shader text is isolated and trivial to lift to standalone .hlsl files later. Pass-internal: imported by
// :renderer, which registers these with the ShaderSystem. Mirrors the draconic.vg.renderer:shaders pattern.

module;
#include "Core/Prelude.h"

export module draconic.particles.subsystem:particle_shaders;

import draconic.core;

export namespace draconic::particles
{
    // Quad from SV_VertexID + per-instance billboard. Rotation (mode 0) and velocity stretch are the
    // additions over the sprite VS. Camera basis from the View matrix (row-major, row-vector).
    [[nodiscard]] inline core::StringView ParticleVS() noexcept
    {
        return core::StringView(u8R"(
#pragma pack_matrix(row_major)
cbuffer ParticleView : register(b0, space0) {
    float4x4 ViewProj;
    float4x4 View;
    float4   DepthParams;   // x=Proj[2][2], y=Proj[3][2], z=Proj[2][3], w=soft-particle fade distance
};
struct VSIn {
    float4 PositionSize : TEXCOORD0;   // xyz world center, w width
    float4 SizeRotMode  : TEXCOORD1;   // x height, y rotation(rad), z orientation mode
    float4 Color        : TEXCOORD2;
    float4 UVRect       : TEXCOORD3;   // xy uv min, zw uv size
    float4 Velocity     : TEXCOORD4;   // xyz world velocity, w stretch scale
    uint   VertexID     : SV_VertexID;
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; float linZ : TEXCOORD1; float softDist : TEXCOORD2; };

static const float2 CORNERS[6] = {
    float2(-0.5, -0.5), float2(0.5, -0.5), float2(-0.5, 0.5),
    float2(-0.5,  0.5), float2(0.5, -0.5), float2( 0.5, 0.5)
};
static const float2 UVS[6] = {
    float2(0, 1), float2(1, 1), float2(0, 0),
    float2(0, 0), float2(1, 1), float2(1, 0)
};

VSOut main(VSIn i) {
    VSOut o;
    float3 worldPos = i.PositionSize.xyz;
    float2 size     = float2(i.PositionSize.w, i.SizeRotMode.x);
    int    mode     = (int)(i.SizeRotMode.z + 0.5);
    float  rot      = i.SizeRotMode.y;

    float3 camRight = float3(View._m00, View._m10, View._m20);
    float3 camUp    = float3(View._m01, View._m11, View._m21);
    float3 camFwd   = float3(View._m02, View._m12, View._m22);

    float3 right, up;
    if (i.Velocity.w > 0.0 && dot(i.Velocity.xyz, i.Velocity.xyz) > 1e-8) {
        // Stretched billboard: length axis follows velocity, width axis perpendicular in view.
        float3 v = i.Velocity.xyz;
        float speed = length(v);
        up = v / speed;
        right = normalize(cross(up, camFwd));
        size.y *= (1.0 + speed * i.Velocity.w);
    } else if (mode == 2) {              // ground-flat (XZ plane, +Y normal) - horizontal billboard
        right = float3(1, 0, 0); up = float3(0, 0, 1);
    } else if (mode == 1) {              // camera-facing about world Y
        right = normalize(float3(camRight.x, 0, camRight.z));
        up    = float3(0, 1, 0);
    } else {                             // full camera-facing, with per-particle roll
        float s = sin(rot), c = cos(rot);
        right = camRight * c + camUp * s;
        up    = camUp * c - camRight * s;
    }

    float2 local = CORNERS[i.VertexID];
    float3 cornerWS = worldPos + right * (local.x * size.x) + up * (local.y * size.y);
    o.pos = mul(float4(cornerWS, 1.0), ViewProj);
    o.uv  = i.UVRect.xy + UVS[i.VertexID] * i.UVRect.zw;
    o.col = i.Color;
    o.linZ = -mul(float4(cornerWS, 1.0), View).z;   // positive view-space depth (soft particles)
    o.softDist = i.SizeRotMode.w;                    // per-system soft-particle fade band (0 = off)
    return o;
}
)");
    }

    [[nodiscard]] inline core::StringView ParticlePS() noexcept
    {
        return core::StringView(u8R"(
#pragma pack_matrix(row_major)
cbuffer ParticleView : register(b0, space0) {
    float4x4 ViewProj;
    float4x4 View;
    float4   DepthParams;   // x=Proj[2][2], y=Proj[3][2], z=Proj[2][3], w=soft fade distance
};
Texture2D    ParticleTexture : register(t0, space1);
SamplerState ParticleSampler : register(s0, space1);
Texture2D    SceneDepth      : register(t0, space2);   // opaque depth (read-only, sampleable)
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 col : COLOR0, float linZ : TEXCOORD1, float softDist : TEXCOORD2) : SV_Target {
    float4 c = ParticleTexture.Sample(ParticleSampler, uv) * col;
    // Soft particle (per-system, softDist>0): fade where this billboard fragment approaches the opaque
    // surface behind it. Sample the scene depth, reconstruct its view-space depth, compare to the fragment's.
    if (softDist > 0.0) {
        float d = SceneDepth.Load(int3(int2(pos.xy), 0)).r;
        float sceneLin = -DepthParams.y / (d * DepthParams.z - DepthParams.x);   // positive view-space depth
        c.a *= saturate((sceneLin - linZ) / max(softDist, 1e-3));
    }
    return c;
}
)");
    }

    // Trails: the ribbon geometry is already world-space + camera-facing (built by the extractor), so the
    // VS just transforms it. Shares the ParticleView cbuffer (set 0) + particle texture (set 1).
    [[nodiscard]] inline core::StringView TrailVS() noexcept
    {
        return core::StringView(u8R"(
#pragma pack_matrix(row_major)
cbuffer ParticleView : register(b0, space0) { float4x4 ViewProj; float4x4 View; float4 DepthParams; };
struct VSIn { float3 Position : POSITION; float2 UV : TEXCOORD0; float4 Color : COLOR0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
VSOut main(VSIn i) { VSOut o; o.pos = mul(float4(i.Position, 1.0), ViewProj); o.uv = i.UV; o.col = i.Color; return o; }
)");
    }

    [[nodiscard]] inline core::StringView TrailPS() noexcept
    {
        return core::StringView(u8R"(
Texture2D    ParticleTexture : register(t0, space1);
SamplerState ParticleSampler : register(s0, space1);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 col : COLOR0) : SV_Target {
    return ParticleTexture.Sample(ParticleSampler, uv) * col;
}
)");
    }
}

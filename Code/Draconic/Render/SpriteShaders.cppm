/// Draconic::Render - the `:sprite_shaders` partition.
///
/// HLSL source for the sprite renderer, split out from the renderer logic (SpriteRenderer.cppm) so the
/// shader text is isolated and trivial to lift into standalone .hlsl files later. Pass-internal: imported
/// by `:sprite_renderer`, which registers these with the ShaderSystem. Mirrors the draconic.vg.renderer:shaders pattern.

module;
#include "Core/Prelude.h"

export module draconic.render:sprite_shaders;

import draconic.core;

export namespace draconic::render {

// Instanced billboard. The quad corners (6 verts) come from SV_VertexID; per-sprite data arrives as
// four instance-stepped float4 attributes (TEXCOORD0..3). Camera basis is pulled from the view matrix
// (row-major, row-vector convention): world-space right = column 0, up = column 1.
[[nodiscard]] inline core::StringView SpriteVS() noexcept
{
    return core::StringView(u8R"(
#pragma pack_matrix(row_major)
cbuffer SpriteView : register(b0, space0) {
    float4x4 ViewProj;
    float4x4 View;
};
struct VSIn {
    float4 PositionSize    : TEXCOORD0;   // xyz = world center, w = width
    float4 SizeOrientation : TEXCOORD1;   // x = height, y = orientation mode
    float4 Tint            : TEXCOORD2;
    float4 UVRect          : TEXCOORD3;    // xy = uv min, zw = uv size
    float4 AxisRight       : TEXCOORD4;    // xyz entity right (mode 3)
    float4 AxisUp          : TEXCOORD5;    // xyz entity up (mode 3)
    uint   VertexID        : SV_VertexID;
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };

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
    float2 size     = float2(i.PositionSize.w, i.SizeOrientation.x);
    int    mode     = (int)(i.SizeOrientation.y + 0.5);

    // Billboard basis in world space.
    float3 right, up;
    if (mode == 3) {                     // entity-oriented (world panels: the entity's plane)
        right = i.AxisRight.xyz; up = i.AxisUp.xyz;
    } else if (mode == 2) {              // world-aligned (XY plane)
        right = float3(1, 0, 0); up = float3(0, 1, 0);
    } else if (mode == 1) {              // camera-facing about world Y (right in XZ, up = world Y)
        float3 camRight = float3(View._m00, View._m10, View._m20);
        right = normalize(float3(camRight.x, 0, camRight.z));
        up    = float3(0, 1, 0);
    } else {                             // full camera-facing
        right = float3(View._m00, View._m10, View._m20);
        up    = float3(View._m01, View._m11, View._m21);
    }

    float2 local = CORNERS[i.VertexID];
    float3 cornerWS = worldPos + right * (local.x * size.x) + up * (local.y * size.y);
    o.pos = mul(float4(cornerWS, 1.0), ViewProj);
    o.uv  = i.UVRect.xy + UVS[i.VertexID] * i.UVRect.zw;
    o.col = i.Tint;
    return o;
}
)");
}

[[nodiscard]] inline core::StringView SpritePS() noexcept
{
    return core::StringView(u8R"(
Texture2D    SpriteTexture : register(t0, space1);
SamplerState SpriteSampler : register(s0, space1);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 col : COLOR0) : SV_Target {
    return SpriteTexture.Sample(SpriteSampler, uv) * col;
}
)");
}

}

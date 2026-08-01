#include "sky_common.hlsli"

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
    o.dir.y *= SkyFlags.x;   // mirror the sampled ray on Y-flip targets (WebGPU/DX12); +1 on Vulkan
    return o;
}

#pragma pack_matrix(row_major)

// Terrain chunk PS. Terrain is always opaque, so it writes the full forward GBUFFER (like unlit.ps):
// SV_Target0 shaded colour, 1 octahedral view-space normal, 2 screen-space motion vector, 3 material
// (roughness/metallic) for SSR. Colour is height-lit: a low-to-high ramp (grass -> rock) modulated by
// a directional term + ambient. Splat-map blending (Phase D2) layers over this later.

cbuffer TerrainView : register(b0, space0) {
    float4x4 ViewProj;
    float4x4 View;
    float4x4 PrevViewProj;
    float4   LightDir;   // xyz = direction TO the light (normalized)
    float4   CameraPos;
    float4   Jitter;     // xy = current jitter, zw = previous
};

struct PSIn {
    float4 pos     : SV_Position;
    float3 normal  : TEXCOORD0;
    float  heightT : TEXCOORD1;
    float4 curClip : TEXCOORD2;
    float4 prevClip: TEXCOORD3;
};

struct PSOutput {
    float4 color    : SV_Target0;
    float2 normal   : SV_Target1; // octahedral view-space normal
    float2 velocity : SV_Target2; // screen-space motion vector (UV delta)
    float2 material : SV_Target3; // R = roughness, G = metallic (for SSR)
};

// Octahedral encode a unit vector -> [-1,1]^2 (matches forward/unlit OctEncode).
float2 OctEncode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = n.xy;
    if (n.z < 0.0) {
        e = (1.0 - abs(float2(e.y, e.x))) *
            float2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0);
    }
    return e;
}

PSOutput main(PSIn i) {
    float3 n = normalize(i.normal);
    float  ndl = saturate(dot(n, normalize(LightDir.xyz)));

    const float3 kLow  = float3(0.24, 0.40, 0.16); // grass
    const float3 kHigh = float3(0.52, 0.50, 0.46); // rock
    float3 base = lerp(kLow, kHigh, i.heightT);

    // Slope darkening: steep faces (small n.y) read as exposed rock.
    float slope = saturate(n.y);
    base = lerp(kHigh * 0.8, base, slope);

    const float3 ambient = float3(0.28, 0.30, 0.34);
    float3 lit = base * (ambient + ndl);

    // GBuffer motion vector: current vs previous NDC (unjittered), NDC.y flipped vs UV.y.
    float2 curNDC  = i.curClip.xy  / i.curClip.w  + Jitter.xy;
    float2 prevNDC = i.prevClip.xy / i.prevClip.w + Jitter.zw;

    PSOutput o;
    o.color    = float4(lit, 1.0);
    o.normal   = OctEncode(normalize(mul(float4(n, 0.0), View).xyz));
    o.velocity = (curNDC - prevNDC) * float2(0.5, -0.5);
    o.material = float2(1.0, 0.0); // fully rough, non-metallic
    return o;
}

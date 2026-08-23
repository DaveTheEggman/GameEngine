#pragma pack_matrix(row_major)

// Terrain chunk PS. Height-lit shading over the interpolated normal: a low-to-high colour ramp
// (grass -> rock) modulated by a directional term + ambient. Splat-map blending over the per-layer
// albedo textures (TerrainResource::layers) is the Phase D2 follow-on and layers a third set on top.

cbuffer TerrainView : register(b0, space0) {
    float4x4 ViewProj;
    float4   LightDir;   // xyz = direction TO the light (normalized)
    float4   CameraPos;
};

struct PSIn {
    float4 pos     : SV_Position;
    float3 normal  : TEXCOORD0;
    float  heightT : TEXCOORD1;
};

float4 main(PSIn i) : SV_Target {
    float3 n = normalize(i.normal);
    float  ndl = saturate(dot(n, normalize(LightDir.xyz)));

    static const float3 kLow  = float3(0.24, 0.40, 0.16); // grass
    static const float3 kHigh = float3(0.52, 0.50, 0.46); // rock
    float3 base = lerp(kLow, kHigh, i.heightT);

    // Slope darkening: steep faces (small n.y) read as exposed rock.
    float slope = saturate(n.y);
    base = lerp(kHigh * 0.8, base, slope);

    const float3 ambient = float3(0.28, 0.30, 0.34);
    float3 lit = base * (ambient + ndl);
    return float4(lit, 1.0);
}

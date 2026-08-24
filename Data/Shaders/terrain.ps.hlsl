#pragma pack_matrix(row_major)

// Terrain chunk PS. Terrain is always opaque, so it writes the full forward GBUFFER (like unlit.ps):
// SV_Target0 shaded colour, 1 octahedral view-space normal, 2 screen-space motion vector, 3 material
// (roughness/metallic) for SSR. Colour is height-lit: a low-to-high ramp (grass -> rock) modulated by
// a directional term + ambient, then attenuated by the CSM (SampleCSM). Splat-map blending (D2) later.

#define TERRAIN_CASCADE_COUNT 4
cbuffer TerrainView : register(b0, space0) {
    float4x4 ChunkToWorld;
    float4x4 ViewProj;
    float4x4 View;
    float4x4 PrevViewProj;
    float4x4 CascadeViewProj[TERRAIN_CASCADE_COUNT];
    float4   LightDir;   // xyz = direction TO the light (normalized)
    float4   CameraPos;
    float4   Jitter;     // xy = current jitter, zw = previous
    float4   CascadeSplitFar;
    float4   CascadeTexelSize;
    float4   ShadowMeta;   // x = cascade count, y = layer base, z = normal bias, w = depth bias
    float4   ShadowParams; // x = far-fade width, y = uv.y sign, zw spare
    float4   LayerTileScales; // per-layer albedo tiling (world units per tile), xyzw = layers 0..3
    float4   SplatParams;     // x = layer count (0 = no splat, use the height ramp), yzw spare
};

struct PSIn {
    float4 pos      : SV_Position;
    float3 normal   : TEXCOORD0; // world-space
    float  heightT  : TEXCOORD1;
    float4 curClip  : TEXCOORD2;
    float4 prevClip : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
    float2 localXZ  : TEXCOORD5; // terrain-LOCAL XZ (pre-ChunkToWorld) for albedo tiling
    float2 splatUV  : TEXCOORD6; // 0..1 across the terrain footprint (splatmap lookup)
};

// Splat material (set 3): the RGBA weight map + up to 4 layer albedos. Absent slots bind a white
// dummy; SplatParams.x (layer count) gates whether splat is used at all.
Texture2D    Splatmap      : register(t0, space3);
Texture2D    Albedo0       : register(t1, space3);
Texture2D    Albedo1       : register(t2, space3);
Texture2D    Albedo2       : register(t3, space3);
Texture2D    Albedo3       : register(t4, space3);
SamplerState SplatSampler  : register(s0, space3); // clamp, bilinear (soft layer boundaries)
SamplerState AlbedoSampler : register(s1, space3); // repeat, trilinear (tiled albedos carry mips)

struct PSOutput {
    float4 color    : SV_Target0;
    float2 normal   : SV_Target1; // octahedral view-space normal
    float2 velocity : SV_Target2; // screen-space motion vector (UV delta)
    float2 material : SV_Target3; // R = roughness, G = metallic (for SSR)
};

// CSM cascade depth ARRAY (t1, one layer per cascade) + a comparison sampler (s0) for hardware PCF.
// Same set-0 layout role as the forward's ShadowMap/ShadowSampler.
Texture2DArray         ShadowMap     : register(t1, space0);
SamplerComparisonState ShadowSampler : register(s0, space0);
static const float kTerrainShadowTexel = 1.0 / 1024.0; // 1 / shadow resolution

// One cascade with a normal-offset bias (fading at grazing angles) + 3x3 hardware PCF. 1 = lit.
// Adapted verbatim from forward.ps.hlsl SampleCascade, mapped to terrain's cbuffer fields.
float SampleCascade(int cascade, float3 worldPos, float3 N, float NdotL) {
    float texelWorld = CascadeTexelSize[cascade];
    float3 biasedPos = worldPos + N * (ShadowMeta.z * texelWorld * (1.0 - NdotL));
    float4 lc = mul(float4(biasedPos, 1.0), CascadeViewProj[cascade]);
    if (lc.w <= 0.0) { return 1.0; }
    float3 ndc = lc.xyz / lc.w;
    float2 uv  = float2(ndc.x * 0.5 + 0.5, ndc.y * ShadowParams.y * 0.5 + 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { return 1.0; }
    float compareDepth = ndc.z - ShadowMeta.w;
    float layer = ShadowMeta.y + (float)cascade;
    float sum = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            sum += ShadowMap.SampleCmpLevelZero(
                ShadowSampler, float3(uv + float2(x, y) * kTerrainShadowTexel, layer), compareDepth);
        }
    }
    return sum * (1.0 / 9.0);
}

// Cascaded shadow: pick the cascade by view-space depth, sample, blend the seam, fade at the far edge.
float SampleCSM(float3 worldPos, float3 N, float NdotL, float viewDepth) {
    int count = (int)ShadowMeta.x;
    if (count <= 0) { return 1.0; } // no directional caster this frame -> fully lit

    int cascade = count - 1;
    [unroll] for (int i = 0; i < TERRAIN_CASCADE_COUNT; ++i) {
        if (i < count && viewDepth < CascadeSplitFar[i]) { cascade = i; break; }
    }
    float shadow = SampleCascade(cascade, worldPos, N, NdotL);

    float splitFar  = CascadeSplitFar[cascade];
    float splitNear = (cascade == 0) ? 0.0 : CascadeSplitFar[cascade - 1];
    float blendBand = (splitFar - splitNear) * 0.15;
    if (cascade < count - 1 && viewDepth > splitFar - blendBand) {
        float t = saturate((viewDepth - (splitFar - blendBand)) / max(blendBand, 1e-4));
        shadow = lerp(shadow, SampleCascade(cascade + 1, worldPos, N, NdotL), t);
    }

    float shadowFar = CascadeSplitFar[count - 1];
    float fadeBand  = max(ShadowParams.x, 0.5);
    float farFade   = saturate((shadowFar - viewDepth) / fadeBand);
    return lerp(1.0, shadow, farFade);
}

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
    float3 sun = normalize(LightDir.xyz);
    float  ndl = saturate(dot(n, sun));

    float3 base;
    if (SplatParams.x >= 0.5) {
        // Splat: normalize the RGBA weights (authoring need not sum to 1), with a zero-sum guard ->
        // layer 0 (unpainted regions render the base layer, never black / divide-by-zero). Each layer
        // samples its albedo at a terrain-LOCAL tiled UV (glued to the surface under move/rotate).
        float4 w = Splatmap.Sample(SplatSampler, i.splatUV);
        float sum = w.r + w.g + w.b + w.a;
        w = (sum < 1e-4) ? float4(1, 0, 0, 0) : (w / sum);
        base = w.r * Albedo0.Sample(AlbedoSampler, i.localXZ / max(LayerTileScales.x, 1e-3)).rgb +
               w.g * Albedo1.Sample(AlbedoSampler, i.localXZ / max(LayerTileScales.y, 1e-3)).rgb +
               w.b * Albedo2.Sample(AlbedoSampler, i.localXZ / max(LayerTileScales.z, 1e-3)).rgb +
               w.a * Albedo3.Sample(AlbedoSampler, i.localXZ / max(LayerTileScales.w, 1e-3)).rgb;
    } else {
        // No layers bound -> the height/slope colour ramp (headless tools, layerless terrains).
        const float3 kLow  = float3(0.24, 0.40, 0.16); // grass
        const float3 kHigh = float3(0.52, 0.50, 0.46); // rock
        base = lerp(kLow, kHigh, i.heightT);
        float slope = saturate(n.y);                    // steep faces read as exposed rock
        base = lerp(kHigh * 0.8, base, slope);
    }

    // CSM: attenuate only the DIRECT (sun) term; ambient is indirect and stays.
    float viewDepth = -mul(float4(i.worldPos, 1.0), View).z;
    float shadow = SampleCSM(i.worldPos, n, ndl, viewDepth);

    const float3 ambient = float3(0.28, 0.30, 0.34);
    float3 lit = base * (ambient + ndl * shadow);

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

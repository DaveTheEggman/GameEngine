/// Draconic::Render - the `:mesh_shaders` partition.
///
/// HLSL source for the mesh forward + shadow-caster shaders, split out from the pass logic
/// (MeshRenderer.cppm) so the shader text is isolated and trivial to lift into standalone .hlsl files
/// later. Pass-internal: imported by `:mesh_renderer`, which registers these with the ShaderSystem.
/// Mirrors the FxaaShaders.cppm pattern.

module;
#include "Core/Prelude.h"

export module draconic.render:mesh_shaders;

import draconic.core;

export namespace draconic::render {

// Built-in forward shader, two permutations selected by INSTANCED. The view-projection is in a
// per-view UBO (set 0); the world matrix + tint come either from a per-object UBO (set 1) or,
// in the instanced permutation, from a per-instance StructuredBuffer indexed by the uint4
// DataOffsets vertex attribute (location 5). Vertex inputs use the RHI's TEXCOORDn convention
// (semantic index = location), matching VertexLayoutType::Mesh at locations 0..4.
[[nodiscard]] inline core::StringView ForwardVS() noexcept
{
    return core::StringView(u8R"(
#define CASCADE_COUNT 4
cbuffer View : register(b0, space0) {
    row_major float4x4 ViewProj;   // Draconic matrices are row-major; annotate so HLSL reads them right.
    row_major float4x4 View;       // for view-space depth (cluster lookup + CSM cascade select, PS only)
    row_major float4x4 CascadeViewProj[CASCADE_COUNT];   // CSM: world -> each cascade's light clip
    float3 CameraPos; float LightCount;
    uint   LightOffset; int ClusterVpX; int ClusterVpY; float IBLMaxLod;   // IBLMaxLod < 0 -> no IBL (flat ambient)
    uint   ClusterGridX; uint ClusterGridY; uint ClusterSliceCount; uint ClusterTileSize;
    float  ClusterNear;  float ClusterFar;  float ClusterLogScale;  float ClusterLogBias;
    float3 Ambient; float ShadowCascadeCount;          // 0 -> no shadow
    float4 CascadeSplitFar;        // view-space far depth of each cascade (cascade selection)
    float4 CascadeTexelSize;       // world units per shadow texel, per cascade (normal-offset bias)
    float  ShadowNormalBias; float ShadowDepthBias; float CascadeLayerBase; uint LocalShadowBase;
    row_major float4x4 PrevViewProj;   // last frame's world->clip (motion vectors)
    float4 Jitter;                     // xy = this frame's NDC jitter, zw = last frame's (TAA)
    float4 ProbeCenter;                // xyz = reflection-probe center (world), w = probe count (0 = none)
    float4 ProbeBoxMin;                // xyz = probe box min corner,  w = probe cube slice (index into ProbeArray)
    float4 ProbeBoxMax;                // xyz = probe box max corner,  w = probe intensity
    float4 ShadowParams;               // x = CSM far-fade width in WORLD UNITS; yzw spare
};
#ifdef SKINNED
// GPU skinning: per-bone skinning matrices (= inverseBind * worldPose), v * skin (row-vector).
// A per-frame pool shared by all skinned draws; BoneBase (Object cbuffer) selects this draw's run.
// Stored as 4 explicit float4 ROWS (Sedulous-faithful) so the major-ness is unambiguous - DXC's
// row_major modifier is unreliable on a StructuredBuffer matrix element.
struct BoneMatrix { float4 Row0, Row1, Row2, Row3; };
StructuredBuffer<BoneMatrix> BoneMatrices : register(t4, space0);
float4x4 BlendBones(uint4 j, float4 w, uint base) {
    BoneMatrix b0 = BoneMatrices[base + j.x];
    BoneMatrix b1 = BoneMatrices[base + j.y];
    BoneMatrix b2 = BoneMatrices[base + j.z];
    BoneMatrix b3 = BoneMatrices[base + j.w];
    return float4x4(b0.Row0 * w.x + b1.Row0 * w.y + b2.Row0 * w.z + b3.Row0 * w.w,
                    b0.Row1 * w.x + b1.Row1 * w.y + b2.Row1 * w.z + b3.Row1 * w.w,
                    b0.Row2 * w.x + b1.Row2 * w.y + b2.Row2 * w.z + b3.Row2 * w.w,
                    b0.Row3 * w.x + b1.Row3 * w.y + b2.Row3 * w.z + b3.Row3 * w.w);
}
#endif
#ifdef INSTANCED
struct InstanceData { row_major float4x4 World; row_major float4x4 PrevWorld; float4 Tint; };
StructuredBuffer<InstanceData> Instances : register(t0, space1);
#else
cbuffer Object : register(b0, space1) {
    row_major float4x4 World;
    row_major float4x4 PrevWorld;   // last frame's world (motion vectors)
    float4             Tint;
    uint               BoneBase;      // first bone matrix for this draw (skinning); 0 otherwise
    uint               PrevBoneBase;  // last frame's bone base (skinned motion vectors)
    uint2              _objPad;
};
#endif
struct VSInput {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 color    : TEXCOORD3;
    float3 tangent  : TEXCOORD4;
#ifdef INSTANCED
    uint4  dataOffsets : TEXCOORD5;   // .x = index into Instances[] (instance-stepped)
#endif
#ifdef SKINNED
    // Locations 6/7: skinned draws are always instanced, so dataOffsets (declared above) takes 5 and
    // DXC assigns these sequentially to 6/7. The skin stream's attribute layout matches.
    uint2  jointsPacked : TEXCOORD6;  // 4x u16 bone indices packed into 2x u32
    float4 weights      : TEXCOORD7;  // bone weights (sum 1)
#endif
};
struct VSOutput {
    float4 clip      : SV_Position;
    float3 normalWS  : TEXCOORD0;
    float4 color     : TEXCOORD1;
    float2 uv        : TEXCOORD2;
    float3 tangentWS : TEXCOORD3;
    float3 worldPos  : TEXCOORD4;
    float4 curClip   : TEXCOORD5;   // unjittered current clip pos (motion vectors)
    float4 prevClip  : TEXCOORD6;   // unjittered previous clip pos (motion vectors)
};
VSOutput main(VSInput input) {
    VSOutput o;
#ifdef INSTANCED
    float4x4 world     = Instances[input.dataOffsets.x].World;
    float4x4 prevWorld = Instances[input.dataOffsets.x].PrevWorld;
    float4   tint      = Instances[input.dataOffsets.x].Tint;
#else
    float4x4 world     = World;
    float4x4 prevWorld = PrevWorld;
    float4   tint      = Tint;
#endif
    float3 lp = input.position;
    float3 ln = input.normal;
    float3 lt = input.tangent;
    float3 lpPrev = input.position;   // previous-frame local position (differs from lp only when skinned)
#ifdef SKINNED
    // Blend the four influencing bones (joint indices packed 4x u16 -> 2x u32) into a skin matrix. The
    // bone base is per-instance (DataOffsets.y) for the instanced path; the device pool holds [cur][prev]
    // per skeleton (DataOffsets.z = prev base, for motion vectors).
  #ifdef INSTANCED
    uint boneBase     = input.dataOffsets.y;
    uint prevBoneBase = input.dataOffsets.z;
  #else
    uint boneBase     = BoneBase;
    uint prevBoneBase = PrevBoneBase;
  #endif
    uint4 j = uint4(input.jointsPacked.x & 0xFFFFu, input.jointsPacked.x >> 16,
                    input.jointsPacked.y & 0xFFFFu, input.jointsPacked.y >> 16);
    float4x4 skin     = BlendBones(j, input.weights, boneBase);
    float4x4 skinPrev = BlendBones(j, input.weights, prevBoneBase);
    lpPrev = mul(float4(input.position, 1.0), skinPrev).xyz;   // deform with LAST frame's pose
    lp = mul(float4(lp, 1.0), skin).xyz;
    ln = mul(float4(ln, 0.0), skin).xyz;
    lt = mul(float4(lt, 0.0), skin).xyz;
#endif
    float4 worldPos     = mul(float4(lp, 1.0), world);
    float4 prevWorldPos = mul(float4(lpPrev, 1.0), prevWorld);
    o.clip      = mul(worldPos, ViewProj);
    o.normalWS  = normalize(mul(float4(ln, 0.0), world).xyz);
    o.color     = input.color * tint;                           // vertex color * per-instance tint
    o.uv        = input.uv;                                     // consume the full vertex layout
    o.tangentWS = mul(float4(lt, 0.0), world).xyz;
    o.worldPos  = worldPos.xyz;
    o.curClip   = o.clip;                                        // (jitter is baked into ViewProj; PS unjitters)
    o.prevClip  = mul(prevWorldPos, PrevViewProj);
    return o;
}
)");
}

[[nodiscard]] inline core::StringView ForwardPS() noexcept
{
    return core::StringView(u8R"(
#define CASCADE_COUNT 4
cbuffer View : register(b0, space0) {        // shared with the VS (same layout)
    row_major float4x4 ViewProj;
    row_major float4x4 View;
    row_major float4x4 CascadeViewProj[CASCADE_COUNT];
    float3 CameraPos; float LightCount;
    uint   LightOffset; int ClusterVpX; int ClusterVpY; float IBLMaxLod;   // IBLMaxLod < 0 -> no IBL (flat ambient)
    uint   ClusterGridX; uint ClusterGridY; uint ClusterSliceCount; uint ClusterTileSize;
    float  ClusterNear;  float ClusterFar;  float ClusterLogScale;  float ClusterLogBias;
    float3 Ambient; float ShadowCascadeCount;
    float4 CascadeSplitFar;
    float4 CascadeTexelSize;
    float  ShadowNormalBias; float ShadowDepthBias; float CascadeLayerBase; uint LocalShadowBase;
    row_major float4x4 PrevViewProj;   // (shared with VS; PS only reads Jitter)
    float4 Jitter;                     // xy = this frame's NDC jitter, zw = last frame's
    float4 ProbeCenter;                // xyz = reflection-probe center (world), w = probe count (0 = none)
    float4 ProbeBoxMin;                // xyz = probe box min corner,  w = probe cube slice (index into ProbeArray)
    float4 ProbeBoxMax;                // xyz = probe box max corner,  w = probe intensity
    float4 ShadowParams;               // x = CSM far-fade width in WORLD UNITS; yzw spare
};
struct GpuLight {                            // matches render::GpuLight (64 bytes)
    float3 positionWS; float range;
    float3 color;      float intensity;
    float3 directionWS;float type;           // 0=Directional, 1=Point, 2=Spot
    float innerCos; float outerCos; float shadowIndex; float pad1;   // shadowIndex >= 0 -> casts shadow
};
StructuredBuffer<GpuLight> Lights : register(t0, space0);
// CSM cascade depth ARRAY (t1, one layer per cascade) + a comparison sampler (s0) for hardware PCF.
Texture2DArray         ShadowMap     : register(t1, space0);
SamplerComparisonState ShadowSampler : register(s0, space0);

// IBL (phase 6): SH9 diffuse irradiance coeffs (t5), prefiltered specular cube (t6), BRDF LUT (t7),
// + a linear env sampler (s1). Diffuse uses spherical harmonics (no irradiance cube). Active only
// when IBLMaxLod >= 0 (else the neutral dummies are bound and the flat-ambient path runs).
StructuredBuffer<float4> IblSH       : register(t5, space0);
TextureCube              PrefilterMap : register(t6, space0);
Texture2D                BRDFLut      : register(t7, space0);
SamplerState             EnvSampler   : register(s1, space0);
// Reflection probes (P2-P4): a cube-ARRAY of prefiltered probe radiance (t8) + a probe metadata buffer
// (t9). ProbeCenter.w carries the probe COUNT; the forward loops Probes[0..count], box-tests + parallax-
// projects + blends each by an influence weight (blendDistance falloff) over the global IBL.
TextureCubeArray         ProbeArray   : register(t8, space0);
struct GpuProbe {
    float4 center;   // xyz = capture center, w = intensity
    float4 boxMin;   // xyz = box min,        w = blendDistance
    float4 boxMax;   // xyz = box max,        w = cube slice (array index)
    float4 params;   // x = mipCount, y = priority, z = parallax (0/1), w = pad
};
StructuredBuffer<GpuProbe> Probes : register(t9, space0);

// Evaluate the 9-coefficient SH irradiance in direction n (Ramamoorthi/Hanrahan cosine-convolved).
float3 EvalSH9(float3 n) {
    float3 r = IblSH[0].rgb * 0.886227;                       // l=0
    r += IblSH[1].rgb * (2.0 * 0.511664 * n.y);              // l=1
    r += IblSH[2].rgb * (2.0 * 0.511664 * n.z);
    r += IblSH[3].rgb * (2.0 * 0.511664 * n.x);
    r += IblSH[4].rgb * (2.0 * 0.429043 * n.x * n.y);        // l=2
    r += IblSH[5].rgb * (2.0 * 0.429043 * n.y * n.z);
    r += IblSH[6].rgb * (0.743125 * (3.0 * n.z * n.z - 1.0));
    r += IblSH[7].rgb * (2.0 * 0.429043 * n.x * n.z);
    r += IblSH[8].rgb * (0.429043 * (n.x * n.x - n.y * n.y));
    return max(r, 0.0);
}

static const float kShadowTexel = 1.0 / 1024.0;   // 1 / shadow resolution

// Sample one cascade with a normal-offset bias (scaled by the cascade's world texel size, fading at
// grazing angles) + 3x3 hardware PCF on its array layer. 1 = lit, 0 = fully shadowed.
float SampleCascade(int cascade, float3 worldPos, float3 N, float NdotL) {
    float texelWorld = CascadeTexelSize[cascade];
    // Normal-offset bias: push along the surface normal, scaled by the cascade's world texel size and
    // FADING TO ZERO as the surface faces the light (1 - NdotL). Face-on receivers get ~no offset (so
    // no visible gap at contacts); only grazing surfaces, where acne is worst, get the full push.
    float3 biasedPos = worldPos + N * (ShadowNormalBias * texelWorld * (1.0 - NdotL));
    float4 lc  = mul(float4(biasedPos, 1.0), CascadeViewProj[cascade]);
    if (lc.w <= 0.0) { return 1.0; }
    float3 ndc = lc.xyz / lc.w;
    float2 uv  = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { return 1.0; }
    float compareDepth = ndc.z - ShadowDepthBias;
    float layer = CascadeLayerBase + (float)cascade;   // this view's slice of the shared array
    float sum = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            sum += ShadowMap.SampleCmpLevelZero(ShadowSampler, float3(uv + float2(x, y) * kShadowTexel, layer), compareDepth);
        }
    }
    return sum * (1.0 / 9.0);
}

// Cascaded shadow: pick the cascade by view-space depth, sample it, and blend into the next cascade
// over the last 15% of the range (hides the cascade seam).
float SampleCSM(float3 worldPos, float3 N, float NdotL, float viewDepth) {
    int count = (int)ShadowCascadeCount;
    if (count <= 0) { return 1.0; }

    int cascade = count - 1;
    [unroll] for (int i = 0; i < CASCADE_COUNT; ++i) {
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

    // Far fade: dissolve shadow toward fully-lit as viewDepth approaches the last cascade's far edge
    // (the shadow distance). Without this the coverage boundary is a hard line -- on a tilted camera
    // it reads as a diagonal where directional shadows pop in/out while rotating. ShadowParams.x is a
    // fixed fade WIDTH in world units (distance-independent), so the soft edge is the same physical
    // thickness whatever the reach.
    float shadowFar = CascadeSplitFar[count - 1];
    float fadeBand  = max(ShadowParams.x, 0.5);
    float farFade   = saturate((shadowFar - viewDepth) / fadeBand);
    shadow = lerp(1.0, shadow, farFade);
    return shadow;
}

// Local-light (spot/point) shadows: a shared 2D depth ATLAS (t2) + per-light entries (t3). Each entry
// is a perspective world->light-clip matrix + the uv scale/bias of its tile in the atlas. Reuses the
// comparison sampler. GpuLight.shadowIndex selects the entry (point lights use 6 faces in 5.3b).
Texture2DArray ShadowAtlas : register(t2, space0);   // layer 0 = realtime, layer 1 = static (cached)
struct GpuLocalShadow {
    row_major float4x4 viewProj;
    float4 atlasScaleBias;       // xy = uv scale, zw = uv offset
    float  depthBias; float atlasSelect; float2 _localPad;   // atlasSelect = atlas array layer
};
StructuredBuffer<GpuLocalShadow> LocalShadows : register(t3, space0);

static const float kAtlasTexel = 1.0 / 2048.0;   // 1 / atlas resolution

// Sample one local-shadow entry: project into its light clip, map the clip uv into the entry's atlas
// tile (on its array layer), 3x3 PCF. 1 = lit, 0 = shadowed. Acne is on the caster-side depth bias.
float SampleLocalShadow(int idx, float3 worldPos) {
    GpuLocalShadow s = LocalShadows[idx];
    if (s.atlasScaleBias.x <= 0.0) { return 1.0; }   // degenerate entry (no atlas tile) -> unshadowed
    float4 lc = mul(float4(worldPos, 1.0), s.viewProj);
    if (lc.w <= 0.0) { return 1.0; }
    float3 ndc = lc.xyz / lc.w;
    float2 uv  = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z <= 0.0 || ndc.z >= 1.0) { return 1.0; }
    float2 atlasUV = uv * s.atlasScaleBias.xy + s.atlasScaleBias.zw;
    float compareDepth = ndc.z - s.depthBias;
    float sum = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            sum += ShadowAtlas.SampleCmpLevelZero(ShadowSampler, float3(atlasUV + float2(x, y) * kAtlasTexel, s.atlasSelect), compareDepth);
        }
    }
    return sum * (1.0 / 9.0);
}

// Pick a point light's cube face (0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z) from the light->fragment direction
// - the dominant axis. Matches BuildPointShadowFace's face ordering.
int CubeFace(float3 dir) {
    float3 a = abs(dir);
    if (a.x >= a.y && a.x >= a.z) { return dir.x > 0.0 ? 0 : 1; }
    if (a.y >= a.z)               { return dir.y > 0.0 ? 2 : 3; }
    return dir.z > 0.0 ? 4 : 5;
}

// Shadow attenuation for a shadowed light (caller checks shadowIndex >= 0): directional -> CSM,
// point -> the cube face of the atlas, spot -> the single atlas tile.
float ShadowFactor(GpuLight L, float3 worldPos, float3 N, float viewDepth) {
    if (L.type < 0.5) { return SampleCSM(worldPos, N, saturate(dot(N, -L.directionWS)), viewDepth); }   // directional
    int base = (int)LocalShadowBase + (int)L.shadowIndex;
    if (L.type < 1.5) { return SampleLocalShadow(base + CubeFace(worldPos - L.positionWS), worldPos); } // point (6 faces)
    return SampleLocalShadow(base, worldPos);                                                            // spot (1 tile)
}

// Clustered light culling (set 3): per-cluster (offset,count) + the flat light-index list. When
// ClusterGridX == 0 (clustering unavailable) the shader falls back to looping all lights.
StructuredBuffer<uint2> ClusterOffsets      : register(t0, space3);
StructuredBuffer<uint>  ClusterLightIndices : register(t1, space3);

// Maps a fragment's screen position + positive view-space depth to a linear cluster index.
uint ClusterIndex(float2 screenPos, float viewDepth) {
    // SV_Position is in full-target pixels; the grid is viewport-local, so subtract the offset.
    float2 local = screenPos - float2((float)ClusterVpX, (float)ClusterVpY);
    uint tileX = (uint)local.x / ClusterTileSize;
    float screenH = (float)(ClusterGridY * ClusterTileSize);
    uint tileY = (uint)((screenH - local.y) / ClusterTileSize);   // flip Y (SV_Position y=0 at top)
    tileX = min(tileX, ClusterGridX - 1);
    tileY = min(tileY, ClusterGridY - 1);
    float logDepth = log(max(viewDepth, ClusterNear));
    int slice = (int)(logDepth * ClusterLogScale + ClusterLogBias);
    slice = clamp(slice, 0, (int)ClusterSliceCount - 1);
    return tileX + tileY * ClusterGridX + (uint)slice * ClusterGridX * ClusterGridY;
}
cbuffer Material : register(b0, space2) {    // data-driven PBR material (inferred from properties)
    float4 BaseColor;
    float  Metallic;
    float  Roughness;
    float  _matPad0;
    float  _matPad1;
};
// Standard PBR material maps (the fixed forward set-2 contract, Sedulous-aligned). Unset maps bind a
// neutral default (white for albedo/MR/AO, flat normal) so untextured materials are unaffected.
// Sampled now: albedo, metallic-roughness (glTF: G=roughness, B=metallic), occlusion. Normal-map and
// emissive are bound (importer can populate) but not yet sampled.
Texture2D    AlbedoMap            : register(t0, space2);
Texture2D    NormalMap            : register(t1, space2);
Texture2D    MetallicRoughnessMap : register(t2, space2);
Texture2D    OcclusionMap         : register(t3, space2);
Texture2D    EmissiveMap          : register(t4, space2);
SamplerState MainSampler          : register(s0, space2);
struct PSInput {
    float4 clip      : SV_Position;
    float3 normalWS  : TEXCOORD0;
    float4 color     : TEXCOORD1;
    float2 uv        : TEXCOORD2;
    float3 tangentWS : TEXCOORD3;
    float3 worldPos  : TEXCOORD4;
    float4 curClip   : TEXCOORD5;   // motion vectors (unjittered current/previous clip pos)
    float4 prevClip  : TEXCOORD6;
};

static const float PI = 3.14159265359;

// GGX normal distribution function.
float DistributionGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * d * d);
}
// Height-correlated Smith GGX visibility (Karis 2013) - folds in 1/(4*NdotV*NdotL).
float VisibilitySmithGGX(float NdotV, float NdotL, float roughness) {
    float a = roughness * roughness;
    float lambdaV = NdotL * (NdotV * (1.0 - a) + a);
    float lambdaL = NdotV * (NdotL * (1.0 - a) + a);
    return 0.5 / (lambdaV + lambdaL + 1e-5);
}
// Fresnel-Schlick with an F90 firefly clamp (limits grazing specular on low-F0 surfaces).
float3 FresnelSchlick(float cosTheta, float3 F0) {
    float f   = pow(saturate(1.0 - cosTheta), 5.0);
    float F90 = saturate(50.0 * dot(F0, float3(0.2126, 0.7152, 0.0722)));
    return F0 + (F90 - F0) * f;
}
// Range-windowed inverse-square attenuation.
float Attenuation(float dist, float range) {
    if (range <= 0.0) return 1.0;
    float d  = dist / range;
    float d2 = d * d;
    float win = saturate(1.0 - d2 * d2);
    return (win * win) / (dist * dist + 1e-4);
}
float SpotAttenuation(float3 L, float3 spotDir, float innerCos, float outerCos) {
    float cosA = dot(-L, spotDir);
    return saturate((cosA - outerCos) / (innerCos - outerCos + 1e-4));
}
// Cook-Torrance evaluation for a single light.
float3 EvaluateLight(GpuLight light, float3 worldPos, float3 N, float3 V,
                     float3 albedo, float roughness, float metallic, float3 F0) {
    float3 L; float atten = 1.0;
    if (light.type < 0.5) {                                    // directional
        L = -light.directionWS;
    } else {                                                   // point / spot
        float3 toLight = light.positionWS - worldPos;
        float  dist    = length(toLight);
        L = toLight / max(dist, 1e-4);
        atten = Attenuation(dist, light.range);
        if (light.type > 1.5) {                                // spot cone
            atten *= SpotAttenuation(L, light.directionWS, light.innerCos, light.outerCos);
        }
    }
    float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0) { return float3(0.0, 0.0, 0.0); }

    float3 H     = normalize(V + L);
    float  NdotH = saturate(dot(N, H));
    float  NdotV = max(dot(N, V), 1e-3);
    float  HdotV = saturate(dot(H, V));

    float  D   = DistributionGGX(NdotH, roughness);
    float  Vis = VisibilitySmithGGX(NdotV, NdotL, roughness);
    float3 F   = FresnelSchlick(HdotV, F0);
    float3 specular = D * Vis * F;                             // D*Vis already includes 1/(4*NdotV*NdotL)

    float3 kD      = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * (light.color * light.intensity) * NdotL * atten;
}

// Octahedral encode a unit vector -> [-1,1]^2 (full sphere, no sign ambiguity). Used to pack the
// view-space normal into the RG16F G-buffer target for the post stack (GTAO/TAA).
float2 OctEncode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = n.xy;
    if (n.z < 0.0) { e = (1.0 - abs(float2(e.y, e.x))) * float2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0); }
    return e;
}

// Forward outputs. GBUFFER (opaque/masked MRT pass): shaded color + view-normal + motion vector.
// Otherwise (the color-only transparent pass): just the blended color.
#ifdef GBUFFER
struct PSOutput {
    float4 color    : SV_Target0;   // shaded HDR (tonemapped later)
    float2 normal   : SV_Target1;   // octahedral view-space normal
    float2 velocity : SV_Target2;   // screen-space motion vector (UV delta)
    float2 material : SV_Target3;   // R=roughness, G=metallic (for SSR)
};
PSOutput main(PSInput input) {
#else
float4 main(PSInput input) : SV_Target0 {
#endif
    float3 N = normalize(input.normalWS);
    float3 V = normalize(CameraPos - input.worldPos);

    float4 albedoTex = AlbedoMap.Sample(MainSampler, input.uv);
    float3 albedo    = input.color.rgb * BaseColor.rgb * albedoTex.rgb;
    float  alpha     = saturate(input.color.a * BaseColor.a * albedoTex.a);   // surface opacity (alpha blend)
#ifdef ALPHA_TEST
    // Masked geometry: cut out sub-cutoff fragments before shading (skips lighting + writes no depth/
    // G-buffer for the hole). 0.5 matches the glTF alpha-cutoff default.
    if (alpha < 0.5) { discard; }
#endif
    float2 mr        = MetallicRoughnessMap.Sample(MainSampler, input.uv).gb;   // glTF: G=roughness, B=metallic
    float  metallic  = saturate(Metallic * mr.y);
    float  roughness = clamp(Roughness * mr.x, 0.045, 1.0);
    float3 F0        = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float3 viewPos   = mul(float4(input.worldPos, 1.0), View).xyz;
    float  viewDepth = -viewPos.z;                             // cascade selection + cluster lookup

    float3 Lo = float3(0.0, 0.0, 0.0);
    if (ClusterGridX == 0) {
        // Clustering unavailable - evaluate every light.
        uint count = (uint)LightCount;
        for (uint i = 0; i < count; ++i) {
            GpuLight L = Lights[LightOffset + i];
            float3 c = EvaluateLight(L, input.worldPos, N, V, albedo, roughness, metallic, F0);
            if (L.shadowIndex >= 0.0) { c *= ShadowFactor(L, input.worldPos, N, viewDepth); }
            Lo += c;
        }
    } else {
        // Clustered - evaluate only the lights binned into this fragment's cluster.
        uint   cluster = ClusterIndex(input.clip.xy, viewDepth);
        uint2  oc = ClusterOffsets[cluster];
        for (uint ci = 0; ci < oc.y; ++ci) {
            uint li = ClusterLightIndices[oc.x + ci];
            GpuLight L = Lights[LightOffset + li];
            float3 c = EvaluateLight(L, input.worldPos, N, V, albedo, roughness, metallic, F0);
            if (L.shadowIndex >= 0.0) { c *= ShadowFactor(L, input.worldPos, N, viewDepth); }
            Lo += c;
        }
    }

    float  ao = OcclusionMap.Sample(MainSampler, input.uv).r;
    float3 ambient;
    if (IBLMaxLod >= 0.0) {
        // Image-based ambient: SH9 diffuse irradiance + split-sum prefiltered specular.
        float  NdotV = max(dot(N, V), 1e-4);
        float3 Fr    = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0;
        float3 F_ibl = F0 + Fr * pow(1.0 - NdotV, 5.0);          // roughness-aware indirect Fresnel
        float3 kD    = (1.0 - F_ibl) * (1.0 - metallic);
        float3 diffuseIBL = kD * albedo * (EvalSH9(N) / PI);     // EvalSH9 -> irradiance E; Lambertian = albedo/pi * E
        float3 R     = reflect(-V, N);
        float3 prefiltered = PrefilterMap.SampleLevel(EnvSampler, R, roughness * IBLMaxLod).rgb;
        // Reflection probes (P4): loop the active probes, and for each whose box contains the fragment,
        // box-project the reflection ray (parallax) + sample its prefiltered cube at roughness*maxLod, then
        // blend all by an influence weight that fades to 0 over blendDistance near the box edge (soft seam).
        // The accumulated probe reflection blends over the global IBL by the total weight (parallax = Lagarde
        // box-projected cubemap; a cube captured from a point tracks geometry only after this projection).
        uint probeBase  = (uint)ProbeCenter.x;   // this view's scene's records (multi-scene frames)
        uint probeCount = (uint)ProbeCenter.w;
        float3 probeAccum = float3(0, 0, 0);
        float  probeWeight = 0.0;
        for (uint pi = 0u; pi < probeCount; ++pi) {
            GpuProbe pr = Probes[probeBase + pi];
            float3 bmin = pr.boxMin.xyz, bmax = pr.boxMax.xyz;
            // Influence: distance to the nearest box face (negative outside) -> fade over blendDistance.
            float3 d = min(input.worldPos - bmin, bmax - input.worldPos);
            float  edge = min(min(d.x, d.y), d.z);
            float  w = saturate(edge / max(pr.boxMin.w, 1e-3));
            if (w <= 0.0) { continue; }
            float3 Rp = R;                                              // no-parallax: raw reflect (infinite env)
            if (pr.params.z > 0.5) {
                float3 invR = 1.0 / R;                                  // R==0 on an axis -> +-inf, handled by max/min
                float3 tMin = (bmin - input.worldPos) * invR;
                float3 tMax = (bmax - input.worldPos) * invR;
                float  dist = min(min(max(tMin.x, tMax.x), max(tMin.y, tMax.y)), max(tMin.z, tMax.z));
                Rp = (input.worldPos + R * dist) - pr.center.xyz;     // re-aim from the capture center
            }
            float3 spec = ProbeArray.SampleLevel(EnvSampler, float4(Rp, pr.boxMax.w), roughness * 4.0).rgb * pr.center.w;
            probeAccum += w * spec;
            probeWeight += w;
        }
        if (probeWeight > 0.0) {
            float3 probeSpec = probeAccum / probeWeight;               // weighted blend across overlapping probes
            prefiltered = lerp(prefiltered, probeSpec, saturate(probeWeight));   // fade to global IBL at box edges
        }
        float2 brdf  = BRDFLut.Sample(EnvSampler, float2(NdotV, roughness)).rg;
        float3 specularIBL = prefiltered * (F_ibl * brdf.x + brdf.y);
        float  Ess   = brdf.x + brdf.y;                          // multi-scatter energy compensation
        specularIBL *= 1.0 + F0 * (1.0 / max(Ess, 1e-3) - 1.0);  // (Kulla-Conty) restore single-scatter's lost energy
        // The authored flat ambient ADDS as a fill term in every sky mode (intensity 0 = pure
        // IBL) - it is not an either/or with the environment.
        ambient = (diffuseIBL + specularIBL + albedo * Ambient) * ao;
    } else {
        ambient = albedo * Ambient * ao;                         // flat-only fallback (no IBL available)
    }
#ifdef GBUFFER
    // Motion vector: current vs previous screen position, both UNJITTERED so only geometric motion
    // remains (else the TAA reprojection wobbles with the jitter). The jitter added to projection(2,0/1)
    // shifts NDC by -Jitter (RH: clip.w = -viewZ), so we ADD Jitter back to recover the geometric NDC.
    // NDC.y is flipped vs UV.y, hence the (0.5, -0.5) scale.
    float2 curNDC  = input.curClip.xy  / input.curClip.w  + Jitter.xy;
    float2 prevNDC = input.prevClip.xy / input.prevClip.w + Jitter.zw;
    float2 velocity = (curNDC - prevNDC) * float2(0.5, -0.5);

    PSOutput o;
    o.color    = float4(ambient + Lo, alpha);
    o.normal   = OctEncode(normalize(mul(float4(N, 0.0), View).xyz));   // view-space normal (octahedral)
    o.velocity = velocity;
    o.material = float2(roughness, metallic);   // SSR reads these to gate/fade reflections
    return o;
#else
    return float4(ambient + Lo, alpha);   // color-only (transparent pass): alpha drives AlphaBlend
#endif
}
)");
}

// Depth-only shadow caster shader (vertex stage ONLY - the depth-only pipeline omits the
// fragment). Transforms each vertex by world * LightViewProj into the light's clip space, so the
// shadow pass writes light-space depth. Two permutations (INSTANCED or not) mirror the forward VS's
// world-matrix source: a per-object UBO (set 1) or the per-instance StructuredBuffer (set 1).
[[nodiscard]] inline core::StringView ShadowVS() noexcept
{
    return core::StringView(u8R"(
cbuffer ShadowView : register(b0, space0) {
    row_major float4x4 LightViewProj;
};
#ifdef SKINNED
// Same skinning pool as the forward path (set-0 t4 SRV); skinned casters deform their shadow too.
struct BoneMatrix { float4 Row0, Row1, Row2, Row3; };
StructuredBuffer<BoneMatrix> BoneMatrices : register(t4, space0);
float4x4 BlendBones(uint4 j, float4 w, uint base) {
    BoneMatrix b0 = BoneMatrices[base + j.x];
    BoneMatrix b1 = BoneMatrices[base + j.y];
    BoneMatrix b2 = BoneMatrices[base + j.z];
    BoneMatrix b3 = BoneMatrices[base + j.w];
    return float4x4(b0.Row0 * w.x + b1.Row0 * w.y + b2.Row0 * w.z + b3.Row0 * w.w,
                    b0.Row1 * w.x + b1.Row1 * w.y + b2.Row1 * w.z + b3.Row1 * w.w,
                    b0.Row2 * w.x + b1.Row2 * w.y + b2.Row2 * w.z + b3.Row2 * w.w,
                    b0.Row3 * w.x + b1.Row3 * w.y + b2.Row3 * w.z + b3.Row3 * w.w);
}
#endif
// Layouts mirror the forward path's Object/InstanceData exactly (shared C++ ring buffers) - the extra
// PrevWorld/PrevBoneBase fields keep the strides/offsets aligned even though the depth pass ignores them.
#ifdef INSTANCED
struct InstanceData { row_major float4x4 World; row_major float4x4 PrevWorld; float4 Tint; };
StructuredBuffer<InstanceData> Instances : register(t0, space1);
#else
cbuffer Object : register(b0, space1) {
    row_major float4x4 World;
    row_major float4x4 PrevWorld;
    float4             Tint;
    uint               BoneBase;
    uint               PrevBoneBase;
    uint2              _objPad;
};
#endif
struct VSInput {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 color    : TEXCOORD3;
    float3 tangent  : TEXCOORD4;
#ifdef INSTANCED
    uint4  dataOffsets : TEXCOORD5;   // .x = index into Instances[] (instance-stepped)
#endif
#ifdef SKINNED
    uint2  jointsPacked : TEXCOORD6;  // locations 6/7 (dataOffsets took 5; skinned is always instanced)
    float4 weights      : TEXCOORD7;
#endif
};
// ALPHA_TEST (masked casters): pass UV so the fragment can sample the cutout alpha. Otherwise the
// depth pass is vertex-only (no fragment) and outputs just clip position.
#ifdef ALPHA_TEST
struct ShadowVSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
ShadowVSOut main(VSInput input) {
    ShadowVSOut o;
#else
float4 main(VSInput input) : SV_Position {
#endif
#ifdef INSTANCED
    float4x4 world = Instances[input.dataOffsets.x].World;
#else
    float4x4 world = World;
#endif
    float3 lp = input.position;
#ifdef SKINNED
  #ifdef INSTANCED
    uint boneBase = input.dataOffsets.y;
  #else
    uint boneBase = BoneBase;
  #endif
    uint4 j = uint4(input.jointsPacked.x & 0xFFFFu, input.jointsPacked.x >> 16,
                    input.jointsPacked.y & 0xFFFFu, input.jointsPacked.y >> 16);
    lp = mul(float4(lp, 1.0), BlendBones(j, input.weights, boneBase)).xyz;
#endif
    float4 worldPos = mul(float4(lp, 1.0), world);
#ifdef ALPHA_TEST
    o.pos = mul(worldPos, LightViewProj);
    o.uv  = input.uv;
    return o;
#else
    return mul(worldPos, LightViewProj);
#endif
}
)");
}

// Masked shadow fragment: sample the material's albedo cutout alpha + discard, so alpha-tested casters
// (foliage/fences) drop holey shadows. Depth-only (no color target); pairs with the ALPHA_TEST VS.
[[nodiscard]] inline core::StringView ShadowMaskedPS() noexcept
{
    return core::StringView(u8R"(
Texture2D    AlbedoMap   : register(t0, space2);
SamplerState MainSampler : register(s0, space2);
void main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    if (AlbedoMap.Sample(MainSampler, uv).a < 0.5) { discard; }
}
)");
}

}

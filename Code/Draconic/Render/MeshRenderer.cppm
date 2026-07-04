/// Draconic::Render — the `:mesh_renderer` partition.
///
/// `MeshRenderer` is the `Renderer` for the mesh categories (Opaque/Masked/Transparent). It
/// owns the built-in forward shader (two permutations), the GPU rings, and the mesh cache,
/// and records draws for the mesh `DrawItem`s it is handed.
///
/// HYBRID instancing (§8): a run of consecutive draws sharing one mesh + material is issued as
/// a single INSTANCED draw; a lone draw takes the simpler per-object path. Both read the
/// view-projection from a shared per-view UBO (set 0). The per-object path adds a per-object
/// UBO (set 1, world + tint, dynamic offset); the instanced path adds a per-instance
/// `StructuredBuffer<InstanceData>` (set 1) indexed by a uint4 DataOffsets vertex stream
/// (location 5, instance-stepped) — the portable base+offset addressing (NOT SV_InstanceID,
/// which differs between DX12 and Vulkan). Transparent draws are never instanced (back-to-front
/// order must dominate). Material set-2 binding + real lighting are later phases.

module;
#include "Core/Prelude.h"

export module draconic.render:mesh_renderer;

import draconic.core;
import draconic.rhi;
import draconic.geometry;
import draconic.shaders;
import draconic.shaders.system;
import draconic.materials;
import draconic.materials.pso;
import :data;
import :views;
import :pipeline;
import :cluster_system;
import :resources;
import :gpu_mesh;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Built-in forward shader, two permutations selected by INSTANCED. The view-projection is in a
// per-view UBO (set 0); the world matrix + tint come either from a per-object UBO (set 1) or,
// in the instanced permutation, from a per-instance StructuredBuffer indexed by the uint4
// DataOffsets vertex attribute (location 5). Vertex inputs use the RHI's TEXCOORDn convention
// (semantic index = location), matching VertexLayoutType::Mesh at locations 0..4.
inline constexpr const char8_t* kForwardVS = u8R"(
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
// Stored as 4 explicit float4 ROWS (Sedulous-faithful) so the major-ness is unambiguous — DXC's
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
)";

inline constexpr const char8_t* kForwardPS = u8R"(
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
// — the dominant axis. Matches BuildPointShadowFace's face ordering.
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
// Height-correlated Smith GGX visibility (Karis 2013) — folds in 1/(4*NdotV*NdotL).
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
        // Clustering unavailable — evaluate every light.
        uint count = (uint)LightCount;
        for (uint i = 0; i < count; ++i) {
            GpuLight L = Lights[LightOffset + i];
            float3 c = EvaluateLight(L, input.worldPos, N, V, albedo, roughness, metallic, F0);
            if (L.shadowIndex >= 0.0) { c *= ShadowFactor(L, input.worldPos, N, viewDepth); }
            Lo += c;
        }
    } else {
        // Clustered — evaluate only the lights binned into this fragment's cluster.
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
        uint probeCount = (uint)ProbeCenter.w;
        float3 probeAccum = float3(0, 0, 0);
        float  probeWeight = 0.0;
        for (uint pi = 0u; pi < probeCount; ++pi) {
            GpuProbe pr = Probes[pi];
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
        ambient = (diffuseIBL + specularIBL) * ao;
    } else {
        ambient = albedo * Ambient * ao;                         // flat fallback (no environment active)
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
)";

// Depth-only shadow caster shader (vertex stage ONLY — the depth-only pipeline omits the
// fragment). Transforms each vertex by world * LightViewProj into the light's clip space, so the
// shadow pass writes light-space depth. Two permutations (INSTANCED or not) mirror the forward VS's
// world-matrix source: a per-object UBO (set 1) or the per-instance StructuredBuffer (set 1).
inline constexpr const char8_t* kShadowVS = u8R"(
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
// Layouts mirror the forward path's Object/InstanceData exactly (shared C++ ring buffers) — the extra
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
)";

// Masked shadow fragment: sample the material's albedo cutout alpha + discard, so alpha-tested casters
// (foliage/fences) drop holey shadows. Depth-only (no color target); pairs with the ALPHA_TEST VS.
inline constexpr const char8_t* kShadowMaskedPS = u8R"(
Texture2D    AlbedoMap   : register(t0, space2);
SamplerState MainSampler : register(s0, space2);
void main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    if (AlbedoMap.Sample(MainSampler, uv).a < 0.5) { discard; }
}
)";

class MeshRenderer final : public Renderer {
public:
    MeshRenderer(rhi::Device& device, shaders::ShaderSystem& shaderSystem,
                 materials::PipelineStateCache& psoCache, materials::MaterialSystem& materialSystem,
                 u32 framesInFlight) noexcept
        : m_device(&device), m_shaders(&shaderSystem), m_psoCache(&psoCache),
          m_materials(&materialSystem), m_meshes(device),
          m_framesInFlight(framesInFlight < 1 ? 1 : framesInFlight),
          m_viewRing(device, framesInFlight, kViewDataSlot, rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"mesh.view"),
          m_shadowViewRing(device, framesInFlight, kViewSlot, rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"mesh.shadowView"),
          m_objectRing(device, framesInFlight, kViewSlot, rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"mesh.object"),
          m_instanceRing(device, framesInFlight, sizeof(InstanceData), rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst, u8"mesh.instances"),
          m_offsetsRing(device, framesInFlight, sizeof(DataOffsets), rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst, u8"mesh.offsets"),
          m_lightRing(device, framesInFlight, sizeof(GpuLight), rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst, u8"mesh.lights"),
          m_localShadowRing(device, framesInFlight, sizeof(GpuLocalShadow), rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst, u8"mesh.localShadows"),
          m_boneRing(device, framesInFlight, sizeof(Mat4), rhi::BufferUsage::Storage | rhi::BufferUsage::CopySrc, u8"mesh.bones.staging") {}

    ~MeshRenderer() override { Shutdown(); }

    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    // Registers the forward shader + creates the bind-group / pipeline layouts.
    Status Initialize() {
        m_shaders->RegisterSource(u8"forward", shaders::ShaderStage::Vertex,   kForwardVS);
        m_shaders->RegisterSource(u8"forward", shaders::ShaderStage::Fragment, kForwardPS);

        // set 0: per-view UBO (ViewProj + camera + light range), dynamic offset, Vertex|Fragment;
        // + the light list as a read-only StructuredBuffer (Fragment), bound whole.
        rhi::BindGroupLayoutEntry viewEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
        viewEntry.hasDynamicOffset = true;
        rhi::BindGroupLayoutEntry lightEntry = rhi::BindGroupLayoutEntry::StorageBuffer(0, rhi::ShaderStage::Fragment, /*readOnly*/ true);
        // Directional shadow map (t1) + comparison sampler (s0) live in set 0 (the bind-group budget
        // is 4 SETS, not 4 bindings — shadows fold into the view set rather than needing a 5th set).
        rhi::BindGroupLayoutEntry shadowTexEntry = rhi::BindGroupLayoutEntry::SampledTexture(1, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::Texture2DArray);
        // Local-light (spot/point) shadow atlas (t2, Texture2D) + per-light shadow entries (t3, SRV).
        rhi::BindGroupLayoutEntry atlasTexEntry = rhi::BindGroupLayoutEntry::SampledTexture(2, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::Texture2DArray);
        rhi::BindGroupLayoutEntry localShadowEntry = rhi::BindGroupLayoutEntry::StorageBuffer(3, rhi::ShaderStage::Fragment, /*readOnly*/ true);
        rhi::BindGroupLayoutEntry shadowSampEntry{};
        shadowSampEntry.binding = 0; shadowSampEntry.visibility = rhi::ShaderStage::Fragment;
        shadowSampEntry.type = rhi::BindingType::ComparisonSampler;
        // t4: the GPU skinning bone-matrix pool (Vertex-visible SRV). Bound on every set-0 BG; the
        // forward VS only reads it under the SKINNED permutation.
        rhi::BindGroupLayoutEntry boneEntry = rhi::BindGroupLayoutEntry::StorageBuffer(4, rhi::ShaderStage::Vertex, /*readOnly*/ true);
        // IBL (phase 6) folds into set 0 too: SH9 diffuse coeffs (t5, SRV), prefiltered specular cube
        // (t6), BRDF LUT (t7), + a linear-clamp env sampler (s1, distinct from the comparison sampler s0).
        rhi::BindGroupLayoutEntry iblShEntry = rhi::BindGroupLayoutEntry::StorageBuffer(5, rhi::ShaderStage::Fragment, /*readOnly*/ true);
        rhi::BindGroupLayoutEntry prefilterEntry = rhi::BindGroupLayoutEntry::SampledTexture(6, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::TextureCube);
        rhi::BindGroupLayoutEntry brdfEntry = rhi::BindGroupLayoutEntry::SampledTexture(7, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::Texture2D);
        rhi::BindGroupLayoutEntry envSampEntry = rhi::BindGroupLayoutEntry::Sampler(1, rhi::ShaderStage::Fragment);
        // Reflection probes: a cube-ARRAY of local probe radiance (t8) + a probe metadata buffer (t9, SRV).
        rhi::BindGroupLayoutEntry probeEntry    = rhi::BindGroupLayoutEntry::SampledTexture(8, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::TextureCubeArray);
        rhi::BindGroupLayoutEntry probeBufEntry = rhi::BindGroupLayoutEntry::StorageBuffer(9, rhi::ShaderStage::Fragment, /*readOnly*/ true);
        rhi::BindGroupLayoutEntry set0[] = { viewEntry, lightEntry, shadowTexEntry, atlasTexEntry, localShadowEntry,
                                             shadowSampEntry, boneEntry, iblShEntry, prefilterEntry, brdfEntry, envSampEntry, probeEntry, probeBufEntry };
        rhi::BindGroupLayoutDesc s0d{};
        s0d.entries = Span<const rhi::BindGroupLayoutEntry>{ set0, 13 };
        if (!m_device->CreateBindGroupLayout(s0d, m_viewLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        // set 1 (non-instanced): per-object UBO (World + Tint), dynamic offset.
        rhi::BindGroupLayoutEntry objEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex);
        objEntry.hasDynamicOffset = true;
        if (!MakeLayout(objEntry, m_objectLayout)) { return Status{ ErrorCode::Unknown }; }

        // set 1 (instanced): per-instance StructuredBuffer (read-only storage).
        rhi::BindGroupLayoutEntry instEntry = rhi::BindGroupLayoutEntry::StorageBuffer(0, rhi::ShaderStage::Vertex, /*readOnly*/ true);
        if (!MakeLayout(instEntry, m_instanceLayout)) { return Status{ ErrorCode::Unknown }; }

        // set 2 (material) is NOT a fixed renderer layout: it is derived per material from the
        // material's own property list (materials::MaterialSystem::GetOrCreateLayout) and the forward
        // pipeline layout is assembled per set-2 layout (GetOrCreatePipelineLayout). This is what makes
        // the renderer truly material-driven — a custom shader (e.g. toon) with a different property set
        // gets its own set-2 layout + PSO with zero renderer changes. The set-0/1/3 layouts below are
        // the stable "frame contract" every material plugs into.

        // set 3: clustered light lists — per-cluster (offset,count) SRV (t0) + flat index SRV (t1).
        rhi::BindGroupLayoutEntry clOffEntry = rhi::BindGroupLayoutEntry::StorageBuffer(0, rhi::ShaderStage::Fragment, /*readOnly*/ true);
        rhi::BindGroupLayoutEntry clIdxEntry = rhi::BindGroupLayoutEntry::StorageBuffer(1, rhi::ShaderStage::Fragment, /*readOnly*/ true);
        rhi::BindGroupLayoutEntry set3[] = { clOffEntry, clIdxEntry };
        rhi::BindGroupLayoutDesc s3d{};
        s3d.entries = Span<const rhi::BindGroupLayoutEntry>{ set3, 2 };
        if (!m_device->CreateBindGroupLayout(s3d, m_clusterLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        // Forward pipeline layouts (view + object/instance + material set-2 + cluster) are built
        // on demand per material set-2 layout and cached in GetOrCreatePipelineLayout.

        // Shadow depth-only path (phase 5): a vertex-only shader + a 2-set pipeline layout
        // (set 0 = light view UBO, set 1 = the SAME object/instance layouts as forward, so the
        // object/instance bind groups are reused). No material/cluster sets.
        m_shaders->RegisterSource(u8"shadow_depth", shaders::ShaderStage::Vertex, kShadowVS);
        m_shaders->RegisterSource(u8"shadow_depth", shaders::ShaderStage::Fragment, kShadowMaskedPS);   // masked casters (alpha-test)
        rhi::BindGroupLayoutEntry shadowViewEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex);
        shadowViewEntry.hasDynamicOffset = true;
        // set 0 also carries the skinning bone-matrix pool (t4) so skinned casters deform their shadow.
        rhi::BindGroupLayoutEntry shadowBoneEntry = rhi::BindGroupLayoutEntry::StorageBuffer(4, rhi::ShaderStage::Vertex, /*readOnly*/ true);
        rhi::BindGroupLayoutEntry shadowSet0[] = { shadowViewEntry, shadowBoneEntry };
        rhi::BindGroupLayoutDesc svd{};
        svd.entries = Span<const rhi::BindGroupLayoutEntry>{ shadowSet0, 2 };
        if (!m_device->CreateBindGroupLayout(svd, m_shadowViewLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }
        if (!MakePipelineLayout(m_shadowViewLayout, m_objectLayout,   m_shadowPipelineLayoutSingle))    { return Status{ ErrorCode::Unknown }; }
        if (!MakePipelineLayout(m_shadowViewLayout, m_instanceLayout, m_shadowPipelineLayoutInstanced)) { return Status{ ErrorCode::Unknown }; }

        if (!CreateDefaultMaterial().IsOk()) { return Status{ ErrorCode::Unknown }; }
        if (!CreateShadowResources().IsOk()) { return Status{ ErrorCode::Unknown }; }
        return CreateDummyClusters();
    }

    // The directional shadow map sampled in the forward shader this frame (the ShadowSystem's
    // texture when a caster exists, else null -> the 1x1 dummy). Set by RenderFrame before PrepareFrame.
    void SetShadowMap(rhi::TextureView* view, u64 generation) override {
        m_activeShadowView = (view != nullptr) ? view : m_dummyShadowView;
        m_activeShadowGen  = (view != nullptr) ? generation : 0;   // dummy never changes
    }

    // The local-light (spot/point) shadow atlas sampled this frame (the ShadowSystem's atlas when any
    // local caster exists, else null -> the 1x1 dummy). Set by RenderFrame before PrepareFrame.
    void SetShadowAtlas(rhi::TextureView* view, u64 generation, u32 passCount) override {
        m_activeAtlasView = (view != nullptr) ? view : m_dummyAtlasView;
        m_activeAtlasGen  = (view != nullptr) ? generation : 0;
        m_localShadowPassCount = (view != nullptr) ? passCount : 0;   // each tile re-emits the casters
    }

    // Reflection-probe capture faces re-emit the draws (one forward pass each) — count them into the ring.
    void SetCaptureFacePasses(u32 passes) override { m_captureFacePasses = passes; }

    // This frame's active reflection probe (P2, single probe): the captured cube-ARRAY view (set-0 t8) +
    // the probe's box/slice/intensity/count for the forward's local-reflection path. null view => dummy
    // cube-array + count 0 (the forward keeps the global IBL reflection).
    void SetProbes(rhi::TextureView* cubeArray, rhi::Buffer* probeBuffer, u32 count) override {
        const bool has = (cubeArray != nullptr && probeBuffer != nullptr && count > 0);
        m_activeProbeCube   = has ? cubeArray : m_dummyProbeCubeView;
        m_activeProbeBuffer = has ? probeBuffer : m_dummyProbeBuffer;
        m_activeProbeCount  = has ? count : 0;   // -> ViewData.probeCenter.w (the forward's loop bound)
    }

    // This frame's IBL products (SH9 diffuse buffer + prefiltered specular cube + BRDF LUT), bound in
    // set 0. null views -> the neutral 1x1 dummies (zero SH + black cube => flat fallback ambient).
    void SetIBL(rhi::Buffer* sh, rhi::TextureView* prefilter, rhi::TextureView* brdf,
                f32 maxLod, u64 generation) override {
        m_activeShBuffer    = (sh != nullptr) ? sh : m_dummyShBuffer;
        m_activePrefilter   = (prefilter != nullptr) ? prefilter : m_dummyCubeView;
        m_activeBrdf        = (brdf != nullptr) ? brdf : m_dummyBrdfView;
        m_iblMaxLod         = maxLod;
        m_iblActive         = (sh != nullptr && prefilter != nullptr && brdf != nullptr);
        m_activeIblGen      = m_iblActive ? generation : 0;
    }

    // Upload this frame's local-shadow entries into the local-shadow ring (bound whole at set 0;
    // the shader reads LocalShadows[LocalShadowBase + shadowIndex]). Called once per frame (after
    // PrepareFrame, which begins the ring). The base is stamped into each view's ViewData in Resolve.
    void UploadLocalShadows(Span<const GpuLocalShadow> shadows, u32 frameIndex) override {
        (void)frameIndex;
        m_localShadowBase = 0;
        u32 n = static_cast<u32>(shadows.Size());
        if (n == 0 || !m_ready) { return; }
        if (n > kMaxLocalShadows) { n = kMaxLocalShadows; }
        const DynamicUniformRing::Range r = m_localShadowRing.AllocateRange(n);
        if (r.ok) {
            MemCopy(r.ptr, shadows.Data(), static_cast<usize>(n) * sizeof(GpuLocalShadow));
            m_localShadowBase = r.slotIndex;
        }
    }

    // ---- Renderer ----

    [[nodiscard]] Span<const RenderCategory> SupportedCategories() const override {
        static constexpr RenderCategory kCats[] = {
            RenderCategories::Opaque, RenderCategories::Masked, RenderCategories::Transparent };
        return Span<const RenderCategory>{ kCats, 3 };
    }

    // Size every ring for the whole frame's draws (once) + select this frame's region. The
    // per-object/instance rings are sized for 2x the camera draws so the shadow depth pass can
    // re-emit the same geometry into the same rings without starving the forward pass.
    void PrepareFrame(u32 maxDraws, u32 frameIndex) override {
        m_ready = false;
        TickRetiredBindGroups();   // free per-frame bind groups retired long enough ago to be idle
        if (maxDraws == 0) { return; }
        // Per-frame ring capacity = draws x (passes that re-emit them): the depth PREPASS + the forward
        // (2 camera passes) + per-cascade CSM re-emit + per-spot-tile local-atlas re-emit. Under-counting
        // overflows the object/instance/offset rings at high draw counts -> Allocate() fails -> dropped
        // draws (was missing the prepass, so the stress tests lost their spheres).
        const u32 drawCap = maxDraws * (2u + ShadowCascades::kCount + m_localShadowPassCount + m_captureFacePasses);
        if (!m_viewRing.Reserve(maxDraws) || !m_shadowViewRing.Reserve(kMaxShadowPasses) ||
            !m_objectRing.Reserve(drawCap) || !m_instanceRing.Reserve(drawCap) ||
            !m_offsetsRing.Reserve(drawCap) || !m_lightRing.Reserve(kMaxLights) ||
            !m_localShadowRing.Reserve(kMaxLocalShadows) || !m_boneRing.Reserve(kMaxBoneMatrices)) { return; }
        if (!EnsureBoneDevice()) { return; }   // device-local mirror of the bone staging ring (VS reads VRAM)
        if (!EnsureViewBindGroup() || !EnsureShadowViewBindGroup() ||
            !EnsureBindGroup(m_objectRing,   m_objectLayout,   sizeof(ObjectData), m_objectBG,   m_objectBGGen,   /*whole*/ false) ||
            !EnsureBindGroup(m_instanceRing, m_instanceLayout, 0,                  m_instanceBG, m_instanceBGGen, /*whole*/ true)) { return; }
        m_viewRing.BeginFrame(frameIndex);
        m_shadowViewRing.BeginFrame(frameIndex);
        m_objectRing.BeginFrame(frameIndex);
        m_instanceRing.BeginFrame(frameIndex);
        m_offsetsRing.BeginFrame(frameIndex);
        m_lightRing.BeginFrame(frameIndex);
        m_localShadowRing.BeginFrame(frameIndex);
        m_boneRing.BeginFrame(frameIndex);
        m_instShareCache.Clear();   // per-frame: the camera prepass fills it, the forward reuses it (ring offsets are frame-scoped)
        m_ready = true;
    }

    // Device-local mirror of the bone staging ring: vertex skinning reads bones across many passes
    // (forward + every cascade), so a CpuToGpu buffer would stream them over PCIe each read. One copy
    // per frame into VRAM (UploadSkinning) makes subsequent reads land at device bandwidth.
    bool EnsureBoneDevice() {
        const u64 want = m_boneRing.ByteCapacity();
        if (m_boneDevice != nullptr && m_boneDeviceBytes == want) { return true; }
        m_device->WaitIdle();
        if (m_boneDevice != nullptr) { m_device->DestroyBuffer(m_boneDevice); m_boneDevice = nullptr; }
        rhi::BufferDesc bd{};
        bd.size = want; bd.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst;
        bd.memory = rhi::MemoryLocation::GpuOnly; bd.label = u8"mesh.bones.device";
        if (!m_device->CreateBuffer(bd, m_boneDevice).IsOk()) { m_boneDevice = nullptr; m_boneDeviceBytes = 0; return false; }
        m_boneDeviceBytes = want;
        ++m_boneDeviceGen;   // invalidate set-0 bind groups that bind the device buffer
        return true;
    }

    // Write every distinct skinned instance's matrices into the bone pool ONCE this frame (current
    // slab then previous slab, per Sedulous's [cur][prev] layout) + copy the populated range to the
    // device mirror. Builds m_boneStart: boneMatrices ptr -> { current base, prev base } in MATRIX
    // units, which Resolve uses for the per-draw bone base (DataOffsets.y/.z later). Called once per
    // frame before any pass; replaces the old per-pass MemCopy into the ring.
    void UploadSkinning(const ExtractedScene& scene, rhi::CommandEncoder& encoder) override {
        m_boneStart.Clear();
        m_skinnedScratch.Clear();
        if (!m_ready) { return; }
        // One-time: bring the out-of-graph dummy depth textures into the layout their descriptors
        // expect, so they're never sampled while UNDEFINED on caster-less frames (VUID-09600).
        if (!m_dummyDepthInit) {
            if (m_dummyShadowTex != nullptr) {
                encoder.TransitionTexture(m_dummyShadowTex, rhi::ResourceState::Undefined, rhi::ResourceState::DepthStencilRead);
            }
            if (m_dummyAtlasTex != nullptr) {
                encoder.TransitionTexture(m_dummyAtlasTex, rhi::ResourceState::Undefined, rhi::ResourceState::DepthStencilRead);
            }
            // IBL color fallbacks (cube + BRDF LUT) — also out-of-graph, sampled when no env is active.
            if (m_dummyCube != nullptr) {
                encoder.TransitionTexture(m_dummyCube, rhi::ResourceState::Undefined, rhi::ResourceState::ShaderRead);
            }
            if (m_dummyBrdf != nullptr) {
                encoder.TransitionTexture(m_dummyBrdf, rhi::ResourceState::Undefined, rhi::ResourceState::ShaderRead);
            }
            if (m_dummyProbeCube != nullptr) {
                encoder.TransitionTexture(m_dummyProbeCube, rhi::ResourceState::Undefined, rhi::ResourceState::ShaderRead);
            }
            m_dummyDepthInit = true;
        }
        // Pass 1: collect DISTINCT skinned instances (by boneMatrices ptr). Total matrices = sum of
        // 2*boneCount (current slab + previous slab). The map reserves the key so dups are skipped.
        u32 total = 0;
        for (RenderData* data : scene.Items()) {
            if (data == nullptr || data->rendererId != RendererId()) { continue; }   // skip non-mesh (e.g. sprite) items
            const auto* md = static_cast<const MeshRenderData*>(data);
            if (md->boneMatrices == nullptr || md->boneCount == 0) { continue; }
            if (md->mesh == nullptr || !md->mesh->IsSkinned()) { continue; }
            if (m_boneStart.Contains(md->boneMatrices)) { continue; }
            m_boneStart.InsertOrAssign(md->boneMatrices, BoneSlot{});   // reserve; bases filled in pass 2
            m_skinnedScratch.PushBack(SkinnedRef{ md->boneMatrices, md->prevBoneMatrices, md->boneCount });
            total += md->boneCount * 2u;
        }
        if (total == 0) { m_boneStart.Clear(); return; }
        const DynamicUniformRing::Range block = m_boneRing.AllocateRange(total);
        if (!block.ok) { m_boneStart.Clear(); return; }

        // Pass 2: write each distinct instance into the block (current then prev) + record its bases.
        u32 cursor = 0;   // matrix-units offset within the block
        for (const SkinnedRef& r : m_skinnedScratch) {
            const u32 n        = r.count;
            const u32 base     = block.slotIndex + cursor;
            const u32 prevBase = base + n;
            Mat4* dst = static_cast<Mat4*>(block.ptr) + cursor;
            MemCopy(dst,     r.cur,                                    static_cast<usize>(n) * sizeof(Mat4));
            MemCopy(dst + n, (r.prev != nullptr) ? r.prev : r.cur,    static_cast<usize>(n) * sizeof(Mat4));
            if (BoneSlot* slot = m_boneStart.Find(r.cur)) { *slot = BoneSlot{ base, prevBase }; }
            cursor += n * 2u;
        }

        // Mirror the populated range to VRAM, then make it visible to vertex-shader reads.
        encoder.CopyBufferToBuffer(m_boneRing.Buffer(), block.byteOffset, m_boneDevice, block.byteOffset,
                                   static_cast<u64>(total) * sizeof(Mat4));
        encoder.TransitionBuffer(m_boneDevice, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead);
    }

    void Resolve(const RenderRecordContext& ctx, Span<const DrawItem> items, Array<ResolvedDraw>& out) override {
        if (!m_ready || items.IsEmpty()) { return; }

        // Upload this view's lights into the light ring (bound whole at set 0; the shader reads
        // Lights[lightOffset + i]). Clamp to the per-frame capacity.
        u32 lightCount = static_cast<u32>(ctx.lights.Size());
        if (lightCount > kMaxLights) { lightCount = kMaxLights; }
        u32 lightOffset = 0;
        if (lightCount > 0) {
            const DynamicUniformRing::Range lr = m_lightRing.AllocateRange(lightCount);
            if (lr.ok) {
                MemCopy(lr.ptr, ctx.lights.Data(), static_cast<usize>(lightCount) * sizeof(GpuLight));
                lightOffset = lr.slotIndex;
            } else { lightCount = 0; }
        }

        // Per-view UBO (shared by every draw in this call): ViewProj + camera + light range. The
        // two pipeline layouts share set 0 (m_viewLayout), so the resolved binding is the same.
        // The clustered light lists for this view (set 3). Empty binding -> dummy + ClusterGridX=0,
        // which makes the shader fall back to looping all lights.
        rhi::BindGroup* clusterBG = m_dummyClusterBG;
        if (ctx.cluster.Valid()) {
            // Per-(view, frame) slot — two views in one frame have different cluster buffers and
            // must not share a bind group (else it thrashes / frees a set still in flight).
            const u32 clusterSlot = ctx.viewIndex * m_framesInFlight + (ctx.frameIndex % m_framesInFlight);
            clusterBG = EnsureClusterBindGroup(clusterSlot, ctx.cluster.offsets, ctx.cluster.lightIndices, ctx.cluster.version);
        }

        const DynamicUniformRing::Range view = m_viewRing.Allocate();
        if (!view.ok) { return; }
        ViewData vd{};
        vd.viewProj      = ctx.viewProj;
        vd.prevViewProj  = ctx.prevViewProj;   // motion vectors (camera)
        vd.jitter        = Vec4{ ctx.jitter.x, ctx.jitter.y, ctx.prevJitter.x, ctx.prevJitter.y };
        vd.view          = ctx.viewMatrix;
        vd.cameraPos     = ctx.cameraPos;
        vd.ambient       = ctx.ambient;
        vd.iblMaxLod     = m_iblActive ? m_iblMaxLod : -1.0f;   // <0 => forward uses flat ambient
        // Probes disabled during probe capture (ctx.probesEnabled=false) -> count 0 so metallics reflect the
        // sky (global IBL), not the not-yet-built probe (which would bake them black — self-reflection).
        // ProbeCenter.w = the probe count (the forward's loop bound over the Probes SRV); box/slice/etc. per
        // probe live in the Probes buffer now.
        vd.probeCenter   = Vec4{ 0, 0, 0, ctx.probesEnabled ? static_cast<f32>(m_activeProbeCount) : 0.0f };

        vd.lightCount    = static_cast<f32>(lightCount);
        vd.lightOffset   = lightOffset;
        // CSM cascade data for THIS view (per-view fit, carried in ctx).
        if (ctx.cascades.valid) {
            for (u32 c = 0; c < ShadowCascades::kCount; ++c) { vd.cascadeViewProj[c] = ctx.cascades.viewProj[c]; }
            vd.cascadeSplitFar     = Vec4{ ctx.cascades.splitFar[0], ctx.cascades.splitFar[1], ctx.cascades.splitFar[2], ctx.cascades.splitFar[3] };
            vd.cascadeTexelSize    = Vec4{ ctx.cascades.texelWorldSize[0], ctx.cascades.texelWorldSize[1], ctx.cascades.texelWorldSize[2], ctx.cascades.texelWorldSize[3] };
            vd.shadowCascadeCount  = static_cast<f32>(ShadowCascades::kCount);
            vd.cascadeLayerBase    = static_cast<f32>(ctx.cascadeLayerBase);   // this view's first array layer
            // Normal-offset is in TEXELS (scaled by the cascade's world texel size in the shader).
            // Keep it tiny (Sedulous uses 0.02) — at large values it shifts the receiver enough to eat
            // the light-facing side of a contact shadow, worse the bigger the cascade's texelWorld grows.
            // Acne is carried by the hardware depth bias (ShadowConfigFor: 50 / 1.5), not this.
            vd.shadowNormalBias    = 0.02f;
            vd.shadowDepthBias     = 0.0009f;
            vd.shadowParams.x      = ctx.shadowFarFade;   // CSM far-fade band (runtime-tunable)
        }
        vd.localShadowBase = m_localShadowBase;   // base into the local-shadow ring (spot/point atlas)
        if (ctx.cluster.Valid()) {
            vd.clusterGridX = ctx.cluster.gridX; vd.clusterGridY = ctx.cluster.gridY;
            vd.clusterSliceCount = ctx.cluster.sliceCount; vd.clusterTileSize = ctx.cluster.tileSize;
            vd.clusterViewportX = ctx.cluster.viewportX; vd.clusterViewportY = ctx.cluster.viewportY;
            vd.clusterNear = ctx.cluster.nearZ; vd.clusterFar = ctx.cluster.farZ;
            vd.clusterLogScale = ctx.cluster.logScale; vd.clusterLogBias = ctx.cluster.logBias;
        }
        *static_cast<ViewData*>(view.ptr) = vd;
        const u32 viewOffset = view.byteOffset;

        const bool allowInstancing = (items[0].data->category != RenderCategories::Transparent);

        usize i = 0;
        while (i < items.Size()) {
            const auto* head = static_cast<const MeshRenderData*>(items[i].data);
            // Skinned meshes carry per-instance bones via DataOffsets.y now, so they batch like static
            // meshes — identical (mesh, material) skinned instances collapse into one instanced draw.
            const bool headSkinned = head->mesh != nullptr && head->mesh->IsSkinned() && head->boneMatrices != nullptr;
            // Extend the run while mesh + material match (a batchable group).
            usize j = i + 1;
            if (allowInstancing) {
                while (j < items.Size()) {
                    const auto* nd = static_cast<const MeshRenderData*>(items[j].data);
                    if (nd->mesh != head->mesh || nd->material != head->material) { break; }
                    ++j;
                }
            }
            const u32 runLen = static_cast<u32>(j - i);

            const GpuMesh* mesh = m_meshes.GetOrUpload(head->mesh);
            if (mesh != nullptr) {
                // Skinned always uses the instanced path (even count 1) — the single path has no bone base.
                if (runLen >= 2 || headSkinned) { ResolveInstanced(ctx, viewOffset, clusterBG, items, i, runLen, *head, *mesh, out); }
                else                            { ResolveSingle(ctx, viewOffset, clusterBG, *head, *mesh, out); }
            }
            i = j;
        }
    }

    // Re-emit this view's draws as DEPTH-ONLY shadow casters (ctx.viewProj = light view-proj,
    // ctx.depthFormat = shadow map format). Mirrors Resolve's run batching but produces depth-only
    // ResolvedDraws (set 0 = light view, set 1 = object/instance; no material/cluster). Reuses the
    // object/instance rings + their bind groups (sized 2x in PrepareFrame).
    void ResolveDepthOnly(const RenderRecordContext& ctx, Span<const DrawItem> items, Array<ResolvedDraw>& out) override {
        if (!m_ready || items.IsEmpty()) { return; }

        const DynamicUniformRing::Range sv = m_shadowViewRing.Allocate();
        if (!sv.ok) { return; }
        *static_cast<ShadowViewData*>(sv.ptr) = ShadowViewData{ ctx.viewProj };
        const u32 shadowViewOffset = sv.byteOffset;

        usize i = 0;
        while (i < items.Size()) {
            const auto* head = static_cast<const MeshRenderData*>(items[i].data);
            // Skinned casters batch like static ones now (per-instance bone base via DataOffsets.y).
            const bool headSkinned = head->mesh != nullptr && head->mesh->IsSkinned() && head->boneMatrices != nullptr;
            usize j = i + 1;
            while (j < items.Size()) {
                const auto* nd = static_cast<const MeshRenderData*>(items[j].data);
                if (nd->mesh != head->mesh || nd->material != head->material) { break; }
                ++j;
            }
            const u32 runLen = static_cast<u32>(j - i);
            const GpuMesh* mesh = m_meshes.GetOrUpload(head->mesh);
            if (mesh != nullptr) {
                if (runLen >= 2 || headSkinned) { ResolveDepthInstanced(ctx, shadowViewOffset, items, i, runLen, *mesh, out); }
                else                            { ResolveDepthSingle(ctx, shadowViewOffset, *head, *mesh, out); }
            }
            i = j;
        }
    }

    void FinishFrame() override {
        m_viewRing.EndFrame();
        m_shadowViewRing.EndFrame();
        m_objectRing.EndFrame();
        m_instanceRing.EndFrame();
        m_boneRing.EndFrame();
        m_offsetsRing.EndFrame();
        // This frame's world matrices become next frame's "previous" (motion vectors). Flat swap: O(1)
        // pointer moves, no clear — each visible entity overwrites its own slot when resolved, and slots
        // for now-invisible entities are never read (their draws don't resolve). Both buffers keep their
        // capacity, so steady state does zero allocation.
        Array<Mat4> recycled = Move(m_prevWorld);
        m_prevWorld = Move(m_curWorld);
        m_curWorld  = Move(recycled);
        m_ready = false;
    }

private:
    struct ViewData {                                    // 528 (matches the View cbuffer)
        Mat4 viewProj;                                   // 64
        Mat4 view;                                       // 64  (view-space depth: cluster + cascade select)
        Mat4 cascadeViewProj[4];                         // 256 (CSM: world -> each cascade's light clip)
        Vec3 cameraPos; f32 lightCount;                  // 16  (light count as float, mirrors HLSL)
        u32  lightOffset; i32 clusterViewportX, clusterViewportY; f32 iblMaxLod = -1.0f;   // 16 (iblMaxLod<0 => no IBL)
        u32  clusterGridX = 0, clusterGridY = 0, clusterSliceCount = 0, clusterTileSize = 0;   // 16
        f32  clusterNear = 0, clusterFar = 0, clusterLogScale = 0, clusterLogBias = 0;         // 16
        Vec3 ambient = Vec3{ 0, 0, 0 }; f32 shadowCascadeCount = 0.0f;                          // 16
        Vec4 cascadeSplitFar  = Vec4{ 0, 0, 0, 0 };                                             // 16
        Vec4 cascadeTexelSize = Vec4{ 0, 0, 0, 0 };                                             // 16
        f32  shadowNormalBias = 0, shadowDepthBias = 0, cascadeLayerBase = 0; u32 localShadowBase = 0;   // 16
        Mat4 prevViewProj = Mat4::Identity();            // 64  (motion vectors: last frame's world->clip)
        Vec4 jitter = Vec4{ 0, 0, 0, 0 };                // 16  (xy = this frame's NDC jitter, zw = last frame's)
        Vec4 probeCenter = Vec4{ 0, 0, 0, 0 };           // 16  (xyz = probe center, w = probe count [0 = none])
        Vec4 probeBoxMin = Vec4{ 0, 0, 0, 0 };           // 16  (xyz = box min, w = probe cube slice)
        Vec4 probeBoxMax = Vec4{ 0, 0, 0, 0 };           // 16  (xyz = box max, w = probe intensity)
        Vec4 shadowParams = Vec4{ 40.0f, 0, 0, 0 };      // 16  (x = CSM far-fade width in world units; yzw spare)
    };
    struct ObjectData   { Mat4 world; Mat4 prevWorld; Color tint; u32 boneBase = 0, prevBoneBase = 0, p1 = 0, p2 = 0; };   // 160 (cbuffer Object)
    struct InstanceData { Mat4 world; Mat4 prevWorld; Color tint; };     // 144 (StructuredBuffer element)
    struct DataOffsets  { u32 x, y, z, w; };             // 16  (instance-stepped vertex attr)
    struct ShadowViewData { Mat4 lightViewProj; };       // 64  (cbuffer ShadowView)

    static constexpr u64 kViewSlot         = 256;        // dynamic UBO offset alignment (object/shadow-view)
    static constexpr u64 kViewDataSlot     = 1024;       // view UBO slot (ViewData is 528B with CSM cascades)
    static constexpr u32 kMaxLights        = 256;        // per-view light budget (phase 4.1; clustered later)
    // shadow-view UBO slots per frame: one per (shadow pass × category run). Cascades (up to
    // kMaxShadowViews*kCount) + local-shadow atlas tiles (up to kMaxLocalShadows), each × a few
    // categories. Sized with headroom — a slot is tiny (256B).
    static constexpr u32 kMaxShadowPasses  = 256;
    static constexpr u32 kMaxLocalShadows  = 64;         // spot/point shadow entries per frame (atlas-bound)
    static constexpr u32 kMaxBoneMatrices  = 1u << 20;   // GPU skinning bone-matrix pool slots per frame.
                                                         // The current arch re-uploads each caster's bones
                                                         // PER PASS (forward + 4 CSM cascades + local), so
                                                         // usage is ~N*bones*(5+); sized big so the stress
                                                         // test doesn't overflow (the persistent-buffer
                                                         // rewrite uploads once, shared across passes).

    void ResolveSingle(const RenderRecordContext& ctx, u32 viewOffset, rhi::BindGroup* clusterBG,
                       const MeshRenderData& md, const GpuMesh& mesh, Array<ResolvedDraw>& out) {
        materials::Material* mat = (md.material != nullptr) ? md.material : m_defaultMaterial.Get();
        materials::PipelineConfig config = ConfigFor(md, ctx, /*instanced*/ false);

        // GPU skinning: a skinned mesh draws the SKINNED + SkinnedMesh-layout permutation, binds the
        // skin stream (buffer 1), and reads its bones from the shared device pool at boneBase (matrix
        // units, computed once this frame by UploadSkinning). No per-pass upload here.
        u32 boneBase = 0;
        bool skinned = md.boneMatrices != nullptr && md.boneCount > 0 &&
                       md.mesh != nullptr && md.mesh->IsSkinned() && mesh.skinBuffer != nullptr;
        if (skinned) {
            const BoneSlot* s = m_boneStart.Find(md.boneMatrices);
            if (s != nullptr) {
                boneBase = s->base;
                config.vertexLayout = materials::VertexLayoutType::SkinnedMesh;
                config.shaderFlags |= shaders::ShaderFlags::Skinned;
            } else {
                skinned = false;   // not uploaded (pool overflow) -> draw bind pose rather than garbage
            }
        }

        const DynamicUniformRing::Range obj = m_objectRing.Allocate();
        if (!obj.ok) { return; }
        ObjectData od{}; od.world = md.world; od.prevWorld = ctx.needsMotion ? PrevWorldFor(md.entityId, md.world) : md.world;
        od.tint = md.color; od.boneBase = boneBase; od.prevBoneBase = boneBase;
        *static_cast<ObjectData*>(obj.ptr) = od;

        // Shared per-submesh draw state (view/object/cluster sets + vertex/index buffers + skinning).
        ResolvedDraw base{};
        base.viewSet = m_viewBG;   base.viewOffset = viewOffset;     base.viewDynamic = true;   // set 0
        base.drawSet = m_objectBG; base.drawOffset = obj.byteOffset; base.drawDynamic = true;   // set 1
        base.clusterSet = clusterBG;                                                            // set 3
        base.vertexBuffer0 = mesh.vertexBuffer; base.vertexOffset0 = mesh.vertexOffset;
        if (skinned) { base.vertexBuffer1 = mesh.skinBuffer; base.vertexOffset1 = mesh.skinOffset; }
        base.indexBuffer = mesh.indexBuffer; base.indexFormat = mesh.indexFormat; base.instanceCount = 1;

        // Emit one draw for an index sub-range with `m`'s material (set 2 = its data-driven bind group).
        const auto emit = [&](materials::Material* m, u64 indexOffset, u32 indexCount) {
            materials::Material* use = (m != nullptr) ? m : mat;
            rhi::BindGroupLayout* set2 = m_materials->GetOrCreateLayout(*use);
            rhi::PipelineLayout* plLayout = GetOrCreatePipelineLayout(set2, /*instanced*/ false);
            if (plLayout == nullptr) { return; }
            rhi::RenderPipeline* pso = m_psoCache->GetPipeline(config, plLayout, ctx.colorFormat);
            if (pso == nullptr) { return; }
            ResolvedDraw d = base;
            d.pso         = pso;
            d.materialSet = m_materials->PrepareInstance(*InstanceFor(use), set2);
            d.indexOffset = indexOffset;
            d.indexCount  = indexCount;
            out.PushBack(d);
        };

        // Multi-material: draw each submesh with its own material; else one draw for the whole mesh.
        if (md.submeshMaterialCount > 0 && md.mesh != nullptr && !md.mesh->subMeshes.IsEmpty()) {
            const u64 stride = (mesh.indexFormat == rhi::IndexFormat::UInt16) ? 2u : 4u;
            for (const geometry::SubMesh& sub : md.mesh->subMeshes) {
                materials::Material* m = (sub.materialIndex >= 0 && static_cast<u32>(sub.materialIndex) < md.submeshMaterialCount)
                                            ? md.submeshMaterials[sub.materialIndex].Get() : nullptr;
                emit(m, mesh.indexOffset + static_cast<u64>(sub.startIndex) * stride, static_cast<u32>(sub.indexCount));
            }
        } else {
            emit(mat, mesh.indexOffset, mesh.indexCount);
        }
    }

    void ResolveInstanced(const RenderRecordContext& ctx, u32 viewOffset, rhi::BindGroup* clusterBG,
                          Span<const DrawItem> items, usize first, u32 count,
                          const MeshRenderData& head, const GpuMesh& mesh, Array<ResolvedDraw>& out) {
        materials::Material* mat = (head.material != nullptr) ? head.material : m_defaultMaterial.Get();
        materials::PipelineConfig config = ConfigFor(head, ctx, /*instanced*/ true);
        // Skinned instanced draw: the shared skin stream (joints/weights) is per-mesh; each instance's
        // bone base rides in DataOffsets.y (current) / .z (prev) so one draw skins N characters.
        const bool skinned = head.boneMatrices != nullptr && head.mesh != nullptr &&
                             head.mesh->IsSkinned() && mesh.skinBuffer != nullptr;
        if (skinned) {
            config.vertexLayout = materials::VertexLayoutType::SkinnedMesh;
            config.shaderFlags |= shaders::ShaderFlags::Skinned;
        }
        // The camera depth prepass already filled this group's InstanceData + DataOffsets (identical objects,
        // same order) and cached the range — REUSE it instead of allocating + re-filling (build once). Falls
        // back to a fresh fill on a miss (no prepass, count mismatch, or ring exhausted).
        u64 offsByteOffset;
        const InstShare* shared = m_instShareCache.Find(InstShareKey(ctx.view, head.mesh, head.material));
        if (shared != nullptr && shared->count == count) {
            offsByteOffset = shared->offsByteOffset;
        } else {
            const DynamicUniformRing::Range inst = m_instanceRing.AllocateRange(count);
            const DynamicUniformRing::Range offs = m_offsetsRing.AllocateRange(count);
            if (!inst.ok || !offs.ok) { return; }
            InstanceData* id = static_cast<InstanceData*>(inst.ptr);
            DataOffsets*  od = static_cast<DataOffsets*>(offs.ptr);
            for (u32 k = 0; k < count; ++k) {
                const auto* md = static_cast<const MeshRenderData*>(items[first + k].data);
                id[k] = InstanceData{ md->world, ctx.needsMotion ? PrevWorldFor(md->entityId, md->world) : md->world, md->color };
                u32 boneBase = 0, prevBase = 0;
                if (skinned) { if (const BoneSlot* s = m_boneStart.Find(md->boneMatrices)) { boneBase = s->base; prevBase = s->prevBase; } }
                od[k] = DataOffsets{ inst.slotIndex + k, boneBase, prevBase, 0 };   // .x=Instances[] idx, .y/.z=bone bases
            }
            offsByteOffset = offs.byteOffset;
        }

        // Shared per-submesh draw state (view/instances/cluster sets + vertex/index buffers + skinning).
        ResolvedDraw base{};
        base.viewSet = m_viewBG;     base.viewOffset = viewOffset; base.viewDynamic = true;   // set 0: view
        base.drawSet = m_instanceBG; base.drawDynamic = false;                                // set 1: instances (whole)
        base.clusterSet = clusterBG;                                                          // set 3: cluster lists
        base.vertexBuffer0 = mesh.vertexBuffer; base.vertexOffset0 = mesh.vertexOffset;
        if (skinned) {
            base.vertexBuffer1 = mesh.skinBuffer;        base.vertexOffset1 = mesh.skinOffset;   // slot 1: skin stream (6/7)
            base.vertexBuffer2 = m_offsetsRing.Buffer(); base.vertexOffset2 = offsByteOffset;     // slot 2: DataOffsets (5)
        } else {
            base.vertexBuffer1 = m_offsetsRing.Buffer(); base.vertexOffset1 = offsByteOffset;     // slot 1: DataOffsets (5)
        }
        base.indexBuffer = mesh.indexBuffer; base.indexFormat = mesh.indexFormat; base.instanceCount = count;

        // Emit one instanced draw for an index sub-range with `m`'s material (set 2 = its bind group).
        const auto emit = [&](materials::Material* m, u64 indexOffset, u32 indexCount) {
            materials::Material* use = (m != nullptr) ? m : mat;
            rhi::BindGroupLayout* set2 = m_materials->GetOrCreateLayout(*use);
            rhi::PipelineLayout* plLayout = GetOrCreatePipelineLayout(set2, /*instanced*/ true);
            if (plLayout == nullptr) { return; }
            rhi::RenderPipeline* pso = m_psoCache->GetPipeline(config, plLayout, ctx.colorFormat);
            if (pso == nullptr) { return; }
            ResolvedDraw d = base;
            d.pso         = pso;
            d.materialSet = m_materials->PrepareInstance(*InstanceFor(use), set2);
            d.indexOffset = indexOffset;
            d.indexCount  = indexCount;
            out.PushBack(d);
        };

        // Multi-material: one instanced draw per submesh (its own material + index range); else one draw.
        if (head.submeshMaterialCount > 0 && head.mesh != nullptr && !head.mesh->subMeshes.IsEmpty()) {
            const u64 stride = (mesh.indexFormat == rhi::IndexFormat::UInt16) ? 2u : 4u;
            for (const geometry::SubMesh& sub : head.mesh->subMeshes) {
                materials::Material* m = (sub.materialIndex >= 0 && static_cast<u32>(sub.materialIndex) < head.submeshMaterialCount)
                                            ? head.submeshMaterials[sub.materialIndex].Get() : nullptr;
                emit(m, mesh.indexOffset + static_cast<u64>(sub.startIndex) * stride, static_cast<u32>(sub.indexCount));
            }
        } else {
            emit(mat, mesh.indexOffset, mesh.indexCount);
        }
    }

    void ResolveDepthSingle(const RenderRecordContext& ctx, u32 shadowViewOffset,
                            const MeshRenderData& md, const GpuMesh& mesh, Array<ResolvedDraw>& out) {
        u32 boneBase = 0;
        bool skinned = md.boneMatrices != nullptr && md.boneCount > 0 &&
                       md.mesh != nullptr && md.mesh->IsSkinned() && mesh.skinBuffer != nullptr;
        // Masked casters cast holey shadows via the alpha-test fragment (needs the material set 2).
        const bool masked = md.material != nullptr && md.material->pipeline.blendMode == materials::BlendMode::Masked;
        materials::PipelineConfig config = ShadowConfigFor(ctx, /*instanced*/ false, masked);
        if (skinned) {
            const BoneSlot* s = m_boneStart.Find(md.boneMatrices);
            if (s != nullptr) {
                boneBase = s->base;   // shared device pool (uploaded once this frame)
                config.vertexLayout = materials::VertexLayoutType::SkinnedMesh;
                config.shaderFlags |= shaders::ShaderFlags::Skinned;
            } else {
                skinned = false;
            }
        }
        rhi::BindGroup* matSet = nullptr;
        rhi::PipelineLayout* layout = m_shadowPipelineLayoutSingle;
        if (masked) {
            rhi::BindGroupLayout* set2 = m_materials->GetOrCreateLayout(*md.material);
            layout = GetOrCreateShadowMaskedLayout(set2, /*instanced*/ false);
            matSet = m_materials->PrepareInstance(*InstanceFor(md.material), set2);
            if (layout == nullptr) { layout = m_shadowPipelineLayoutSingle; matSet = nullptr; config = ShadowConfigFor(ctx, false, false); }
        }
        rhi::RenderPipeline* pso = m_psoCache->GetPipeline(config, layout, rhi::TextureFormat::Undefined);
        if (pso == nullptr) { return; }
        const DynamicUniformRing::Range obj = m_objectRing.Allocate();
        if (!obj.ok) { return; }
        ObjectData od{}; od.world = md.world; od.prevWorld = md.world;   // depth pass ignores prevWorld
        od.tint = md.color; od.boneBase = boneBase;
        *static_cast<ObjectData*>(obj.ptr) = od;

        ResolvedDraw d{};
        d.pso = pso;
        d.viewSet = m_shadowViewBG; d.viewOffset = shadowViewOffset; d.viewDynamic = true;   // set 0: light view
        d.drawSet = m_objectBG;     d.drawOffset = obj.byteOffset;   d.drawDynamic = true;   // set 1: object UBO
        d.materialSet = matSet;     // set 2: material (masked casters only — for the alpha-test sample)
        d.vertexBuffer0 = mesh.vertexBuffer; d.vertexOffset0 = mesh.vertexOffset;
        if (skinned) { d.vertexBuffer1 = mesh.skinBuffer; d.vertexOffset1 = mesh.skinOffset; }   // buffer 1: skin stream
        d.indexBuffer = mesh.indexBuffer; d.indexOffset = mesh.indexOffset; d.indexFormat = mesh.indexFormat;
        d.indexCount = mesh.indexCount; d.instanceCount = 1;
        out.PushBack(d);
    }

    void ResolveDepthInstanced(const RenderRecordContext& ctx, u32 shadowViewOffset,
                               Span<const DrawItem> items, usize first, u32 count,
                               const GpuMesh& mesh, Array<ResolvedDraw>& out) {
        const auto& head = *static_cast<const MeshRenderData*>(items[first].data);
        const bool skinned = head.boneMatrices != nullptr && head.mesh != nullptr &&
                             head.mesh->IsSkinned() && mesh.skinBuffer != nullptr;
        const bool masked = head.material != nullptr && head.material->pipeline.blendMode == materials::BlendMode::Masked;
        materials::PipelineConfig config = ShadowConfigFor(ctx, /*instanced*/ true, masked);
        if (skinned) {
            config.vertexLayout = materials::VertexLayoutType::SkinnedMesh;
            config.shaderFlags |= shaders::ShaderFlags::Skinned;
        }
        rhi::BindGroup* matSet = nullptr;
        rhi::PipelineLayout* layout = m_shadowPipelineLayoutInstanced;
        if (masked) {
            rhi::BindGroupLayout* set2 = m_materials->GetOrCreateLayout(*head.material);
            layout = GetOrCreateShadowMaskedLayout(set2, /*instanced*/ true);
            matSet = m_materials->PrepareInstance(*InstanceFor(head.material), set2);
            if (layout == nullptr) { layout = m_shadowPipelineLayoutInstanced; matSet = nullptr; config = ShadowConfigFor(ctx, true, false); }
        }
        rhi::RenderPipeline* pso = m_psoCache->GetPipeline(config, layout, rhi::TextureFormat::Undefined);
        if (pso == nullptr) { return; }
        const DynamicUniformRing::Range inst = m_instanceRing.AllocateRange(count);
        const DynamicUniformRing::Range offs = m_offsetsRing.AllocateRange(count);
        if (!inst.ok || !offs.ok) { return; }

        InstanceData* id = static_cast<InstanceData*>(inst.ptr);
        DataOffsets*  od = static_cast<DataOffsets*>(offs.ptr);
        // When this prepass feeds the forward (camera depth prepass, fillInstanceCache), build the FULL
        // InstanceData incl. REAL prevWorld so the forward can reuse it for motion vectors. Shadow casters
        // leave prevWorld = world (the depth/shadow shaders ignore it — no extra prev-world lookup).
        const bool feedsForward = ctx.fillInstanceCache;
        for (u32 k = 0; k < count; ++k) {
            const auto* md = static_cast<const MeshRenderData*>(items[first + k].data);
            const Mat4 prev = (feedsForward && ctx.needsMotion) ? PrevWorldFor(md->entityId, md->world) : md->world;
            id[k] = InstanceData{ md->world, prev, md->color };
            u32 boneBase = 0, prevBase = 0;
            if (skinned) { if (const BoneSlot* s = m_boneStart.Find(md->boneMatrices)) { boneBase = s->base; prevBase = s->prevBase; } }
            od[k] = DataOffsets{ inst.slotIndex + k, boneBase, prevBase, 0 };
        }
        if (feedsForward) {   // record this group's DataOffsets range for the forward to reuse
            m_instShareCache.InsertOrAssign(InstShareKey(ctx.view, head.mesh, head.material), InstShare{ offs.byteOffset, count });
        }

        ResolvedDraw d{};
        d.pso = pso;
        d.viewSet = m_shadowViewBG; d.viewOffset = shadowViewOffset; d.viewDynamic = true;   // set 0: light view
        d.drawSet = m_instanceBG;   d.drawDynamic = false;                                    // set 1: instances (whole)
        d.materialSet = matSet;     // set 2: material (masked casters only — alpha-test sample)
        d.vertexBuffer0 = mesh.vertexBuffer;      d.vertexOffset0 = mesh.vertexOffset;
        if (skinned) {
            d.vertexBuffer1 = mesh.skinBuffer;        d.vertexOffset1 = mesh.skinOffset;   // slot 1: skin stream
            d.vertexBuffer2 = m_offsetsRing.Buffer(); d.vertexOffset2 = offs.byteOffset;    // slot 2: DataOffsets
        } else {
            d.vertexBuffer1 = m_offsetsRing.Buffer(); d.vertexOffset1 = offs.byteOffset;    // slot 1: DataOffsets
        }
        d.indexBuffer = mesh.indexBuffer; d.indexOffset = mesh.indexOffset; d.indexFormat = mesh.indexFormat;
        d.indexCount = mesh.indexCount; d.instanceCount = count;
        out.PushBack(d);
    }

    // Depth-only PSO config for the shadow pass. Back-face cull + a small depth bias/slope to push
    // shadow acne off lit surfaces (tuned on GPU; 5.2 refines with normal-offset bias in the shader).
    [[nodiscard]] static materials::PipelineConfig ShadowConfigFor(const RenderRecordContext& ctx, bool instanced, bool masked = false) {
        materials::PipelineConfig c{};
        c.shaderName   = u8"shadow_depth";
        c.vertexLayout = materials::VertexLayoutType::Mesh;
        c.instanced    = instanced;
        if (instanced) { c.shaderFlags |= shaders::ShaderFlags::Instanced; }
        // Masked casters run the alpha-test fragment (samples cutout alpha -> discard) so their shadows
        // have holes; opaque casters stay vertex-only (depthOnly, no fragment).
        c.depthOnly         = !masked;
        c.colorTargetCount  = 0;
        if (masked) { c.shaderFlags |= shaders::ShaderFlags::AlphaTest; }
        c.depthFormat       = ctx.depthFormat;
        c.depthMode         = materials::DepthMode::ReadWrite;
        c.depthCompare      = rhi::CompareFunction::Less;
        // Render FRONT faces into the shadow map (cull back) — matches Sedulous (ShadowPipeline: .Back)
        // and is the conventional default: flat/architectural casters get tight contacts. CURVED casters
        // (spheres) keep a small grazing-contact gap inherent to shadow maps; the general fix is a later
        // contact/screen-space shadow pass, not a cull-mode or bias change (back-face culling only trades
        // the gap onto flat casters, which are far more common).
        c.cullMode          = materials::CullModeConfig::Back;
        // Hardware depth bias (ported from Sedulous): a constant offset + a slope-scaled term, applied
        // in shadow-map depth space (so it adds little visible spatial gap, unlike the normal-offset).
        // Pairs with the receiver-side (1 - NdotL) normal-offset bias in forward.frag for acne control.
        // The camera depth PREPASS must NOT bias — its depth has to equal the forward pass's exactly so
        // the LessEqual early-Z accepts the re-drawn opaque fragments (bias would z-fight / reject them).
        if (!ctx.depthPrepass) {
            c.depthBias           = 50;
            c.depthBiasSlopeScale = 1.5f;
        }
        return c;
    }

    [[nodiscard]] static materials::PipelineConfig ConfigFor(const MeshRenderData& md, const RenderRecordContext& ctx, bool instanced) {
        materials::PipelineConfig config = (md.material != nullptr)
            ? md.material->pipeline
            : materials::PipelineConfig::ForOpaqueMesh(u8"forward");
        config.depthFormat = ctx.depthFormat;
        config.instanced   = instanced;
        if (instanced) { config.shaderFlags |= shaders::ShaderFlags::Instanced; }
        // Opaque + masked render the MRT G-buffer pass: target 0 = shaded color (format overridden
        // per-view at build); targets 1/2 = view-space normal + motion vector (the GBUFFER permutation
        // writes them). Transparent renders a separate color-only pass, so it stays single-target.
        const bool gbuffer = (config.blendMode == materials::BlendMode::Opaque ||
                              config.blendMode == materials::BlendMode::Masked);
        if (gbuffer) {
            config.colorTargetCount = 4;
            config.colorFormats[1]  = kGNormalFormat;
            config.colorFormats[2]  = kGVelocityFormat;
            config.colorFormats[3]  = kGMaterialFormat;   // SSR: roughness/metallic
            config.shaderFlags     |= shaders::ShaderFlags::GBuffer;
            // Equal-depth fragments from the depth prepass must pass (early-Z shades each opaque pixel once).
            config.depthCompare     = rhi::CompareFunction::LessEqual;
            // Masked: enable the alpha-test discard permutation (opaque-like, but cuts sub-cutoff pixels).
            if (config.blendMode == materials::BlendMode::Masked) { config.shaderFlags |= shaders::ShaderFlags::AlphaTest; }
        } else {
            config.colorTargetCount = 1;   // transparent: color-only
        }
        return config;
    }

    bool MakeLayout(const rhi::BindGroupLayoutEntry& entry, rhi::BindGroupLayout*& out) {
        rhi::BindGroupLayoutDesc ld{};
        ld.entries = Span<const rhi::BindGroupLayoutEntry>{ &entry, 1 };
        return m_device->CreateBindGroupLayout(ld, out).IsOk();
    }

    bool MakePipelineLayout(rhi::BindGroupLayout* set0, rhi::BindGroupLayout* set1, rhi::BindGroupLayout* set2,
                            rhi::BindGroupLayout* set3, rhi::PipelineLayout*& out) {
        rhi::BindGroupLayout* layouts[] = { set0, set1, set2, set3 };
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 4 };
        return m_device->CreatePipelineLayout(pld, out).IsOk();
    }

    // Two-set pipeline layout (the depth-only shadow pipelines: light-view + object/instance).
    bool MakePipelineLayout(rhi::BindGroupLayout* set0, rhi::BindGroupLayout* set1, rhi::PipelineLayout*& out) {
        rhi::BindGroupLayout* layouts[] = { set0, set1 };
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 2 };
        return m_device->CreatePipelineLayout(pld, out).IsOk();
    }

    // Three-set pipeline layout (masked shadow: light-view + object/instance + material) — the material
    // set feeds the alpha-test fragment's albedo sample.
    bool MakePipelineLayout3(rhi::BindGroupLayout* set0, rhi::BindGroupLayout* set1, rhi::BindGroupLayout* set2,
                             rhi::PipelineLayout*& out) {
        rhi::BindGroupLayout* layouts[] = { set0, set1, set2 };
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 3 };
        return m_device->CreatePipelineLayout(pld, out).IsOk();
    }

    // Masked-shadow pipeline layout for a material set-2 layout, cached by (set2, instanced): light-view
    // (0) + object/instance (1) + material (2). Retired with the renderer.
    [[nodiscard]] rhi::PipelineLayout* GetOrCreateShadowMaskedLayout(rhi::BindGroupLayout* set2, bool instanced) {
        const u64 key = (reinterpret_cast<u64>(set2) * 2u) + (instanced ? 1u : 0u);
        if (rhi::PipelineLayout** cached = m_shadowMaskedLayouts.Find(key)) { return *cached; }
        rhi::BindGroupLayout* set1 = instanced ? m_instanceLayout : m_objectLayout;
        rhi::PipelineLayout* layout = nullptr;
        if (!MakePipelineLayout3(m_shadowViewLayout, set1, set2, layout)) { return nullptr; }
        m_shadowMaskedLayouts.InsertOrAssign(key, layout);
        return layout;
    }

    // The default material — a standard PBR material (materials::CreatePBR) used whenever a draw
    // carries no material. Its set-2 layout/bind group flow through the same data-driven path as any
    // other material, so there is nothing special about the "no material" case.
    Status CreateDefaultMaterial() {
        m_defaultMaterial = materials::CreatePBR(u8"__default_pbr");
        return (m_defaultMaterial.Get() != nullptr) ? Status{} : Status{ ErrorCode::Unknown };
    }

    // The MaterialInstance the renderer owns for `material` (one per material, created lazily). The
    // instance carries per-draw overrides (textures/uniforms) + caches its bind group in the system.
    [[nodiscard]] materials::MaterialInstance* InstanceFor(materials::Material* material) {
        if (materials::MaterialInstance** found = m_instances.Find(material)) { return *found; }
        UniquePtr<materials::MaterialInstance> created = MakeUnique<materials::MaterialInstance>(DefaultAllocator(), material);
        materials::MaterialInstance* inst = created.Get();
        m_instanceStorage.PushBack(Move(created));       // owns the instance
        m_instances.InsertOrAssign(material, inst);       // raw lookup (HashMap can't hold UniquePtr)
        return inst;
    }

    // The set-2 (material) bind group for `material`, built data-driven from its property list by the
    // material system (UBO + textures/samplers in declared order, with white/flat-normal/default-sampler
    // fallbacks for unset slots). `material` is non-null (callers substitute the default material).
    [[nodiscard]] rhi::BindGroup* MaterialBindGroup(materials::Material* material) {
        rhi::BindGroupLayout* layout = m_materials->GetOrCreateLayout(*material);
        return m_materials->PrepareInstance(*InstanceFor(material), layout);
    }

    // The forward pipeline layout for a given material set-2 layout, cached by (set2 layout, instanced).
    // set0 = view, set1 = object (single) / instances (instanced), set2 = material, set3 = clusters.
    [[nodiscard]] rhi::PipelineLayout* GetOrCreatePipelineLayout(rhi::BindGroupLayout* set2, bool instanced) {
        const u64 key = (reinterpret_cast<u64>(set2) * 2u) + (instanced ? 1u : 0u);
        if (rhi::PipelineLayout** cached = m_pipelineLayouts.Find(key)) { return *cached; }
        rhi::BindGroupLayout* set1 = instanced ? m_instanceLayout : m_objectLayout;
        rhi::PipelineLayout* layout = nullptr;
        if (!MakePipelineLayout(m_viewLayout, set1, set2, m_clusterLayout, layout)) { return nullptr; }
        m_pipelineLayouts.InsertOrAssign(key, layout);
        return layout;
    }

    // Per-frame bind groups (view/shadow-view/object/instance) are rebuilt when their inputs change —
    // e.g. the directional shadow view alternates per in-flight slot, so the view BG rebuilds every
    // frame. The OLD bind group may still be referenced by an in-flight command buffer, so it can't be
    // freed immediately (vkFreeDescriptorSets-00309): retire it and free after the frame ring cycles.
    void RetireBindGroup(rhi::BindGroup* bg) {
        if (bg != nullptr) { m_retiredBGs.PushBack(RetiredBG{ bg, m_framesInFlight }); }
    }
    void TickRetiredBindGroups() {
        usize w = 0;
        for (usize i = 0; i < m_retiredBGs.Size(); ++i) {
            RetiredBG r = m_retiredBGs[i];
            if (r.framesLeft <= 1) { m_device->DestroyBindGroup(r.bg); }
            else { r.framesLeft -= 1; m_retiredBGs[w++] = r; }
        }
        m_retiredBGs.Resize(w);
    }

    // (Re)create a bind group over a ring's buffer when the ring (re)allocated. `whole` binds
    // the entire buffer (storage, indexed); otherwise a `bindSize` window (dynamic-offset UBO).
    bool EnsureBindGroup(DynamicUniformRing& ring, rhi::BindGroupLayout* layout, u64 bindSize,
                         rhi::BindGroup*& bg, u32& bgGen, bool whole) {
        if (bg != nullptr && bgGen == ring.Generation()) { return true; }
        RetireBindGroup(bg); bg = nullptr;
        rhi::Buffer* buffer = ring.Buffer();
        if (buffer == nullptr) { return false; }
        rhi::BindGroupEntry be = rhi::BindGroupEntry::BufferEntry(buffer, 0, whole ? ring.ByteCapacity() : bindSize);
        rhi::BindGroupDesc bgd{};
        bgd.layout = layout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ &be, 1 };
        if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { bg = nullptr; return false; }
        bgGen = ring.Generation();
        return true;
    }

    // The set-0 bind group spans two rings: the per-view UBO (dynamic-offset window of one
    // ViewData) + the light list (whole light buffer, read as Lights[lightOffset + i]). Rebuild
    // when either ring (re)allocated this frame.
    bool EnsureViewBindGroup() {
        if (m_activeShadowView == nullptr) { m_activeShadowView = m_dummyShadowView; }
        if (m_activeAtlasView == nullptr)  { m_activeAtlasView  = m_dummyAtlasView; }
        if (m_activeShBuffer == nullptr)   { m_activeShBuffer   = m_dummyShBuffer; }
        if (m_activePrefilter == nullptr)  { m_activePrefilter  = m_dummyCubeView; }
        if (m_activeBrdf == nullptr)       { m_activeBrdf       = m_dummyBrdfView; }
        if (m_activeProbeCube == nullptr)   { m_activeProbeCube   = m_dummyProbeCubeView; }
        if (m_activeProbeBuffer == nullptr) { m_activeProbeBuffer = m_dummyProbeBuffer; }
        if (m_viewBG != nullptr && m_viewBGViewGen == m_viewRing.Generation() &&
            m_viewBGLightGen == m_lightRing.Generation() &&
            m_viewBGShadow == m_activeShadowView && m_viewBGShadowGen == m_activeShadowGen &&
            m_viewBGAtlas == m_activeAtlasView && m_viewBGAtlasGen == m_activeAtlasGen &&
            m_viewBGLocalGen == m_localShadowRing.Generation() &&
            m_viewBGBoneGen == m_boneDeviceGen &&
            m_viewBGIblGen == m_activeIblGen && m_viewBGPrefilter == m_activePrefilter &&
            m_viewBGProbeCube == m_activeProbeCube && m_viewBGProbeBuf == m_activeProbeBuffer) {
            return true;
        }
        RetireBindGroup(m_viewBG); m_viewBG = nullptr;
        rhi::Buffer* viewBuf = m_viewRing.Buffer();
        rhi::Buffer* lightBuf = m_lightRing.Buffer();
        rhi::Buffer* localBuf = m_localShadowRing.Buffer();
        rhi::Buffer* boneBuf  = m_boneDevice;   // VS reads the device mirror, not the staging ring
        if (viewBuf == nullptr || lightBuf == nullptr || localBuf == nullptr || boneBuf == nullptr ||
            m_activeShadowView == nullptr || m_activeAtlasView == nullptr || m_shadowSampler == nullptr ||
            m_activeShBuffer == nullptr || m_activePrefilter == nullptr || m_activeBrdf == nullptr || m_envSampler == nullptr) { return false; }
        // Order must match the set-0 layout: view UBO, lights, cascade map (t1), local atlas (t2),
        // local-shadow entries (t3), comparison sampler (s0), bone-matrix pool (t4), IBL SH9 (t5),
        // prefilter cube (t6), BRDF LUT (t7), env sampler (s1). Buffers bound whole.
        rhi::BindGroupEntry entries[] = {
            rhi::BindGroupEntry::BufferEntry(viewBuf, 0, sizeof(ViewData)),
            rhi::BindGroupEntry::BufferEntry(lightBuf, 0, m_lightRing.ByteCapacity()),
            rhi::BindGroupEntry::TextureEntry(m_activeShadowView),
            rhi::BindGroupEntry::TextureEntry(m_activeAtlasView),
            rhi::BindGroupEntry::BufferEntry(localBuf, 0, m_localShadowRing.ByteCapacity()),
            rhi::BindGroupEntry::SamplerEntry(m_shadowSampler),
            rhi::BindGroupEntry::BufferEntry(boneBuf, 0, m_boneDeviceBytes),
            rhi::BindGroupEntry::BufferEntry(m_activeShBuffer, 0, kShBytes),
            rhi::BindGroupEntry::TextureEntry(m_activePrefilter),
            rhi::BindGroupEntry::TextureEntry(m_activeBrdf),
            rhi::BindGroupEntry::SamplerEntry(m_envSampler),
            rhi::BindGroupEntry::TextureEntry(m_activeProbeCube),
            rhi::BindGroupEntry::BufferEntry(m_activeProbeBuffer, 0, kProbeBufferBytes),
        };
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_viewLayout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ entries, 13 };
        if (!m_device->CreateBindGroup(bgd, m_viewBG).IsOk()) { m_viewBG = nullptr; return false; }
        m_viewBGViewGen = m_viewRing.Generation();
        m_viewBGLightGen = m_lightRing.Generation();
        m_viewBGShadow = m_activeShadowView;
        m_viewBGShadowGen = m_activeShadowGen;
        m_viewBGAtlas = m_activeAtlasView;
        m_viewBGAtlasGen = m_activeAtlasGen;
        m_viewBGLocalGen = m_localShadowRing.Generation();
        m_viewBGBoneGen = m_boneDeviceGen;
        m_viewBGIblGen = m_activeIblGen;
        m_viewBGPrefilter = m_activePrefilter;
        m_viewBGProbeCube = m_activeProbeCube;
        m_viewBGProbeBuf  = m_activeProbeBuffer;
        return true;
    }

    // The comparison sampler (hardware PCF) + a 1x1 dummy depth map bound into set 0 when no shadow
    // caster exists this frame (so the descriptor set is always complete).
    Status CreateShadowResources() {
        rhi::SamplerDesc sd{};
        sd.minFilter = rhi::FilterMode::Linear; sd.magFilter = rhi::FilterMode::Linear;
        sd.mipmapFilter = rhi::MipmapFilterMode::Nearest;
        sd.addressU = rhi::AddressMode::ClampToEdge; sd.addressV = rhi::AddressMode::ClampToEdge; sd.addressW = rhi::AddressMode::ClampToEdge;
        sd.compare = rhi::CompareFunction::LessEqual;   // lit when fragment depth <= stored depth
        sd.label = u8"mesh.shadowSampler";
        if (!m_device->CreateSampler(sd, m_shadowSampler).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::TextureDesc td{};
        td.format = rhi::TextureFormat::Depth32Float; td.width = 1; td.height = 1; td.arrayLayerCount = 1;
        td.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
        td.label = u8"mesh.dummyShadow";
        if (!m_device->CreateTexture(td, m_dummyShadowTex).IsOk()) { return Status{ ErrorCode::Unknown }; }
        // A Texture2DArray view (1 layer) so it matches the shader's Texture2DArray shadow binding.
        rhi::TextureViewDesc vd{}; vd.format = rhi::TextureFormat::Depth32Float; vd.aspect = rhi::TextureAspect::DepthOnly;
        vd.dimension = rhi::TextureViewDimension::Texture2DArray; vd.arrayLayerCount = 1;
        if (!m_device->CreateTextureView(m_dummyShadowTex, vd, m_dummyShadowView).IsOk()) { return Status{ ErrorCode::Unknown }; }
        m_activeShadowView = m_dummyShadowView;

        // A 1x1 Texture2D dummy for the local-shadow atlas (t2) + a 1-element dummy data buffer (t3),
        // bound when no local shadow caster exists this frame (the descriptor set stays complete).
        rhi::TextureDesc atd{};
        atd.format = rhi::TextureFormat::Depth32Float; atd.width = 1; atd.height = 1; atd.arrayLayerCount = 2;
        atd.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
        atd.label = u8"mesh.dummyAtlas";
        if (!m_device->CreateTexture(atd, m_dummyAtlasTex).IsOk()) { return Status{ ErrorCode::Unknown }; }
        rhi::TextureViewDesc avd{}; avd.format = rhi::TextureFormat::Depth32Float; avd.aspect = rhi::TextureAspect::DepthOnly;
        avd.dimension = rhi::TextureViewDimension::Texture2DArray; avd.arrayLayerCount = 2;
        if (!m_device->CreateTextureView(m_dummyAtlasTex, avd, m_dummyAtlasView).IsOk()) { return Status{ ErrorCode::Unknown }; }
        m_activeAtlasView = m_dummyAtlasView;

        // IBL fallbacks (bound when no environment is active): a zero-filled SH9 buffer (=> no diffuse
        // ambient), a 1x1x6 black cube (prefilter), a 1x1 black BRDF LUT, and a linear-clamp env
        // sampler. The color dummies transition UNDEFINED->ShaderRead once (with the depth dummies).
        rhi::SamplerDesc es{};
        es.minFilter = rhi::FilterMode::Linear; es.magFilter = rhi::FilterMode::Linear; es.mipmapFilter = rhi::MipmapFilterMode::Linear;
        es.addressU = rhi::AddressMode::ClampToEdge; es.addressV = rhi::AddressMode::ClampToEdge; es.addressW = rhi::AddressMode::ClampToEdge;
        es.label = u8"mesh.envSampler";
        if (!m_device->CreateSampler(es, m_envSampler).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BufferDesc shd{}; shd.size = kShBytes; shd.usage = rhi::BufferUsage::Storage; shd.memory = rhi::MemoryLocation::GpuOnly; shd.label = u8"mesh.dummySH";
        if (!m_device->CreateBuffer(shd, m_dummyShBuffer).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::TextureDesc cd{};
        cd.format = rhi::TextureFormat::RGBA16Float; cd.width = 1; cd.height = 1; cd.arrayLayerCount = 6;
        cd.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst; cd.label = u8"mesh.dummyCube";
        if (!m_device->CreateTexture(cd, m_dummyCube).IsOk()) { return Status{ ErrorCode::Unknown }; }
        rhi::TextureViewDesc cvd{}; cvd.format = rhi::TextureFormat::RGBA16Float;
        cvd.dimension = rhi::TextureViewDimension::TextureCube; cvd.arrayLayerCount = 6;
        if (!m_device->CreateTextureView(m_dummyCube, cvd, m_dummyCubeView).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::TextureDesc ld{};
        ld.format = rhi::TextureFormat::RG16Float; ld.width = 1; ld.height = 1;
        ld.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst; ld.label = u8"mesh.dummyBRDF";
        if (!m_device->CreateTexture(ld, m_dummyBrdf).IsOk()) { return Status{ ErrorCode::Unknown }; }
        rhi::TextureViewDesc lvd{}; lvd.format = rhi::TextureFormat::RG16Float; lvd.dimension = rhi::TextureViewDimension::Texture2D;
        if (!m_device->CreateTextureView(m_dummyBrdf, lvd, m_dummyBrdfView).IsOk()) { return Status{ ErrorCode::Unknown }; }

        // Dummy probe cube-ARRAY (6 layers = 1 cube) bound when no probe is active — count 0 keeps the
        // forward on the global IBL reflection, so the content is irrelevant.
        rhi::TextureDesc pcd{};
        pcd.format = rhi::TextureFormat::RGBA16Float; pcd.width = 1; pcd.height = 1; pcd.arrayLayerCount = 6;
        pcd.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst; pcd.label = u8"mesh.dummyProbeCube";
        if (!m_device->CreateTexture(pcd, m_dummyProbeCube).IsOk()) { return Status{ ErrorCode::Unknown }; }
        rhi::TextureViewDesc pcv{}; pcv.format = rhi::TextureFormat::RGBA16Float;
        pcv.dimension = rhi::TextureViewDimension::TextureCubeArray; pcv.arrayLayerCount = 6;
        if (!m_device->CreateTextureView(m_dummyProbeCube, pcv, m_dummyProbeCubeView).IsOk()) { return Status{ ErrorCode::Unknown }; }
        // Dummy probe-metadata buffer (t9) for the no-probe path — never read (count 0), just a valid SRV.
        rhi::BufferDesc pbd{};
        pbd.size = kProbeBufferBytes; pbd.usage = rhi::BufferUsage::Storage;
        pbd.memory = rhi::MemoryLocation::GpuOnly; pbd.label = u8"mesh.dummyProbeBuf";
        if (!m_device->CreateBuffer(pbd, m_dummyProbeBuffer).IsOk()) { return Status{ ErrorCode::Unknown }; }

        m_activeShBuffer    = m_dummyShBuffer;
        m_activePrefilter   = m_dummyCubeView;
        m_activeBrdf        = m_dummyBrdfView;
        m_activeProbeCube   = m_dummyProbeCubeView;
        m_activeProbeBuffer = m_dummyProbeBuffer;
        return Status{};
    }

    // The set-0 bind group for the shadow depth pass: just the light-view UBO (dynamic-offset
    // window of one ShadowViewData). Rebuilt when the shadow-view ring (re)allocated.
    bool EnsureShadowViewBindGroup() {
        if (m_shadowViewBG != nullptr && m_shadowViewBGGen == m_shadowViewRing.Generation() &&
            m_shadowViewBGBoneGen == m_boneDeviceGen) { return true; }
        RetireBindGroup(m_shadowViewBG); m_shadowViewBG = nullptr;
        rhi::Buffer* buf = m_shadowViewRing.Buffer();
        rhi::Buffer* boneBuf = m_boneDevice;   // shared device mirror (same as the forward set 0)
        if (buf == nullptr || boneBuf == nullptr) { return false; }
        rhi::BindGroupEntry be[] = {
            rhi::BindGroupEntry::BufferEntry(buf, 0, sizeof(ShadowViewData)),
            rhi::BindGroupEntry::BufferEntry(boneBuf, 0, m_boneDeviceBytes),   // t4: skinning pool
        };
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_shadowViewLayout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ be, 2 };
        if (!m_device->CreateBindGroup(bgd, m_shadowViewBG).IsOk()) { m_shadowViewBG = nullptr; return false; }
        m_shadowViewBGGen = m_shadowViewRing.Generation();
        m_shadowViewBGBoneGen = m_boneDeviceGen;
        return true;
    }

    // Tiny placeholder cluster buffers + a set-3 bind group over them, bound when clustering is
    // unavailable. The shader's ClusterGridX==0 path never reads them, but set 3 must be bound.
    Status CreateDummyClusters() {
        rhi::BufferDesc obd{}; obd.size = sizeof(u32) * 2; obd.usage = rhi::BufferUsage::Storage; obd.memory = rhi::MemoryLocation::GpuOnly; obd.label = u8"cluster.dummyOffsets";
        if (!m_device->CreateBuffer(obd, m_dummyClusterOffsets).IsOk()) { return Status{ ErrorCode::Unknown }; }
        rhi::BufferDesc ibd{}; ibd.size = sizeof(u32); ibd.usage = rhi::BufferUsage::Storage; ibd.memory = rhi::MemoryLocation::GpuOnly; ibd.label = u8"cluster.dummyIndices";
        if (!m_device->CreateBuffer(ibd, m_dummyClusterIndices).IsOk()) { return Status{ ErrorCode::Unknown }; }
        rhi::BindGroupEntry entries[] = {
            rhi::BindGroupEntry::BufferEntry(m_dummyClusterOffsets, 0, sizeof(u32) * 2),
            rhi::BindGroupEntry::BufferEntry(m_dummyClusterIndices, 0, sizeof(u32)),
        };
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_clusterLayout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ entries, 2 };
        if (!m_device->CreateBindGroup(bgd, m_dummyClusterBG).IsOk()) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    // The set-3 bind group for a frame-in-flight slot, over the ClusterSystem's per-frame cluster
    // buffers. One per slot (the buffers alternate per frame) so a slot's group is stable.
    // Rebuild when the cluster buffers' VERSION changes (the ClusterSystem bumps it on realloc).
    // Pointer identity is NOT a safe "unchanged" test: a freed rhi::Buffer* address can be reused by
    // the new allocation, so a stale bind group would point at a destroyed VkBuffer (use-after-free).
    rhi::BindGroup* EnsureClusterBindGroup(u32 slot, rhi::Buffer* offsets, rhi::Buffer* indices, u32 version) {
        if (slot >= kMaxClusterSlots || offsets == nullptr || indices == nullptr) { return m_dummyClusterBG; }
        if (m_clusterBGs[slot] != nullptr && m_clusterBGOffsets[slot] == offsets && m_clusterBGVersion[slot] == version) {
            return m_clusterBGs[slot];
        }
        if (m_clusterBGs[slot] != nullptr) { m_device->DestroyBindGroup(m_clusterBGs[slot]); m_clusterBGs[slot] = nullptr; }
        rhi::BindGroupEntry entries[] = {
            rhi::BindGroupEntry::BufferEntry(offsets, 0, offsets->desc.size),
            rhi::BindGroupEntry::BufferEntry(indices, 0, indices->desc.size),
        };
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_clusterLayout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ entries, 2 };
        if (!m_device->CreateBindGroup(bgd, m_clusterBGs[slot]).IsOk()) { m_clusterBGs[slot] = nullptr; return m_dummyClusterBG; }
        m_clusterBGOffsets[slot] = offsets;
        m_clusterBGVersion[slot] = version;
        return m_clusterBGs[slot];
    }

    void Shutdown() {
        // Release material instances first (their dtors notify the still-live MaterialSystem, which
        // owns + destroys their data-driven bind groups). The default material's RefPtr then drops.
        m_instances.Clear();
        m_instanceStorage.Clear();
        m_defaultMaterial.Reset();
        m_meshes.Clear();
        for (RetiredBG& r : m_retiredBGs) { m_device->DestroyBindGroup(r.bg); }
        m_retiredBGs.Clear();
        if (m_viewBG)       { m_device->DestroyBindGroup(m_viewBG); m_viewBG = nullptr; }
        if (m_shadowViewBG) { m_device->DestroyBindGroup(m_shadowViewBG); m_shadowViewBG = nullptr; }
        if (m_boneDevice)   { m_device->DestroyBuffer(m_boneDevice); m_boneDevice = nullptr; m_boneDeviceBytes = 0; }
        if (m_dummyShadowView) { m_device->DestroyTextureView(m_dummyShadowView); m_dummyShadowView = nullptr; }
        if (m_dummyShadowTex)  { m_device->DestroyTexture(m_dummyShadowTex); m_dummyShadowTex = nullptr; }
        if (m_dummyAtlasView)  { m_device->DestroyTextureView(m_dummyAtlasView); m_dummyAtlasView = nullptr; }
        if (m_dummyAtlasTex)   { m_device->DestroyTexture(m_dummyAtlasTex); m_dummyAtlasTex = nullptr; }
        if (m_shadowSampler)   { m_device->DestroySampler(m_shadowSampler); m_shadowSampler = nullptr; }
        if (m_dummyCubeView)   { m_device->DestroyTextureView(m_dummyCubeView); m_dummyCubeView = nullptr; }
        if (m_dummyCube)       { m_device->DestroyTexture(m_dummyCube); m_dummyCube = nullptr; }
        if (m_dummyBrdfView)   { m_device->DestroyTextureView(m_dummyBrdfView); m_dummyBrdfView = nullptr; }
        if (m_dummyBrdf)       { m_device->DestroyTexture(m_dummyBrdf); m_dummyBrdf = nullptr; }
        if (m_dummyProbeCubeView) { m_device->DestroyTextureView(m_dummyProbeCubeView); m_dummyProbeCubeView = nullptr; }
        if (m_dummyProbeCube)  { m_device->DestroyTexture(m_dummyProbeCube); m_dummyProbeCube = nullptr; }
        if (m_dummyProbeBuffer) { m_device->DestroyBuffer(m_dummyProbeBuffer); m_dummyProbeBuffer = nullptr; }
        if (m_dummyShBuffer)   { m_device->DestroyBuffer(m_dummyShBuffer); m_dummyShBuffer = nullptr; }
        if (m_envSampler)      { m_device->DestroySampler(m_envSampler); m_envSampler = nullptr; }
        if (m_objectBG)   { m_device->DestroyBindGroup(m_objectBG); m_objectBG = nullptr; }
        if (m_instanceBG) { m_device->DestroyBindGroup(m_instanceBG); m_instanceBG = nullptr; }
        for (u32 i = 0; i < kMaxClusterSlots; ++i) {
            if (m_clusterBGs[i] != nullptr) { m_device->DestroyBindGroup(m_clusterBGs[i]); m_clusterBGs[i] = nullptr; }
        }
        if (m_dummyClusterBG)      { m_device->DestroyBindGroup(m_dummyClusterBG); m_dummyClusterBG = nullptr; }
        if (m_dummyClusterOffsets) { m_device->DestroyBuffer(m_dummyClusterOffsets); m_dummyClusterOffsets = nullptr; }
        if (m_dummyClusterIndices) { m_device->DestroyBuffer(m_dummyClusterIndices); m_dummyClusterIndices = nullptr; }
        for (auto& kv : m_pipelineLayouts) { if (kv.value != nullptr) { m_device->DestroyPipelineLayout(kv.value); } }
        m_pipelineLayouts.Clear();
        for (auto& kv : m_shadowMaskedLayouts) { if (kv.value != nullptr) { m_device->DestroyPipelineLayout(kv.value); } }
        m_shadowMaskedLayouts.Clear();
        if (m_shadowPipelineLayoutSingle)    { m_device->DestroyPipelineLayout(m_shadowPipelineLayoutSingle); m_shadowPipelineLayoutSingle = nullptr; }
        if (m_shadowPipelineLayoutInstanced) { m_device->DestroyPipelineLayout(m_shadowPipelineLayoutInstanced); m_shadowPipelineLayoutInstanced = nullptr; }
        if (m_viewLayout)     { m_device->DestroyBindGroupLayout(m_viewLayout); m_viewLayout = nullptr; }
        if (m_objectLayout)   { m_device->DestroyBindGroupLayout(m_objectLayout); m_objectLayout = nullptr; }
        if (m_instanceLayout) { m_device->DestroyBindGroupLayout(m_instanceLayout); m_instanceLayout = nullptr; }
        if (m_clusterLayout)  { m_device->DestroyBindGroupLayout(m_clusterLayout); m_clusterLayout = nullptr; }
        if (m_shadowViewLayout) { m_device->DestroyBindGroupLayout(m_shadowViewLayout); m_shadowViewLayout = nullptr; }
        // rings free their buffers in their destructors (m_device still valid after this).
    }

    rhi::Device*                   m_device;
    shaders::ShaderSystem*         m_shaders;
    materials::PipelineStateCache* m_psoCache;
    materials::MaterialSystem*     m_materials;
    GpuMeshCache                   m_meshes;
    u32                            m_framesInFlight = 2;

    rhi::BindGroupLayout* m_viewLayout     = nullptr;
    rhi::BindGroupLayout* m_objectLayout   = nullptr;
    rhi::BindGroupLayout* m_instanceLayout = nullptr;
    rhi::BindGroupLayout* m_clusterLayout  = nullptr;
    rhi::BindGroupLayout* m_shadowViewLayout = nullptr;   // set 0 for the depth-only shadow pipeline
    rhi::PipelineLayout*  m_shadowPipelineLayoutSingle    = nullptr;
    rhi::PipelineLayout*  m_shadowPipelineLayoutInstanced = nullptr;
    // Forward pipeline layouts, keyed by (material set-2 layout, instanced); built on demand so each
    // material's own set-2 layout drives the PSO (material-driven; supports custom shaders).
    HashMap<u64, rhi::PipelineLayout*> m_pipelineLayouts;
    HashMap<u64, rhi::PipelineLayout*> m_shadowMaskedLayouts;   // masked-shadow 3-set layouts (by set-2, instanced)

    // Material set-2 resources. The default material (standard PBR) backs draws with no material;
    // instances carry per-material overrides and flow through the material system's data-driven BG path.
    RefPtr<materials::Material>                                m_defaultMaterial;
    HashMap<materials::Material*, materials::MaterialInstance*> m_instances;        // lookup (raw)
    Array<UniquePtr<materials::MaterialInstance>>              m_instanceStorage;  // ownership

    DynamicUniformRing m_viewRing;
    DynamicUniformRing m_shadowViewRing;
    DynamicUniformRing m_objectRing;
    DynamicUniformRing m_instanceRing;
    DynamicUniformRing m_offsetsRing;
    DynamicUniformRing m_lightRing;
    DynamicUniformRing m_localShadowRing;   // per-frame GpuLocalShadow entries (spot/point atlas)
    DynamicUniformRing m_boneRing;          // bone-matrix STAGING ring (CpuToGpu; written once/frame)
    rhi::Buffer*       m_boneDevice = nullptr;   // GpuOnly device mirror the VS reads (set-0 t4 SRV)
    u64               m_boneDeviceBytes = 0;
    u32               m_boneDeviceGen   = 0;     // bumps on (re)create -> invalidates set-0 bind groups
    // Per-frame map: a skinned instance's boneMatrices pointer -> its bases (matrix units) in the pool.
    struct BoneSlot { u32 base = 0; u32 prevBase = 0; };
    struct SkinnedRef { const Mat4* cur; const Mat4* prev; u32 count; };
    HashMap<const Mat4*, BoneSlot> m_boneStart;
    Array<SkinnedRef>              m_skinnedScratch;

    // Per-entity previous-frame world matrix, for rigid-object motion vectors. FLAT double-buffer indexed
    // by entity INDEX (entityId low 32 bits) — not a hashmap: direct O(1) index, no hashing/probing/rehash
    // (the per-instance Find was the motion-vector hot cost at scale). m_prevWorld holds LAST frame's worlds
    // (read by every view this frame); resolves write THIS frame's into m_curWorld; the two swap at
    // FinishFrame. Out-of-range / never-written -> prev == cur (no motion), so newly-visible objects don't
    // smear on their first frame.
    Array<Mat4> m_prevWorld;
    Array<Mat4> m_curWorld;

    // Record `cur` as this frame's world (idempotent across a frame's views — same value each time) and
    // return the entity's previous-frame world (or `cur` if unknown). Indexed by entity index so multiple
    // views resolving the same object read a STABLE prev (writing cur never clobbers prev — separate buffers).
    [[nodiscard]] Mat4 PrevWorldFor(u64 entityId, const Mat4& cur) {
        const u32 idx = static_cast<u32>(entityId);   // entityId = (generation << 32) | index
        if (idx >= m_curWorld.Size()) { m_curWorld.Resize(idx + 1u); }   // grows toward the max live index, then stable
        m_curWorld[idx] = cur;
        return (idx < m_prevWorld.Size()) ? m_prevWorld[idx] : cur;
    }

    // Camera depth-prepass -> forward instance sharing. The prepass fills the FULL InstanceData (world +
    // real prevWorld + tint) for each opaque (mesh,material) group once and records the group's DataOffsets
    // range here; the forward looks it up and REUSES that instance data instead of re-filling it (build once,
    // not twice). Keyed by (viewIndex, mesh, material); cleared each frame. Instanced path only.
    struct InstShare { u64 offsByteOffset = 0; u32 count = 0; };
    HashMap<u64, InstShare> m_instShareCache;
    // Keyed by the VIEW pointer (not viewIndex): the main view's prepass + forward share one RenderView, but
    // probe-capture forwards reuse viewIndex 0 with a different draw list — a distinct pointer avoids collision.
    static u64 InstShareKey(const void* view, const void* mesh, const void* mat) noexcept {
        u64 k = 1469598103934665603ull;
        k = (k ^ static_cast<u64>(reinterpret_cast<usize>(view))) * 1099511628211ull;
        k = (k ^ static_cast<u64>(reinterpret_cast<usize>(mesh))) * 1099511628211ull;
        k = (k ^ static_cast<u64>(reinterpret_cast<usize>(mat)))  * 1099511628211ull;
        return k;
    }

    // Bind groups retired this/prior frames but possibly still referenced by in-flight command
    // buffers; freed by TickRetiredBindGroups once the frame ring has cycled (framesLeft hits 0).
    struct RetiredBG { rhi::BindGroup* bg; u32 framesLeft; };
    Array<RetiredBG> m_retiredBGs;

    rhi::BindGroup* m_viewBG       = nullptr;
    rhi::BindGroup* m_shadowViewBG = nullptr;
    u32             m_shadowViewBGGen = 0;
    u32             m_shadowViewBGBoneGen = 0;
    rhi::BindGroup* m_objectBG   = nullptr;
    rhi::BindGroup* m_instanceBG = nullptr;
    // The set-0 bind group spans the view UBO + light SB + shadow map + sampler; rebuild it when any
    // of those change (rings roll over, or the active shadow map view changes).
    u32 m_viewBGViewGen = 0, m_viewBGLightGen = 0;
    // The set-0 shadow binding is cached by (view pointer, ShadowSystem generation): a freed shadow
    // texture's view address can be reused by a recreated one (5.2 atlas/resolution changes), so the
    // generation — not the pointer alone — is what reliably invalidates the bind group. See the
    // ClusterBinding::version + ShadowSystem::Generation() docs for the same rationale.
    rhi::TextureView* m_viewBGShadow = nullptr;
    u64               m_viewBGShadowGen = 0;
    // Shadow set-0 resources: the comparison sampler + a 1x1 dummy map; m_activeShadowView points at
    // the real ShadowSystem map (set each frame) or the dummy.
    rhi::Sampler*     m_shadowSampler    = nullptr;
    rhi::Texture*     m_dummyShadowTex   = nullptr;
    rhi::TextureView* m_dummyShadowView  = nullptr;
    rhi::TextureView* m_activeShadowView = nullptr;
    // The dummy shadow/atlas depth textures are bound (and statically sampled by the mesh shader) on
    // frames with no real caster, but live OUTSIDE the render graph, so the barrier solver never
    // transitions them out of UNDEFINED. Transition them once to DepthStencilRead the first frame we
    // hold the command encoder, so the layout the descriptor expects always matches (VUID-09600).
    bool              m_dummyDepthInit   = false;
    u64               m_activeShadowGen  = 0;
    // Local-light (spot/point) shadow atlas (t2) + per-light entries (t3) — 5.3. The atlas view + its
    // generation + the local-shadow ring generation extend the set-0 bind-group cache key.
    rhi::Texture*     m_dummyAtlasTex    = nullptr;
    rhi::TextureView* m_dummyAtlasView   = nullptr;
    rhi::TextureView* m_activeAtlasView  = nullptr;
    u64               m_activeAtlasGen   = 0;
    rhi::TextureView* m_viewBGAtlas      = nullptr;
    u64               m_viewBGAtlasGen   = 0;
    u32               m_viewBGLocalGen   = 0;
    u32               m_viewBGBoneGen    = 0;
    u32               m_localShadowBase  = 0;   // this frame's base into m_localShadowRing
    u32               m_localShadowPassCount = 0;   // # atlas depth passes (caster re-emits) this frame
    u32               m_captureFacePasses    = 0;   // # probe-capture face passes (caster re-emits) this frame
    u32 m_objectBGGen = 0, m_instanceBGGen = 0;

    // IBL (phase 6): SH9 diffuse buffer (t5) + prefiltered specular cube (t6) + BRDF LUT (t7) + a
    // linear env sampler (s1), all set 0. Neutral 1x1 dummies are bound when no environment is active.
    static constexpr u64 kShBytes = sizeof(f32) * 4 * 9;   // 9 RGB SH coeffs as float4
    rhi::Sampler*     m_envSampler   = nullptr;
    rhi::Buffer*      m_dummyShBuffer = nullptr;
    rhi::Texture*     m_dummyCube     = nullptr;  rhi::TextureView* m_dummyCubeView = nullptr;
    rhi::Texture*     m_dummyBrdf     = nullptr;  rhi::TextureView* m_dummyBrdfView = nullptr;
    rhi::Buffer*      m_activeShBuffer = nullptr;
    rhi::TextureView* m_activePrefilter = nullptr;
    rhi::TextureView* m_activeBrdf      = nullptr;
    rhi::TextureView* m_viewBGPrefilter = nullptr;   // bind-group cache key (active prefilter view)
    f32               m_iblMaxLod    = 0.0f;
    bool              m_iblActive    = false;
    u64               m_activeIblGen = 0;
    u64               m_viewBGIblGen = 0;

    // Reflection probes (P2-P4): prefiltered cube-ARRAY (t8) + probe-metadata SRV (t9) + probe count.
    static constexpr u64 kProbeBufferBytes = 64u * 16u;   // sizeof(GpuProbe)=64 * kMaxReflectionProbes=16
    rhi::Texture*     m_dummyProbeCube     = nullptr;  rhi::TextureView* m_dummyProbeCubeView = nullptr;
    rhi::Buffer*      m_dummyProbeBuffer   = nullptr;   // 1 zeroed record for the no-probe path
    rhi::TextureView* m_activeProbeCube    = nullptr;
    rhi::Buffer*      m_activeProbeBuffer  = nullptr;
    rhi::TextureView* m_viewBGProbeCube    = nullptr;   // bind-group cache key (view is created once => stable)
    rhi::Buffer*      m_viewBGProbeBuf     = nullptr;
    u32               m_activeProbeCount   = 0;

    // set 3 (clustered light lists). A dummy bound when clustering is off; otherwise one bind group
    // per (view, frame-in-flight) slot over the ClusterSystem's per-view cluster buffers.
    static constexpr u32 kMaxFramesInFlight = 8;
    static constexpr u32 kMaxViewsPerFrame  = 8;
    static constexpr u32 kMaxClusterSlots   = kMaxViewsPerFrame * kMaxFramesInFlight;
    rhi::Buffer*    m_dummyClusterOffsets = nullptr;
    rhi::Buffer*    m_dummyClusterIndices = nullptr;
    rhi::BindGroup* m_dummyClusterBG      = nullptr;
    rhi::BindGroup* m_clusterBGs[kMaxClusterSlots] = {};
    rhi::Buffer*    m_clusterBGOffsets[kMaxClusterSlots] = {};
    u32             m_clusterBGVersion[kMaxClusterSlots] = {};

    bool m_ready = false;
};

} // namespace draconic::render

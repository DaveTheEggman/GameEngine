/// Raptor::Render — the `:mesh_renderer` partition.
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

export module raptor.render:mesh_renderer;

import raptor.core;
import raptor.rhi;
import raptor.geometry;
import raptor.shaders;
import raptor.shaders.system;
import raptor.materials;
import raptor.materials.pso;
import :data;
import :views;
import :pipeline;
import :cluster_system;
import :resources;
import :gpu_mesh;

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

// Built-in forward shader, two permutations selected by INSTANCED. The view-projection is in a
// per-view UBO (set 0); the world matrix + tint come either from a per-object UBO (set 1) or,
// in the instanced permutation, from a per-instance StructuredBuffer indexed by the uint4
// DataOffsets vertex attribute (location 5). Vertex inputs use the RHI's TEXCOORDn convention
// (semantic index = location), matching VertexLayoutType::Mesh at locations 0..4.
inline constexpr const char8_t* kForwardVS = u8R"(
#define CASCADE_COUNT 4
cbuffer View : register(b0, space0) {
    row_major float4x4 ViewProj;   // Raptor matrices are row-major; annotate so HLSL reads them right.
    row_major float4x4 View;       // for view-space depth (cluster lookup + CSM cascade select, PS only)
    row_major float4x4 CascadeViewProj[CASCADE_COUNT];   // CSM: world -> each cascade's light clip
    float3 CameraPos; float LightCount;
    uint   LightOffset; int ClusterVpX; int ClusterVpY; uint _viewPad;
    uint   ClusterGridX; uint ClusterGridY; uint ClusterSliceCount; uint ClusterTileSize;
    float  ClusterNear;  float ClusterFar;  float ClusterLogScale;  float ClusterLogBias;
    float3 Ambient; float ShadowCascadeCount;          // 0 -> no shadow
    float4 CascadeSplitFar;        // view-space far depth of each cascade (cascade selection)
    float4 CascadeTexelSize;       // world units per shadow texel, per cascade (normal-offset bias)
    float  ShadowNormalBias; float ShadowDepthBias; float CascadeLayerBase; uint LocalShadowBase;
};
#ifdef INSTANCED
struct InstanceData { row_major float4x4 World; float4 Tint; };
StructuredBuffer<InstanceData> Instances : register(t0, space1);
#else
cbuffer Object : register(b0, space1) {
    row_major float4x4 World;
    float4             Tint;
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
};
struct VSOutput {
    float4 clip      : SV_Position;
    float3 normalWS  : TEXCOORD0;
    float4 color     : TEXCOORD1;
    float2 uv        : TEXCOORD2;
    float3 tangentWS : TEXCOORD3;
    float3 worldPos  : TEXCOORD4;
};
VSOutput main(VSInput input) {
    VSOutput o;
#ifdef INSTANCED
    float4x4 world = Instances[input.dataOffsets.x].World;
    float4   tint  = Instances[input.dataOffsets.x].Tint;
#else
    float4x4 world = World;
    float4   tint  = Tint;
#endif
    float4 worldPos = mul(float4(input.position, 1.0), world);
    o.clip      = mul(worldPos, ViewProj);
    o.normalWS  = normalize(mul(float4(input.normal, 0.0), world).xyz);
    o.color     = input.color * tint;                           // vertex color * per-instance tint
    o.uv        = input.uv;                                     // consume the full vertex layout
    o.tangentWS = mul(float4(input.tangent, 0.0), world).xyz;
    o.worldPos  = worldPos.xyz;
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
    uint   LightOffset; int ClusterVpX; int ClusterVpY; uint _viewPad;
    uint   ClusterGridX; uint ClusterGridY; uint ClusterSliceCount; uint ClusterTileSize;
    float  ClusterNear;  float ClusterFar;  float ClusterLogScale;  float ClusterLogBias;
    float3 Ambient; float ShadowCascadeCount;
    float4 CascadeSplitFar;
    float4 CascadeTexelSize;
    float  ShadowNormalBias; float ShadowDepthBias; float CascadeLayerBase; uint LocalShadowBase;
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
struct PSInput {
    float4 clip      : SV_Position;
    float3 normalWS  : TEXCOORD0;
    float4 color     : TEXCOORD1;
    float2 uv        : TEXCOORD2;
    float3 tangentWS : TEXCOORD3;
    float3 worldPos  : TEXCOORD4;
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

float4 main(PSInput input) : SV_Target {
    float3 N = normalize(input.normalWS);
    float3 V = normalize(CameraPos - input.worldPos);

    float3 albedo    = input.color.rgb * BaseColor.rgb;        // (vertex color * tint) * base color
    float  metallic  = saturate(Metallic);
    float  roughness = clamp(Roughness, 0.045, 1.0);
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

    float3 ambient = albedo * Ambient;                         // per-scene environment ambient (IBL later)
    return float4(ambient + Lo, 1.0);
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
#ifdef INSTANCED
struct InstanceData { row_major float4x4 World; float4 Tint; };
StructuredBuffer<InstanceData> Instances : register(t0, space1);
#else
cbuffer Object : register(b0, space1) {
    row_major float4x4 World;
    float4             Tint;
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
};
float4 main(VSInput input) : SV_Position {
#ifdef INSTANCED
    float4x4 world = Instances[input.dataOffsets.x].World;
#else
    float4x4 world = World;
#endif
    float4 worldPos = mul(float4(input.position, 1.0), world);
    return mul(worldPos, LightViewProj);
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
          m_localShadowRing(device, framesInFlight, sizeof(GpuLocalShadow), rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst, u8"mesh.localShadows") {}

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
        rhi::BindGroupLayoutEntry set0[] = { viewEntry, lightEntry, shadowTexEntry, atlasTexEntry, localShadowEntry, shadowSampEntry };
        rhi::BindGroupLayoutDesc s0d{};
        s0d.entries = Span<const rhi::BindGroupLayoutEntry>{ set0, 6 };
        if (!m_device->CreateBindGroupLayout(s0d, m_viewLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        // set 1 (non-instanced): per-object UBO (World + Tint), dynamic offset.
        rhi::BindGroupLayoutEntry objEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex);
        objEntry.hasDynamicOffset = true;
        if (!MakeLayout(objEntry, m_objectLayout)) { return Status{ ErrorCode::Unknown }; }

        // set 1 (instanced): per-instance StructuredBuffer (read-only storage).
        rhi::BindGroupLayoutEntry instEntry = rhi::BindGroupLayoutEntry::StorageBuffer(0, rhi::ShaderStage::Vertex, /*readOnly*/ true);
        if (!MakeLayout(instEntry, m_instanceLayout)) { return Status{ ErrorCode::Unknown }; }

        // set 2: material — a Fragment-stage UBO at b0 (the standard forward material's BaseColor
        // etc). Inferred-per-material layouts share this shape, so one pipeline layout fits them;
        // PrepareInstance is given THIS layout so the instance bind group is compatible.
        rhi::BindGroupLayoutEntry matEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Fragment);
        if (!MakeLayout(matEntry, m_materialLayout)) { return Status{ ErrorCode::Unknown }; }

        // set 3: clustered light lists — per-cluster (offset,count) SRV (t0) + flat index SRV (t1).
        rhi::BindGroupLayoutEntry clOffEntry = rhi::BindGroupLayoutEntry::StorageBuffer(0, rhi::ShaderStage::Fragment, /*readOnly*/ true);
        rhi::BindGroupLayoutEntry clIdxEntry = rhi::BindGroupLayoutEntry::StorageBuffer(1, rhi::ShaderStage::Fragment, /*readOnly*/ true);
        rhi::BindGroupLayoutEntry set3[] = { clOffEntry, clIdxEntry };
        rhi::BindGroupLayoutDesc s3d{};
        s3d.entries = Span<const rhi::BindGroupLayoutEntry>{ set3, 2 };
        if (!m_device->CreateBindGroupLayout(s3d, m_clusterLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        if (!MakePipelineLayout(m_viewLayout, m_objectLayout,   m_materialLayout, m_clusterLayout, m_pipelineLayoutSingle))   { return Status{ ErrorCode::Unknown }; }
        if (!MakePipelineLayout(m_viewLayout, m_instanceLayout, m_materialLayout, m_clusterLayout, m_pipelineLayoutInstanced)) { return Status{ ErrorCode::Unknown }; }

        // Shadow depth-only path (phase 5): a vertex-only shader + a 2-set pipeline layout
        // (set 0 = light view UBO, set 1 = the SAME object/instance layouts as forward, so the
        // object/instance bind groups are reused). No material/cluster sets.
        m_shaders->RegisterSource(u8"shadow_depth", shaders::ShaderStage::Vertex, kShadowVS);
        rhi::BindGroupLayoutEntry shadowViewEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex);
        shadowViewEntry.hasDynamicOffset = true;
        if (!MakeLayout(shadowViewEntry, m_shadowViewLayout)) { return Status{ ErrorCode::Unknown }; }
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
        if (maxDraws == 0) { return; }
        // camera draws + per-cascade re-emit (CSM) + per-spot-tile re-emit (local atlas).
        const u32 drawCap = maxDraws * (1u + ShadowCascades::kCount + m_localShadowPassCount);
        if (!m_viewRing.Reserve(maxDraws) || !m_shadowViewRing.Reserve(kMaxShadowPasses) ||
            !m_objectRing.Reserve(drawCap) || !m_instanceRing.Reserve(drawCap) ||
            !m_offsetsRing.Reserve(drawCap) || !m_lightRing.Reserve(kMaxLights) ||
            !m_localShadowRing.Reserve(kMaxLocalShadows)) { return; }
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
        m_ready = true;
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
        vd.view          = ctx.viewMatrix;
        vd.cameraPos     = ctx.cameraPos;
        vd.ambient       = ctx.ambient;
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
                if (runLen >= 2) { ResolveInstanced(ctx, viewOffset, clusterBG, items, i, runLen, *head, *mesh, out); }
                else             { ResolveSingle(ctx, viewOffset, clusterBG, *head, *mesh, out); }
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
            usize j = i + 1;
            while (j < items.Size()) {
                const auto* nd = static_cast<const MeshRenderData*>(items[j].data);
                if (nd->mesh != head->mesh || nd->material != head->material) { break; }
                ++j;
            }
            const u32 runLen = static_cast<u32>(j - i);
            const GpuMesh* mesh = m_meshes.GetOrUpload(head->mesh);
            if (mesh != nullptr) {
                if (runLen >= 2) { ResolveDepthInstanced(ctx, shadowViewOffset, items, i, runLen, *mesh, out); }
                else             { ResolveDepthSingle(ctx, shadowViewOffset, *head, *mesh, out); }
            }
            i = j;
        }
    }

    void FinishFrame() override {
        m_viewRing.EndFrame();
        m_shadowViewRing.EndFrame();
        m_objectRing.EndFrame();
        m_instanceRing.EndFrame();
        m_offsetsRing.EndFrame();
        m_ready = false;
    }

private:
    struct ViewData {                                    // 512 (matches the View cbuffer)
        Mat4 viewProj;                                   // 64
        Mat4 view;                                       // 64  (view-space depth: cluster + cascade select)
        Mat4 cascadeViewProj[4];                         // 256 (CSM: world -> each cascade's light clip)
        Vec3 cameraPos; f32 lightCount;                  // 16  (light count as float, mirrors HLSL)
        u32  lightOffset; i32 clusterViewportX, clusterViewportY; u32 pad0;   // 16 (cluster grid is viewport-local)
        u32  clusterGridX = 0, clusterGridY = 0, clusterSliceCount = 0, clusterTileSize = 0;   // 16
        f32  clusterNear = 0, clusterFar = 0, clusterLogScale = 0, clusterLogBias = 0;         // 16
        Vec3 ambient = Vec3{ 0, 0, 0 }; f32 shadowCascadeCount = 0.0f;                          // 16
        Vec4 cascadeSplitFar  = Vec4{ 0, 0, 0, 0 };                                             // 16
        Vec4 cascadeTexelSize = Vec4{ 0, 0, 0, 0 };                                             // 16
        f32  shadowNormalBias = 0, shadowDepthBias = 0, cascadeLayerBase = 0; u32 localShadowBase = 0;   // 16
    };
    struct ObjectData   { Mat4 world; Color tint; };     // 80  (cbuffer Object: World + Tint)
    struct InstanceData { Mat4 world; Color tint; };     // 80  (StructuredBuffer element)
    struct DataOffsets  { u32 x, y, z, w; };             // 16  (instance-stepped vertex attr)
    struct ShadowViewData { Mat4 lightViewProj; };       // 64  (cbuffer ShadowView)

    static constexpr u64 kViewSlot         = 256;        // dynamic UBO offset alignment (object/shadow-view)
    static constexpr u64 kViewDataSlot     = 1024;       // view UBO slot (ViewData is 512B with CSM cascades)
    static constexpr u32 kMaxLights        = 256;        // per-view light budget (phase 4.1; clustered later)
    // shadow-view UBO slots per frame: one per (shadow pass × category run). Cascades (up to
    // kMaxShadowViews*kCount) + local-shadow atlas tiles (up to kMaxLocalShadows), each × a few
    // categories. Sized with headroom — a slot is tiny (256B).
    static constexpr u32 kMaxShadowPasses  = 256;
    static constexpr u32 kMaxLocalShadows  = 64;         // spot/point shadow entries per frame (atlas-bound)

    void ResolveSingle(const RenderRecordContext& ctx, u32 viewOffset, rhi::BindGroup* clusterBG,
                       const MeshRenderData& md, const GpuMesh& mesh, Array<ResolvedDraw>& out) {
        materials::PipelineConfig config = ConfigFor(md, ctx, /*instanced*/ false);
        rhi::RenderPipeline* pso = m_psoCache->GetPipeline(config, m_pipelineLayoutSingle, ctx.colorFormat);
        if (pso == nullptr) { return; }

        const DynamicUniformRing::Range obj = m_objectRing.Allocate();
        if (!obj.ok) { return; }
        *static_cast<ObjectData*>(obj.ptr) = ObjectData{ md.world, md.color };

        ResolvedDraw d{};
        d.pso = pso;
        d.viewSet = m_viewBG;   d.viewOffset = viewOffset;     d.viewDynamic = true;   // set 0: view
        d.drawSet = m_objectBG; d.drawOffset = obj.byteOffset; d.drawDynamic = true;   // set 1: object UBO
        d.materialSet = MaterialBindGroup(md.material);                                // set 2: material
        d.clusterSet = clusterBG;                                                      // set 3: cluster lists
        d.vertexBuffer0 = mesh.vertexBuffer; d.vertexOffset0 = mesh.vertexOffset;
        d.indexBuffer = mesh.indexBuffer; d.indexOffset = mesh.indexOffset; d.indexFormat = mesh.indexFormat;
        d.indexCount = mesh.indexCount; d.instanceCount = 1;
        out.PushBack(d);
    }

    void ResolveInstanced(const RenderRecordContext& ctx, u32 viewOffset, rhi::BindGroup* clusterBG,
                          Span<const DrawItem> items, usize first, u32 count,
                          const MeshRenderData& head, const GpuMesh& mesh, Array<ResolvedDraw>& out) {
        materials::PipelineConfig config = ConfigFor(head, ctx, /*instanced*/ true);
        rhi::RenderPipeline* pso = m_psoCache->GetPipeline(config, m_pipelineLayoutInstanced, ctx.colorFormat);
        if (pso == nullptr) { return; }

        const DynamicUniformRing::Range inst = m_instanceRing.AllocateRange(count);
        const DynamicUniformRing::Range offs = m_offsetsRing.AllocateRange(count);
        if (!inst.ok || !offs.ok) { return; }

        InstanceData* id = static_cast<InstanceData*>(inst.ptr);
        DataOffsets*  od = static_cast<DataOffsets*>(offs.ptr);
        for (u32 k = 0; k < count; ++k) {
            const auto* md = static_cast<const MeshRenderData*>(items[first + k].data);
            id[k] = InstanceData{ md->world, md->color };
            od[k] = DataOffsets{ inst.slotIndex + k, 0, 0, 0 };   // absolute index into Instances[]
        }

        ResolvedDraw d{};
        d.pso = pso;
        d.viewSet = m_viewBG;     d.viewOffset = viewOffset; d.viewDynamic = true;     // set 0: view
        d.drawSet = m_instanceBG; d.drawDynamic = false;                               // set 1: instances (whole buffer)
        d.materialSet = MaterialBindGroup(head.material);                              // set 2: material
        d.clusterSet = clusterBG;                                                      // set 3: cluster lists
        d.vertexBuffer0 = mesh.vertexBuffer;    d.vertexOffset0 = mesh.vertexOffset;
        d.vertexBuffer1 = m_offsetsRing.Buffer(); d.vertexOffset1 = offs.byteOffset;          // DataOffsets stream
        d.indexBuffer = mesh.indexBuffer; d.indexOffset = mesh.indexOffset; d.indexFormat = mesh.indexFormat;
        d.indexCount = mesh.indexCount; d.instanceCount = count;
        out.PushBack(d);
    }

    void ResolveDepthSingle(const RenderRecordContext& ctx, u32 shadowViewOffset,
                            const MeshRenderData& md, const GpuMesh& mesh, Array<ResolvedDraw>& out) {
        rhi::RenderPipeline* pso = m_psoCache->GetPipeline(ShadowConfigFor(ctx, /*instanced*/ false),
                                                           m_shadowPipelineLayoutSingle, rhi::TextureFormat::Undefined);
        if (pso == nullptr) { return; }
        const DynamicUniformRing::Range obj = m_objectRing.Allocate();
        if (!obj.ok) { return; }
        *static_cast<ObjectData*>(obj.ptr) = ObjectData{ md.world, md.color };

        ResolvedDraw d{};
        d.pso = pso;
        d.viewSet = m_shadowViewBG; d.viewOffset = shadowViewOffset; d.viewDynamic = true;   // set 0: light view
        d.drawSet = m_objectBG;     d.drawOffset = obj.byteOffset;   d.drawDynamic = true;   // set 1: object UBO
        // no material/cluster sets for depth-only
        d.vertexBuffer0 = mesh.vertexBuffer; d.vertexOffset0 = mesh.vertexOffset;
        d.indexBuffer = mesh.indexBuffer; d.indexOffset = mesh.indexOffset; d.indexFormat = mesh.indexFormat;
        d.indexCount = mesh.indexCount; d.instanceCount = 1;
        out.PushBack(d);
    }

    void ResolveDepthInstanced(const RenderRecordContext& ctx, u32 shadowViewOffset,
                               Span<const DrawItem> items, usize first, u32 count,
                               const GpuMesh& mesh, Array<ResolvedDraw>& out) {
        rhi::RenderPipeline* pso = m_psoCache->GetPipeline(ShadowConfigFor(ctx, /*instanced*/ true),
                                                           m_shadowPipelineLayoutInstanced, rhi::TextureFormat::Undefined);
        if (pso == nullptr) { return; }
        const DynamicUniformRing::Range inst = m_instanceRing.AllocateRange(count);
        const DynamicUniformRing::Range offs = m_offsetsRing.AllocateRange(count);
        if (!inst.ok || !offs.ok) { return; }

        InstanceData* id = static_cast<InstanceData*>(inst.ptr);
        DataOffsets*  od = static_cast<DataOffsets*>(offs.ptr);
        for (u32 k = 0; k < count; ++k) {
            const auto* md = static_cast<const MeshRenderData*>(items[first + k].data);
            id[k] = InstanceData{ md->world, md->color };
            od[k] = DataOffsets{ inst.slotIndex + k, 0, 0, 0 };
        }

        ResolvedDraw d{};
        d.pso = pso;
        d.viewSet = m_shadowViewBG; d.viewOffset = shadowViewOffset; d.viewDynamic = true;   // set 0: light view
        d.drawSet = m_instanceBG;   d.drawDynamic = false;                                    // set 1: instances (whole)
        d.vertexBuffer0 = mesh.vertexBuffer;      d.vertexOffset0 = mesh.vertexOffset;
        d.vertexBuffer1 = m_offsetsRing.Buffer(); d.vertexOffset1 = offs.byteOffset;
        d.indexBuffer = mesh.indexBuffer; d.indexOffset = mesh.indexOffset; d.indexFormat = mesh.indexFormat;
        d.indexCount = mesh.indexCount; d.instanceCount = count;
        out.PushBack(d);
    }

    // Depth-only PSO config for the shadow pass. Back-face cull + a small depth bias/slope to push
    // shadow acne off lit surfaces (tuned on GPU; 5.2 refines with normal-offset bias in the shader).
    [[nodiscard]] static materials::PipelineConfig ShadowConfigFor(const RenderRecordContext& ctx, bool instanced) {
        materials::PipelineConfig c{};
        c.shaderName   = u8"shadow_depth";
        c.vertexLayout = materials::VertexLayoutType::Mesh;
        c.instanced    = instanced;
        if (instanced) { c.shaderFlags |= shaders::ShaderFlags::Instanced; }
        c.depthOnly         = true;
        c.colorTargetCount  = 0;
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
        c.depthBias           = 50;
        c.depthBiasSlopeScale = 1.5f;
        return c;
    }

    [[nodiscard]] static materials::PipelineConfig ConfigFor(const MeshRenderData& md, const RenderRecordContext& ctx, bool instanced) {
        materials::PipelineConfig config = (md.material != nullptr)
            ? md.material->pipeline
            : materials::PipelineConfig::ForOpaqueMesh(u8"forward");
        config.depthFormat = ctx.depthFormat;
        config.instanced   = instanced;
        if (instanced) { config.shaderFlags |= shaders::ShaderFlags::Instanced; }
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

    // A fallback material bind group used for materials that declare no properties, so set 2 is
    // always bound (the shader always expects the PBR Material UBO). Matches the cbuffer layout:
    // {BaseColor, Metallic, Roughness, pad, pad} = 32 bytes.
    Status CreateDefaultMaterial() {
        struct PbrDefault { Vec4 baseColor; f32 metallic; f32 roughness; f32 pad0; f32 pad1; };
        const PbrDefault def{ Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 0.5f, 0.0f, 0.0f };
        static_assert(sizeof(PbrDefault) == 32);
        rhi::BufferDesc bd{};
        bd.size = sizeof(PbrDefault);
        bd.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst;
        bd.memory = rhi::MemoryLocation::CpuToGpu;
        bd.label = u8"material.default";
        if (!m_device->CreateBuffer(bd, m_defaultMaterialBuffer).IsOk()) { return Status{ ErrorCode::Unknown }; }
        if (void* p = m_defaultMaterialBuffer->Map()) { MemCopy(p, &def, sizeof(def)); m_defaultMaterialBuffer->Unmap(); }

        rhi::BindGroupEntry be = rhi::BindGroupEntry::BufferEntry(m_defaultMaterialBuffer, 0, sizeof(PbrDefault));
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_materialLayout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ &be, 1 };
        if (!m_device->CreateBindGroup(bgd, m_defaultMaterialBG).IsOk()) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    // The material set-2 bind group for `material`: the data-driven bind group from the
    // MaterialSystem (built from declared properties, sharing m_materialLayout), or the default
    // white material if the material declares no set-2 properties. Auto-instanced per material.
    [[nodiscard]] rhi::BindGroup* MaterialBindGroup(materials::Material* material) {
        if (material == nullptr) { return m_defaultMaterialBG; }
        materials::MaterialInstance* inst = nullptr;
        if (materials::MaterialInstance** found = m_instances.Find(material)) {
            inst = *found;
        } else {
            UniquePtr<materials::MaterialInstance> created = MakeUnique<materials::MaterialInstance>(DefaultAllocator(), material);
            inst = created.Get();
            m_instanceStorage.PushBack(Move(created));       // owns the instance
            m_instances.InsertOrAssign(material, inst);       // raw lookup (HashMap can't hold UniquePtr)
        }
        rhi::BindGroup* bg = m_materials->PrepareInstance(*inst, m_materialLayout);
        return (bg != nullptr) ? bg : m_defaultMaterialBG;   // material with no set-2 props -> default
    }

    // (Re)create a bind group over a ring's buffer when the ring (re)allocated. `whole` binds
    // the entire buffer (storage, indexed); otherwise a `bindSize` window (dynamic-offset UBO).
    bool EnsureBindGroup(DynamicUniformRing& ring, rhi::BindGroupLayout* layout, u64 bindSize,
                         rhi::BindGroup*& bg, u32& bgGen, bool whole) {
        if (bg != nullptr && bgGen == ring.Generation()) { return true; }
        if (bg) { m_device->DestroyBindGroup(bg); bg = nullptr; }
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
        if (m_viewBG != nullptr && m_viewBGViewGen == m_viewRing.Generation() &&
            m_viewBGLightGen == m_lightRing.Generation() &&
            m_viewBGShadow == m_activeShadowView && m_viewBGShadowGen == m_activeShadowGen &&
            m_viewBGAtlas == m_activeAtlasView && m_viewBGAtlasGen == m_activeAtlasGen &&
            m_viewBGLocalGen == m_localShadowRing.Generation()) {
            return true;
        }
        if (m_viewBG) { m_device->DestroyBindGroup(m_viewBG); m_viewBG = nullptr; }
        rhi::Buffer* viewBuf = m_viewRing.Buffer();
        rhi::Buffer* lightBuf = m_lightRing.Buffer();
        rhi::Buffer* localBuf = m_localShadowRing.Buffer();
        if (viewBuf == nullptr || lightBuf == nullptr || localBuf == nullptr ||
            m_activeShadowView == nullptr || m_activeAtlasView == nullptr || m_shadowSampler == nullptr) { return false; }
        // Order must match the set-0 layout: view UBO, lights, cascade map (t1), local atlas (t2),
        // local-shadow entries (t3), comparison sampler. Buffers bound whole + indexed in-shader.
        rhi::BindGroupEntry entries[] = {
            rhi::BindGroupEntry::BufferEntry(viewBuf, 0, sizeof(ViewData)),
            rhi::BindGroupEntry::BufferEntry(lightBuf, 0, m_lightRing.ByteCapacity()),
            rhi::BindGroupEntry::TextureEntry(m_activeShadowView),
            rhi::BindGroupEntry::TextureEntry(m_activeAtlasView),
            rhi::BindGroupEntry::BufferEntry(localBuf, 0, m_localShadowRing.ByteCapacity()),
            rhi::BindGroupEntry::SamplerEntry(m_shadowSampler),
        };
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_viewLayout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ entries, 6 };
        if (!m_device->CreateBindGroup(bgd, m_viewBG).IsOk()) { m_viewBG = nullptr; return false; }
        m_viewBGViewGen = m_viewRing.Generation();
        m_viewBGLightGen = m_lightRing.Generation();
        m_viewBGShadow = m_activeShadowView;
        m_viewBGShadowGen = m_activeShadowGen;
        m_viewBGAtlas = m_activeAtlasView;
        m_viewBGAtlasGen = m_activeAtlasGen;
        m_viewBGLocalGen = m_localShadowRing.Generation();
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
        return Status{};
    }

    // The set-0 bind group for the shadow depth pass: just the light-view UBO (dynamic-offset
    // window of one ShadowViewData). Rebuilt when the shadow-view ring (re)allocated.
    bool EnsureShadowViewBindGroup() {
        if (m_shadowViewBG != nullptr && m_shadowViewBGGen == m_shadowViewRing.Generation()) { return true; }
        if (m_shadowViewBG) { m_device->DestroyBindGroup(m_shadowViewBG); m_shadowViewBG = nullptr; }
        rhi::Buffer* buf = m_shadowViewRing.Buffer();
        if (buf == nullptr) { return false; }
        rhi::BindGroupEntry be = rhi::BindGroupEntry::BufferEntry(buf, 0, sizeof(ShadowViewData));
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_shadowViewLayout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ &be, 1 };
        if (!m_device->CreateBindGroup(bgd, m_shadowViewBG).IsOk()) { m_shadowViewBG = nullptr; return false; }
        m_shadowViewBGGen = m_shadowViewRing.Generation();
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
        // Release material instances first (their dtors notify the still-live MaterialSystem).
        m_instances.Clear();
        m_instanceStorage.Clear();
        m_meshes.Clear();
        if (m_defaultMaterialBG)     { m_device->DestroyBindGroup(m_defaultMaterialBG); m_defaultMaterialBG = nullptr; }
        if (m_defaultMaterialBuffer) { m_device->DestroyBuffer(m_defaultMaterialBuffer); m_defaultMaterialBuffer = nullptr; }
        if (m_viewBG)       { m_device->DestroyBindGroup(m_viewBG); m_viewBG = nullptr; }
        if (m_shadowViewBG) { m_device->DestroyBindGroup(m_shadowViewBG); m_shadowViewBG = nullptr; }
        if (m_dummyShadowView) { m_device->DestroyTextureView(m_dummyShadowView); m_dummyShadowView = nullptr; }
        if (m_dummyShadowTex)  { m_device->DestroyTexture(m_dummyShadowTex); m_dummyShadowTex = nullptr; }
        if (m_dummyAtlasView)  { m_device->DestroyTextureView(m_dummyAtlasView); m_dummyAtlasView = nullptr; }
        if (m_dummyAtlasTex)   { m_device->DestroyTexture(m_dummyAtlasTex); m_dummyAtlasTex = nullptr; }
        if (m_shadowSampler)   { m_device->DestroySampler(m_shadowSampler); m_shadowSampler = nullptr; }
        if (m_objectBG)   { m_device->DestroyBindGroup(m_objectBG); m_objectBG = nullptr; }
        if (m_instanceBG) { m_device->DestroyBindGroup(m_instanceBG); m_instanceBG = nullptr; }
        for (u32 i = 0; i < kMaxClusterSlots; ++i) {
            if (m_clusterBGs[i] != nullptr) { m_device->DestroyBindGroup(m_clusterBGs[i]); m_clusterBGs[i] = nullptr; }
        }
        if (m_dummyClusterBG)      { m_device->DestroyBindGroup(m_dummyClusterBG); m_dummyClusterBG = nullptr; }
        if (m_dummyClusterOffsets) { m_device->DestroyBuffer(m_dummyClusterOffsets); m_dummyClusterOffsets = nullptr; }
        if (m_dummyClusterIndices) { m_device->DestroyBuffer(m_dummyClusterIndices); m_dummyClusterIndices = nullptr; }
        if (m_pipelineLayoutSingle)    { m_device->DestroyPipelineLayout(m_pipelineLayoutSingle); m_pipelineLayoutSingle = nullptr; }
        if (m_pipelineLayoutInstanced) { m_device->DestroyPipelineLayout(m_pipelineLayoutInstanced); m_pipelineLayoutInstanced = nullptr; }
        if (m_shadowPipelineLayoutSingle)    { m_device->DestroyPipelineLayout(m_shadowPipelineLayoutSingle); m_shadowPipelineLayoutSingle = nullptr; }
        if (m_shadowPipelineLayoutInstanced) { m_device->DestroyPipelineLayout(m_shadowPipelineLayoutInstanced); m_shadowPipelineLayoutInstanced = nullptr; }
        if (m_viewLayout)     { m_device->DestroyBindGroupLayout(m_viewLayout); m_viewLayout = nullptr; }
        if (m_objectLayout)   { m_device->DestroyBindGroupLayout(m_objectLayout); m_objectLayout = nullptr; }
        if (m_instanceLayout) { m_device->DestroyBindGroupLayout(m_instanceLayout); m_instanceLayout = nullptr; }
        if (m_materialLayout) { m_device->DestroyBindGroupLayout(m_materialLayout); m_materialLayout = nullptr; }
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
    rhi::BindGroupLayout* m_materialLayout = nullptr;
    rhi::BindGroupLayout* m_clusterLayout  = nullptr;
    rhi::BindGroupLayout* m_shadowViewLayout = nullptr;   // set 0 for the depth-only shadow pipeline
    rhi::PipelineLayout*  m_pipelineLayoutSingle    = nullptr;
    rhi::PipelineLayout*  m_pipelineLayoutInstanced = nullptr;
    rhi::PipelineLayout*  m_shadowPipelineLayoutSingle    = nullptr;
    rhi::PipelineLayout*  m_shadowPipelineLayoutInstanced = nullptr;

    // Auto-instanced material set-2 resources.
    rhi::Buffer*    m_defaultMaterialBuffer = nullptr;
    rhi::BindGroup* m_defaultMaterialBG     = nullptr;
    HashMap<materials::Material*, materials::MaterialInstance*>     m_instances;        // lookup (raw)
    Array<UniquePtr<materials::MaterialInstance>>                  m_instanceStorage;  // ownership

    DynamicUniformRing m_viewRing;
    DynamicUniformRing m_shadowViewRing;
    DynamicUniformRing m_objectRing;
    DynamicUniformRing m_instanceRing;
    DynamicUniformRing m_offsetsRing;
    DynamicUniformRing m_lightRing;
    DynamicUniformRing m_localShadowRing;   // per-frame GpuLocalShadow entries (spot/point atlas)

    rhi::BindGroup* m_viewBG       = nullptr;
    rhi::BindGroup* m_shadowViewBG = nullptr;
    u32             m_shadowViewBGGen = 0;
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
    u32               m_localShadowBase  = 0;   // this frame's base into m_localShadowRing
    u32               m_localShadowPassCount = 0;   // # atlas depth passes (caster re-emits) this frame
    u32 m_objectBGGen = 0, m_instanceBGGen = 0;

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

} // namespace raptor::render

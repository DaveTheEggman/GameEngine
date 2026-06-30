/// Draconic::Render — the `:ibl` partition.
///
/// Image-Based Lighting: the split-sum environment pipeline (ported from Sedulous.Renderer/IBL with
/// improvements). Owns the precompute products + declares the render-graph passes that build them:
///   - env cubemap (256², RGBA16F)        : the source radiance, written from the active sky source
///                                          (procedural gradient now; HDR equirect + analytic later).
///   - SH9 diffuse irradiance (buffer)    : 9 RGB spherical-harmonic coeffs projected from the env
///                                          cube (REPLACES Sedulous's 32² irradiance cube — cheaper,
///                                          smoother, seamless). Improvement over Sedulous.
///   - GGX prefiltered specular (cube+mips): Karis split-sum, importance-sampled per roughness mip.
///   - BRDF integration LUT (256², RG16F) : generated at runtime (Sedulous embeds a baked array).
///
/// Precompute runs only when the source is dirty; products are persistent, imported every frame so the
/// forward pass orders after + samples them (set 0). Multi-scatter energy compensation + prefilter
/// mip-sampling are forward-shader / follow-up refinements (see [[ibl-plan]]).

module;
#include "Core/Prelude.h"

export module draconic.render:ibl;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;
import :data;   // SkySnapshot / SkyMode (the per-frame environment settings)

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// ---- shaders ---------------------------------------------------------------------------------

// Fullscreen-triangle VS (positions + uv from SV_VertexID), shared by every cube-face + LUT pass.
inline constexpr const char8_t* kIblFullscreenVS = u8R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    o.uv  = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}
)";

// Per-face/mip params for the cube passes (tightly-packed push constants). Sky authoring (mode +
// intensity + gradient colors + sun + rotation) rides here so the procedural env pass reads it.
inline constexpr const char8_t* kIblCommon = u8R"(
struct IblPush {
    int FaceIndex; int Mode; float Roughness; float SkyIntensity;
    float4 Sun;        // xyz = direction, w = sun angular size (degrees)
    float4 Horizon;    // rgb, a = sun intensity
    float4 Zenith;     // rgb, a = sky rotation (radians)
    float4 Ground;     // rgb
};
[[vk::push_constant]] IblPush pc;

// Standard Vulkan cube-face direction from a face index + [0,1] face uv.
float3 DirForFace(int face, float2 uv) {
    float2 t = uv * 2.0 - 1.0;
    t.y = -t.y;
    float3 d;
    if      (face == 0) d = float3( 1.0,  t.y, -t.x);   // +X
    else if (face == 1) d = float3(-1.0,  t.y,  t.x);   // -X
    else if (face == 2) d = float3( t.x,  1.0, -t.y);   // +Y
    else if (face == 3) d = float3( t.x, -1.0,  t.y);   // -Y
    else if (face == 4) d = float3( t.x,  t.y,  1.0);   // +Z
    else                d = float3(-t.x,  t.y, -1.0);   // -Z
    return normalize(d);
}
)";

// Procedural sky -> one env cube face. Gradient (horizon/zenith/ground) + a sun disc whose direction
// comes from the first directional light (SunDir.xyz, .w = intensity). Matches Sedulous's sky.frag.
inline constexpr const char8_t* kIblProcEnvPS = u8R"(
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 dir = DirForFace(pc.FaceIndex, uv);
    // Yaw the sample direction by the sky rotation (matters for HDR/cubemap; harmless on the gradient).
    float rot = pc.Zenith.a;
    float cr = cos(rot), sr = sin(rot);
    dir = float3(cr * dir.x + sr * dir.z, dir.y, -sr * dir.x + cr * dir.z);

    float3 sky;
    if (pc.Mode == 1) {                              // Color mode: uniform zenith color
        sky = pc.Zenith.rgb;
    } else {                                         // Procedural gradient (also HDR/cubemap fallback)
        sky = (dir.y >= 0.0) ? lerp(pc.Horizon.rgb, pc.Zenith.rgb, pow(saturate(dir.y), 0.5))
                             : lerp(pc.Horizon.rgb, pc.Ground.rgb, pow(saturate(-dir.y), 0.8));
        // Sun disc + glow toward the light direction.
        float3 sunDir   = normalize(-pc.Sun.xyz);
        float  d        = max(dot(dir, sunDir), 0.0);
        float  sunInt   = max(pc.Horizon.a, 0.0);
        float  discCos  = cos(radians(max(pc.Sun.w, 0.05)));
        sky += step(discCos, d) * sunInt * 3.0;
        sky += pow(d, 256.0) * sunInt * 0.5;
    }
    return float4(sky * max(pc.SkyIntensity, 0.0), 1.0);
}
)";

// GGX prefilter (Karis split-sum specular): importance-sample the env cube around the reflection
// direction (= N = V) at this mip's roughness. 1024 Hammersley samples / texel.
inline constexpr const char8_t* kIblPrefilterPS = u8R"(
TextureCube<float4> EnvMap : register(t0, space0);
SamplerState        EnvSamp : register(s0, space0);

static const float PI = 3.14159265359;

float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
float2 Hammersley(uint i, uint n) { return float2(float(i) / float(n), RadicalInverse_VdC(i)); }

float3 ImportanceSampleGGX(float2 xi, float3 n, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    float3 h = float3(cos(phi) * sinT, sin(phi) * sinT, cosT);
    float3 up = abs(n.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tx = normalize(cross(up, n));
    float3 ty = cross(n, tx);
    return normalize(tx * h.x + ty * h.y + n * h.z);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 N = DirForFace(pc.FaceIndex, uv);
    float3 V = N;
    const uint SAMPLES = 1024u;
    float3 color = 0.0;
    float  weight = 0.0;
    for (uint i = 0u; i < SAMPLES; ++i) {
        float2 xi = Hammersley(i, SAMPLES);
        float3 H = ImportanceSampleGGX(xi, N, pc.Roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float ndl = dot(N, L);
        if (ndl > 0.0) {
            color += EnvMap.SampleLevel(EnvSamp, L, 0.0).rgb * ndl;
            weight += ndl;
        }
    }
    return float4(color / max(weight, 1e-4), 1.0);
}
)";

// BRDF integration LUT: split-sum's second term. uv = (NdotV, roughness) -> (scale, bias) for F0.
inline constexpr const char8_t* kIblBrdfPS = u8R"(
static const float PI = 3.14159265359;

float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
float2 Hammersley(uint i, uint n) { return float2(float(i) / float(n), RadicalInverse_VdC(i)); }
float3 ImportanceSampleGGX(float2 xi, float3 n, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    float3 h = float3(cos(phi) * sinT, sin(phi) * sinT, cosT);
    float3 up = abs(n.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tx = normalize(cross(up, n));
    float3 ty = cross(n, tx);
    return normalize(tx * h.x + ty * h.y + n * h.z);
}
float GeometrySchlickGGX(float ndv, float k) { return ndv / (ndv * (1.0 - k) + k); }
float GeometrySmith(float3 n, float3 v, float3 l, float k) {
    return GeometrySchlickGGX(max(dot(n, v), 0.0), k) * GeometrySchlickGGX(max(dot(n, l), 0.0), k);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float ndv = max(uv.x, 1e-3);
    float roughness = uv.y;
    float3 V = float3(sqrt(1.0 - ndv * ndv), 0.0, ndv);
    float3 N = float3(0, 0, 1);
    float A = 0.0, B = 0.0;
    const uint SAMPLES = 1024u;
    float k = (roughness * roughness) / 2.0;   // IBL geometry term
    for (uint i = 0u; i < SAMPLES; ++i) {
        float2 xi = Hammersley(i, SAMPLES);
        float3 H = ImportanceSampleGGX(xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float ndl = max(L.z, 0.0);
        float ndh = max(H.z, 0.0);
        float vdh = max(dot(V, H), 0.0);
        if (ndl > 0.0) {
            float G = GeometrySmith(N, V, L, k);
            float Gvis = (G * vdh) / max(ndh * ndv, 1e-4);
            float Fc = pow(1.0 - vdh, 5.0);
            A += (1.0 - Fc) * Gvis;
            B += Fc * Gvis;
        }
    }
    return float4(A / float(SAMPLES), B / float(SAMPLES), 0.0, 1.0);
}
)";

// SH9 diffuse projection: reduce the env cube to 9 RGB spherical-harmonic coefficients (one thread;
// runs once per source change). Solid-angle-weighted cosine convolution is then evaluated cheaply in
// the forward shader. Output layout: 9 float4 (xyz = coeff, w unused).
inline constexpr const char8_t* kIblShProjectCS = u8R"(
TextureCube<float4> EnvMap : register(t0, space0);
SamplerState        EnvSamp : register(s0, space0);
RWStructuredBuffer<float4> ShOut : register(u0, space0);

static const float PI = 3.14159265359;

float3 DirForFace(int face, float2 uv) {
    float2 t = uv * 2.0 - 1.0; t.y = -t.y;
    float3 d;
    if      (face == 0) d = float3( 1.0,  t.y, -t.x);
    else if (face == 1) d = float3(-1.0,  t.y,  t.x);
    else if (face == 2) d = float3( t.x,  1.0, -t.y);
    else if (face == 3) d = float3( t.x, -1.0,  t.y);
    else if (face == 4) d = float3( t.x,  t.y,  1.0);
    else                d = float3(-t.x,  t.y, -1.0);
    return normalize(d);
}

[numthreads(1, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    float3 sh[9];
    for (int i = 0; i < 9; ++i) sh[i] = 0.0;
    float wsum = 0.0;
    const int N = 32;   // per-face resolution for the projection
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                float2 uv = (float2(x, y) + 0.5) / float(N);
                float3 dir = DirForFace(face, uv);
                // Differential solid angle for this cube texel.
                float2 t = uv * 2.0 - 1.0;
                float tmp = 1.0 + t.x * t.x + t.y * t.y;
                float w = 4.0 / (sqrt(tmp) * tmp) / float(N * N);
                float3 c = EnvMap.SampleLevel(EnvSamp, dir, 0.0).rgb * w;
                wsum += w;
                // Real SH basis (l=0..2).
                sh[0] += c * 0.282095;
                sh[1] += c * 0.488603 * dir.y;
                sh[2] += c * 0.488603 * dir.z;
                sh[3] += c * 0.488603 * dir.x;
                sh[4] += c * 1.092548 * dir.x * dir.y;
                sh[5] += c * 1.092548 * dir.y * dir.z;
                sh[6] += c * 0.315392 * (3.0 * dir.z * dir.z - 1.0);
                sh[7] += c * 1.092548 * dir.x * dir.z;
                sh[8] += c * 0.546274 * (dir.x * dir.x - dir.y * dir.y);
            }
        }
    }
    float norm = (4.0 * PI) / max(wsum, 1e-4);
    for (int j = 0; j < 9; ++j) ShOut[j] = float4(sh[j] * norm, 0.0);
}
)";

// Owns the IBL precompute products + the passes that build them. One per renderer (scene-global env).
class IBLSystem {
public:
    static constexpr u32 kEnvResolution    = 256;
    static constexpr u32 kPrefilterRes     = 256;
    static constexpr u32 kPrefilterMips    = 5;     // roughness = mip / (kPrefilterMips - 1)
    static constexpr u32 kBrdfResolution   = 256;
    static constexpr u32 kShCoeffCount     = 9;

    IBLSystem(rhi::Device& device, shaders::ShaderSystem& shaders) noexcept
        : m_device(&device), m_shaders(&shaders) {}
    ~IBLSystem() { Shutdown(); }
    IBLSystem(const IBLSystem&) = delete;
    IBLSystem& operator=(const IBLSystem&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"ibl_fs", shaders::ShaderStage::Vertex, kIblFullscreenVS);
        // Cube/LUT fragment shaders share the fullscreen VS; the cube ones prepend kIblCommon.
        m_shaders->RegisterSource(u8"ibl_procenv",  shaders::ShaderStage::Fragment, Concat(kIblCommon, kIblProcEnvPS));
        m_shaders->RegisterSource(u8"ibl_prefilter",shaders::ShaderStage::Fragment, Concat(kIblCommon, kIblPrefilterPS));
        m_shaders->RegisterSource(u8"ibl_brdf",     shaders::ShaderStage::Fragment, kIblBrdfPS);
        m_shaders->RegisterSource(u8"ibl_sh",       shaders::ShaderStage::Compute,  kIblShProjectCS);

        if (!CreateResources()) { return Status{ ErrorCode::Unknown }; }
        if (!CreatePipelines()) { return Status{ ErrorCode::Unknown }; }
        m_dirty = true;   // build once on first ProcessPending
        return Status{};
    }

    // The directional sun feeding the procedural sky (xyz = light direction). A changed direction
    // re-dirties the precompute so the env reflects the new sun.
    void SetSun(const Vec3& dir) {
        if (dir.x != m_sunDir.x || dir.y != m_sunDir.y || dir.z != m_sunDir.z) { m_sunDir = dir; m_dirty = true; }
    }

    // The scene's sky authoring (mode + intensity + gradient colors + sun + rotation). A changed value
    // re-dirties the precompute (env/SH/prefilter rebuild to match).
    void SetSky(const SkySnapshot& s) {
        if (!SkyEqual(s, m_sky)) { m_sky = s; m_dirty = true; }
    }

    // Products bound into the forward set 0. Stable for a renderer's lifetime (textures recreated only
    // on shutdown), so a plain generation of 1 suffices for bind-group cache keys.
    [[nodiscard]] rhi::TextureView* PrefilterView() const noexcept { return m_prefilterView; }
    [[nodiscard]] rhi::TextureView* BrdfView()      const noexcept { return m_brdfView; }
    [[nodiscard]] rhi::Buffer*      ShBuffer()      const noexcept { return m_shBuffer; }
    [[nodiscard]] u64               ShBytes()       const noexcept { return sizeof(f32) * 4 * kShCoeffCount; }
    [[nodiscard]] u64               Generation()    const noexcept { return m_generation; }
    [[nodiscard]] f32               MaxLod()        const noexcept { return static_cast<f32>(kPrefilterMips - 1); }
    [[nodiscard]] bool              Ready()         const noexcept { return m_ready; }

    // This frame's graph handles for the products the forward pass samples (valid after ProcessPending).
    // The forward ReadTexture/ReadBuffer's these so the graph orders any precompute writes -> forward and
    // barriers the products to a shader-readable layout before the forward bundle samples them.
    [[nodiscard]] rendergraph::RGHandle PrefilterHandle() const noexcept { return m_prefilterH; }
    [[nodiscard]] rendergraph::RGHandle BrdfHandle()      const noexcept { return m_brdfH; }
    [[nodiscard]] rendergraph::RGHandle ShHandle()        const noexcept { return m_shH; }

    // Declare the precompute passes into this frame's graph (before forward). The products are imported
    // EVERY frame (so the forward can read this frame's handles); the env-dependent write passes only run
    // when the sky source is dirty, and the BRDF LUT (constant) is written exactly once.
    void ProcessPending(rendergraph::RenderGraph& graph) {
        if (!m_ready) { return; }

        // Frame-persistent product imports (handles the forward reads this frame).
        m_prefilterH = graph.ImportTarget(u8"ibl.prefilter", m_prefilterCube, m_prefilterView,
                                          rhi::ResourceState::ShaderRead, m_prefilterState);
        m_prefilterState = rhi::ResourceState::ShaderRead;
        m_brdfH = graph.ImportTarget(u8"ibl.brdf", m_brdfLut, m_brdfView,
                                     rhi::ResourceState::ShaderRead, m_brdfState);
        m_brdfState = rhi::ResourceState::ShaderRead;
        m_shH = graph.ImportBuffer(u8"ibl.sh", m_shBuffer);

        // BRDF LUT: constant, generate exactly once.
        if (!m_brdfDone) { DeclareBrdf(graph, m_brdfH); m_brdfDone = true; }

        if (!m_dirty) { return; }
        m_dirty = false;
        ++m_generation;

        // (1) Source -> env cube: 6 procedural faces.
        const rendergraph::RGHandle envH = graph.ImportTarget(
            u8"ibl.env", m_envCube, m_envSampleView,
            rhi::ResourceState::ShaderRead, m_envState);
        m_envState = rhi::ResourceState::ShaderRead;
        for (u32 face = 0; face < 6; ++face) {
            IblPush push = MakeSkyPush(static_cast<i32>(face));
            graph.AddRenderPass(u8"ibl.env.face", [this, envH, face, push](rendergraph::PassBuilder& b) {
                b.SetColorTarget(0, envH, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black(),
                                 rendergraph::RGSubresourceRange{ 0, 1, face, 1 });
                b.SetViewport(0, 0, kEnvResolution, kEnvResolution);
                b.NeverCull();
                b.SetExecute([this, push](rhi::RenderPassEncoder& rp) {
                    rp.SetPipeline(m_envPipeline);
                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(IblPush), &push);
                    rp.Draw(3, 1, 0, 0);
                });
            });
        }

        // (2) env -> SH9 diffuse (compute), (3) env -> prefilter mips.
        DeclareShProjection(graph, envH, m_shH);
        DeclarePrefilter(graph, envH, m_prefilterH);
    }

private:
    struct IblPush {
        i32 faceIndex = 0; i32 mode = 0; f32 roughness = 0.0f; f32 skyIntensity = 1.0f;
        Vec4 sun{};       // xyz = direction, w = sun angular size (deg)
        Vec4 horizon{};   // rgb, a = sun intensity
        Vec4 zenith{};    // rgb, a = rotation (radians)
        Vec4 ground{};    // rgb
    };

    // Build the procedural-env push for one cube face from the current sky + sun direction.
    [[nodiscard]] IblPush MakeSkyPush(i32 face) const {
        IblPush p{};
        p.faceIndex    = face;
        p.mode         = static_cast<i32>(m_sky.mode);
        p.skyIntensity = m_sky.intensity;
        p.sun     = Vec4{ m_sunDir.x, m_sunDir.y, m_sunDir.z, m_sky.sunAngularSize };
        p.horizon = Vec4{ m_sky.horizon.x, m_sky.horizon.y, m_sky.horizon.z, m_sky.sunIntensity };
        p.zenith  = Vec4{ m_sky.zenith.x, m_sky.zenith.y, m_sky.zenith.z, m_sky.rotation };
        p.ground  = Vec4{ m_sky.ground.x, m_sky.ground.y, m_sky.ground.z, 0.0f };
        return p;
    }

    [[nodiscard]] static bool SkyEqual(const SkySnapshot& a, const SkySnapshot& b) {
        return a.mode == b.mode && a.intensity == b.intensity && a.rotation == b.rotation &&
               a.horizon.x == b.horizon.x && a.horizon.y == b.horizon.y && a.horizon.z == b.horizon.z &&
               a.zenith.x == b.zenith.x && a.zenith.y == b.zenith.y && a.zenith.z == b.zenith.z &&
               a.ground.x == b.ground.x && a.ground.y == b.ground.y && a.ground.z == b.ground.z &&
               a.sunIntensity == b.sunIntensity && a.sunAngularSize == b.sunAngularSize;
    }

    void DeclareShProjection(rendergraph::RenderGraph& graph, rendergraph::RGHandle envH, rendergraph::RGHandle shH) {
        graph.AddComputePass(u8"ibl.sh", [this, envH, shH](rendergraph::PassBuilder& b) {
            b.ReadTexture(envH);
            b.WriteStorage(shH);
            b.SetComputeExecute([this](rhi::ComputePassEncoder& cp) {
                if (m_shBindGroup == nullptr) { return; }
                cp.SetPipeline(m_shPipeline);
                cp.SetBindGroup(0, m_shBindGroup, Span<const u32>{});
                cp.Dispatch(1, 1, 1);
            });
        });
    }

    void DeclarePrefilter(rendergraph::RenderGraph& graph, rendergraph::RGHandle envH, rendergraph::RGHandle preH) {
        for (u32 mip = 0; mip < kPrefilterMips; ++mip) {
            const u32 res = kPrefilterRes >> mip;
            const f32 roughness = (kPrefilterMips > 1) ? static_cast<f32>(mip) / static_cast<f32>(kPrefilterMips - 1) : 0.0f;
            for (u32 face = 0; face < 6; ++face) {
                IblPush push{}; push.faceIndex = static_cast<i32>(face); push.roughness = roughness;
                graph.AddRenderPass(u8"ibl.prefilter", [this, envH, preH, mip, face, res, push](rendergraph::PassBuilder& b) {
                    b.ReadTexture(envH);
                    b.SetColorTarget(0, preH, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black(),
                                     rendergraph::RGSubresourceRange{ mip, 1, face, 1 });
                    b.SetViewport(0, 0, res, res);
                    b.NeverCull();
                    b.SetExecute([this, push](rhi::RenderPassEncoder& rp) {
                        rp.SetPipeline(m_prefilterPipeline);
                        rp.SetBindGroup(0, m_envBindGroup, Span<const u32>{});
                        rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(IblPush), &push);
                        rp.Draw(3, 1, 0, 0);
                    });
                });
            }
        }
    }

    void DeclareBrdf(rendergraph::RenderGraph& graph, rendergraph::RGHandle brdfH) {
        graph.AddRenderPass(u8"ibl.brdf", [this, brdfH](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, brdfH, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
            b.SetViewport(0, 0, kBrdfResolution, kBrdfResolution);
            b.NeverCull();
            b.SetExecute([this](rhi::RenderPassEncoder& rp) {
                rp.SetPipeline(m_brdfPipeline);
                rp.Draw(3, 1, 0, 0);
            });
        });
    }

    static constexpr rhi::TextureFormat kCubeFormat = rhi::TextureFormat::RGBA16Float;
    static constexpr rhi::TextureFormat kBrdfFormat = rhi::TextureFormat::RG16Float;

    bool CreateResources() {
        // Env cube (single mip — prefilter samples mip 0; the mip-sampling improvement adds a chain later).
        rhi::TextureDesc ed{};
        ed.format = kCubeFormat; ed.width = kEnvResolution; ed.height = kEnvResolution;
        ed.arrayLayerCount = 6; ed.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
        ed.label = u8"ibl.env";
        if (!m_device->CreateTexture(ed, m_envCube).IsOk()) { return false; }
        rhi::TextureViewDesc ev{}; ev.format = kCubeFormat;
        ev.dimension = rhi::TextureViewDimension::TextureCube; ev.arrayLayerCount = 6;
        if (!m_device->CreateTextureView(m_envCube, ev, m_envSampleView).IsOk()) { return false; }

        // Prefilter cube (mip chain).
        rhi::TextureDesc pd{};
        pd.format = kCubeFormat; pd.width = kPrefilterRes; pd.height = kPrefilterRes;
        pd.arrayLayerCount = 6; pd.mipLevelCount = kPrefilterMips;
        pd.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled; pd.label = u8"ibl.prefilter";
        if (!m_device->CreateTexture(pd, m_prefilterCube).IsOk()) { return false; }
        rhi::TextureViewDesc pv{}; pv.format = kCubeFormat;
        pv.dimension = rhi::TextureViewDimension::TextureCube; pv.arrayLayerCount = 6; pv.mipLevelCount = kPrefilterMips;
        if (!m_device->CreateTextureView(m_prefilterCube, pv, m_prefilterView).IsOk()) { return false; }

        // BRDF LUT (2D).
        rhi::TextureDesc bd{};
        bd.format = kBrdfFormat; bd.width = kBrdfResolution; bd.height = kBrdfResolution;
        bd.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled; bd.label = u8"ibl.brdf";
        if (!m_device->CreateTexture(bd, m_brdfLut).IsOk()) { return false; }
        rhi::TextureViewDesc bv{}; bv.format = kBrdfFormat; bv.dimension = rhi::TextureViewDimension::Texture2D;
        if (!m_device->CreateTextureView(m_brdfLut, bv, m_brdfView).IsOk()) { return false; }

        // SH9 coefficient buffer (RW for the compute write, read-only in forward).
        rhi::BufferDesc sd{};
        sd.size = ShBytes(); sd.usage = rhi::BufferUsage::Storage; sd.memory = rhi::MemoryLocation::GpuOnly;
        sd.label = u8"ibl.sh";
        if (!m_device->CreateBuffer(sd, m_shBuffer).IsOk()) { return false; }

        // Linear-clamp sampler for cube/env sampling.
        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Linear; ss.magFilter = rhi::FilterMode::Linear;
        ss.mipmapFilter = rhi::MipmapFilterMode::Linear;
        ss.addressU = rhi::AddressMode::ClampToEdge; ss.addressV = rhi::AddressMode::ClampToEdge; ss.addressW = rhi::AddressMode::ClampToEdge;
        ss.label = u8"ibl.sampler";
        if (!m_device->CreateSampler(ss, m_sampler).IsOk()) { return false; }
        return true;
    }

    bool CreatePipelines() {
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"ibl_fs", shaders::ShaderStage::Vertex, shaders::ShaderFlags::None);
        if (vs == nullptr) { return false; }

        // --- env sample bind group layout (t0 cube + s0 sampler), shared by prefilter ---
        rhi::BindGroupLayoutEntry envTex = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::TextureCube);
        rhi::BindGroupLayoutEntry envSamp = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry envEntries[] = { envTex, envSamp };
        rhi::BindGroupLayoutDesc envLd{}; envLd.entries = Span<const rhi::BindGroupLayoutEntry>{ envEntries, 2 };
        if (!m_device->CreateBindGroupLayout(envLd, m_envLayout).IsOk()) { return false; }

        // --- pipeline layouts ---
        rhi::PushConstantRange pcRange{}; pcRange.stages = rhi::ShaderStage::Fragment; pcRange.offset = 0; pcRange.size = sizeof(IblPush);
        // procedural env: push constants only.
        rhi::PipelineLayoutDesc envPld{}; envPld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pcRange, 1 };
        if (!m_device->CreatePipelineLayout(envPld, m_envOnlyLayout).IsOk()) { return false; }
        // prefilter: env bind group + push constants.
        rhi::BindGroupLayout* preLayouts[] = { m_envLayout };
        rhi::PipelineLayoutDesc prePld{};
        prePld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ preLayouts, 1 };
        prePld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pcRange, 1 };
        if (!m_device->CreatePipelineLayout(prePld, m_prefilterLayout).IsOk()) { return false; }
        // brdf: no inputs.
        rhi::PipelineLayoutDesc brdfPld{};
        if (!m_device->CreatePipelineLayout(brdfPld, m_brdfPipelineLayout).IsOk()) { return false; }

        m_envPipeline       = MakeFullscreenPipeline(vs, u8"ibl_procenv", m_envOnlyLayout, kCubeFormat);
        m_prefilterPipeline = MakeFullscreenPipeline(vs, u8"ibl_prefilter", m_prefilterLayout, kCubeFormat);
        m_brdfPipeline      = MakeFullscreenPipeline(vs, u8"ibl_brdf", m_brdfPipelineLayout, kBrdfFormat);
        if (m_envPipeline == nullptr || m_prefilterPipeline == nullptr || m_brdfPipeline == nullptr) { return false; }

        // env sample bind group (for prefilter).
        rhi::BindGroupEntry be[] = { rhi::BindGroupEntry::TextureEntry(m_envSampleView), rhi::BindGroupEntry::SamplerEntry(m_sampler) };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_envLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ be, 2 };
        if (!m_device->CreateBindGroup(bgd, m_envBindGroup).IsOk()) { return false; }

        // --- SH compute pipeline + bind group (t0 cube + s0 sampler + u0 SH buffer) ---
        rhi::ShaderModule* cs = m_shaders->GetVariant(u8"ibl_sh", shaders::ShaderStage::Compute, shaders::ShaderFlags::None);
        if (cs == nullptr) { return false; }
        rhi::BindGroupLayoutEntry shTex = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Compute, rhi::TextureViewDimension::TextureCube);
        rhi::BindGroupLayoutEntry shSamp = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Compute);
        rhi::BindGroupLayoutEntry shOut = rhi::BindGroupLayoutEntry::StorageBuffer(0, rhi::ShaderStage::Compute, /*readOnly*/ false);
        rhi::BindGroupLayoutEntry shEntries[] = { shTex, shSamp, shOut };
        rhi::BindGroupLayoutDesc shLd{}; shLd.entries = Span<const rhi::BindGroupLayoutEntry>{ shEntries, 3 };
        if (!m_device->CreateBindGroupLayout(shLd, m_shLayout).IsOk()) { return false; }
        rhi::BindGroupLayout* shLayouts[] = { m_shLayout };
        rhi::PipelineLayoutDesc shPld{}; shPld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ shLayouts, 1 };
        if (!m_device->CreatePipelineLayout(shPld, m_shPipelineLayout).IsOk()) { return false; }
        rhi::ComputePipelineDesc cpd{}; cpd.layout = m_shPipelineLayout;
        cpd.compute = rhi::ProgrammableStage{ cs, u8"main", rhi::ShaderStage::Compute }; cpd.label = u8"ibl.sh";
        if (!m_device->CreateComputePipeline(cpd, m_shPipeline).IsOk()) { return false; }
        rhi::BindGroupEntry she[] = {
            rhi::BindGroupEntry::TextureEntry(m_envSampleView),
            rhi::BindGroupEntry::SamplerEntry(m_sampler),
            rhi::BindGroupEntry::BufferEntry(m_shBuffer, 0, ShBytes()),
        };
        rhi::BindGroupDesc shBgd{}; shBgd.layout = m_shLayout; shBgd.entries = Span<const rhi::BindGroupEntry>{ she, 3 };
        if (!m_device->CreateBindGroup(shBgd, m_shBindGroup).IsOk()) { return false; }

        m_ready = true;
        return true;
    }

    rhi::RenderPipeline* MakeFullscreenPipeline(rhi::ShaderModule* vs, StringView psName,
                                                rhi::PipelineLayout* layout, rhi::TextureFormat fmt) {
        rhi::ShaderModule* ps = m_shaders->GetVariant(psName, shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (ps == nullptr) { return nullptr; }
        rhi::ColorTargetState color{}; color.format = fmt;
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ &color, 1 };
        rhi::RenderPipelineDesc pd{};
        pd.layout = layout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = psName;
        rhi::RenderPipeline* p = nullptr;
        if (!m_device->CreateRenderPipeline(pd, p).IsOk()) { return nullptr; }
        return p;
    }

    // Concatenate two shader source literals into an owned String (kIblCommon + a PS body).
    static String Concat(const char8_t* a, const char8_t* b) {
        String s(StringView{ a }); s.Append(StringView{ b }); return s;
    }

    void Shutdown() {
        if (m_shBindGroup) { m_device->DestroyBindGroup(m_shBindGroup); m_shBindGroup = nullptr; }
        if (m_envBindGroup) { m_device->DestroyBindGroup(m_envBindGroup); m_envBindGroup = nullptr; }
        if (m_shPipeline) { m_device->DestroyComputePipeline(m_shPipeline); m_shPipeline = nullptr; }
        if (m_envPipeline) { m_device->DestroyRenderPipeline(m_envPipeline); m_envPipeline = nullptr; }
        if (m_prefilterPipeline) { m_device->DestroyRenderPipeline(m_prefilterPipeline); m_prefilterPipeline = nullptr; }
        if (m_brdfPipeline) { m_device->DestroyRenderPipeline(m_brdfPipeline); m_brdfPipeline = nullptr; }
        if (m_shPipelineLayout) { m_device->DestroyPipelineLayout(m_shPipelineLayout); m_shPipelineLayout = nullptr; }
        if (m_envOnlyLayout) { m_device->DestroyPipelineLayout(m_envOnlyLayout); m_envOnlyLayout = nullptr; }
        if (m_prefilterLayout) { m_device->DestroyPipelineLayout(m_prefilterLayout); m_prefilterLayout = nullptr; }
        if (m_brdfPipelineLayout) { m_device->DestroyPipelineLayout(m_brdfPipelineLayout); m_brdfPipelineLayout = nullptr; }
        if (m_shLayout) { m_device->DestroyBindGroupLayout(m_shLayout); m_shLayout = nullptr; }
        if (m_envLayout) { m_device->DestroyBindGroupLayout(m_envLayout); m_envLayout = nullptr; }
        if (m_sampler) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
        if (m_shBuffer) { m_device->DestroyBuffer(m_shBuffer); m_shBuffer = nullptr; }
        if (m_envSampleView) { m_device->DestroyTextureView(m_envSampleView); m_envSampleView = nullptr; }
        if (m_envCube) { m_device->DestroyTexture(m_envCube); m_envCube = nullptr; }
        if (m_prefilterView) { m_device->DestroyTextureView(m_prefilterView); m_prefilterView = nullptr; }
        if (m_prefilterCube) { m_device->DestroyTexture(m_prefilterCube); m_prefilterCube = nullptr; }
        if (m_brdfView) { m_device->DestroyTextureView(m_brdfView); m_brdfView = nullptr; }
        if (m_brdfLut) { m_device->DestroyTexture(m_brdfLut); m_brdfLut = nullptr; }
    }

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;

    rhi::Texture*     m_envCube = nullptr;        rhi::TextureView* m_envSampleView = nullptr;
    rhi::Texture*     m_prefilterCube = nullptr;  rhi::TextureView* m_prefilterView = nullptr;
    rhi::Texture*     m_brdfLut = nullptr;        rhi::TextureView* m_brdfView = nullptr;
    rhi::Buffer*      m_shBuffer = nullptr;
    rhi::Sampler*     m_sampler = nullptr;

    rhi::BindGroupLayout* m_envLayout = nullptr;
    rhi::BindGroupLayout* m_shLayout = nullptr;
    rhi::PipelineLayout*  m_envOnlyLayout = nullptr;
    rhi::PipelineLayout*  m_prefilterLayout = nullptr;
    rhi::PipelineLayout*  m_brdfPipelineLayout = nullptr;
    rhi::PipelineLayout*  m_shPipelineLayout = nullptr;
    rhi::RenderPipeline*  m_envPipeline = nullptr;
    rhi::RenderPipeline*  m_prefilterPipeline = nullptr;
    rhi::RenderPipeline*  m_brdfPipeline = nullptr;
    rhi::ComputePipeline* m_shPipeline = nullptr;
    rhi::BindGroup*       m_envBindGroup = nullptr;
    rhi::BindGroup*       m_shBindGroup = nullptr;

    // Imported-target persisted states (carried across frames for the graph's barrier solver).
    rhi::ResourceState m_envState = rhi::ResourceState::Undefined;
    rhi::ResourceState m_prefilterState = rhi::ResourceState::Undefined;
    rhi::ResourceState m_brdfState = rhi::ResourceState::Undefined;

    // This frame's product handles (re-imported each ProcessPending; read by the forward pass).
    rendergraph::RGHandle m_prefilterH = {};
    rendergraph::RGHandle m_brdfH = {};
    rendergraph::RGHandle m_shH = {};

    Vec3        m_sunDir{ 0.0f, -1.0f, 0.0f };   // from the directional light (set per frame)
    SkySnapshot m_sky{};                          // current sky authoring
    bool m_ready = false;
    bool m_dirty = false;
    bool m_brdfDone = false;   // the BRDF LUT is constant — generated once, not per sky change
    u64  m_generation = 0;
};

} // namespace draconic::render

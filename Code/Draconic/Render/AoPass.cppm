/// Draconic::Render - the `:ao` partition.
///
/// Ambient occlusion. Two interchangeable generators feed one shared pipeline:
///   - GTAO: Ground-Truth AO (Jimenez horizon integration over screen-space slices).
///   - SSAO: classic hemisphere-kernel occlusion (Crysis-style), ported from Sedulous.
/// Both consume the opaque depth + octahedral view-normal (G-buffer), write an R8 AO transient,
/// then share the SAME depth-aware bilateral blur, the SAME apply pass (multiply into the HDR before
/// TAA so the resolve stabilizes it), the SAME bind-group cache, and the SAME debug channels. The two
/// modes are mutually exclusive (AoMode). All targets are render-graph transients (sized per view).

module;
#include "Core/Prelude.h"

export module draconic.render:ao;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Which AO generator to run (mutually exclusive - both write the same AO buffer).
enum class AoMode : u32 { Off = 0, GTAO = 1, SSAO = 2 };

// Fullscreen-triangle VS, top-origin uv (matches the other post passes under the negative-viewport flip).
inline constexpr const char8_t* kAoVS = u8R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    float2 raw = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(raw * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(raw.x, 1.0 - raw.y);
    return o;
}
)";

// Shared reconstruction helpers, textbook for both generators. NDC convention derived from the (proven,
// since TAA works) motion-vector mapping in the forward pass: velocity = (ndc - prev) * (0.5, -0.5) means
// uv.y = (1 - ndc.y)/2, i.e. top-origin uv.y=0 -> ndc.y=+1 under the RHI's automatic negative viewport.
inline constexpr const char8_t* kAoCommon = u8R"(
// Octahedral decode -> view-space normal (matches the forward's OctEncode).
float3 OctDecode(float2 e) {
    float3 n = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    float  t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
)";

// GTAO generate: horizon-based AO in view space. Depth (t0) + octahedral view-normal (t1).
inline constexpr const char8_t* kGtaoGenPS = u8R"(
Texture2D    DepthTex  : register(t0, space0);
Texture2D    NormalTex : register(t1, space0);
SamplerState PointSamp : register(s0, space0);
struct GtaoPush {
    row_major float4x4 InvProj;   // inverse projection: (ndc, depth) -> view space
    float2 TexelSize;             // 1 / size
    float  Radius;                // AO world-space radius
    float  Intensity;             // AO power
    float  ProjScaleY;            // projection(1,1): world radius -> screen (at unit view depth)
    int    FrameMod;              // per-frame noise rotation (unused while AO is static)
    int    DebugMode;             // 0=AO, 2=Nx, 3=Ny, 4=Nz, 5=viewZ, 6=rawDepth
    int    _pad;
};
[[vk::push_constant]] GtaoPush pc;

static const float PI     = 3.14159265359;
static const float HALFPI = 1.57079632679;
static const int   SLICES = 3;
static const int   STEPS  = 6;

float3 ViewPos(float2 uv, float depth) {
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 h = mul(float4(ndc, depth, 1.0), pc.InvProj);
    return h.xyz / h.w;
}
// Cosine-weighted arc integral for one horizon angle H, given the projected-normal angle n.
float ArcCosWeight(float H, float n) { return -cos(2.0 * H - n) + cos(n) + 2.0 * H * sin(n); }

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float depth = DepthTex.SampleLevel(PointSamp, uv, 0).r;
    if (depth >= 1.0) { return float4(1.0, 0, 0, 0); }   // background: no occlusion

    float3 P = ViewPos(uv, depth);
    float3 N = OctDecode(NormalTex.SampleLevel(PointSamp, uv, 0).rg);
    float3 V = normalize(-P);

    // uv-space march radius for this pixel's depth. NDC spans [-1,1] (length 2) -> uv [0,1] (length 1),
    // hence the /2. Clamp: at least one texel, at most a quarter screen.
    float screenR = pc.Radius * pc.ProjScaleY / (2.0 * max(-P.z, 1e-3));
    screenR = clamp(screenR, pc.TexelSize.y, 0.25);

    // Static interleaved-gradient noise (blur denoises it; static avoids temporal flicker).
    float noise      = frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715))));
    float noiseSlice = noise;
    float noiseStep  = frac(noise * 1.6180339887);

    const float invR2     = 2.0 / max(pc.Radius * pc.Radius, 1e-4);
    const float thickness = 0.1;

    float ao = 0.0;
    [unroll] for (int s = 0; s < SLICES; ++s) {
        float  phi  = (PI / float(SLICES)) * (float(s) + noiseSlice);
        float2 dir2 = float2(cos(phi), sin(phi));
        float3 sliceDir = normalize(float3(dir2, 0.0));

        float3 planeN = cross(sliceDir, V);
        float  planeLen = length(planeN);
        if (planeLen < 1e-4) { continue; }
        planeN /= planeLen;
        float3 T     = cross(V, planeN);
        float3 projN = N - planeN * dot(N, planeN);
        float  projLen = length(projN);
        if (projLen < 1e-4) { continue; }
        // Signed projected-normal angle; sign must match the reference (-sign(dot(projN,T))) or the arc
        // is corrupted on curved surfaces (where N varies) while looking fine on flat camera-facing faces.
        float  cosN = clamp(dot(projN, V) / projLen, -1.0, 1.0);
        float  n = -sign(dot(projN, T)) * acos(cosN);

        float2 hcos = float2(-1.0, -1.0);   // x = negative side, y = positive side
        [unroll] for (int t = 1; t <= STEPS; ++t) {
            float  r   = screenR * (float(t) - noiseStep) / float(STEPS);
            float2 off = dir2 * r;
            float2 up  = uv + off;
            if (all(up >= 0.0) && all(up <= 1.0)) {
                float3 ds = ViewPos(up, DepthTex.SampleLevel(PointSamp, up, 0).r) - P;
                float  d2 = dot(ds, ds);
                float  H  = dot(ds, V) * rsqrt(max(d2, 1e-12));
                float  fo = saturate(d2 * invR2);
                hcos.y = (H > hcos.y) ? lerp(H, hcos.y, fo) : lerp(H, hcos.y, thickness);
            }
            float2 un = uv - off;
            if (all(un >= 0.0) && all(un <= 1.0)) {
                float3 ds = ViewPos(un, DepthTex.SampleLevel(PointSamp, un, 0).r) - P;
                float  d2 = dot(ds, ds);
                float  H  = dot(ds, V) * rsqrt(max(d2, 1e-12));
                float  fo = saturate(d2 * invR2);
                hcos.x = (H > hcos.x) ? lerp(H, hcos.x, fo) : lerp(H, hcos.x, thickness);
            }
        }
        float h1 = acos(clamp(hcos.x, -1.0, 1.0));
        float h2 = acos(clamp(hcos.y, -1.0, 1.0));
        float H1 = n + max(-h1 - n, -HALFPI);
        float H2 = n + min( h2 - n,  HALFPI);
        ao += projLen * 0.25 * (ArcCosWeight(H1, n) + ArcCosWeight(H2, n));
    }
    ao = saturate(ao / float(SLICES));
    ao = pow(ao, max(pc.Intensity, 0.01));

    if (pc.DebugMode == 2) { return float4(N.x * 0.5 + 0.5, 0, 0, 0); }
    if (pc.DebugMode == 3) { return float4(N.y * 0.5 + 0.5, 0, 0, 0); }
    if (pc.DebugMode == 4) { return float4(N.z * 0.5 + 0.5, 0, 0, 0); }
    if (pc.DebugMode == 5) { return float4(saturate(-P.z / 50.0), 0, 0, 0); }
    if (pc.DebugMode == 6) { return float4(saturate((depth - 0.8) * 5.0), 0, 0, 0); }
    return float4(ao, 0, 0, 0);
}
)";

// SSAO generate: hemisphere-kernel occlusion (ported from Sedulous). Depth (t0) + view-normal (t1).
// Reconstruct P, orient a Poisson hemisphere kernel by the normal (TBN + per-pixel rotation), offset in
// view space, project back with the diagonal proj terms, and compare depths. Projecting with ProjXX/ProjYY
// (symmetric perspective) instead of a full matrix keeps the push under the 128-byte portable limit.
inline constexpr const char8_t* kSsaoGenPS = u8R"(
Texture2D    DepthTex  : register(t0, space0);
Texture2D    NormalTex : register(t1, space0);
SamplerState PointSamp : register(s0, space0);
struct SsaoPush {
    row_major float4x4 InvProj;   // (ndc, depth) -> view space
    float2 TexelSize;
    float2 Jitter;                // this frame's projection jitter (proj(2,0), proj(2,1)) in NDC
    float  ProjXX;                // projection(0,0): view -> ndc.x
    float  ProjYY;                // projection(1,1): view -> ndc.y
    float  Radius;
    float  Intensity;
    float  Bias;
    int    SampleCount;
    int    DebugMode;
};
[[vk::push_constant]] SsaoPush pc;

float3 ViewPos(float2 uv, float depth) {
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 h = mul(float4(ndc, depth, 1.0), pc.InvProj);
    return h.xyz / h.w;
}
// View-space position -> top-origin uv (inverse of ViewPos's uv->ndc: uv.y = (1-ndc.y)/2). The jitter
// (in the projection's z-row, not the diagonal ProjXX/YY) must be included or the back-projected UV won't
// match the jittered depth buffer we sample -> per-frame occlusion oscillation (flicker under TAA).
float2 ViewToUv(float3 vp) {
    // Full jittered ndc = diagonal ndc - jitter (the jitter lives in proj's z-row: ndc shifts by -jitter).
    float2 ndc = float2(vp.x * pc.ProjXX, vp.y * pc.ProjYY) / max(-vp.z, 1e-4) - pc.Jitter;
    return float2(ndc.x * 0.5 + 0.5, 0.5 - 0.5 * ndc.y);
}
static const float3 KERNEL[16] = {
    float3( 0.5381, 0.1856,-0.4319), float3( 0.1379, 0.2486, 0.4430),
    float3( 0.3371, 0.5679,-0.0057), float3(-0.6999,-0.0451,-0.0019),
    float3( 0.0689,-0.1598,-0.8547), float3( 0.0560, 0.0069,-0.1843),
    float3(-0.0146, 0.1402, 0.0762), float3( 0.0100,-0.1924,-0.0344),
    float3(-0.3577,-0.5301,-0.4358), float3(-0.3169, 0.1063, 0.0158),
    float3( 0.0103,-0.5869, 0.0046), float3(-0.0897,-0.4940, 0.3287),
    float3( 0.7119,-0.0154,-0.0918), float3(-0.0533, 0.0596,-0.5411),
    float3( 0.0352,-0.0631, 0.5460), float3(-0.4776, 0.2847,-0.0271)
};

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float depth = DepthTex.SampleLevel(PointSamp, uv, 0).r;
    if (depth >= 1.0) { return float4(1.0, 0, 0, 0); }

    float3 P = ViewPos(uv, depth);
    float3 N = OctDecode(NormalTex.SampleLevel(PointSamp, uv, 0).rg);

    // Per-pixel rotation + TBN for hemisphere orientation. Interleaved-gradient noise (not white-noise
    // hash): it denoises cleanly under the bilateral blur + TAA, whereas a white-noise pattern swims under
    // motion and flickers when the AO is baked into color pre-TAA.
    float ign = frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715))));
    float a = ign * 6.2831853;
    float ca = cos(a), sa = sin(a);
    float3 tangent = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    tangent = normalize(tangent - N * dot(tangent, N));
    float3 bitangent = cross(N, tangent);

    int   count = min(pc.SampleCount, 16);
    float occlusion = 0.0; int valid = 0;
    [loop] for (int i = 0; i < count; ++i) {
        float3 k = KERNEL[i];
        float3 rot = float3(k.x * ca - k.y * sa, k.x * sa + k.y * ca, k.z);   // rotate in tangent plane
        float3 off = tangent * rot.x + bitangent * rot.y + N * rot.z;         // orient to hemisphere
        float  scale = (float(i) + 1.0) / float(count);
        scale = lerp(0.1, 1.0, scale * scale);                               // cluster samples near P
        float3 samplePos = P + off * pc.Radius * scale;

        float2 sUv = ViewToUv(samplePos);
        if (any(sUv < 0.0) || any(sUv > 1.0)) { continue; }
        float  sampleZ = ViewPos(sUv, DepthTex.SampleLevel(PointSamp, sUv, 0).r).z;
        float  diff = sampleZ - P.z;   // RH view space: an occluder (closer) is less negative -> larger
        // Smooth occlusion ramp, NOT a hard step: under TAA the depth is jittered sub-pixel each frame, so
        // a binary test flips samples on/off between frames -> shimmer. A soft band makes SSAO continuous
        // in its inputs (like GTAO's arc integral), so TAA can stabilize it.
        float  band = max(pc.Radius * 0.15, 1e-3);
        float  occluded = smoothstep(pc.Bias, pc.Bias + band, diff);
        float  rangeCheck = smoothstep(0.0, 1.0, pc.Radius / (abs(diff) + 0.001));
        if (abs(diff) > pc.Radius * 2.0) { rangeCheck = 0.0; }               // reject far leaks
        occlusion += occluded * rangeCheck;
        valid++;
    }
    float ao = 1.0;
    if (valid > 0) { ao = 1.0 - occlusion / float(valid); ao = pow(saturate(ao), max(pc.Intensity, 0.01)); }

    if (pc.DebugMode == 2) { return float4(N.x * 0.5 + 0.5, 0, 0, 0); }
    if (pc.DebugMode == 3) { return float4(N.y * 0.5 + 0.5, 0, 0, 0); }
    if (pc.DebugMode == 4) { return float4(N.z * 0.5 + 0.5, 0, 0, 0); }
    if (pc.DebugMode == 5) { return float4(saturate(-P.z / 50.0), 0, 0, 0); }
    if (pc.DebugMode == 6) { return float4(saturate((depth - 0.8) * 5.0), 0, 0, 0); }
    return float4(ao, 0, 0, 0);
}
)";

// Depth-aware separable bilateral blur (denoise the raw AO). One pass = one axis; run twice.
inline constexpr const char8_t* kAoBlurPS = u8R"(
Texture2D    AoTex     : register(t0, space0);
Texture2D    DepthTex  : register(t1, space0);
SamplerState PointSamp : register(s0, space0);
struct BlurPush { float2 Dir; float2 TexelSize; float DepthSigma; float3 _pad; };
[[vk::push_constant]] BlurPush pc;

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float  centerD = DepthTex.SampleLevel(PointSamp, uv, 0).r;
    float  sum = 0.0, wsum = 0.0;
    [unroll] for (int i = -3; i <= 3; ++i) {
        float2 suv = uv + pc.Dir * pc.TexelSize * float(i);
        float  a   = AoTex.SampleLevel(PointSamp, suv, 0).r;
        float  d   = DepthTex.SampleLevel(PointSamp, suv, 0).r;
        float  wd  = exp(-abs(d - centerD) * pc.DepthSigma);
        float  ws  = exp(-float(i * i) * 0.25);
        float  w   = wd * ws;
        sum += a * w; wsum += w;
    }
    return float4(sum / max(wsum, 1e-5), 0, 0, 0);
}
)";

// Multiply AO into an HDR color target: out = hdr * lerp(1, ao, Strength). Run BEFORE the TAA resolve
// so TAA temporally stabilizes the AO (applying it post-TAA wobbles, since the AO is computed from the
// jittered G-buffer and shifts sub-pixel each frame).
inline constexpr const char8_t* kAoApplyPS = u8R"(
Texture2D<float4> HdrTex    : register(t0, space0);
Texture2D<float4> AoTex     : register(t1, space0);
SamplerState      PointSamp : register(s0, space0);
struct ApplyPush { float Strength; float3 _pad; };
[[vk::push_constant]] ApplyPush pc;
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 c  = HdrTex.SampleLevel(PointSamp, uv, 0).rgb;
    float  ao = lerp(1.0, AoTex.SampleLevel(PointSamp, uv, 0).r, saturate(pc.Strength));
    return float4(c * ao, 1.0);
}
)";

// Owns both AO generators + the shared blur/apply pipelines. Produces an AO transient (R8) applied to
// the HDR before TAA.
class AoPass {
public:
    static constexpr rhi::TextureFormat kAoFormat = rhi::TextureFormat::R8Unorm;

    AoPass(rhi::Device& device, shaders::ShaderSystem& shaders) noexcept
        : m_device(&device), m_shaders(&shaders) {}
    ~AoPass() { Shutdown(); }
    AoPass(const AoPass&) = delete;
    AoPass& operator=(const AoPass&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"ao_gtao",  shaders::ShaderStage::Vertex,   kAoVS);
        m_shaders->RegisterSource(u8"ao_gtao",  shaders::ShaderStage::Fragment, Concat(kAoCommon, kGtaoGenPS));
        m_shaders->RegisterSource(u8"ao_ssao",  shaders::ShaderStage::Vertex,   kAoVS);
        m_shaders->RegisterSource(u8"ao_ssao",  shaders::ShaderStage::Fragment, Concat(kAoCommon, kSsaoGenPS));
        m_shaders->RegisterSource(u8"ao_blur",  shaders::ShaderStage::Vertex,   kAoVS);
        m_shaders->RegisterSource(u8"ao_blur",  shaders::ShaderStage::Fragment, kAoBlurPS);
        m_shaders->RegisterSource(u8"ao_apply", shaders::ShaderStage::Vertex,   kAoVS);
        m_shaders->RegisterSource(u8"ao_apply", shaders::ShaderStage::Fragment, kAoApplyPS);

        // Shared bind-group layout: two sampled textures (t0, t1) + a sampler (s0).
        rhi::BindGroupLayoutEntry ge[] = {
            rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::SampledTexture(1, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment),
        };
        rhi::BindGroupLayoutDesc gld{}; gld.entries = Span<const rhi::BindGroupLayoutEntry>{ ge, 3 };
        if (!m_device->CreateBindGroupLayout(gld, m_layout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        m_gtaoLayout  = MakePipelineLayout(sizeof(GtaoPushC));
        m_ssaoLayout  = MakePipelineLayout(sizeof(SsaoPushC));
        m_blurLayout  = MakePipelineLayout(sizeof(BlurPushC));
        m_applyLayout = MakePipelineLayout(sizeof(ApplyPushC));
        if (m_gtaoLayout == nullptr || m_ssaoLayout == nullptr || m_blurLayout == nullptr || m_applyLayout == nullptr) {
            return Status{ ErrorCode::Unknown };
        }

        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Nearest; ss.magFilter = rhi::FilterMode::Nearest;
        ss.addressU = rhi::AddressMode::ClampToEdge; ss.addressV = rhi::AddressMode::ClampToEdge; ss.addressW = rhi::AddressMode::ClampToEdge;
        ss.label = u8"ao.sampler";
        if (!m_device->CreateSampler(ss, m_sampler).IsOk()) { return Status{ ErrorCode::Unknown }; }

        m_gtaoPipeline  = MakePipeline(u8"ao_gtao",  m_gtaoLayout);
        m_ssaoPipeline  = MakePipeline(u8"ao_ssao",  m_ssaoLayout);
        m_blurPipeline  = MakePipeline(u8"ao_blur",  m_blurLayout);
        m_applyPipeline = MakePipeline(u8"ao_apply", m_applyLayout, kHdrFormat);
        if (m_gtaoPipeline == nullptr || m_ssaoPipeline == nullptr || m_blurPipeline == nullptr || m_applyPipeline == nullptr) {
            return Status{ ErrorCode::Unknown };
        }
        return Status{};
    }

    [[nodiscard]] rhi::TextureFormat AoFormat() const noexcept { return kAoFormat; }

    // Declare AO for one view; returns the (blurred) AO handle, or invalid if mode is Off. invProj =
    // inverse camera projection, proj = camera projection (GTAO uses proj(1,1); SSAO uses proj(0,0)/(1,1)).
    [[nodiscard]] rendergraph::RGHandle DeclareAo(rendergraph::RenderGraph& graph, rendergraph::RGHandle depth,
                                                  rendergraph::RGHandle normal, u32 w, u32 h, const Float4x4& invProj,
                                                  const Float4x4& proj, f32 radius, f32 intensity, u32 frameIndex,
                                                  AoMode mode, i32 debugMode = 0) {
        if (mode == AoMode::Off || w == 0 || h == 0) { return {}; }
        Tick(frameIndex);
        const Float2 texel{ 1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h) };
        const rendergraph::RGHandle aoRaw = graph.CreateTransient(u8"ao.raw", rendergraph::RGTextureDesc(kAoFormat, w, h));
        const rendergraph::RGHandle aoTmp = graph.CreateTransient(u8"ao.tmp", rendergraph::RGTextureDesc(kAoFormat, w, h));
        const rendergraph::RGHandle aoOut = graph.CreateTransient(u8"ao.ao",  rendergraph::RGTextureDesc(kAoFormat, w, h));

        PushBuf push{};
        rhi::RenderPipeline* pipeline = nullptr;
        if (mode == AoMode::GTAO) {
            GtaoPushC gp{}; gp.invProj = invProj; gp.texelSize = texel; gp.radius = radius; gp.intensity = intensity;
            gp.projScaleY = proj(1, 1); gp.frameMod = static_cast<i32>(frameIndex & 63u); gp.debugMode = debugMode;
            push = PushBuf::From(gp); pipeline = m_gtaoPipeline;
        } else {
            SsaoPushC sp{}; sp.invProj = invProj; sp.texelSize = texel; sp.projXX = proj(0, 0); sp.projYY = proj(1, 1);
            sp.jitter = Float2{ proj(2, 0), proj(2, 1) };   // NDC jitter (proj z-row) so back-projection matches the jittered depth
            sp.radius = radius; sp.intensity = intensity; sp.bias = 0.05f; sp.sampleCount = 16; sp.debugMode = debugMode;
            push = PushBuf::From(sp); pipeline = m_ssaoPipeline;
        }

        graph.AddRenderPass(u8"ao.gen", [this, &graph, depth, normal, aoRaw, w, h, pipeline, push](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, aoRaw, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::White());
            b.ReadTexture(depth);
            b.ReadTexture(normal);
            b.SetViewport(0, 0, w, h);
            b.NeverCull();
            b.SetExecute([this, &graph, depth, normal, pipeline, push](rhi::RenderPassEncoder& rp) {
                rhi::BindGroup* bg = EnsureBindGroup(graph.GetTextureView(depth), graph.GetTextureView(normal),
                                                     graph.GetTextureGeneration(depth) ^ (graph.GetTextureGeneration(normal) * 1099511628211ull));
                if (bg == nullptr) { return; }
                rp.SetPipeline(pipeline);
                rp.SetBindGroup(0, bg, Span<const u32>{});
                rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, push.size, push.data);
                rp.Draw(3, 1, 0, 0);
            });
        });
        // Non-AO debug channels are raw per-pixel values - skip the bilateral blur that would smear them.
        if (debugMode >= 2) { return aoRaw; }
        DeclareBlur(graph, aoRaw, depth, aoTmp, w, h, texel, Float2{ 1.0f, 0.0f });
        DeclareBlur(graph, aoTmp, depth, aoOut, w, h, texel, Float2{ 0.0f, 1.0f });
        return aoOut;
    }

    // Multiply the AO into `hdr` (out = hdr * lerp(1, ao, strength)) into a fresh HDR transient, returned.
    // Run BEFORE the TAA resolve so TAA stabilizes the AO (post-TAA application wobbles under jitter).
    [[nodiscard]] rendergraph::RGHandle DeclareApply(rendergraph::RenderGraph& graph, rendergraph::RGHandle hdr,
                                                     rendergraph::RGHandle ao, u32 w, u32 h, f32 strength) {
        if (w == 0 || h == 0) { return hdr; }
        const rendergraph::RGHandle out = graph.CreateTransient(u8"ao.applied", rendergraph::RGTextureDesc(kHdrFormat, w, h));
        ApplyPushC ap{}; ap.strength = strength;
        graph.AddRenderPass(u8"ao.apply", [this, &graph, hdr, ao, out, w, h, ap](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, out, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
            b.ReadTexture(hdr);
            b.ReadTexture(ao);
            b.SetViewport(0, 0, w, h);
            b.NeverCull();
            b.SetExecute([this, &graph, hdr, ao, ap](rhi::RenderPassEncoder& rp) {
                rhi::BindGroup* bg = EnsureBindGroup(graph.GetTextureView(hdr), graph.GetTextureView(ao),
                                                     graph.GetTextureGeneration(hdr) ^ (graph.GetTextureGeneration(ao) * 1099511628211ull));
                if (bg == nullptr) { return; }
                rp.SetPipeline(m_applyPipeline);
                rp.SetBindGroup(0, bg, Span<const u32>{});
                rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(ApplyPushC), &ap);
                rp.Draw(3, 1, 0, 0);
            });
        });
        return out;
    }

private:
    static constexpr rhi::TextureFormat kHdrFormat = rhi::TextureFormat::RGBA16Float;   // matches the scene HDR
    struct GtaoPushC { Float4x4 invProj{}; Float2 texelSize{}; f32 radius = 0.5f; f32 intensity = 1.0f; f32 projScaleY = 1.0f; i32 frameMod = 0; i32 debugMode = 0; i32 pad = 0; };
    struct SsaoPushC { Float4x4 invProj{}; Float2 texelSize{}; Float2 jitter{}; f32 projXX = 1.0f; f32 projYY = 1.0f; f32 radius = 0.5f; f32 intensity = 1.0f; f32 bias = 0.05f; i32 sampleCount = 16; i32 debugMode = 0; };
    struct BlurPushC { Float2 dir{}; Float2 texelSize{}; f32 depthSigma = 120.0f; f32 p0 = 0, p1 = 0, p2 = 0; };
    struct ApplyPushC { f32 strength = 1.0f; f32 p0 = 0, p1 = 0, p2 = 0; };

    // A fixed byte buffer so a generate push (GTAO or SSAO) can be captured by value into the pass lambda.
    struct PushBuf {
        u8  data[128] = {};
        u32 size = 0;
        template <class T> static PushBuf From(const T& v) {
            static_assert(sizeof(T) <= 128, "AO push exceeds the portable 128-byte push-constant limit");
            PushBuf b; b.size = sizeof(T);
            const u8* src = reinterpret_cast<const u8*>(&v);
            for (u32 i = 0; i < b.size; ++i) { b.data[i] = src[i]; }
            return b;
        }
    };

    // Prepend the shared helpers (OctDecode etc. live in kAoCommon) to a generate shader body.
    static String Concat(const char8_t* a, const char8_t* b) { String s(StringView{ a }); s.Append(StringView{ b }); return s; }

    rhi::PipelineLayout* MakePipelineLayout(usize pushSize) {
        rhi::BindGroupLayout* gl[] = { m_layout };
        rhi::PushConstantRange pc{}; pc.stages = rhi::ShaderStage::Fragment; pc.offset = 0; pc.size = static_cast<u32>(pushSize);
        rhi::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ gl, 1 };
        pld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pc, 1 };
        rhi::PipelineLayout* layout = nullptr;
        if (!m_device->CreatePipelineLayout(pld, layout).IsOk()) { return nullptr; }
        return layout;
    }

    void DeclareBlur(rendergraph::RenderGraph& graph, rendergraph::RGHandle ao, rendergraph::RGHandle depth,
                     rendergraph::RGHandle out, u32 w, u32 h, Float2 texel, Float2 dir) {
        BlurPushC bp{}; bp.dir = dir; bp.texelSize = texel;
        graph.AddRenderPass(u8"ao.blur", [this, &graph, ao, depth, out, w, h, bp](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, out, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::White());
            b.ReadTexture(ao);
            b.ReadTexture(depth);
            b.SetViewport(0, 0, w, h);
            b.NeverCull();
            b.SetExecute([this, &graph, ao, depth, bp](rhi::RenderPassEncoder& rp) {
                rhi::BindGroup* bg = EnsureBindGroup(graph.GetTextureView(ao), graph.GetTextureView(depth),
                                                     graph.GetTextureGeneration(ao) ^ (graph.GetTextureGeneration(depth) * 14695981039346656037ull));
                if (bg == nullptr) { return; }
                rp.SetPipeline(m_blurPipeline);
                rp.SetBindGroup(0, bg, Span<const u32>{});
                rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(BlurPushC), &bp);
                rp.Draw(3, 1, 0, 0);
            });
        });
    }

    rhi::RenderPipeline* MakePipeline(StringView name, rhi::PipelineLayout* layout, rhi::TextureFormat fmt = kAoFormat) {
        rhi::ShaderModule* vs = m_shaders->GetVariant(name, shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(name, shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return nullptr; }
        rhi::ColorTargetState color{}; color.format = fmt;
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ &color, 1 };
        rhi::RenderPipelineDesc pd{};
        pd.layout = layout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = name;
        rhi::RenderPipeline* p = nullptr;
        if (!m_device->CreateRenderPipeline(pd, p).IsOk()) { return nullptr; }
        return p;
    }

    // Advance the deferred-free list once per frame (frees bind groups retired long enough ago to be
    // idle). The graph aliases transients, so a raw-pointer cache thrashes mid-frame - replaced sets go
    // to the retire list (freed after kRetireFrames) instead of being freed while still in-flight.
    void Tick(u32 frameIndex) {
        if (frameIndex == m_lastFrame) { return; }
        m_lastFrame = frameIndex;
        usize w = 0;
        for (usize i = 0; i < m_retired.Size(); ++i) {
            if (m_retired[i].left <= 1) { m_device->DestroyBindGroup(m_retired[i].bg); }
            else { m_retired[i].left -= 1; m_retired[w++] = m_retired[i]; }
        }
        m_retired.Resize(w);
    }

    // Two-texture bind group (t0, t1) + sampler, cached by (t0 view, combined generation). Shared by every
    // AO pass (gen depth+normal, blur ao+depth, apply hdr+ao) - same layout.
    rhi::BindGroup* EnsureBindGroup(rhi::TextureView* a, rhi::TextureView* bView, u64 generation) {
        if (a == nullptr || bView == nullptr) { return nullptr; }
        if (Entry* e = m_bindGroups.Find(a)) {
            if (e->gen == generation && e->b == bView && e->bg != nullptr) { return e->bg; }
            if (e->bg != nullptr) { m_retired.PushBack(Retired{ e->bg, kRetireFrames }); e->bg = nullptr; }   // defer-free (in-flight)
        }
        rhi::BindGroupEntry ent[] = {
            rhi::BindGroupEntry::TextureEntry(a), rhi::BindGroupEntry::TextureEntry(bView),
            rhi::BindGroupEntry::SamplerEntry(m_sampler),
        };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_layout; bgd.entries = Span<const rhi::BindGroupEntry>{ ent, 3 };
        rhi::BindGroup* bg = nullptr;
        if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { return nullptr; }
        m_bindGroups.InsertOrAssign(a, Entry{ bg, bView, generation });
        return bg;
    }

    void Shutdown() {
        for (auto& kv : m_bindGroups) { if (kv.value.bg != nullptr) { m_device->DestroyBindGroup(kv.value.bg); } }
        m_bindGroups.Clear();
        for (auto& r : m_retired) { m_device->DestroyBindGroup(r.bg); }
        m_retired.Clear();
        if (m_gtaoPipeline != nullptr) { m_device->DestroyRenderPipeline(m_gtaoPipeline); m_gtaoPipeline = nullptr; }
        if (m_ssaoPipeline != nullptr) { m_device->DestroyRenderPipeline(m_ssaoPipeline); m_ssaoPipeline = nullptr; }
        if (m_blurPipeline != nullptr) { m_device->DestroyRenderPipeline(m_blurPipeline); m_blurPipeline = nullptr; }
        if (m_applyPipeline != nullptr) { m_device->DestroyRenderPipeline(m_applyPipeline); m_applyPipeline = nullptr; }
        if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
        if (m_gtaoLayout != nullptr) { m_device->DestroyPipelineLayout(m_gtaoLayout); m_gtaoLayout = nullptr; }
        if (m_ssaoLayout != nullptr) { m_device->DestroyPipelineLayout(m_ssaoLayout); m_ssaoLayout = nullptr; }
        if (m_blurLayout != nullptr) { m_device->DestroyPipelineLayout(m_blurLayout); m_blurLayout = nullptr; }
        if (m_applyLayout != nullptr) { m_device->DestroyPipelineLayout(m_applyLayout); m_applyLayout = nullptr; }
        if (m_layout != nullptr) { m_device->DestroyBindGroupLayout(m_layout); m_layout = nullptr; }
    }

    struct Entry { rhi::BindGroup* bg = nullptr; rhi::TextureView* b = nullptr; u64 gen = 0; };
    struct Retired { rhi::BindGroup* bg = nullptr; u32 left = 0; };
    static constexpr u32 kRetireFrames = 4;   // frames-in-flight headroom before a replaced set is idle

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    rhi::BindGroupLayout*  m_layout = nullptr;
    rhi::PipelineLayout*   m_gtaoLayout = nullptr;
    rhi::PipelineLayout*   m_ssaoLayout = nullptr;
    rhi::PipelineLayout*   m_blurLayout = nullptr;
    rhi::PipelineLayout*   m_applyLayout = nullptr;
    rhi::RenderPipeline*   m_gtaoPipeline = nullptr;
    rhi::RenderPipeline*   m_ssaoPipeline = nullptr;
    rhi::RenderPipeline*   m_blurPipeline = nullptr;
    rhi::RenderPipeline*   m_applyPipeline = nullptr;
    rhi::Sampler*          m_sampler = nullptr;
    HashMap<rhi::TextureView*, Entry> m_bindGroups;
    Array<Retired>                    m_retired;
    u32                               m_lastFrame = 0xFFFFFFFFu;
};

} // namespace draconic::render

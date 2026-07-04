/// Draconic::Render — the `:ssr` partition.
///
/// Screen-space reflections. A single fullscreen pass that reflects the lit HDR scene into itself:
/// reconstruct view-space position + normal from the G-buffer, reflect the view ray, march it against
/// the depth buffer, and — on a hit — sample the scene color at the hit and composite it back into the
/// HDR (LERP by a reflectivity weight, so SSR *replaces* the surface's IBL/probe specular rather than
/// adding to it, which avoids double-counting the reflection).
///
/// Inputs (all render-graph transients from the forward G-buffer): scene HDR (t0), depth (t1),
/// octahedral view-normal (t2), material = roughness/metallic (t3). Runs AFTER sky/decals and BEFORE
/// AO + the TAA resolve, so TAA temporally stabilizes the (necessarily noisy) march. Everything is done
/// in view space — the normal is already view-space, so no world round-trip is needed.
///
/// Structurally a sibling of `:ao` (same fullscreen VS, same view reconstruction, same generation-keyed
/// bind-group cache + deferred free), but with a 4-texture bind group and its own march shader.

module;
#include "Core/Prelude.h"

export module draconic.render:ssr;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Fullscreen-triangle VS, top-origin uv (matches the other post passes under the negative-viewport flip).
inline constexpr const char8_t* kSsrVS = u8R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    float2 raw = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(raw * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(raw.x, 1.0 - raw.y);
    return o;
}
)";

// The march + composite. A screen-space, perspective-correct ray march (traktor/McGuire form): project
// both ray endpoints to the view's UV rect, march a uniform parameter j along that screen segment while
// interpolating 1/w LINEARLY IN SCREEN SPACE (recovering ray depth as 1/mix(iz0,iz1,j)), then binary-
// refine the crossing. Even screen coverage (no "cut up" gaps), and VIEWPORT-AWARE: reconstruction /
// projection happen in the view's local uv (so split-screen sub-rects project correctly), while texture
// sampling uses full-texture uv. Depth-buffer NDC + top-origin uv conventions match `:ao`.
inline constexpr const char8_t* kSsrPS = u8R"(
Texture2D<float4> SceneTex    : register(t0, space0);   // lit HDR (reflected + composited into)
Texture2D         DepthTex    : register(t1, space0);
Texture2D         NormalTex   : register(t2, space0);   // octahedral view-space normal
Texture2D         MaterialTex : register(t3, space0);   // R=roughness, G=metallic
SamplerState      PointSamp   : register(s0, space0);   // depth / reconstruction (exact)
SamplerState      LinearSamp  : register(s1, space0);   // glossy color cone-gather

// 12-tap Poisson disk (unit radius) for the roughness cone-gather.
static const float2 kPoisson12[12] = {
    float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696,  0.457),
    float2(-0.203,  0.621), float2( 0.962, -0.195), float2( 0.473, -0.480),
    float2( 0.519,  0.767), float2( 0.185, -0.893), float2( 0.507,  0.064),
    float2( 0.896,  0.412), float2(-0.322, -0.933), float2(-0.792, -0.598)
};

struct SsrPush {
    row_major float4x4 InvProj;   // (viewport-local ndc, depth) -> view space
    float2 VpMin;                 // this view's sub-rect origin in FULL-texture uv
    float2 VpSize;                // this view's sub-rect size in FULL-texture uv
    float2 Jitter;                // projection z-row (proj(2,0), proj(2,1)): jittered ndc match
    float  ProjXX;                // projection(0,0): view.x -> ndc.x
    float  ProjYY;                // projection(1,1): view.y -> ndc.y
    float  Thickness;             // view-space linear-depth acceptance band (hit thickness)
    float  Intensity;             // reflection strength multiplier
    float  EdgeFade;              // uv fraction from each border over which SSR fades out
    float  RoughnessCutoff;       // roughness at/above which SSR is fully off (fades to IBL)
    int    MaxSteps;             // march step budget
    int    FrameMod;             // per-frame jitter rotation (TAA cleans the dither)
    int    Debug;                // 0=off, 1=raw reflected color, 2=hit uv, 3=weight, 4=reflect dir
    float  Glossy;               // glossy cone-gather scale (0 = sharp mirror)
};
[[vk::push_constant]] SsrPush pc;

// Octahedral decode -> view-space normal (matches the forward's OctEncode).
float3 OctDecode(float2 e) {
    float3 n = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    float  t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
// viewport-LOCAL uv (0..1 within the view) + depth -> view-space position.
float3 ViewPos(float2 luv, float depth) {
    float2 ndc = float2(luv.x * 2.0 - 1.0, (1.0 - luv.y) * 2.0 - 1.0);
    float4 h = mul(float4(ndc, depth, 1.0), pc.InvProj);
    return h.xyz / h.w;
}
// view-space position -> viewport-LOCAL uv (jitter-aware). Diagonal proj terms + w = -view.z.
float2 ViewToLocal(float3 vp) {
    float2 ndc = float2(vp.x * pc.ProjXX, vp.y * pc.ProjYY) / max(-vp.z, 1e-4) - pc.Jitter;
    return float2(ndc.x * 0.5 + 0.5, 0.5 - 0.5 * ndc.y);
}
float2 LocalToFull(float2 luv) { return pc.VpMin + luv * pc.VpSize; }   // local uv -> full-texture uv (sampling)
float2 FullToLocal(float2 fuv) { return (fuv - pc.VpMin) / pc.VpSize; } // full-texture uv -> local uv
// Interleaved-gradient noise (denoises cleanly under TAA; frame-rotated so TAA averages it out).
float Ign(float2 p) { return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715)))); }

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 hdr = SceneTex.SampleLevel(PointSamp, uv, 0).rgb;
    float  depth = DepthTex.SampleLevel(PointSamp, uv, 0).r;
    if (depth >= 1.0) { return float4(hdr, 1.0); }   // background: no reflector

    float2 mat       = MaterialTex.SampleLevel(PointSamp, uv, 0).rg;
    float  roughness = mat.r;
    float  metallic  = mat.g;
    // Rough surfaces fall back to the IBL/probe reflection already in the HDR (SSR is a sharp mirror term).
    float  roughFade = saturate(1.0 - roughness / max(pc.RoughnessCutoff, 1e-3));
    if (roughFade <= 0.0) { return float4(hdr, 1.0); }

    float2 luv = FullToLocal(uv);                 // this pixel's viewport-local uv
    float3 P = ViewPos(luv, depth);
    float3 N = OctDecode(NormalTex.SampleLevel(PointSamp, uv, 0).rg);
    float3 V = normalize(-P);
    float3 R = reflect(-V, N);                     // view-space reflection ray
    if (pc.Debug == 4) { return float4(R * 0.5 + 0.5, 1.0); }

    // Endpoint in view space (length scales with distance). If the reflection points toward the camera
    // (R.z > 0), clamp so the endpoint stays in front of the near plane.
    float rayLen = max(2.0, -P.z);
    if (R.z > 1e-4) { rayLen = min(rayLen, max((-0.05 - P.z) / R.z, 0.0)); }
    float3 endVS = P + R * rayLen;

    // Segment in viewport-local uv; 1/w interpolated linearly along it (perspective-correct depth).
    float2 luv0 = luv;
    float2 luv1 = ViewToLocal(endVS);
    float  iz0 = 1.0 / max(-P.z, 1e-4);
    float  iz1 = 1.0 / max(-endVS.z, 1e-4);

    // Clamp the far end to the viewport box [0,1]^2 so all MaxSteps land on-screen (max sample density).
    float2 d = luv1 - luv0;
    float2 dsgn = float2(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0);
    d = dsgn * max(abs(d), float2(1e-6, 1e-6));   // avoid divide-by-zero on axis-aligned segments
    float2 tTo0 = (float2(0.0, 0.0) - luv0) / d;
    float2 tTo1 = (float2(1.0, 1.0) - luv0) / d;
    float2 tHi  = max(tTo0, tTo1);                 // exit parameter per axis
    float  tExit = clamp(min(min(tHi.x, tHi.y), 1.0), 0.0, 1.0);
    luv1 = luv0 + (luv1 - luv0) * tExit;
    iz1  = lerp(iz0, iz1, tExit);                  // 1/w is linear in the segment parameter

    // Static (per-pixel, not frame-rotated) dither: decorrelates step banding but stays stable frame-to-
    // frame, so SSR doesn't shimmer without TAA. Frame-rotation returns once the temporal pass can resolve it.
    float jit = Ign(pos.xy);
    bool  hit = false;
    float jHit = 0.0, jPrev = 0.0;
    [loop] for (int i = 1; i <= pc.MaxSteps; ++i) {
        float  j = (float(i) - jit) / float(pc.MaxSteps);
        float2 ls = lerp(luv0, luv1, j);
        if (any(ls < 0.0) || any(ls > 1.0)) { break; }         // left the viewport
        float  sd = DepthTex.SampleLevel(PointSamp, LocalToFull(ls), 0).r;
        if (sd >= 1.0) { jPrev = j; continue; }                // sky: nothing to hit
        float  rayLin  = 1.0 / lerp(iz0, iz1, j);              // ray linear depth (= -view.z)
        float  surfLin = -ViewPos(ls, sd).z;                   // stored surface linear depth
        float  dif = rayLin - surfLin;                         // >0 once the ray passes behind the surface
        if (dif > 0.05 && dif < pc.Thickness) { hit = true; jHit = j; break; }
        jPrev = j;
    }
    if (!hit) { return (pc.Debug > 0) ? float4(0.0, 0.0, 0.0, 1.0) : float4(hdr, 1.0); }

    // Binary refine the crossing within [jPrev, jHit] for a sub-pixel-sharp hit (4 iters).
    float a = jPrev, b = jHit;
    [unroll] for (int r = 0; r < 4; ++r) {
        float  m = 0.5 * (a + b);
        float2 mls = lerp(luv0, luv1, m);
        float  mSurf = -ViewPos(mls, DepthTex.SampleLevel(PointSamp, LocalToFull(mls), 0).r).z;
        if (1.0 / lerp(iz0, iz1, m) - mSurf > 0.0) { b = m; } else { a = m; }
    }
    float2 hitLocal = lerp(luv0, luv1, b);
    float2 hitFull  = LocalToFull(hitLocal);

    // Glossy cone-gather: average the reflected color over a disk whose radius grows with roughness and
    // ray-travel distance (rougher / farther = blurrier). Near-mirror surfaces stay razor-sharp. Taps are
    // clamped to this view's sub-rect so a rough reflection never bleeds in the other split-screen view.
    float  segLen = length(hitLocal - luv);
    float  coneR  = pc.Glossy * roughness * (0.015 + 0.18 * segLen);   // viewport-local blur radius (0 = sharp)
    float3 refl;
    if (coneR < 2e-4) {
        refl = SceneTex.SampleLevel(LinearSamp, hitFull, 0).rgb;
    } else {
        refl = float3(0.0, 0.0, 0.0);
        float2 lo = pc.VpMin + 1e-4;
        float2 hi = pc.VpMin + pc.VpSize - 1e-4;
        [unroll] for (int gi = 0; gi < 12; ++gi) {
            float2 s = clamp(hitFull + kPoisson12[gi] * coneR * pc.VpSize, lo, hi);
            refl += SceneTex.SampleLevel(LinearSamp, s, 0).rgb;
        }
        refl *= (1.0 / 12.0);
    }
    // Screen-edge fade (off-screen has no data) on the LOCAL uv + Fresnel + roughness gate. LERP-replace
    // so the SSR term stands in for the IBL specular rather than adding to it.
    float2 e = smoothstep(0.0, pc.EdgeFade, hitLocal) * smoothstep(0.0, pc.EdgeFade, 1.0 - hitLocal);
    float  edgeFade = e.x * e.y;
    float  NdotV = saturate(dot(N, V));
    float  F0 = lerp(0.04, 1.0, metallic);
    float  fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
    float  weight = saturate(edgeFade * fresnel * roughFade * pc.Intensity);
    if (pc.Debug == 1) { return float4(refl, 1.0); }                  // raw reflected color at the hit
    if (pc.Debug == 2) { return float4(hitLocal, 0.0, 1.0); }         // hit uv (R=x, G=y, viewport-local)
    if (pc.Debug == 3) { return float4(weight, weight, weight, 1.0); }// composite weight
    return float4(lerp(hdr, refl, weight), 1.0);
}
)";

// Owns the SSR pipeline. Produces a fresh HDR transient (scene with reflections composited in).
class SsrPass {
public:
    SsrPass(rhi::Device& device, shaders::ShaderSystem& shaders) noexcept
        : m_device(&device), m_shaders(&shaders) {}
    ~SsrPass() { Shutdown(); }
    SsrPass(const SsrPass&) = delete;
    SsrPass& operator=(const SsrPass&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"ssr", shaders::ShaderStage::Vertex,   kSsrVS);
        m_shaders->RegisterSource(u8"ssr", shaders::ShaderStage::Fragment, kSsrPS);

        // Bind-group layout: scene(t0) + depth(t1) + normal(t2) + material(t3) + point sampler(s0) for
        // depth/reconstruction + linear sampler(s1) for the glossy color cone-gather.
        rhi::BindGroupLayoutEntry ge[] = {
            rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::SampledTexture(1, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::SampledTexture(2, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::SampledTexture(3, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::Sampler(1, rhi::ShaderStage::Fragment),
        };
        rhi::BindGroupLayoutDesc gld{}; gld.entries = Span<const rhi::BindGroupLayoutEntry>{ ge, 6 };
        if (!m_device->CreateBindGroupLayout(gld, m_layout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayout* gl[] = { m_layout };
        rhi::PushConstantRange pcr{}; pcr.stages = rhi::ShaderStage::Fragment; pcr.offset = 0; pcr.size = sizeof(SsrPushC);
        rhi::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ gl, 1 };
        pld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pcr, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Nearest; ss.magFilter = rhi::FilterMode::Nearest;
        ss.addressU = rhi::AddressMode::ClampToEdge; ss.addressV = rhi::AddressMode::ClampToEdge; ss.addressW = rhi::AddressMode::ClampToEdge;
        ss.label = u8"ssr.sampler";
        if (!m_device->CreateSampler(ss, m_sampler).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::SamplerDesc ls{};
        ls.minFilter = rhi::FilterMode::Linear; ls.magFilter = rhi::FilterMode::Linear;
        ls.addressU = rhi::AddressMode::ClampToEdge; ls.addressV = rhi::AddressMode::ClampToEdge; ls.addressW = rhi::AddressMode::ClampToEdge;
        ls.label = u8"ssr.linear";
        if (!m_device->CreateSampler(ls, m_linearSampler).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"ssr", shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(u8"ssr", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return Status{ ErrorCode::Unknown }; }
        rhi::ColorTargetState color{}; color.format = kHdrFormat;
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ &color, 1 };
        rhi::RenderPipelineDesc pd{};
        pd.layout = m_pipelineLayout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = u8"ssr";
        if (!m_device->CreateRenderPipeline(pd, m_pipeline).IsOk()) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    // Tunables (driven from the render subsystem / UI).
    struct Params {
        f32 intensity       = 1.0f;
        f32 thickness       = 0.5f;    // view-space linear-depth hit-acceptance band
        f32 edgeFade        = 0.1f;    // uv fraction faded at each screen border
        f32 roughnessCutoff = 0.8f;    // roughness at/above which SSR is off (rougher = blurred cone-gather)
        f32 glossy          = 1.0f;    // glossy blur scale (0 = sharp mirror, higher = blurrier)
        i32 maxSteps        = 96;      // ray-march samples along the segment
        i32 debug           = 0;       // 0=off, 1=raw refl, 2=hit uv, 3=weight, 4=reflect dir
    };

    // Reflect `hdr` into a fresh HDR transient and return it. w,h = full target size; vx/vy/vw/vh = this
    // view's viewport sub-rect (so split-screen views reconstruct/project in their own local uv, while
    // sampling the full texture). invProj/proj = camera inverse-proj / proj (proj z-row carries TAA jitter).
    [[nodiscard]] rendergraph::RGHandle DeclareSsr(rendergraph::RenderGraph& graph, rendergraph::RGHandle hdr,
                                                   rendergraph::RGHandle depth, rendergraph::RGHandle normal,
                                                   rendergraph::RGHandle material, u32 w, u32 h,
                                                   i32 vx, i32 vy, u32 vw, u32 vh, const Mat4& invProj,
                                                   const Mat4& proj, const Params& p, u32 frameIndex) {
        if (w == 0 || h == 0 || m_pipeline == nullptr) { return hdr; }
        Tick(frameIndex);
        const rendergraph::RGHandle out = graph.CreateTransient(u8"ssr.scene", rendergraph::RGTextureDesc(kHdrFormat, w, h));

        const f32 fw = static_cast<f32>(w), fh = static_cast<f32>(h);
        SsrPushC pc{};
        pc.invProj    = invProj;
        pc.vpMin      = Vec2{ static_cast<f32>(vx) / fw, static_cast<f32>(vy) / fh };
        pc.vpSize     = Vec2{ static_cast<f32>(vw) / fw, static_cast<f32>(vh) / fh };
        pc.jitter     = Vec2{ proj(2, 0), proj(2, 1) };   // NDC jitter (proj z-row)
        pc.projXX     = proj(0, 0);
        pc.projYY     = proj(1, 1);
        pc.thickness  = p.thickness;
        pc.intensity  = p.intensity;
        pc.edgeFade   = (p.edgeFade > 1e-4f) ? p.edgeFade : 1e-4f;
        pc.roughCutoff = p.roughnessCutoff;
        pc.maxSteps   = (p.maxSteps > 1) ? p.maxSteps : 1;
        pc.frameMod   = static_cast<i32>(frameIndex & 63u);
        pc.debug      = p.debug;
        pc.glossy     = (p.glossy >= 0.0f) ? p.glossy : 0.0f;

        graph.AddRenderPass(u8"ssr", [this, &graph, hdr, depth, normal, material, out, w, h, pc](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, out, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
            b.ReadTexture(hdr);
            b.ReadTexture(depth);
            b.ReadTexture(normal);
            b.ReadTexture(material);
            b.SetViewport(0, 0, w, h);
            b.NeverCull();
            b.SetExecute([this, &graph, hdr, depth, normal, material, pc](rhi::RenderPassEncoder& rp) {
                rhi::BindGroup* bg = EnsureBindGroup(graph.GetTextureView(hdr), graph.GetTextureView(depth),
                                                     graph.GetTextureView(normal), graph.GetTextureView(material),
                                                     Combine(graph, hdr, depth, normal, material));
                if (bg == nullptr) { return; }
                rp.SetPipeline(m_pipeline);
                rp.SetBindGroup(0, bg, Span<const u32>{});
                rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(SsrPushC), &pc);
                rp.Draw(3, 1, 0, 0);
            });
        });
        return out;
    }

private:
    static constexpr rhi::TextureFormat kHdrFormat = rhi::TextureFormat::RGBA16Float;   // matches the scene HDR
    // Byte-identical to the HLSL SsrPush (124 bytes, under the portable 128-byte push limit).
    struct SsrPushC {
        Mat4 invProj{};
        Vec2 vpMin{ 0.0f, 0.0f };
        Vec2 vpSize{ 1.0f, 1.0f };
        Vec2 jitter{};
        f32  projXX = 1.0f;
        f32  projYY = 1.0f;
        f32  thickness = 0.5f;
        f32  intensity = 1.0f;
        f32  edgeFade = 0.1f;
        f32  roughCutoff = 0.6f;
        i32  maxSteps = 96;
        i32  frameMod = 0;
        i32  debug = 0;
        f32  glossy = 1.0f;
    };
    static_assert(sizeof(SsrPushC) <= 128, "SSR push exceeds the portable 128-byte push-constant limit");

    // Combine four transient generations into one cache key (same FNV-ish mixing as :ao).
    static u64 Combine(rendergraph::RenderGraph& g, rendergraph::RGHandle a, rendergraph::RGHandle b,
                       rendergraph::RGHandle c, rendergraph::RGHandle d) {
        u64 k = g.GetTextureGeneration(a);
        k = (k ^ g.GetTextureGeneration(b)) * 1099511628211ull;
        k = (k ^ g.GetTextureGeneration(c)) * 1099511628211ull;
        k = (k ^ g.GetTextureGeneration(d)) * 1099511628211ull;
        return k;
    }

    // Advance the deferred-free list once per frame (the graph aliases transients, so replaced sets must
    // outlive in-flight frames before being freed — a raw-pointer cache would thrash mid-frame).
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

    // Four-texture bind group (t0..t3) + sampler, cached by (scene view, combined generation).
    rhi::BindGroup* EnsureBindGroup(rhi::TextureView* scene, rhi::TextureView* depth, rhi::TextureView* normal,
                                    rhi::TextureView* material, u64 generation) {
        if (scene == nullptr || depth == nullptr || normal == nullptr || material == nullptr) { return nullptr; }
        if (Entry* e = m_bindGroups.Find(scene)) {
            if (e->gen == generation && e->depth == depth && e->normal == normal && e->material == material && e->bg != nullptr) {
                return e->bg;
            }
            if (e->bg != nullptr) { m_retired.PushBack(Retired{ e->bg, kRetireFrames }); e->bg = nullptr; }
        }
        rhi::BindGroupEntry ent[] = {
            rhi::BindGroupEntry::TextureEntry(scene), rhi::BindGroupEntry::TextureEntry(depth),
            rhi::BindGroupEntry::TextureEntry(normal), rhi::BindGroupEntry::TextureEntry(material),
            rhi::BindGroupEntry::SamplerEntry(m_sampler), rhi::BindGroupEntry::SamplerEntry(m_linearSampler),
        };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_layout; bgd.entries = Span<const rhi::BindGroupEntry>{ ent, 6 };
        rhi::BindGroup* bg = nullptr;
        if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { return nullptr; }
        m_bindGroups.InsertOrAssign(scene, Entry{ bg, depth, normal, material, generation });
        return bg;
    }

    void Shutdown() {
        for (auto& kv : m_bindGroups) { if (kv.value.bg != nullptr) { m_device->DestroyBindGroup(kv.value.bg); } }
        m_bindGroups.Clear();
        for (auto& r : m_retired) { m_device->DestroyBindGroup(r.bg); }
        m_retired.Clear();
        if (m_pipeline != nullptr) { m_device->DestroyRenderPipeline(m_pipeline); m_pipeline = nullptr; }
        if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
        if (m_linearSampler != nullptr) { m_device->DestroySampler(m_linearSampler); m_linearSampler = nullptr; }
        if (m_pipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_layout != nullptr) { m_device->DestroyBindGroupLayout(m_layout); m_layout = nullptr; }
    }

    struct Entry { rhi::BindGroup* bg = nullptr; rhi::TextureView* depth = nullptr; rhi::TextureView* normal = nullptr; rhi::TextureView* material = nullptr; u64 gen = 0; };
    struct Retired { rhi::BindGroup* bg = nullptr; u32 left = 0; };
    static constexpr u32 kRetireFrames = 4;

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    rhi::BindGroupLayout*  m_layout = nullptr;
    rhi::PipelineLayout*   m_pipelineLayout = nullptr;
    rhi::RenderPipeline*   m_pipeline = nullptr;
    rhi::Sampler*          m_sampler = nullptr;         // point: depth/reconstruction
    rhi::Sampler*          m_linearSampler = nullptr;   // linear: glossy color gather
    HashMap<rhi::TextureView*, Entry> m_bindGroups;
    Array<Retired>                    m_retired;
    u32                               m_lastFrame = 0xFFFFFFFFu;
};

} // namespace draconic::render

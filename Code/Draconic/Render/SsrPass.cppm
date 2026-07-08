/// Draconic::Render - the `:ssr` partition.
///
/// Screen-space reflections. A single fullscreen pass that reflects the lit HDR scene into itself:
/// reconstruct view-space position + normal from the G-buffer, reflect the view ray, march it against
/// the depth buffer, and - on a hit - sample the scene color at the hit and composite it back into the
/// HDR (LERP by a reflectivity weight, so SSR *replaces* the surface's IBL/probe specular rather than
/// adding to it, which avoids double-counting the reflection).
///
/// Inputs (all render-graph transients from the forward G-buffer): scene HDR (t0), depth (t1),
/// octahedral view-normal (t2), material = roughness/metallic (t3). Runs AFTER sky/decals and BEFORE
/// AO + the TAA resolve, so TAA temporally stabilizes the (necessarily noisy) march. Everything is done
/// in view space - the normal is already view-space, so no world round-trip is needed.
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

// Trace outputs the REFLECTION buffer (rgb = reflected radiance, a = confidence/weight). Compositing
// into the HDR happens in the resolve pass (after temporal accumulation). No reflection -> (0,0,0,0).
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float  depth = DepthTex.SampleLevel(PointSamp, uv, 0).r;
    if (depth >= 1.0) { return float4(0.0, 0.0, 0.0, 0.0); }   // background: no reflector

    float2 mat       = MaterialTex.SampleLevel(PointSamp, uv, 0).rg;
    float  roughness = mat.r;
    float  metallic  = mat.g;
    // Rough surfaces fall back to the IBL/probe reflection already in the HDR (SSR is a sharp mirror term).
    float  roughFade = saturate(1.0 - roughness / max(pc.RoughnessCutoff, 1e-3));
    if (roughFade <= 0.0) { return float4(0.0, 0.0, 0.0, 0.0); }

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

    // Frame-rotated dither (FrameMod animates it); the temporal resolve accumulates it away. When temporal
    // is off the caller passes FrameMod=0 -> a static per-pixel dither that's stable without TAA.
    float jit = frac(Ign(pos.xy) + float(pc.FrameMod) * 0.6180339887);
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
    if (!hit) { return float4(0.0, 0.0, 0.0, 0.0); }   // miss: no reflection (resolve keeps the HDR)

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
    if (pc.Debug == 2) { return float4(hitLocal, 0.0, 1.0); }         // hit uv (R=x, G=y, viewport-local)
    if (pc.Debug == 3) { return float4(weight, weight, weight, 1.0); }// composite weight
    return float4(refl, weight);   // rgb = reflected radiance, a = confidence
}
)";

// Temporal resolve + composite. Reprojects the previous accumulated reflection by surface velocity
// (viewport-aware), YCoCg variance-clips it to the current neighborhood (kills ghosting/ fireflies),
// blends toward history (motion-adaptive), then LERP-composites the accumulated reflection into the HDR.
// MRT: SV_Target0 = composited HDR (downstream), SV_Target1 = next-frame reflection history.
inline constexpr const char8_t* kSsrResolvePS = u8R"(
Texture2D<float4> ReflTex     : register(t0, space0);   // current reflection (rgb + confidence)
Texture2D<float4> HistoryTex  : register(t1, space0);   // previous accumulated reflection
Texture2D         VelocityTex : register(t2, space0);   // screen-space motion (viewport-local uv delta)
Texture2D<float4> HdrTex      : register(t3, space0);   // scene HDR to composite into
SamplerState      PointSamp   : register(s0, space0);
SamplerState      LinearSamp  : register(s1, space0);

struct SsrResolvePush {
    float2 VpMin;          // view sub-rect origin in full-texture uv
    float2 VpSize;         // view sub-rect size in full-texture uv
    float2 TexelSize;      // 1 / full size
    float  BlendFactor;    // max history weight (~0.9)
    float  HistoryValid;   // 0 = first frame (no history)
    float  VarianceGamma;  // neighborhood clip half-width in stddevs (~1.0)
    float  MotionScale;    // how fast history drops with motion
    int    TemporalOn;     // 0 = skip history blend (pass current through)
    int    Debug;          // >0 = output raw reflection (no composite)
    float  GhostReject;    // history-vs-current luma-diff rejection strength (higher = less ghosting)
};
[[vk::push_constant]] SsrResolvePush pc;

float3 RGBToYCoCg(float3 c) { return float3(0.25*c.r + 0.5*c.g + 0.25*c.b, 0.5*c.r - 0.5*c.b, -0.25*c.r + 0.5*c.g - 0.25*c.b); }
float3 YCoCgToRGB(float3 c) { float t = c.x - c.z; return float3(t + c.y, c.x + c.z, t - c.y); }
float3 ClipToAABB(float3 color, float3 aabbMin, float3 aabbMax) {
    float3 center  = (aabbMax + aabbMin) * 0.5;
    float3 extents = (aabbMax - aabbMin) * 0.5;
    float3 shift   = color - center;
    float3 absUnit = abs(shift / max(extents, 1e-4));
    float  maxUnit = max(max(absUnit.x, absUnit.y), absUnit.z);
    return maxUnit > 1.0 ? center + (shift / maxUnit) : color;
}

struct PSOut { float4 Color : SV_Target0; float4 History : SV_Target1; };

PSOut main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    float4 curR  = ReflTex.SampleLevel(PointSamp, uv, 0);   // rgb + confidence
    float4 accum = curR;

    if (pc.TemporalOn != 0 && pc.HistoryValid > 0.5 && pc.Debug == 0) {
        float2 localUv = (uv - pc.VpMin) / pc.VpSize;
        float2 vel     = VelocityTex.SampleLevel(PointSamp, uv, 0).rg;   // viewport-local uv delta
        float2 histLoc = localUv - vel;
        if (all(histLoc >= 0.0) && all(histLoc <= 1.0)) {
            // YCoCg neighborhood variance box from the CURRENT reflection (3x3).
            float3 m1 = float3(0,0,0), m2 = float3(0,0,0);
            [unroll] for (int ny = -1; ny <= 1; ++ny) {
                [unroll] for (int nx = -1; nx <= 1; ++nx) {
                    float3 y = RGBToYCoCg(ReflTex.SampleLevel(PointSamp, uv + float2(nx, ny) * pc.TexelSize, 0).rgb);
                    m1 += y; m2 += y * y;
                }
            }
            m1 /= 9.0; m2 /= 9.0;
            float3 sigma  = sqrt(max(m2 - m1 * m1, 0.0));
            float3 boxMin = m1 - pc.VarianceGamma * sigma;
            float3 boxMax = m1 + pc.VarianceGamma * sigma;

            float2 histFull = pc.VpMin + histLoc * pc.VpSize;
            float4 hist    = HistoryTex.SampleLevel(LinearSamp, histFull, 0);
            float3 rawHistY = RGBToYCoCg(hist.rgb);
            float3 histY   = ClipToAABB(rawHistY, boxMin, boxMax);
            float3 curY    = RGBToYCoCg(curR.rgb);
            // Content-change rejection: where the reprojected history's luma disagrees with the current
            // reflection (a moving reflected object trailed into this pixel), drop history and trust current.
            // This is what kills ghosting that surface-velocity reprojection can't (the surface is static
            // but its reflection moved). Plus a motion-adaptive term for camera movement.
            float  ghost = saturate(1.0 - abs(rawHistY.x - curY.x) * pc.GhostReject);
            float  motionMag = saturate(length(vel) * pc.MotionScale);
            float  blend = pc.BlendFactor * (1.0 - 0.5 * motionMag) * ghost;
            accum = float4(max(YCoCgToRGB(lerp(curY, histY, blend)), 0.0), lerp(curR.a, hist.a, blend));
        }
    }

    PSOut o;
    o.History = accum;
    float3 hdr = HdrTex.SampleLevel(PointSamp, uv, 0).rgb;
    o.Color = (pc.Debug > 0) ? float4(accum.rgb, 1.0) : float4(lerp(hdr, accum.rgb, saturate(accum.a)), 1.0);
    return o;
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

        // --- Resolve pipeline (temporal accumulate + composite): reflection(t0) history(t1) velocity(t2)
        //     hdr(t3) + point(s0) linear(s1). MRT out = composited HDR + next-frame reflection history. ---
        m_shaders->RegisterSource(u8"ssr_resolve", shaders::ShaderStage::Vertex,   kSsrVS);
        m_shaders->RegisterSource(u8"ssr_resolve", shaders::ShaderStage::Fragment, kSsrResolvePS);
        rhi::BindGroupLayoutEntry re[] = {
            rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::SampledTexture(1, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::SampledTexture(2, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::SampledTexture(3, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::Sampler(1, rhi::ShaderStage::Fragment),
        };
        rhi::BindGroupLayoutDesc rld{}; rld.entries = Span<const rhi::BindGroupLayoutEntry>{ re, 6 };
        if (!m_device->CreateBindGroupLayout(rld, m_resolveLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }
        rhi::BindGroupLayout* rgl[] = { m_resolveLayout };
        rhi::PushConstantRange rpc{}; rpc.stages = rhi::ShaderStage::Fragment; rpc.offset = 0; rpc.size = sizeof(SsrResolvePushC);
        rhi::PipelineLayoutDesc rpld{}; rpld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ rgl, 1 };
        rpld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &rpc, 1 };
        if (!m_device->CreatePipelineLayout(rpld, m_resolvePipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }
        rhi::ShaderModule* rvs = m_shaders->GetVariant(u8"ssr_resolve", shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* rps = m_shaders->GetVariant(u8"ssr_resolve", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (rvs == nullptr || rps == nullptr) { return Status{ ErrorCode::Unknown }; }
        rhi::ColorTargetState rtargets[2] = {};
        rtargets[0].format = kHdrFormat;   // composited HDR
        rtargets[1].format = kHdrFormat;   // reflection history
        rhi::FragmentState rfrag{}; rfrag.shader = rhi::ProgrammableStage{ rps, u8"main", rhi::ShaderStage::Fragment };
        rfrag.targets = Span<const rhi::ColorTargetState>{ rtargets, 2 };
        rhi::RenderPipelineDesc rpd{};
        rpd.layout = m_resolvePipelineLayout;
        rpd.vertex.shader = rhi::ProgrammableStage{ rvs, u8"main", rhi::ShaderStage::Vertex };
        rpd.fragment = rfrag;
        rpd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        rpd.primitive.cullMode = rhi::CullMode::None;
        rpd.label = u8"ssr_resolve";
        if (!m_device->CreateRenderPipeline(rpd, m_resolvePipeline).IsOk()) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    // Tunables (driven from the render subsystem / UI).
    struct Params {
        f32  intensity       = 1.0f;
        f32  thickness       = 0.5f;    // view-space linear-depth hit-acceptance band
        f32  edgeFade        = 0.1f;    // uv fraction faded at each screen border
        f32  roughnessCutoff = 0.8f;    // roughness at/above which SSR is off (rougher = blurred cone-gather)
        f32  glossy          = 1.0f;    // glossy blur scale (0 = sharp mirror, higher = blurrier)
        i32  maxSteps        = 96;      // ray-march samples along the segment
        i32  debug           = 0;       // 0=off, 1=raw refl, 2=hit uv, 3=weight, 4=reflect dir
        bool temporal        = true;    // temporal accumulate (reproject + variance-clip history)
        f32  historyBlend    = 0.88f;   // max history weight on stable pixels
        f32  varianceGamma   = 1.0f;    // neighborhood clip half-width (stddevs)
        f32  motionScale     = 24.0f;   // how fast history drops with motion
        f32  ghostReject     = 6.0f;    // history-vs-current luma-diff rejection (higher = less ghosting)
    };

    // Reflect the scene into a fresh HDR transient (returned). Two passes: trace -> reflection buffer,
    // then resolve (temporal accumulate + composite). w,h = full target size; vx/vy/vw/vh = this view's
    // sub-rect (split-screen views reconstruct/project in local uv, sample the full texture). velocity =
    // the G-buffer motion vectors (for reprojection). invProj/proj = camera inverse-proj / proj.
    [[nodiscard]] rendergraph::RGHandle DeclareSsr(rendergraph::RenderGraph& graph, rendergraph::RGHandle hdr,
                                                   rendergraph::RGHandle depth, rendergraph::RGHandle normal,
                                                   rendergraph::RGHandle material, rendergraph::RGHandle velocity,
                                                   u32 w, u32 h, i32 vx, i32 vy, u32 vw, u32 vh, const Float4x4& invProj,
                                                   const Float4x4& proj, const Params& p, u32 viewIndex, u32 frameIndex) {
        if (w == 0 || h == 0 || m_pipeline == nullptr || m_resolvePipeline == nullptr || viewIndex >= kMaxViews) { return hdr; }
        Tick(frameIndex);
        const f32 fw = static_cast<f32>(w), fh = static_cast<f32>(h);
        const Float2 vpMin{ static_cast<f32>(vx) / fw, static_cast<f32>(vy) / fh };
        const Float2 vpSize{ static_cast<f32>(vw) / fw, static_cast<f32>(vh) / fh };
        const bool temporalOn = p.temporal;

        // --- Trace: reflection buffer (rgb reflected radiance, a = confidence) ---
        const rendergraph::RGHandle refl = graph.CreateTransient(u8"ssr.refl", rendergraph::RGTextureDesc(kHdrFormat, w, h));
        SsrPushC pc{};
        pc.invProj    = invProj;
        pc.vpMin      = vpMin;  pc.vpSize = vpSize;
        pc.jitter     = Float2{ proj(2, 0), proj(2, 1) };
        pc.projXX     = proj(0, 0);  pc.projYY = proj(1, 1);
        pc.thickness  = p.thickness;  pc.intensity = p.intensity;
        pc.edgeFade   = (p.edgeFade > 1e-4f) ? p.edgeFade : 1e-4f;
        pc.roughCutoff = p.roughnessCutoff;
        pc.maxSteps   = (p.maxSteps > 1) ? p.maxSteps : 1;
        // STATIC per-pixel dither (frameMod=0): a frame-rotated dither shimmers, and the temporal pass
        // can't average it away without also re-admitting the ghosting the reject term suppresses. Static
        // dither is deterministic per (pixel, camera) -> under a still camera current==history -> the
        // temporal accumulation is stable, while ghost-reject still handles moving reflected content.
        pc.frameMod   = 0;
        pc.debug      = p.debug;
        pc.glossy     = (p.glossy >= 0.0f) ? p.glossy : 0.0f;
        graph.AddRenderPass(u8"ssr.trace", [this, &graph, hdr, depth, normal, material, refl, w, h, pc](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, refl, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
            b.ReadTexture(hdr); b.ReadTexture(depth); b.ReadTexture(normal); b.ReadTexture(material);
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

        // --- Resolve: reproject + variance-clip + accumulate history, then composite into the HDR ---
        ViewHistory& hist = m_views[viewIndex];
        if (!EnsureHistory(hist, w, h)) { return hdr; }
        const u32 cur = hist.cur, prev = cur ^ 1u;
        const rendergraph::RGHandle out = graph.CreateTransient(u8"ssr.scene", rendergraph::RGTextureDesc(kHdrFormat, w, h));
        const rendergraph::RGHandle histPrev = graph.ImportTarget(u8"ssr.histPrev", hist.tex[prev], hist.view[prev],
                                                                  rhi::ResourceState::ShaderRead, hist.state[prev]);
        hist.state[prev] = rhi::ResourceState::ShaderRead;
        const rendergraph::RGHandle histCur = graph.ImportTarget(u8"ssr.histCur", hist.tex[cur], hist.view[cur],
                                                                 rhi::ResourceState::RenderTarget, hist.state[cur]);
        hist.state[cur] = rhi::ResourceState::RenderTarget;

        SsrResolvePushC rpc2{};
        rpc2.vpMin = vpMin;  rpc2.vpSize = vpSize;
        rpc2.texelSize = Float2{ 1.0f / fw, 1.0f / fh };
        rpc2.blendFactor = p.historyBlend;
        rpc2.historyValid = hist.valid ? 1.0f : 0.0f;
        rpc2.varianceGamma = p.varianceGamma;
        rpc2.motionScale = p.motionScale;
        rpc2.temporalOn = temporalOn ? 1 : 0;
        rpc2.debug = p.debug;
        rpc2.ghostReject = p.ghostReject;

        rhi::TextureView* histPrevView = hist.view[prev];
        graph.AddRenderPass(u8"ssr.resolve", [this, &graph, refl, histPrev, velocity, hdr, out, histCur, histPrevView, rpc2](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, out,     rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
            b.SetColorTarget(1, histCur, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
            b.ReadTexture(refl); b.ReadTexture(histPrev); b.ReadTexture(velocity); b.ReadTexture(hdr);
            b.NeverCull();
            b.SetExecute([this, &graph, refl, velocity, hdr, histPrevView, rpc2](rhi::RenderPassEncoder& rp) {
                rhi::BindGroup* bg = EnsureResolveBindGroup(graph.GetTextureView(refl), histPrevView,
                                                            graph.GetTextureView(velocity), graph.GetTextureView(hdr),
                                                            graph.GetTextureGeneration(refl) ^ (graph.GetTextureGeneration(hdr) * 1099511628211ull));
                if (bg == nullptr) { return; }
                rp.SetPipeline(m_resolvePipeline);
                rp.SetBindGroup(0, bg, Span<const u32>{});
                rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(SsrResolvePushC), &rpc2);
                rp.Draw(3, 1, 0, 0);
            });
        });
        hist.cur = prev;   // this frame's history output becomes next frame's read
        hist.valid = true;
        return out;
    }

private:
    static constexpr rhi::TextureFormat kHdrFormat = rhi::TextureFormat::RGBA16Float;   // matches the scene HDR
    // Byte-identical to the HLSL SsrPush (124 bytes, under the portable 128-byte push limit).
    struct SsrPushC {
        Float4x4 invProj{};
        Float2 vpMin{ 0.0f, 0.0f };
        Float2 vpSize{ 1.0f, 1.0f };
        Float2 jitter{};
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

    // Byte-identical to the HLSL SsrResolvePush.
    struct SsrResolvePushC {
        Float2 vpMin{ 0.0f, 0.0f };
        Float2 vpSize{ 1.0f, 1.0f };
        Float2 texelSize{};
        f32  blendFactor = 0.88f;
        f32  historyValid = 0.0f;
        f32  varianceGamma = 1.0f;
        f32  motionScale = 24.0f;
        i32  temporalOn = 1;
        i32  debug = 0;
        f32  ghostReject = 6.0f;
    };
    static_assert(sizeof(SsrResolvePushC) <= 128, "SSR resolve push exceeds the portable 128-byte limit");

    static constexpr u32 kMaxViews = 8;
    struct ViewHistory {
        rhi::Texture*      tex[2]  = {};
        rhi::TextureView*  view[2] = {};
        rhi::ResourceState state[2] = { rhi::ResourceState::Undefined, rhi::ResourceState::Undefined };
        u32  w = 0, h = 0, cur = 0;
        bool valid = false;
    };

    bool EnsureHistory(ViewHistory& hist, u32 w, u32 h) {
        if (hist.tex[0] != nullptr && hist.w == w && hist.h == h) { return true; }
        DestroyHistory(hist);
        for (u32 i = 0; i < 2; ++i) {
            rhi::TextureDesc td{};
            td.format = kHdrFormat; td.width = w; td.height = h;
            td.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled; td.label = u8"ssr.history";
            if (!m_device->CreateTexture(td, hist.tex[i]).IsOk()) { hist.tex[i] = nullptr; DestroyHistory(hist); return false; }
            rhi::TextureViewDesc vd{}; vd.format = kHdrFormat; vd.dimension = rhi::TextureViewDimension::Texture2D;
            if (!m_device->CreateTextureView(hist.tex[i], vd, hist.view[i]).IsOk()) { hist.view[i] = nullptr; DestroyHistory(hist); return false; }
            hist.state[i] = rhi::ResourceState::Undefined;
        }
        hist.w = w; hist.h = h; hist.cur = 0; hist.valid = false;
        return true;
    }
    void DestroyHistory(ViewHistory& hist) {
        for (u32 i = 0; i < 2; ++i) {
            if (hist.view[i]) { m_device->DestroyTextureView(hist.view[i]); hist.view[i] = nullptr; }
            if (hist.tex[i])  { m_device->DestroyTexture(hist.tex[i]);       hist.tex[i]  = nullptr; }
        }
        hist.w = hist.h = 0; hist.valid = false;
    }

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
    // outlive in-flight frames before being freed - a raw-pointer cache would thrash mid-frame).
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

    // Resolve bind group (reflection, history, velocity, hdr + 2 samplers), cached by (history view, gen).
    rhi::BindGroup* EnsureResolveBindGroup(rhi::TextureView* refl, rhi::TextureView* histPrev,
                                           rhi::TextureView* velocity, rhi::TextureView* hdr, u64 generation) {
        if (refl == nullptr || histPrev == nullptr || velocity == nullptr || hdr == nullptr) { return nullptr; }
        if (ResolveEntry* e = m_resolveBindGroups.Find(histPrev)) {
            if (e->gen == generation && e->refl == refl && e->velocity == velocity && e->hdr == hdr && e->bg != nullptr) { return e->bg; }
            if (e->bg != nullptr) { m_retired.PushBack(Retired{ e->bg, kRetireFrames }); e->bg = nullptr; }
        }
        rhi::BindGroupEntry ent[] = {
            rhi::BindGroupEntry::TextureEntry(refl), rhi::BindGroupEntry::TextureEntry(histPrev),
            rhi::BindGroupEntry::TextureEntry(velocity), rhi::BindGroupEntry::TextureEntry(hdr),
            rhi::BindGroupEntry::SamplerEntry(m_sampler), rhi::BindGroupEntry::SamplerEntry(m_linearSampler),
        };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_resolveLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ ent, 6 };
        rhi::BindGroup* bg = nullptr;
        if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { return nullptr; }
        m_resolveBindGroups.InsertOrAssign(histPrev, ResolveEntry{ bg, refl, velocity, hdr, generation });
        return bg;
    }

    void Shutdown() {
        for (auto& kv : m_bindGroups) { if (kv.value.bg != nullptr) { m_device->DestroyBindGroup(kv.value.bg); } }
        m_bindGroups.Clear();
        for (auto& kv : m_resolveBindGroups) { if (kv.value.bg != nullptr) { m_device->DestroyBindGroup(kv.value.bg); } }
        m_resolveBindGroups.Clear();
        for (auto& r : m_retired) { m_device->DestroyBindGroup(r.bg); }
        m_retired.Clear();
        for (u32 v = 0; v < kMaxViews; ++v) { DestroyHistory(m_views[v]); }
        if (m_pipeline != nullptr) { m_device->DestroyRenderPipeline(m_pipeline); m_pipeline = nullptr; }
        if (m_resolvePipeline != nullptr) { m_device->DestroyRenderPipeline(m_resolvePipeline); m_resolvePipeline = nullptr; }
        if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
        if (m_linearSampler != nullptr) { m_device->DestroySampler(m_linearSampler); m_linearSampler = nullptr; }
        if (m_pipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_resolvePipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_resolvePipelineLayout); m_resolvePipelineLayout = nullptr; }
        if (m_layout != nullptr) { m_device->DestroyBindGroupLayout(m_layout); m_layout = nullptr; }
        if (m_resolveLayout != nullptr) { m_device->DestroyBindGroupLayout(m_resolveLayout); m_resolveLayout = nullptr; }
    }

    struct Entry { rhi::BindGroup* bg = nullptr; rhi::TextureView* depth = nullptr; rhi::TextureView* normal = nullptr; rhi::TextureView* material = nullptr; u64 gen = 0; };
    struct ResolveEntry { rhi::BindGroup* bg = nullptr; rhi::TextureView* refl = nullptr; rhi::TextureView* velocity = nullptr; rhi::TextureView* hdr = nullptr; u64 gen = 0; };
    struct Retired { rhi::BindGroup* bg = nullptr; u32 left = 0; };
    static constexpr u32 kRetireFrames = 4;

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    rhi::BindGroupLayout*  m_layout = nullptr;
    rhi::PipelineLayout*   m_pipelineLayout = nullptr;
    rhi::RenderPipeline*   m_pipeline = nullptr;               // trace
    rhi::BindGroupLayout*  m_resolveLayout = nullptr;
    rhi::PipelineLayout*   m_resolvePipelineLayout = nullptr;
    rhi::RenderPipeline*   m_resolvePipeline = nullptr;        // temporal resolve + composite
    rhi::Sampler*          m_sampler = nullptr;         // point: depth/reconstruction
    rhi::Sampler*          m_linearSampler = nullptr;   // linear: glossy color gather
    ViewHistory            m_views[kMaxViews];
    HashMap<rhi::TextureView*, Entry>        m_bindGroups;
    HashMap<rhi::TextureView*, ResolveEntry> m_resolveBindGroups;
    Array<Retired>                    m_retired;
    u32                               m_lastFrame = 0xFFFFFFFFu;
};

} // namespace draconic::render

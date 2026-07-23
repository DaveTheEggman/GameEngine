/// Draconic::Render - the `:ao_shaders` partition.
///
/// HLSL source for the AO pass, split out from AoPass.cppm so the shader text is isolated and trivial to
/// lift to standalone .hlsl files later. Pass-internal: imported by `:ao`. AoCommon() is a shared prefix
/// concatenated with the GTAO/SSAO generator bodies in the pass.

module;
#include "Core/Prelude.h"

export module draconic.render:ao_shaders;

import draconic.core;

export namespace draconic::render
{

    // Fullscreen-triangle VS, top-origin uv (matches the other post passes under the negative-viewport flip).
    [[nodiscard]] inline core::StringView AoVS() noexcept
    {
        return core::StringView(u8R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    float2 raw = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(raw * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(raw.x, 1.0 - raw.y);
    return o;
}
)");
    }

    // Shared reconstruction helpers, textbook for both generators. NDC convention derived from the (proven,
    // since TAA works) motion-vector mapping in the forward pass: velocity = (ndc - prev) * (0.5, -0.5) means
    // uv.y = (1 - ndc.y)/2, i.e. top-origin uv.y=0 -> ndc.y=+1 under the RHI's automatic negative viewport.
    [[nodiscard]] inline core::StringView AoCommon() noexcept
    {
        return core::StringView(u8R"(
// Octahedral decode -> view-space normal (matches the forward's OctEncode).
float3 OctDecode(float2 e) {
    float3 n = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    float  t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
)");
    }

    // GTAO generate: horizon-based AO in view space. Depth (t0) + octahedral view-normal (t1).
    [[nodiscard]] inline core::StringView GtaoGenPS() noexcept
    {
        return core::StringView(u8R"(
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
)");
    }

    // SSAO generate: hemisphere-kernel occlusion (ported from Sedulous). Depth (t0) + view-normal (t1).
    // Reconstruct P, orient a Poisson hemisphere kernel by the normal (TBN + per-pixel rotation), offset in
    // view space, project back with the diagonal proj terms, and compare depths. Projecting with ProjXX/ProjYY
    // (symmetric perspective) instead of a full matrix keeps the push under the 128-byte portable limit.
    [[nodiscard]] inline core::StringView SsaoGenPS() noexcept
    {
        return core::StringView(u8R"(
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
)");
    }

    // Depth-aware separable bilateral blur (denoise the raw AO). One pass = one axis; run twice.
    [[nodiscard]] inline core::StringView AoBlurPS() noexcept
    {
        return core::StringView(u8R"(
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
)");
    }

    // Multiply AO into an HDR color target: out = hdr * lerp(1, ao, Strength). Run BEFORE the TAA resolve
    // so TAA temporally stabilizes the AO (applying it post-TAA wobbles, since the AO is computed from the
    // jittered G-buffer and shifts sub-pixel each frame).
    [[nodiscard]] inline core::StringView AoApplyPS() noexcept
    {
        return core::StringView(u8R"(
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
)");
    }

}

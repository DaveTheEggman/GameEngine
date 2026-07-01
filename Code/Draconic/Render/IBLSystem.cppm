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

// Canonical cube-face direction from a face index + [0,1] face uv. NOTE: no t.y negation — the cube
// faces are rendered through the RHI's negative-viewport (Y-flipped), so the stored texel already
// matches the standard cube-sampling convention; negating here would double-flip and break edge
// continuity. (Verified correct in-engine: the sky background samples this cube by world ray and the
// procedural gradient reads right-side-up.)
float3 DirForFace(int face, float2 uv) {
    float2 t = uv * 2.0 - 1.0;
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
    if (pc.Mode == 2) {                              // Color mode (SkyMode ordinal 2): uniform zenith color
        sky = pc.Zenith.rgb;
    } else {                                         // Procedural gradient (also HDR/cubemap fallback)
        sky = (dir.y >= 0.0) ? lerp(pc.Horizon.rgb, pc.Zenith.rgb, pow(saturate(dir.y), 0.5))
                             : lerp(pc.Horizon.rgb, pc.Ground.rgb, pow(saturate(-dir.y), 0.8));
        // A soft, broad sun GLOW only (no sharp disc) — the crisp sun is drawn analytically at screen
        // resolution by the sky pass; baking a sub-texel disc into the 256^2 cube would alias to a square.
        float3 sunDir = normalize(-pc.Sun.xyz);
        float  d      = max(dot(dir, sunDir), 0.0);
        sky += pow(d, 64.0) * max(pc.Horizon.a, 0.0) * 0.3;
    }
    return float4(sky * max(pc.SkyIntensity, 0.0), 1.0);
}
)";

// Preetham analytic daylight -> one env cube face. Physically-based sky luminance/chromaticity from
// the sun elevation + turbidity (pc.Ground.a). Sun dir = -pc.Sun.xyz. A richer procedural sky.
inline constexpr const char8_t* kIblAnalyticPS = u8R"(
static const float API = 3.14159265359;
float3 PreethamRGB(float cosTheta, float gamma, float thetaSun, float T) {
    cosTheta = max(cosTheta, 0.02);
    float cg = cos(gamma);
    // Perez distribution coefficients (per channel: Y, x, y), linear in turbidity T.
    float3 A = float3( 0.1787*T-1.4630, -0.0193*T-0.2592, -0.0167*T-0.2608);
    float3 B = float3(-0.3554*T+0.4275, -0.0665*T+0.0008, -0.0950*T+0.0092);
    float3 C = float3(-0.0227*T+5.3251, -0.0004*T+0.2125, -0.0079*T+0.2102);
    float3 D = float3( 0.1206*T-2.5771, -0.0641*T-0.8989, -0.0441*T-1.6537);
    float3 E = float3(-0.0670*T+0.3703, -0.0033*T+0.0452, -0.0109*T+0.0529);
    float3 num = (1.0 + A * exp(B / cosTheta)) * (1.0 + C * exp(D * gamma) + E * cg * cg);
    float cts = cos(thetaSun);
    float3 den = (1.0 + A * exp(B))          * (1.0 + C * exp(D * thetaSun) + E * cts * cts);
    float3 F = num / den;
    // Zenith luminance + chromaticity.
    float chi = (4.0/9.0 - T/120.0) * (API - 2.0*thetaSun);
    float Yz = (4.0453*T - 4.9710) * tan(chi) - 0.2155*T + 2.4192;
    float ts = thetaSun, ts2 = ts*ts, ts3 = ts2*ts, T2 = T*T;
    float xz = ( 0.00166*ts3-0.00375*ts2+0.00209*ts)*T2 + (-0.02903*ts3+0.06377*ts2-0.03202*ts+0.00394)*T + ( 0.11693*ts3-0.21196*ts2+0.06052*ts+0.25886);
    float yz = ( 0.00275*ts3-0.00610*ts2+0.00317*ts)*T2 + (-0.04214*ts3+0.08970*ts2-0.04153*ts+0.00516)*T + ( 0.15346*ts3-0.26756*ts2+0.06670*ts+0.26688);
    float Y = Yz * F.x, x = xz * F.y, y = yz * F.z;
    // xyY -> XYZ -> linear sRGB.
    float3 XYZ; XYZ.y = Y; XYZ.x = (x / max(y, 1e-4)) * Y; XYZ.z = ((1.0 - x - y) / max(y, 1e-4)) * Y;
    float3 rgb = float3(dot(XYZ, float3( 3.2404542,-1.5371385,-0.4985314)),
                        dot(XYZ, float3(-0.9692660, 1.8760108, 0.0415560)),
                        dot(XYZ, float3( 0.0556434,-0.2040259, 1.0572252)));
    return max(rgb, 0.0);
}
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 dir    = normalize(DirForFace(pc.FaceIndex, uv));
    float3 sunDir = normalize(-pc.Sun.xyz);
    float  T        = clamp(pc.Ground.a, 1.7, 10.0);
    float  thetaSun = acos(clamp(sunDir.y, 0.0, 1.0));
    float  gamma    = acos(clamp(dot(dir, sunDir), -1.0, 1.0));
    float3 sky = (dir.y >= 0.0) ? PreethamRGB(dir.y, gamma, thetaSun, T) * 0.05
                                : pc.Ground.rgb * 0.5;   // below horizon: dim ground tint
    return float4(sky * max(pc.SkyIntensity, 0.0), 1.0);
}
)";

// HDR equirectangular -> one env cube face: map the face direction to equirect uv and sample. Uses
// the same DirForFace (canonical, negative-viewport-aware) as the procedural pass.
inline constexpr const char8_t* kIblEquirectPS = u8R"(
Texture2D    EquirectMap  : register(t0, space0);
SamplerState EquirectSamp : register(s0, space0);
static const float PI2 = 3.14159265359;
float2 DirToEquirect(float3 d) {
    float phi   = atan2(d.z, d.x);
    float theta = asin(clamp(d.y, -1.0, 1.0));
    return float2(phi / (2.0 * PI2) + 0.5, 1.0 - (theta / PI2 + 0.5));
}
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 dir = DirForFace(pc.FaceIndex, uv);
    return float4(EquirectMap.SampleLevel(EquirectSamp, DirToEquirect(dir), 0.0).rgb * max(pc.SkyIntensity, 0.0), 1.0);
}
)";

// Loaded cubemap -> env cube face: resample the (possibly larger / LDR) source cube along the face
// direction (downsamples + format-converts into the RGBA16F env cube). Shares the cube bind-group
// layout with the prefilter.
inline constexpr const char8_t* kIblCubemapPS = u8R"(
TextureCube  SrcCube  : register(t0, space0);
SamplerState SrcSamp  : register(s0, space0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 dir = DirForFace(pc.FaceIndex, uv);
    return float4(SrcCube.SampleLevel(SrcSamp, dir, 0.0).rgb * max(pc.SkyIntensity, 0.0), 1.0);
}
)";

// Box-downsample one env cube mip from the previous (finer) mip: sample the source cube (bound as a
// single-mip view) along the face direction with linear filtering — averages the 2x2 finer texels into
// this half-res texel. Builds the env mip pyramid the prefilter samples by PDF (firefly suppression).
inline constexpr const char8_t* kIblDownsamplePS = u8R"(
TextureCube  SrcCube : register(t0, space0);
SamplerState SrcSamp : register(s0, space0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 dir = DirForFace(pc.FaceIndex, uv);
    return float4(SrcCube.SampleLevel(SrcSamp, dir, 0.0).rgb, 1.0);
}
)";

// GGX prefilter (Karis split-sum specular): importance-sample the env cube around the reflection
// direction (= N = V) at this mip's roughness. 1024 Hammersley samples / texel. Each sample reads a
// PDF-selected env mip (solid-angle matched) so bright pixels are pre-averaged — kills specular fireflies.
inline constexpr const char8_t* kIblPrefilterPS = u8R"(
TextureCube<float4> EnvMap : register(t0, space0);
SamplerState        EnvSamp : register(s0, space0);

static const float PI = 3.14159265359;
static const float ENV_RES = 256.0;   // env cube face resolution (mip 0)

float DistributionGGX(float ndh, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (ndh * ndh) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

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
            // Karis: pick the env mip whose texel solid angle matches this sample's solid angle, so
            // sparse high-roughness samples average many source texels instead of aliasing bright ones.
            float ndh = max(dot(N, H), 0.0);   // N == V, so NdotH == HdotV
            float D   = DistributionGGX(ndh, pc.Roughness);
            float pdf = (D * ndh / (4.0 * ndh)) + 1e-4;
            float saTexel  = 4.0 * PI / (6.0 * ENV_RES * ENV_RES);
            float saSample = 1.0 / (float(SAMPLES) * pdf + 1e-4);
            float mip = (pc.Roughness < 1e-3) ? 0.0 : max(0.5 * log2(saSample / saTexel), 0.0);
            color += EnvMap.SampleLevel(EnvSamp, L, mip).rgb * ndl;
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
    float2 t = uv * 2.0 - 1.0;
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
    static constexpr u32 kEnvMips          = 5;     // env mip pyramid (256..16) for prefilter PDF sampling
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
        m_shaders->RegisterSource(u8"ibl_analytic", shaders::ShaderStage::Fragment, Concat(kIblCommon, kIblAnalyticPS));
        m_shaders->RegisterSource(u8"ibl_equirect", shaders::ShaderStage::Fragment, Concat(kIblCommon, kIblEquirectPS));
        m_shaders->RegisterSource(u8"ibl_cubemap",  shaders::ShaderStage::Fragment, Concat(kIblCommon, kIblCubemapPS));
        m_shaders->RegisterSource(u8"ibl_downsample",shaders::ShaderStage::Fragment, Concat(kIblCommon, kIblDownsamplePS));
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
        // Re-dirty the precompute only for fields baked into the env cube. sunAngularSize is analytic-
        // only (the sky pass draws the disc live each frame), so it updates without a rebuild.
        if (!PrecomputeEqual(s, m_sky)) { m_dirty = true; }
        m_sky = s;   // always store the latest (the sky pass reads sun size/intensity live)
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
    // The full-radiance environment cube — sampled by the sky pass (background) at full detail.
    [[nodiscard]] rendergraph::RGHandle EnvHandle()       const noexcept { return m_envH; }
    [[nodiscard]] rhi::TextureView*     EnvView()         const noexcept { return m_envSampleView; }
    [[nodiscard]] f32                   SkyIntensity()    const noexcept { return m_sky.intensity; }
    // Sun (from the directional light) for the sky pass's crisp analytic disc.
    [[nodiscard]] Vec3                  SunDir()          const noexcept { return m_sunDir; }
    [[nodiscard]] f32                   SunIntensity()    const noexcept { return m_sky.sunIntensity; }
    [[nodiscard]] f32                   SunAngularSize()  const noexcept { return m_sky.sunAngularSize; }
    // The sky pass draws a crisp analytic sun disc for the untextured skies (procedural + Preetham);
    // textured envs (HDR/cubemap) carry their own sun, so it's suppressed there.
    [[nodiscard]] bool                  HasSunDisc()      const noexcept { return m_sky.mode != SkyMode::HDREquirect && m_sky.mode != SkyMode::Cubemap; }

    // Set the HDR equirectangular source (RGBA32F, w*h*4 floats). The env cube rebuilds from it when
    // the sky mode is HDREquirect. Upload happens on the next frame's encoder (see Upload).
    void SetEquirect(u32 w, u32 h, Span<const f32> rgba) {
        if (!m_ready || w == 0 || h == 0 || rgba.Size() < static_cast<usize>(w) * h * 4u) { return; }
        DestroyEquirect();
        rhi::TextureDesc td{};
        td.format = rhi::TextureFormat::RGBA32Float; td.width = w; td.height = h;
        td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst; td.label = u8"ibl.equirect";
        if (!m_device->CreateTexture(td, m_equirectTex).IsOk()) { m_equirectTex = nullptr; return; }
        rhi::TextureViewDesc vd{}; vd.format = rhi::TextureFormat::RGBA32Float; vd.dimension = rhi::TextureViewDimension::Texture2D;
        if (!m_device->CreateTextureView(m_equirectTex, vd, m_equirectView).IsOk()) { DestroyEquirect(); return; }
        const u64 bytes = static_cast<u64>(w) * h * 4u * sizeof(f32);
        rhi::BufferDesc sd{}; sd.size = bytes; sd.usage = rhi::BufferUsage::CopySrc; sd.memory = rhi::MemoryLocation::CpuToGpu; sd.label = u8"ibl.equirectStaging";
        if (!m_device->CreateBuffer(sd, m_equirectStaging).IsOk()) { DestroyEquirect(); return; }
        if (void* p = m_equirectStaging->Map()) { MemCopy(p, rgba.Data(), bytes); m_equirectStaging->Unmap(); }
        if (!EnsureEquirectPipeline()) { DestroyEquirect(); return; }
        rhi::BindGroupEntry be[] = { rhi::BindGroupEntry::TextureEntry(m_equirectView), rhi::BindGroupEntry::SamplerEntry(m_equirectSampler) };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_equirectLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ be, 2 };
        if (!m_device->CreateBindGroup(bgd, m_equirectBindGroup).IsOk()) { m_equirectBindGroup = nullptr; DestroyEquirect(); return; }
        m_equirectW = w; m_equirectH = h; m_equirectPending = true; m_dirty = true;
    }

    // Set a cubemap source: 6 RGBA8 faces (+X,-X,+Y,-Y,+Z,-Z) concatenated, each faceSize*faceSize*4
    // bytes. Resampled into the env cube when the mode is Cubemap (handles size + format conversion).
    void SetCubemap(u32 faceSize, Span<const u8> sixFaces) {
        const u64 faceBytes = static_cast<u64>(faceSize) * faceSize * 4u;
        if (!m_ready || faceSize == 0 || sixFaces.Size() < faceBytes * 6u) { return; }
        DestroyCubemap();
        // sRGB format so the hardware decodes the (sRGB-encoded LDR) faces to linear on sample — the
        // env cube is a linear working-space texture. Without this the sky reads washed out.
        rhi::TextureDesc td{};
        td.format = rhi::TextureFormat::RGBA8UnormSrgb; td.width = faceSize; td.height = faceSize; td.arrayLayerCount = 6;
        td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst; td.label = u8"ibl.srcCube";
        if (!m_device->CreateTexture(td, m_srcCube).IsOk()) { m_srcCube = nullptr; return; }
        rhi::TextureViewDesc vd{}; vd.format = rhi::TextureFormat::RGBA8UnormSrgb;
        vd.dimension = rhi::TextureViewDimension::TextureCube; vd.arrayLayerCount = 6;
        if (!m_device->CreateTextureView(m_srcCube, vd, m_srcCubeView).IsOk()) { DestroyCubemap(); return; }
        rhi::BufferDesc sd{}; sd.size = faceBytes * 6u; sd.usage = rhi::BufferUsage::CopySrc; sd.memory = rhi::MemoryLocation::CpuToGpu; sd.label = u8"ibl.srcCubeStaging";
        if (!m_device->CreateBuffer(sd, m_cubemapStaging).IsOk()) { DestroyCubemap(); return; }
        if (void* p = m_cubemapStaging->Map()) { MemCopy(p, sixFaces.Data(), faceBytes * 6u); m_cubemapStaging->Unmap(); }
        if (!EnsureCubemapPipeline()) { DestroyCubemap(); return; }
        rhi::BindGroupEntry be[] = { rhi::BindGroupEntry::TextureEntry(m_srcCubeView), rhi::BindGroupEntry::SamplerEntry(m_sampler) };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_envLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ be, 2 };
        if (!m_device->CreateBindGroup(bgd, m_cubemapBindGroup).IsOk()) { m_cubemapBindGroup = nullptr; DestroyCubemap(); return; }
        m_cubemapFaceSize = faceSize; m_cubemapPending = true; m_dirty = true;
    }

    // Pending texture uploads (equirect/cubemap staging -> texture) on the frame's encoder, BEFORE the
    // graph executes — so the env-build passes sample an already-uploaded, shader-readable source.
    void Upload(rhi::CommandEncoder& enc) {
        if (m_equirectPending && m_equirectTex != nullptr && m_equirectStaging != nullptr) {
            enc.TransitionTexture(m_equirectTex, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst);
            rhi::BufferTextureCopyRegion r{};
            r.bytesPerRow = m_equirectW * 4u * static_cast<u32>(sizeof(f32)); r.rowsPerImage = m_equirectH;
            r.textureExtent = rhi::Extent3D{ m_equirectW, m_equirectH, 1 };
            enc.CopyBufferToTexture(m_equirectStaging, m_equirectTex, r);
            enc.TransitionTexture(m_equirectTex, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead);
            m_equirectPending = false;
        }
        if (m_cubemapPending && m_srcCube != nullptr && m_cubemapStaging != nullptr) {
            enc.TransitionTexture(m_srcCube, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst);
            const u64 faceBytes = static_cast<u64>(m_cubemapFaceSize) * m_cubemapFaceSize * 4u;
            for (u32 f = 0; f < 6; ++f) {
                rhi::BufferTextureCopyRegion r{};
                r.bufferOffset = faceBytes * f; r.bytesPerRow = m_cubemapFaceSize * 4u; r.rowsPerImage = m_cubemapFaceSize;
                r.textureArrayLayer = f; r.textureExtent = rhi::Extent3D{ m_cubemapFaceSize, m_cubemapFaceSize, 1 };
                enc.CopyBufferToTexture(m_cubemapStaging, m_srcCube, r);
            }
            enc.TransitionTexture(m_srcCube, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead);
            m_cubemapPending = false;
        }
    }

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
        // The env cube is imported every frame too (the sky pass reads it for the visible background).
        m_envH = graph.ImportTarget(u8"ibl.env", m_envCube, m_envSampleView,
                                    rhi::ResourceState::ShaderRead, m_envState);
        m_envState = rhi::ResourceState::ShaderRead;

        // BRDF LUT: constant, generate exactly once.
        if (!m_brdfDone) { DeclareBrdf(graph, m_brdfH); m_brdfDone = true; }

        if (!m_dirty) { return; }
        m_dirty = false;
        ++m_generation;

        // (1) Source -> env cube: 6 faces. Procedural (analytic gradient) or HDR equirect (sample the
        // uploaded equirect map); both write the canonical cube faces.
        const rendergraph::RGHandle envH = m_envH;
        const bool useEquirect = (m_sky.mode == SkyMode::HDREquirect) && m_equirectBindGroup != nullptr;
        const bool useCubemap  = (m_sky.mode == SkyMode::Cubemap) && m_cubemapBindGroup != nullptr;
        const bool useAnalytic = (m_sky.mode == SkyMode::Analytic);
        rhi::RenderPipeline* envPipe = useEquirect ? m_equirectPipeline : useCubemap ? m_cubemapPipeline
                                     : useAnalytic ? m_analyticPipeline : m_envPipeline;
        rhi::BindGroup*      envBG   = useEquirect ? m_equirectBindGroup : useCubemap ? m_cubemapBindGroup : nullptr;
        for (u32 face = 0; face < 6; ++face) {
            IblPush push = MakeSkyPush(static_cast<i32>(face));
            graph.AddRenderPass(u8"ibl.env.face", [envH, face, push, envPipe, envBG](rendergraph::PassBuilder& b) {
                b.SetColorTarget(0, envH, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black(),
                                 rendergraph::RGSubresourceRange{ 0, 1, face, 1 });
                b.SetViewport(0, 0, kEnvResolution, kEnvResolution);
                b.NeverCull();
                b.SetExecute([push, envPipe, envBG](rhi::RenderPassEncoder& rp) {
                    rp.SetPipeline(envPipe);
                    if (envBG != nullptr) { rp.SetBindGroup(0, envBG, Span<const u32>{}); }
                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(IblPush), &push);
                    rp.Draw(3, 1, 0, 0);
                });
            });
        }

        // (2) Build the env mip pyramid: box-downsample each mip from the previous. Reads mip m-1 (a
        // single-mip view) and writes mip m — non-overlapping subresources, so the graph orders + barriers
        // it correctly. SH/prefilter (whole-resource reads) then run after the whole chain is written.
        DeclareEnvMips(graph, envH);

        // (3) env -> SH9 diffuse (compute), (4) env -> prefilter mips (PDF-samples the pyramid).
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
        p.ground  = Vec4{ m_sky.ground.x, m_sky.ground.y, m_sky.ground.z, m_sky.turbidity };
        return p;
    }

    // Equal w.r.t. the fields baked into the env cube (drives the precompute-rebuild decision).
    // sunAngularSize is EXCLUDED — it only affects the analytic sky-pass sun, not the cube.
    [[nodiscard]] static bool PrecomputeEqual(const SkySnapshot& a, const SkySnapshot& b) {
        return a.mode == b.mode && a.intensity == b.intensity && a.rotation == b.rotation &&
               a.horizon.x == b.horizon.x && a.horizon.y == b.horizon.y && a.horizon.z == b.horizon.z &&
               a.zenith.x == b.zenith.x && a.zenith.y == b.zenith.y && a.zenith.z == b.zenith.z &&
               a.ground.x == b.ground.x && a.ground.y == b.ground.y && a.ground.z == b.ground.z &&
               a.sunIntensity == b.sunIntensity && a.turbidity == b.turbidity;
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

    // Box-downsample the env cube's mip pyramid: mip m from mip m-1 (per face). Each pass reads only the
    // finer mip (a single-mip source view/bind-group) and renders the coarser one, so read + write never
    // touch the same subresource; the graph's per-subresource barriers serialize the chain by mip.
    void DeclareEnvMips(rendergraph::RenderGraph& graph, rendergraph::RGHandle envH) {
        for (u32 mip = 1; mip < kEnvMips; ++mip) {
            const u32 res = kEnvResolution >> mip;
            rhi::BindGroup* srcBG = m_envMipBG[mip - 1];
            for (u32 face = 0; face < 6; ++face) {
                IblPush push{}; push.faceIndex = static_cast<i32>(face);
                graph.AddRenderPass(u8"ibl.env.mip", [this, envH, mip, face, res, push, srcBG](rendergraph::PassBuilder& b) {
                    b.ReadTexture(envH, rendergraph::RGSubresourceRange{ mip - 1, 1, 0, 6 });
                    b.SetColorTarget(0, envH, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black(),
                                     rendergraph::RGSubresourceRange{ mip, 1, face, 1 });
                    b.SetViewport(0, 0, res, res);
                    b.NeverCull();
                    b.SetExecute([this, push, srcBG](rhi::RenderPassEncoder& rp) {
                        rp.SetPipeline(m_downsamplePipeline);
                        rp.SetBindGroup(0, srcBG, Span<const u32>{});
                        rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(IblPush), &push);
                        rp.Draw(3, 1, 0, 0);
                    });
                });
            }
        }
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
        // Env cube (mip pyramid): mip 0 holds the full-res source radiance; mips 1..N are box-downsampled
        // so the prefilter can PDF-sample a pre-averaged mip per GGX sample (firefly suppression).
        rhi::TextureDesc ed{};
        ed.format = kCubeFormat; ed.width = kEnvResolution; ed.height = kEnvResolution;
        ed.arrayLayerCount = 6; ed.mipLevelCount = kEnvMips;
        ed.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
        ed.label = u8"ibl.env";
        if (!m_device->CreateTexture(ed, m_envCube).IsOk()) { return false; }
        rhi::TextureViewDesc ev{}; ev.format = kCubeFormat;
        ev.dimension = rhi::TextureViewDimension::TextureCube; ev.arrayLayerCount = 6; ev.mipLevelCount = kEnvMips;
        if (!m_device->CreateTextureView(m_envCube, ev, m_envSampleView).IsOk()) { return false; }
        // Single-mip cube views of each env mip — bound as the source when downsampling the NEXT mip, so
        // the read descriptor covers only mip m (never the mip m+1 being rendered → no read/write hazard).
        for (u32 m = 0; m < kEnvMips; ++m) {
            rhi::TextureViewDesc mv{}; mv.format = kCubeFormat;
            mv.dimension = rhi::TextureViewDimension::TextureCube;
            mv.baseMipLevel = m; mv.mipLevelCount = 1; mv.arrayLayerCount = 6;
            if (!m_device->CreateTextureView(m_envCube, mv, m_envMipView[m]).IsOk()) { return false; }
        }

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

        m_envPipeline        = MakeFullscreenPipeline(vs, u8"ibl_procenv", m_envOnlyLayout, kCubeFormat);
        m_analyticPipeline   = MakeFullscreenPipeline(vs, u8"ibl_analytic", m_envOnlyLayout, kCubeFormat);
        m_downsamplePipeline = MakeFullscreenPipeline(vs, u8"ibl_downsample", m_prefilterLayout, kCubeFormat);
        m_prefilterPipeline  = MakeFullscreenPipeline(vs, u8"ibl_prefilter", m_prefilterLayout, kCubeFormat);
        m_brdfPipeline       = MakeFullscreenPipeline(vs, u8"ibl_brdf", m_brdfPipelineLayout, kBrdfFormat);
        if (m_envPipeline == nullptr || m_analyticPipeline == nullptr || m_downsamplePipeline == nullptr ||
            m_prefilterPipeline == nullptr || m_brdfPipeline == nullptr) { return false; }

        // env sample bind group (for prefilter/SH: full mip chain).
        rhi::BindGroupEntry be[] = { rhi::BindGroupEntry::TextureEntry(m_envSampleView), rhi::BindGroupEntry::SamplerEntry(m_sampler) };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_envLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ be, 2 };
        if (!m_device->CreateBindGroup(bgd, m_envBindGroup).IsOk()) { return false; }
        // Per-mip source bind groups (mip m as the downsample input for mip m+1).
        for (u32 m = 0; m < kEnvMips; ++m) {
            rhi::BindGroupEntry me[] = { rhi::BindGroupEntry::TextureEntry(m_envMipView[m]), rhi::BindGroupEntry::SamplerEntry(m_sampler) };
            rhi::BindGroupDesc md{}; md.layout = m_envLayout; md.entries = Span<const rhi::BindGroupEntry>{ me, 2 };
            if (!m_device->CreateBindGroup(md, m_envMipBG[m]).IsOk()) { return false; }
        }

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

    // Lazily create the equirect->cube pipeline (2D source tex + sampler + push) — only when an HDR
    // equirect is first set, since most scenes are procedural.
    bool EnsureEquirectPipeline() {
        if (m_equirectPipeline != nullptr) { return true; }
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"ibl_fs", shaders::ShaderStage::Vertex, shaders::ShaderFlags::None);
        if (vs == nullptr) { return false; }
        rhi::BindGroupLayoutEntry tex  = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::Texture2D);
        rhi::BindGroupLayoutEntry samp = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry e[] = { tex, samp };
        rhi::BindGroupLayoutDesc ld{}; ld.entries = Span<const rhi::BindGroupLayoutEntry>{ e, 2 };
        if (!m_device->CreateBindGroupLayout(ld, m_equirectLayout).IsOk()) { return false; }
        rhi::PushConstantRange pc{}; pc.stages = rhi::ShaderStage::Fragment; pc.offset = 0; pc.size = sizeof(IblPush);
        rhi::BindGroupLayout* layouts[] = { m_equirectLayout };
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        pld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pc, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_equirectPipelineLayout).IsOk()) { return false; }
        m_equirectPipeline = MakeFullscreenPipeline(vs, u8"ibl_equirect", m_equirectPipelineLayout, kCubeFormat);
        if (m_equirectPipeline == nullptr) { return false; }
        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Linear; ss.magFilter = rhi::FilterMode::Linear; ss.mipmapFilter = rhi::MipmapFilterMode::Linear;
        ss.addressU = rhi::AddressMode::Repeat; ss.addressV = rhi::AddressMode::ClampToEdge; ss.addressW = rhi::AddressMode::ClampToEdge;
        ss.label = u8"ibl.equirectSampler";
        if (!m_device->CreateSampler(ss, m_equirectSampler).IsOk()) { return false; }
        return true;
    }

    // Lazily create the cubemap->cube pipeline (samples the source cube; reuses the prefilter's cube
    // bind-group + pipeline layout — cube tex + sampler + push).
    bool EnsureCubemapPipeline() {
        if (m_cubemapPipeline != nullptr) { return true; }
        if (m_prefilterLayout == nullptr || m_envLayout == nullptr) { return false; }
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"ibl_fs", shaders::ShaderStage::Vertex, shaders::ShaderFlags::None);
        if (vs == nullptr) { return false; }
        m_cubemapPipeline = MakeFullscreenPipeline(vs, u8"ibl_cubemap", m_prefilterLayout, kCubeFormat);
        return m_cubemapPipeline != nullptr;
    }

    void DestroyCubemap() {
        if (m_cubemapBindGroup) { m_device->DestroyBindGroup(m_cubemapBindGroup); m_cubemapBindGroup = nullptr; }
        if (m_cubemapStaging) { m_device->DestroyBuffer(m_cubemapStaging); m_cubemapStaging = nullptr; }
        if (m_srcCubeView) { m_device->DestroyTextureView(m_srcCubeView); m_srcCubeView = nullptr; }
        if (m_srcCube) { m_device->DestroyTexture(m_srcCube); m_srcCube = nullptr; }
        m_cubemapPending = false;
    }

    // Free the per-source equirect texture/staging/view/bind-group (the pipeline + layout + sampler
    // persist, recreated lazily once).
    void DestroyEquirect() {
        if (m_equirectBindGroup) { m_device->DestroyBindGroup(m_equirectBindGroup); m_equirectBindGroup = nullptr; }
        if (m_equirectStaging) { m_device->DestroyBuffer(m_equirectStaging); m_equirectStaging = nullptr; }
        if (m_equirectView) { m_device->DestroyTextureView(m_equirectView); m_equirectView = nullptr; }
        if (m_equirectTex) { m_device->DestroyTexture(m_equirectTex); m_equirectTex = nullptr; }
        m_equirectPending = false;
    }

    void Shutdown() {
        DestroyCubemap();
        if (m_cubemapPipeline) { m_device->DestroyRenderPipeline(m_cubemapPipeline); m_cubemapPipeline = nullptr; }
        DestroyEquirect();
        if (m_equirectPipeline) { m_device->DestroyRenderPipeline(m_equirectPipeline); m_equirectPipeline = nullptr; }
        if (m_equirectPipelineLayout) { m_device->DestroyPipelineLayout(m_equirectPipelineLayout); m_equirectPipelineLayout = nullptr; }
        if (m_equirectLayout) { m_device->DestroyBindGroupLayout(m_equirectLayout); m_equirectLayout = nullptr; }
        if (m_equirectSampler) { m_device->DestroySampler(m_equirectSampler); m_equirectSampler = nullptr; }
        if (m_shBindGroup) { m_device->DestroyBindGroup(m_shBindGroup); m_shBindGroup = nullptr; }
        if (m_envBindGroup) { m_device->DestroyBindGroup(m_envBindGroup); m_envBindGroup = nullptr; }
        for (u32 m = 0; m < kEnvMips; ++m) { if (m_envMipBG[m]) { m_device->DestroyBindGroup(m_envMipBG[m]); m_envMipBG[m] = nullptr; } }
        if (m_shPipeline) { m_device->DestroyComputePipeline(m_shPipeline); m_shPipeline = nullptr; }
        if (m_envPipeline) { m_device->DestroyRenderPipeline(m_envPipeline); m_envPipeline = nullptr; }
        if (m_analyticPipeline) { m_device->DestroyRenderPipeline(m_analyticPipeline); m_analyticPipeline = nullptr; }
        if (m_downsamplePipeline) { m_device->DestroyRenderPipeline(m_downsamplePipeline); m_downsamplePipeline = nullptr; }
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
        for (u32 m = 0; m < kEnvMips; ++m) { if (m_envMipView[m]) { m_device->DestroyTextureView(m_envMipView[m]); m_envMipView[m] = nullptr; } }
        if (m_envCube) { m_device->DestroyTexture(m_envCube); m_envCube = nullptr; }
        if (m_prefilterView) { m_device->DestroyTextureView(m_prefilterView); m_prefilterView = nullptr; }
        if (m_prefilterCube) { m_device->DestroyTexture(m_prefilterCube); m_prefilterCube = nullptr; }
        if (m_brdfView) { m_device->DestroyTextureView(m_brdfView); m_brdfView = nullptr; }
        if (m_brdfLut) { m_device->DestroyTexture(m_brdfLut); m_brdfLut = nullptr; }
    }

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;

    // HDR equirectangular source (optional): uploaded to a 2D texture, sampled by the equirect->cube pass.
    rhi::Texture*     m_equirectTex = nullptr;    rhi::TextureView* m_equirectView = nullptr;
    rhi::Buffer*      m_equirectStaging = nullptr;
    rhi::Sampler*     m_equirectSampler = nullptr;
    rhi::BindGroupLayout* m_equirectLayout = nullptr;
    rhi::PipelineLayout*  m_equirectPipelineLayout = nullptr;
    rhi::RenderPipeline*  m_equirectPipeline = nullptr;
    rhi::BindGroup*       m_equirectBindGroup = nullptr;
    u32  m_equirectW = 0, m_equirectH = 0;
    bool m_equirectPending = false;

    // Cubemap source (optional): 6 RGBA8 faces resampled into the env cube by the cubemap->cube pass.
    rhi::Texture*        m_srcCube = nullptr;       rhi::TextureView* m_srcCubeView = nullptr;
    rhi::Buffer*         m_cubemapStaging = nullptr;
    rhi::RenderPipeline* m_cubemapPipeline = nullptr;   // reuses m_prefilterLayout + m_envLayout
    rhi::BindGroup*      m_cubemapBindGroup = nullptr;
    u32  m_cubemapFaceSize = 0;
    bool m_cubemapPending = false;

    rhi::Texture*     m_envCube = nullptr;        rhi::TextureView* m_envSampleView = nullptr;
    rhi::TextureView* m_envMipView[kEnvMips] = {};   // single-mip cube views (downsample sources)
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
    rhi::RenderPipeline*  m_analyticPipeline = nullptr;   // Preetham; reuses m_envOnlyLayout (push only)
    rhi::RenderPipeline*  m_downsamplePipeline = nullptr; // env mip pyramid; reuses m_prefilterLayout
    rhi::RenderPipeline*  m_prefilterPipeline = nullptr;
    rhi::RenderPipeline*  m_brdfPipeline = nullptr;
    rhi::ComputePipeline* m_shPipeline = nullptr;
    rhi::BindGroup*       m_envBindGroup = nullptr;
    rhi::BindGroup*       m_envMipBG[kEnvMips] = {};       // per-mip source bind groups (downsample)
    rhi::BindGroup*       m_shBindGroup = nullptr;

    // Imported-target persisted states (carried across frames for the graph's barrier solver).
    rhi::ResourceState m_envState = rhi::ResourceState::Undefined;
    rhi::ResourceState m_prefilterState = rhi::ResourceState::Undefined;
    rhi::ResourceState m_brdfState = rhi::ResourceState::Undefined;

    // This frame's product handles (re-imported each ProcessPending; read by the forward pass).
    rendergraph::RGHandle m_prefilterH = {};
    rendergraph::RGHandle m_brdfH = {};
    rendergraph::RGHandle m_shH = {};
    rendergraph::RGHandle m_envH = {};

    Vec3        m_sunDir{ 0.0f, -1.0f, 0.0f };   // from the directional light (set per frame)
    SkySnapshot m_sky{};                          // current sky authoring
    bool m_ready = false;
    bool m_dirty = false;
    bool m_brdfDone = false;   // the BRDF LUT is constant — generated once, not per sky change
    u64  m_generation = 0;
};

} // namespace draconic::render

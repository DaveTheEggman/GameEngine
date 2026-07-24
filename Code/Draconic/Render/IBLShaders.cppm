/// Draconic::Render - the `:ibl_shaders` partition.
///
/// HLSL source for the IBL precompute pipeline, split out from IBLSystem.cppm so the shader text is
/// isolated and trivial to lift to standalone .hlsl files later. Pass-internal: imported by `:ibl`.
/// IblCommon() is a shared prefix concatenated with each cube-pass fragment body in the pass.

module;
#include "Core/Prelude.h"

export module draconic.render:ibl_shaders;

import draconic.core;

export namespace draconic::render
{

    // Fullscreen-triangle VS (positions + uv from SV_VertexID), shared by every cube-face + LUT pass.
    [[nodiscard]] inline core::StringView IblFullscreenVS() noexcept
    {
        return core::StringView(u8R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    o.uv  = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}
)");
    }

    // Per-face/mip params for the cube passes (tightly-packed push constants). Sky authoring (mode +
    // intensity + gradient colors + sun + rotation) rides here so the procedural env pass reads it.
    [[nodiscard]] inline core::StringView IblCommon() noexcept
    {
        return core::StringView(u8R"(
struct IblPush {
    int FaceIndex; int Mode; float Roughness; float SkyIntensity;
    float4 Sun;        // xyz = direction, w = sun angular size (degrees)
    float4 Horizon;    // rgb, a = sun intensity
    float4 Zenith;     // rgb, a = sky rotation (radians)
    float4 Ground;     // rgb
};
[[vk::push_constant]] ConstantBuffer<IblPush> pc : register(b0, space1);

// Canonical cube-face direction from a face index + [0,1] face uv. NOTE: no t.y negation - the cube
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
)");
    }

    // Procedural sky -> one env cube face. Gradient (horizon/zenith/ground) + a sun disc whose direction
    // comes from the first directional light (SunDir.xyz, .w = intensity). Matches Sedulous's sky.frag.
    [[nodiscard]] inline core::StringView IblProcEnvPS() noexcept
    {
        return core::StringView(u8R"(
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
        // A soft, broad sun GLOW only (no sharp disc) - the crisp sun is drawn analytically at screen
        // resolution by the sky pass; baking a sub-texel disc into the 256^2 cube would alias to a square.
        float3 sunDir = normalize(-pc.Sun.xyz);
        float  d      = max(dot(dir, sunDir), 0.0);
        sky += pow(d, 64.0) * max(pc.Horizon.a, 0.0) * 0.3;
    }
    return float4(sky * max(pc.SkyIntensity, 0.0), 1.0);
}
)");
    }

    // Preetham analytic daylight -> one env cube face. Physically-based sky luminance/chromaticity from
    // the sun elevation + turbidity (pc.Ground.a). Sun dir = -pc.Sun.xyz. A richer procedural sky.
    [[nodiscard]] inline core::StringView IblAnalyticPS() noexcept
    {
        return core::StringView(u8R"(
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
)");
    }

    // HDR equirectangular -> one env cube face: map the face direction to equirect uv and sample. Uses
    // the same DirForFace (canonical, negative-viewport-aware) as the procedural pass.
    [[nodiscard]] inline core::StringView IblEquirectPS() noexcept
    {
        return core::StringView(u8R"(
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
    // Yaw by the sky rotation (Zenith.a) - this and the cubemap pass are where the user's
    // rotation slider actually lands (the procedural gradient is rotation-invariant).
    float rot = pc.Zenith.a;
    float cr = cos(rot), sr = sin(rot);
    dir = float3(cr * dir.x + sr * dir.z, dir.y, -sr * dir.x + cr * dir.z);
    return float4(EquirectMap.SampleLevel(EquirectSamp, DirToEquirect(dir), 0.0).rgb * max(pc.SkyIntensity, 0.0), 1.0);
}
)");
    }

    // Loaded cubemap -> env cube face: resample the (possibly larger / LDR) source cube along the face
    // direction (downsamples + format-converts into the RGBA16F env cube). Shares the cube bind-group
    // layout with the prefilter.
    [[nodiscard]] inline core::StringView IblCubemapPS() noexcept
    {
        return core::StringView(u8R"(
TextureCube  SrcCube  : register(t0, space0);
SamplerState SrcSamp  : register(s0, space0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 dir = DirForFace(pc.FaceIndex, uv);
    float rot = pc.Zenith.a;   // sky rotation yaw (see the equirect pass)
    float cr = cos(rot), sr = sin(rot);
    dir = float3(cr * dir.x + sr * dir.z, dir.y, -sr * dir.x + cr * dir.z);
    return float4(SrcCube.SampleLevel(SrcSamp, dir, 0.0).rgb * max(pc.SkyIntensity, 0.0), 1.0);
}
)");
    }

    // Box-downsample one env cube mip from the previous (finer) mip: sample the source cube (bound as a
    // single-mip view) along the face direction with linear filtering - averages the 2x2 finer texels into
    // this half-res texel. Builds the env mip pyramid the prefilter samples by PDF (firefly suppression).
    [[nodiscard]] inline core::StringView IblDownsamplePS() noexcept
    {
        return core::StringView(u8R"(
TextureCube  SrcCube : register(t0, space0);
SamplerState SrcSamp : register(s0, space0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 dir = DirForFace(pc.FaceIndex, uv);
    return float4(SrcCube.SampleLevel(SrcSamp, dir, 0.0).rgb, 1.0);
}
)");
    }

    // GGX prefilter (Karis split-sum specular): importance-sample the env cube around the reflection
    // direction (= N = V) at this mip's roughness. 1024 Hammersley samples / texel. Each sample reads a
    // PDF-selected env mip (solid-angle matched) so bright pixels are pre-averaged - kills specular fireflies.
    [[nodiscard]] inline core::StringView IblPrefilterPS() noexcept
    {
        return core::StringView(u8R"(
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
)");
    }

    // BRDF integration LUT: split-sum's second term. uv = (NdotV, roughness) -> (scale, bias) for F0.
    [[nodiscard]] inline core::StringView IblBrdfPS() noexcept
    {
        return core::StringView(u8R"(
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
)");
    }

    // SH9 diffuse projection: reduce the env cube to 9 RGB spherical-harmonic coefficients (one thread;
    // runs once per source change). Solid-angle-weighted cosine convolution is then evaluated cheaply in
    // the forward shader. Output layout: 9 float4 (xyz = coeff, w unused).
    [[nodiscard]] inline core::StringView IblShProjectCS() noexcept
    {
        return core::StringView(u8R"(
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
)");
    }

}

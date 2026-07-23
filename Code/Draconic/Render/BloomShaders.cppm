/// Draconic::Render - the `:bloom_shaders` partition.
///
/// HLSL source for the bloom pass, split out from BloomPass.cppm so the shader text is isolated and
/// trivial to lift to standalone .hlsl files later. Pass-internal: imported by `:bloom`. BloomCommon()
/// is a shared prefix concatenated with each fragment body in the pass.

module;
#include "Core/Prelude.h"

export module draconic.render:bloom_shaders;

import draconic.core;

export namespace draconic::render
{

    // Fullscreen-triangle VS with a top-origin [0,1] uv (uv.y=0 at the top). The RHI's negative-viewport
    // Y-flip means a naive uv would run bottom-up, so every RT-sampling pass would flip Y - flip uv.y here
    // once so the whole pyramid (and the tonemap composite) stays orientation-consistent with the RTs.
    [[nodiscard]] inline core::StringView BloomVS() noexcept
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

    [[nodiscard]] inline core::StringView BloomCommon() noexcept
    {
        return core::StringView(u8R"(
Texture2D<float4> Src  : register(t0, space0);
SamplerState      Samp : register(s0, space0);
struct BloomPush {
    float2 SrcTexel;   // 1 / source size (filter tap spacing)
    float  Threshold;  // brightness cutoff (first downsample only)
    float  Knee;       // soft-knee width
    int    FirstPass;  // 1 = threshold + firefly-average the source (mip 0)
    float3 _pad;
};
[[vk::push_constant]] BloomPush pc;
)");
    }

    // 13-tap downsample (CoD/Jimenez). On the first pass, soft-knee threshold + a Karis luma weighting on
    // the 2x2 groups to stop single bright pixels from causing bloom flicker.
    [[nodiscard]] inline core::StringView BloomDownPS() noexcept
    {
        return core::StringView(u8R"(
float3 Prefilter(float3 c) {
    float br   = max(c.r, max(c.g, c.b));
    float soft = clamp(br - pc.Threshold + pc.Knee, 0.0, 2.0 * pc.Knee);
    soft       = (soft * soft) / (4.0 * pc.Knee + 1e-5);
    float contrib = max(soft, br - pc.Threshold) / max(br, 1e-5);
    return c * contrib;
}
float KarisWeight(float3 c) { return 1.0 / (1.0 + max(c.r, max(c.g, c.b))); }
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float2 t = pc.SrcTexel;
    float3 a = Src.SampleLevel(Samp, uv + t * float2(-2,-2), 0).rgb;
    float3 b = Src.SampleLevel(Samp, uv + t * float2( 0,-2), 0).rgb;
    float3 c = Src.SampleLevel(Samp, uv + t * float2( 2,-2), 0).rgb;
    float3 d = Src.SampleLevel(Samp, uv + t * float2(-2, 0), 0).rgb;
    float3 e = Src.SampleLevel(Samp, uv,                     0).rgb;
    float3 f = Src.SampleLevel(Samp, uv + t * float2( 2, 0), 0).rgb;
    float3 g = Src.SampleLevel(Samp, uv + t * float2(-2, 2), 0).rgb;
    float3 h = Src.SampleLevel(Samp, uv + t * float2( 0, 2), 0).rgb;
    float3 i = Src.SampleLevel(Samp, uv + t * float2( 2, 2), 0).rgb;
    float3 j = Src.SampleLevel(Samp, uv + t * float2(-1,-1), 0).rgb;
    float3 k = Src.SampleLevel(Samp, uv + t * float2( 1,-1), 0).rgb;
    float3 l = Src.SampleLevel(Samp, uv + t * float2(-1, 1), 0).rgb;
    float3 m = Src.SampleLevel(Samp, uv + t * float2( 1, 1), 0).rgb;
    float3 result;
    if (pc.FirstPass != 0) {
        // Karis-weighted average of the 5 inner 2x2 groups (firefly suppression), then threshold.
        float3 g0 = (j + k + l + m) * 0.25;
        float3 g1 = (a + b + d + e) * 0.25;
        float3 g2 = (b + c + e + f) * 0.25;
        float3 g3 = (d + e + g + h) * 0.25;
        float3 g4 = (e + f + h + i) * 0.25;
        float w0 = KarisWeight(g0), w1 = KarisWeight(g1), w2 = KarisWeight(g2), w3 = KarisWeight(g3), w4 = KarisWeight(g4);
        result = (g0*w0*0.5 + g1*w1*0.125 + g2*w2*0.125 + g3*w3*0.125 + g4*w4*0.125)
               / max(w0*0.5 + w1*0.125 + w2*0.125 + w3*0.125 + w4*0.125, 1e-5);
        result = Prefilter(result);
    } else {
        result = e * 0.125
               + (a + c + g + i) * 0.03125
               + (b + d + f + h) * 0.0625
               + (j + k + l + m) * 0.125;
    }
    return float4(result, 1.0);
}
)");
    }

    // 9-tap tent upsample; the pipeline uses additive blend so it accumulates onto the finer mip.
    [[nodiscard]] inline core::StringView BloomUpPS() noexcept
    {
        return core::StringView(u8R"(
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float2 t = pc.SrcTexel;
    float3 s = Src.SampleLevel(Samp, uv + t * float2(-1,-1), 0).rgb * 1.0
             + Src.SampleLevel(Samp, uv + t * float2( 0,-1), 0).rgb * 2.0
             + Src.SampleLevel(Samp, uv + t * float2( 1,-1), 0).rgb * 1.0
             + Src.SampleLevel(Samp, uv + t * float2(-1, 0), 0).rgb * 2.0
             + Src.SampleLevel(Samp, uv,                     0).rgb * 4.0
             + Src.SampleLevel(Samp, uv + t * float2( 1, 0), 0).rgb * 2.0
             + Src.SampleLevel(Samp, uv + t * float2(-1, 1), 0).rgb * 1.0
             + Src.SampleLevel(Samp, uv + t * float2( 0, 1), 0).rgb * 2.0
             + Src.SampleLevel(Samp, uv + t * float2( 1, 1), 0).rgb * 1.0;
    return float4(s * (1.0 / 16.0), 1.0);
}
)");
    }

}

Texture2D<float4> HdrTex    : register(t0, space0);
Texture2D<float4> AoTex     : register(t1, space0);
SamplerState      PointSamp : register(s0, space0);
struct ApplyPush { float Strength; float3 _pad; };
[[vk::push_constant]] ConstantBuffer<ApplyPush> pc : register(b0, space1);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 c  = HdrTex.SampleLevel(PointSamp, uv, 0).rgb;
    float  ao = lerp(1.0, AoTex.SampleLevel(PointSamp, uv, 0).r, saturate(pc.Strength));
    return float4(c * ao, 1.0);
}

Texture2D    AoTex     : register(t0, space0);
Texture2D    DepthTex  : register(t1, space0);
SamplerState PointSamp : register(s0, space0);
struct BlurPush { float2 Dir; float2 TexelSize; float DepthSigma; float3 _pad; };
[[vk::push_constant]] ConstantBuffer<BlurPush> pc : register(b0, space1);

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

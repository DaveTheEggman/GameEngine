Texture2D<float4> Src  : register(t0, space0);
SamplerState      Samp : register(s0, space0);
struct BloomPush {
    float2 SrcTexel;   // 1 / source size (filter tap spacing)
    float  Threshold;  // brightness cutoff (first downsample only)
    float  Knee;       // soft-knee width
    int    FirstPass;  // 1 = threshold + firefly-average the source (mip 0)
    float3 _pad;
};
[[vk::push_constant]] ConstantBuffer<BloomPush> pc : register(b0, space1);

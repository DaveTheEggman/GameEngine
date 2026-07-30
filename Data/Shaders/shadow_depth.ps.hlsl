Texture2D    AlbedoMap   : register(t0, space2);
SamplerState MainSampler : register(s0, space2);
void main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    if (AlbedoMap.Sample(MainSampler, uv).a < 0.5) { discard; }
}

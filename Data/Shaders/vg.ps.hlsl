// VG (2D vector-graphics) fragment shader. Samples the atlas at t0/s0, modulates by the vertex
// color, and premultiplies coverage into alpha. Resolved via ShaderSystem::GetVariant("vg", Fragment).
struct PSInput { float4 Position:SV_Position; float2 TexCoord:TEXCOORD0; float4 Color:COLOR0; float Coverage:COVERAGE; };
Texture2D VGTexture : register(t0);
SamplerState VGSampler : register(s0);
float4 main(PSInput input) : SV_Target {
    float4 texColor = VGTexture.Sample(VGSampler, input.TexCoord);
    float4 result = texColor * input.Color;
    result.a *= input.Coverage;
    return result;
}

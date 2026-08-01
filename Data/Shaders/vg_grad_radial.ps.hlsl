// VG radial-gradient fragment shader. The tessellator emits the gradient-space coordinate
// (pos-center)/radius as TexCoord, so the parameter t = length(TexCoord) is computed PER PIXEL
// (exact radial falloff) instead of Gouraud-interpolating it across triangles. t is sampled from
// the baked 256x1 ramp LUT at texel centers (pad spread; the ramp itself is sRGB-decoded by the
// GPU). Outputs PREMULTIPLIED-alpha color to pair with the PremultipliedAlpha blend. Resolved via
// ShaderSystem::GetVariant("vg_grad_radial", Fragment).
struct PSInput { float4 Position:SV_Position; float2 TexCoord:TEXCOORD0; float4 Color:COLOR0; float Coverage:COVERAGE; };
Texture2D VGTexture : register(t0);
SamplerState VGSampler : register(s0);
float4 main(PSInput input) : SV_Target {
    float t = saturate(length(input.TexCoord));
    float u = (0.5 + t * 255.0) / 256.0; // texel center: pads + dodges the Repeat wrap seam
    float4 ramp = VGTexture.Sample(VGSampler, float2(u, 0.5));
    float4 result = ramp * input.Color;
    result.a *= input.Coverage;
    result.rgb *= result.a; // premultiply
    return result;
}

// VG (2D vector-graphics) vertex shader. Transforms a 2D position by the projection cbuffer and
// passes texcoord / color / coverage through. Shared by the standard and distance-field pipelines.
// Cooked into the engine shader pack like every other engine shader (WGSL for web, SPIR-V/DXIL for
// desktop) - the VG renderer/UI resolves it via ShaderSystem::GetVariant("vg", Vertex).
#pragma pack_matrix(row_major)
cbuffer VGUniforms : register(b0) { float4x4 Projection; float DFPxRange; float DFAtlasW; float DFAtlasH; float _pad; };
struct VSInput { float2 Position:TEXCOORD0; float2 TexCoord:TEXCOORD1; float4 Color:TEXCOORD2; float Coverage:TEXCOORD3; };
struct VSOutput { float4 Position:SV_Position; float2 TexCoord:TEXCOORD0; float4 Color:COLOR0; float Coverage:COVERAGE; };
VSOutput main(VSInput input) {
    VSOutput o;
    o.Position = mul(float4(input.Position, 0.0, 1.0), Projection);
    o.TexCoord = input.TexCoord; o.Color = input.Color; o.Coverage = input.Coverage;
    return o;
}

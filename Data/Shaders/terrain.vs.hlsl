#pragma pack_matrix(row_major)

// Terrain chunk VS. ONE 65x65 grid of unit-square UVs is drawn for every chunk; this shader places
// each grid vertex in the world (per-chunk OriginXZ + SizeXZ) and lifts it to the sampled height,
// fetched exactly from the R16Uint height texture via an integer Load. The chunk's world transform is
// folded into ViewProj at resolve time, so positions here are in the heightfield's local space. The
// surface normal comes from central differences on the height texture (no precomputed normals).

cbuffer TerrainView : register(b0, space0) {
    float4x4 ViewProj;   // chunkToWorld * cameraViewProj (local-space -> clip)
    float4   LightDir;   // xyz = direction TO the light (normalized); w unused
    float4   CameraPos;  // xyz world camera (reserved)
};

cbuffer TerrainChunk : register(b0, space1) {
    float2 OriginXZ;    // world XZ of the chunk's grid origin
    float2 SizeXZ;      // world XZ span of the chunk (kChunkQuads quads)
    float2 TexelBase;   // height-texture texel coord of the chunk origin (gridX0, gridZ0)
    float2 TexelSpan;   // texels spanned across the chunk (= kChunkQuads)
    float2 HeightRange; // minY, maxY (world)
    float2 GridSize;    // heightfield side S (texel clamp bound), same in x and y
};

Texture2D<uint> HeightTex : register(t0, space2);

struct VSIn  { float2 UV : TEXCOORD0; };
struct VSOut {
    float4 pos     : SV_Position;
    float3 normal  : TEXCOORD0;
    float  heightT : TEXCOORD1; // 0..1 within [minY, maxY]
};

float SampleHeightY(int2 texel) {
    int2 m = clamp(texel, int2(0, 0), int2((int)GridSize.x - 1, (int)GridSize.y - 1));
    uint s = HeightTex.Load(int3(m, 0)).r;
    return HeightRange.x + ((float)s / 65535.0) * (HeightRange.y - HeightRange.x);
}

VSOut main(VSIn i) {
    VSOut o;

    float2 texelF = TexelBase + i.UV * TexelSpan;
    int2   texel  = int2((int)round(texelF.x), (int)round(texelF.y));
    float  y      = SampleHeightY(texel);

    float2 wxz = OriginXZ + i.UV * SizeXZ;
    o.pos = mul(float4(wxz.x, y, wxz.y, 1.0), ViewProj);

    // Normal from central differences on the height texture (world units per texel = SizeXZ / span).
    float hL = SampleHeightY(texel + int2(-1,  0));
    float hR = SampleHeightY(texel + int2( 1,  0));
    float hD = SampleHeightY(texel + int2( 0, -1));
    float hU = SampleHeightY(texel + int2( 0,  1));
    float2 cell = SizeXZ / max(TexelSpan, float2(1.0, 1.0));
    o.normal  = normalize(float3(hL - hR, cell.x + cell.y, hD - hU));
    o.heightT = saturate((y - HeightRange.x) / max(HeightRange.y - HeightRange.x, 1e-3));
    return o;
}

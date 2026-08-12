// Scene-pass MSAA first-sample (sample 0) resolve (msaa.md Decision 3 + the aux-resolve note).
//
// Reads the multisampled depth + G-buffer aux (normal / velocity / material) written by the MSAA
// forward pass and writes SAMPLE 0 into single-sample 1x targets that the post consumers
// (GTAO / SSR / TAA / motion reprojection) read UNCHANGED. Depth goes through SV_Depth into a real
// depth-format target (Fable pin 1) so no consumer's binding or shader declaration changes - which is
// the whole point of the resolved-depth ruling. Scene COLOR is resolved separately by the hardware
// resolve attachment (averaged - the real edge AA); this pass never touches color.
//
// SAMPLE-0 NUANCE (Fable pin 2): sample 0 of a standard MSAA pattern is NOT the pixel centre, so
// resolved depth/normals sit ~0.4px off where a 1x prepass would have sampled. It is consistent
// frame-to-frame and benign under TAA's own jitter - accepted. Depth is NEVER averaged: it is
// nonlinear, so an averaged edge depth is a point in empty space; taking one sample is exactly the
// "identical to today's quality" rule Decision 3 protects.

Texture2DMS<float2> gNormalMS   : register(t0, space0); // RG16Float octahedral view-space normal
Texture2DMS<float2> gVelocityMS : register(t1, space0); // RG16Float screen-space motion (UV delta)
Texture2DMS<float2> gMaterialMS : register(t2, space0); // RG8Unorm  R=roughness G=metallic
Texture2DMS<float>  gDepthMS    : register(t3, space0); // Depth32Float

struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
struct PSOut {
    float2 normal   : SV_Target0;
    float2 velocity : SV_Target1;
    float2 material : SV_Target2;
    float  depth    : SV_Depth;
};

PSOut main(PSIn i) {
    int2 c = int2(i.pos.xy);
    PSOut o;
    o.normal   = gNormalMS.Load(c, 0);
    o.velocity = gVelocityMS.Load(c, 0);
    o.material = gMaterialMS.Load(c, 0);
    o.depth    = gDepthMS.Load(c, 0);
    return o;
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// variants: HOLES
//
// Terrain depth-pass fragment stage, used ONLY for holed chunks under HOLES (the camera prepass
// and the shadow cascades): it discards the fragments inside the cut by the bilinear hole mask,
// exactly as terrain.ps does, so the prepass depth and the shadow map open where the surface
// does. Under no flag it is an empty stage the pack cooks and nothing binds.
#ifdef HOLES
Texture2D    HoleMask    : register(t1, space2);
SamplerState HoleSampler : register(s0, space2);
struct DepthPSIn {
    float4 pos     : SV_Position;
    float2 splatUV : TEXCOORD6;
};
void main(DepthPSIn i) {
    float2 dims;
    HoleMask.GetDimensions(dims.x, dims.y);
    if (HoleMask.Sample(HoleSampler, i.splatUV + 0.5 / max(dims, float2(1.0, 1.0))).r > 0.5) { discard; }
}
#else
void main(float4 pos : SV_Position) {}
#endif

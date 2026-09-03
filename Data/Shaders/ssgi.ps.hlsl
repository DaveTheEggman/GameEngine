// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Screen-space global illumination - the DIFFUSE twin of ssr.ps.hlsl. Per pixel, march a few
// cosine-weighted hemisphere rays around the mapped normal against the depth buffer; a hit
// gathers the lit HDR there as one-bounce radiance. The noise ROTATES per frame (unlike SSR's
// static dither) so the temporal resolve converges the low ray count; misses contribute
// nothing - the ambient/IBL already in the HDR stands in for off-screen light.

#include "push_constant.hlsli"
Texture2D<float4> SceneTex  : register(t0, space0);   // lit HDR (the bounce source)
Texture2D         DepthTex  : register(t1, space0);
Texture2D         NormalTex : register(t2, space0);   // octahedral view-space normal
SamplerState      PointSamp : register(s0, space0);   // depth / reconstruction (exact)
SamplerState      LinearSamp: register(s1, space0);   // radiance gather

struct SsgiPush {
    row_major float4x4 InvProj;   // (viewport-local ndc, depth) -> view space
    float2 VpMin;                 // this view's sub-rect origin in FULL-texture uv
    float2 VpSize;                // this view's sub-rect size in FULL-texture uv
    float2 Jitter;                // projection z-row (proj(2,0), proj(2,1)): jittered ndc match
    float  ProjXX;                // projection(0,0): view.x -> ndc.x
    float  ProjYY;                // projection(1,1): view.y -> ndc.y
    float  Thickness;             // view-space linear-depth acceptance band (hit thickness)
    float  Radius;                // view-space max ray length (world units)
    int    MaxSteps;              // march step budget PER RAY
    int    RayCount;              // hemisphere rays per pixel (1..4)
    float  YSign;                 // scene-NDC Y sign: -1 Vulkan (neg viewport), +1 Y-flip targets
    uint   FrameIndex;            // rotates the noise so temporal accumulation converges
};
PUSH_CONSTANT(SsgiPush, pc, space1);

float3 OctDecode(float2 e) {
    float3 n = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    float  t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
float3 ViewPos(float2 luv, float depth) {
    float2 ndc = float2(luv.x * 2.0 - 1.0, (luv.y * 2.0 - 1.0) * pc.YSign);
    float4 h = mul(float4(ndc, depth, 1.0), pc.InvProj);
    return h.xyz / h.w;
}
float2 ViewToLocal(float3 vp) {
    float2 ndc = float2(vp.x * pc.ProjXX, vp.y * pc.ProjYY) / max(-vp.z, 1e-4) - pc.Jitter;
    return float2(ndc.x * 0.5 + 0.5, ndc.y * pc.YSign * 0.5 + 0.5);
}
float2 LocalToFull(float2 luv) { return pc.VpMin + luv * pc.VpSize; }
float2 FullToLocal(float2 fuv) { return (fuv - pc.VpMin) / pc.VpSize; }
float Ign(float2 p) { return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715)))); }

// One depth-march along a view-space direction; returns the gathered radiance (0 on miss)
// and reports the hit in `hitOut`.
float3 MarchRay(float3 P, float3 D, float2 luv0start, float iz0, float jit, out float hitOut) {
    hitOut = 0.0;
    // Endpoint: the GI gather radius, clamped so a camera-facing ray stays past the near plane.
    float rayLen = pc.Radius;
    if (D.z > 1e-4) { rayLen = min(rayLen, max((-0.05 - P.z) / D.z, 0.0)); }
    if (rayLen <= 1e-4) { return float3(0.0, 0.0, 0.0); }
    float3 endVS = P + D * rayLen;

    float2 luv0 = luv0start;
    float2 luv1 = ViewToLocal(endVS);
    float  iz1 = 1.0 / max(-endVS.z, 1e-4);

    // Clamp the far end to the viewport box so every step lands on-screen.
    float2 d = luv1 - luv0;
    float2 dsgn = float2(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0);
    d = dsgn * max(abs(d), float2(1e-6, 1e-6));
    float2 tTo0 = (float2(0.0, 0.0) - luv0) / d;
    float2 tTo1 = (float2(1.0, 1.0) - luv0) / d;
    float2 tHi  = max(tTo0, tTo1);
    float  tExit = clamp(min(min(tHi.x, tHi.y), 1.0), 0.0, 1.0);
    luv1 = luv0 + (luv1 - luv0) * tExit;
    iz1  = lerp(iz0, iz1, tExit);

    bool  hit = false;
    float jHit = 0.0, jPrev = 0.0;
    [loop] for (int i = 1; i <= pc.MaxSteps; ++i) {
        float  j = (float(i) - jit) / float(pc.MaxSteps);
        float2 ls = lerp(luv0, luv1, j);
        if (any(ls < 0.0) || any(ls > 1.0)) { break; }
        float  sd = DepthTex.SampleLevel(PointSamp, LocalToFull(ls), 0).r;
        if (sd >= 1.0) { jPrev = j; continue; }
        float  rayLin  = 1.0 / lerp(iz0, iz1, j);
        float  surfLin = -ViewPos(ls, sd).z;
        float  dif = rayLin - surfLin;
        if (dif > 0.05 && dif < pc.Thickness) { hit = true; jHit = j; break; }
        jPrev = j;
    }
    if (!hit) { return float3(0.0, 0.0, 0.0); }

    // Two refine iterations are plenty for a diffuse gather (SSR uses four for mirror sharpness).
    float a = jPrev, b = jHit;
    [unroll] for (int r = 0; r < 2; ++r) {
        float  m = 0.5 * (a + b);
        float2 mls = lerp(luv0, luv1, m);
        float  mSurf = -ViewPos(mls, DepthTex.SampleLevel(PointSamp, LocalToFull(mls), 0).r).z;
        if (1.0 / lerp(iz0, iz1, m) - mSurf > 0.0) { b = m; } else { a = m; }
    }
    float2 hitLocal = lerp(luv0, luv1, b);
    hitOut = 1.0;
    return SceneTex.SampleLevel(LinearSamp, LocalToFull(hitLocal), 0).rgb;
}

// Trace outputs the GI buffer (rgb = one-bounce radiance estimate, a = hit fraction). The
// temporal accumulate + additive composite happen in ssgi_resolve.
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float depth = DepthTex.SampleLevel(PointSamp, uv, 0).r;
    if (depth >= 1.0) { return float4(0.0, 0.0, 0.0, 0.0); }   // background receives no GI

    float2 luv = FullToLocal(uv);
    float3 P = ViewPos(luv, depth);
    float3 N = OctDecode(NormalTex.SampleLevel(PointSamp, uv, 0).rg);
    float  iz0 = 1.0 / max(-P.z, 1e-4);

    // Tangent frame around the (view-space) normal for hemisphere sampling.
    float3 up = abs(N.z) < 0.98 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    // Frame-rotated noise (R2 sequence per frame + IGN per pixel): a still camera CONVERGES
    // under the temporal resolve instead of holding one fixed sample pattern.
    float  frameA = float(pc.FrameIndex & 255u);
    float2 rnd = frac(float2(Ign(pos.xy), Ign(pos.yx + 17.31)) +
                      frameA * float2(0.7548776662, 0.5698402909));

    float3 gi = float3(0.0, 0.0, 0.0);
    float  hits = 0.0;
    int rays = clamp(pc.RayCount, 1, 4);
    [loop] for (int rI = 0; rI < rays; ++rI) {
        // Stratified cosine-hemisphere sample (cosine-distributed directions make the plain
        // radiance average the correct diffuse estimator - no explicit NdotL weighting).
        float u1 = frac(rnd.x + float(rI) * 0.6180339887);
        float u2 = frac(rnd.y + float(rI) * 0.7548776662);
        float rr = sqrt(u1);
        float phi = 6.28318530718 * u2;
        float3 D = normalize(T * (rr * cos(phi)) + B * (rr * sin(phi)) + N * sqrt(1.0 - u1));

        float hit;
        float jit = frac(rnd.x + rnd.y + float(rI) * 0.3819660113);
        gi += MarchRay(P, D, luv, iz0, jit, hit);
        hits += hit;
    }
    float inv = 1.0 / float(rays);
    return float4(gi * inv, hits * inv);
}

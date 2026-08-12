# Reflection Probes - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/reflection-probes.md
> Track: [[reflection-probes-plan]]

NON-AUTHORITATIVE. The locked design behind the 2026-07 reflection-probe core. Present-tense truth is
`Systems/reflection-probes.md`; the full original doc (the per-face capture design, the probe-buffer
layout, the froxel-binning detail) is in git at the P0 commit 3b92560d. Kept for the "why".

## Locked decisions (2026-07-03)

1. **Capture**: runtime capture from the live scene + static caching (re-bake only when dirty).
2. **Selection**: cluster-assigned from the start - reuse the froxel `ClusterSystem` `SphereVsAABB`
   machinery to bin probes into froxels; the forward reads only its cluster's probes.
3. **Proxy**: box, with a blend-distance falloff for influence weighting (parallax = ray-vs-box).

## What was reused

`IBLSystem`'s GGX-prefilter + SH9 passes run per-probe unchanged (`DeclarePrefilter`,
`DeclareShProjection`); roughness->mip already `m/(mips-1)`, sample `R` at `roughness*maxLod`. A
persistent `HashMap<ProbeKey, slot>` with deferred (frames-in-flight) destruction of per-face capture
bind groups.

## Where it diverged (shipped)

The probe core shipped and was user-verified, but **froxel/cluster probe selection (decision 2) was
deferred**: the forward LOOPS the probe buffer directly (a direct forward path) rather than reading only
its froxel's assigned probes. Cluster assignment via the `ClusterSystem` machinery remains the intended
scale path; deferred because it is an optimization, not a blocker at current probe counts. SH9 diffuse
per-probe, an OBB (oriented) proxy, and a capture budget were also deferred.

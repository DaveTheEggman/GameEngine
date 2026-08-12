# Reflection Probes

> Status: CURRENT
> Verified: 2026-08-12 @ b07dab67
> Track: [[reflection-probes-plan]]

Local, parallax-corrected, blended cubemap reflections: per-region probes replace the single global IBL
specular so a reflective surface reflects ITS surroundings and the reflection tracks geometry as the
camera moves. The probe core shipped + user-verified; cluster/froxel probe selection is the one deferred
piece (the forward loops the probe buffer directly today).

## Shipped core

- **`ReflectionProbeComponent`** (`engine.render`) - a box-proxy probe with a blend-distance falloff for
  influence weighting; parallax is a ray-vs-box against the proxy.
- **`ReflectionProbeSystem`** - runtime capture from the live scene + static caching (re-bake only when
  dirty), round-robin across probes.
- **Per-probe IBL**: the `IBLSystem` GGX-prefilter passes run per-probe unchanged (roughness -> mip via
  `m/(mips-1)`; sample `R` at `roughness * maxLod`). Persistent probe slots with deferred (frames-in-
  flight) destruction of per-face capture bind groups.
- **Multi-probe blend**: the forward blends the nearest influencing probes by their box falloff weights,
  parallax-correcting each - beyond Sedulous (which has probes but no parallax, no blending, just
  per-camera nearest-probe with a CPU IBL view-swap).

## Deferred

- **Cluster/froxel probe selection.** The locked design was to bin probes into froxels via the
  `ClusterSystem` `SphereVsAABB` machinery so the forward reads only its cluster's probes; what shipped
  instead is a direct forward path that LOOPS the probe buffer. Cluster assignment is deferred (a scale
  optimization, not a blocker at current probe counts).
- **SH9 diffuse per-probe** (per-probe irradiance, not just specular), an **OBB proxy** (oriented box,
  vs today's AABB-aligned box), and a **capture budget** (filter the capture draw list / cap re-bakes/
  frame). All noted + deferred with the user.

---

The design rationale (the locked capture/selection/proxy decisions and the froxel-selection design) is
in `Documentation/Archive/reflection-probes-design-history.md`.

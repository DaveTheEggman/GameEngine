# Reflection Probes - design

Local, **parallax-corrected**, **cluster-assigned**, **blended** cubemap reflections. Replaces the single
global IBL specular reflection with per-region probes so a reflective surface reflects *its* surroundings and
the reflection tracks geometry as the camera moves. Goes beyond Sedulous (which has probes but no parallax, no
blending, no cluster assignment - just per-camera nearest-probe with a CPU IBL view-swap).

Status: DESIGN (locked decisions below). Not yet implemented. Do not commit this doc.

## Locked decisions (user, 2026-07-03)

1. **Capture:** runtime capture from the live scene + static caching (re-bake only when dirty).
2. **Selection:** cluster-assigned from the start - reuse the froxel `ClusterSystem` `SphereVsAABB` machinery to
   bin probes into froxels; the forward reads only its cluster's probes.
3. **Proxy:** box, with a blend-distance falloff for influence weighting (parallax = ray-vs-box).

## What we reuse (from the Draconic seam map)

- `IBLSystem`'s GGX-prefilter + SH9 passes run **per-probe** unchanged (`Render/IBLSystem.cppm`,
  `DeclarePrefilter`, `DeclareShProjection`). Roughness→mip already `m/(mips-1)`; sample `R` at `roughness*maxLod`.
- **Set-0 has room:** `t0–t7`/`s0–s1`/`b0` used; `t8+`, `s2+`, `b1+` free.
- **Render-to-cube-face exists** (subresource-range color targeting) for fullscreen env-gen; scene-into-cube is a
  small extension of `RenderView` (offscreen `targetTexture` + per-array-layer targeting - shadows already render
  into array layers).
- **Probes are view-independent** - captured from a fixed point → ONE probe set shared by all split-screen views
  (unlike per-view CSM). Capture happens once per dirty probe, not per view.
- **`ClusterSystem`** froxel build generalizes to probe assignment (a parallel probe index list).

## Data model

`ReflectionProbeComponent` (new component + manager, mirrors the light/shadow component pattern):

- `Transform` - position = capture center.
- **Influence/proxy box** - half-extents (OBB via probe rotation; AABB first, OBB later if needed). Used BOTH as
  the parallax proxy (ray-vs-box) and the influence volume.
- `blendDistance` - soft falloff width inward from the box edge (weight 1→0).
- `priority` - tie-break / layering across overlapping probes.
- `resolution` (e.g. 128) + roughness mip count.
- `update` - `ProbeUpdate { Static, Realtime, Manual }` (borrowed from Sedulous `UpdateMode`):
  - `Static`: capture 6 faces once + full GGX prefilter; cache until dirty (moved / explicitly invalidated).
  - `Realtime`: round-robin 1 face/frame (full cube every 6); cheap prefilter (irradiance + raw mip-0 specular).
  - `Manual`: (re)capture only when a dirty flag is set.
- `intensity` - multiplier.

Extracted into a probe list on `ExtractedScene` (like lights/local-shadow casters). Keyed by a stable
`ProbeKey` (entity id) → persistent GPU slot, so array slots survive frames and only dirty probes re-render.

## GPU resources - `ReflectionProbeSystem` (new, mirrors `IBLSystem`)

- **Prefiltered specular cube-ARRAY** `[maxProbes×6]` layers, 128², 5 mips, RGBA16F → set-0 `t8`
  (`TextureCubeArray`). Slice `probe.sliceBase` selects the probe.
- **Per-probe captured cube** (scratch, per probe or a small ring) - the raw 6-face render before prefilter.
- **Probe-metadata `StructuredBuffer`** → set-0 `t9`: `{ center, boxMin, boxMax, (rotation for OBB), blendDistance,
  priority, sliceBase, mipCount, intensity }` packed SoA/AoS.
- Reuse `EnvSampler` (`s1`). View UBO `b0` += `probeCount` / `probeMaxLod`.
- **Set-3 (cluster)** += `ClusterProbeOffsets` (`t2,space3`) + `ClusterProbeIndices` (`t3,space3`); layout grows
  2→4 entries. Written by a new compute pass mirroring light assignment.
- Persistent `HashMap<ProbeKey, slot>`; deferred (frames-in-flight) destruction of per-face capture bind groups.

## Capture

Order: **shadows → probe capture → main views** (so captures include shadowed direct lighting). Per dirty probe:

1. Acquire 6 face-cameras (90° FOV, ±X±Y±Z) from the probe center; each targets a face-view of the probe's
   captured cube slice (`RenderView.targetTexture` + subresource layer).
2. Render **sky + opaque + masked + direct lighting + GLOBAL IBL only** - **NO probe sampling** (feedback guard:
   bind the sky/global IBL during capture, never the probe array). Skip transparent + post.
3. When a full cube is ready (`facesCaptured >= 6`), prefilter into the probe's `t8` slices (+ SH9 later).

Cadence: `Static`/`Manual` bake-and-cache; `Realtime` round-robin 1 face/frame + cheap prefilter. Per-frame
**capture budget** (cap probes/faces captured per frame) to avoid spikes. Static invalidation = a signature over
the probe transform (like local-shadow static caching), refreshed across one frames-in-flight cycle.

### Capture gotchas to pre-empt (from Sedulous's comments)

- **Face orientation/mirror:** RH `LookAt` faces are horizontally mirrored vs the cube-sampling convention →
  flip on blit and keep face fwd/up vectors consistent with `CubeUVToDirection`. Compounds with our
  negative-viewport Y-flip - verify against a known scene.
- **Object cbuffer byte-identity:** the capture pass's object/instance cbuffer MUST be byte-identical to the main
  pass's (a size drift once fed garbage alpha → `discard` → vanished skinned meshes).
- Partial-cube **layout transitions** under round-robin (Undefined→RenderTarget→ShaderRead per face; depth flip).
- HDR RGBA16F end-to-end, no tonemap in the capture path.

## Selection + parallax + blend (forward ambient block, `MeshRenderer.cppm:496-514`)

Replace the raw global reflection (`R = reflect(-V,N)`; `PrefilterMap.SampleLevel(EnvSampler, R, …)`):

```hlsl
float3 R = reflect(-V, N);
float3 specAccum = 0; float wAccum = 0;
// cluster gives this fragment's probe list (set 3), like lights
for (probe in cluster.probes) {
    float w = InfluenceWeight(worldPos, probe.box, probe.blendDistance) * probe.priorityBias;
    if (w <= 0) continue;
    float3 Rp = ParallaxCorrectBox(R, worldPos, probe.center, probe.boxMin, probe.boxMax);
    float3 s  = PrefilterArray.SampleLevel(EnvSampler, float4(Rp, probe.sliceBase), roughness*probeMaxLod).rgb;
    specAccum += w * s; wAccum += w;
}
float3 probeSpec = (wAccum > 0) ? specAccum / wAccum : 0;
// remainder falls back to the global sky/IBL prefilter
float3 spec = lerp(globalPrefiltered, probeSpec, saturate(wAccum));
```

- `ParallaxCorrectBox` - standard box projection: intersect the reflection ray from `worldPos` with the probe
  box, take the hit point, aim from `probe.center` to the hit → corrected direction. (The headline over Sedulous.)
- `InfluenceWeight` - 1 inside, smooth falloff to 0 across `blendDistance` at the box edge.
- Multi-probe: normalize the weighted sum; `saturate(wAccum)` blends the probe result over the global IBL so
  fragments outside all volumes get the sky fallback with no seam.
- Diffuse: keep the global SH9 for now; per-probe SH9 diffuse is a later phase.

## Phases (green build clang+gcc each; commit per phase)

Status as of 2026-07-03 - the probe core is **DONE + user-verified**. What actually shipped diverged from the
original plan in one place: **froxel/cluster probe selection was deferred** (the forward loops the probe buffer
directly, which is fine for a handful of probes); cluster assignment moved to the P4c backlog below.

- **P0 - DONE** (1619743) - data model + `ReflectionProbeComponent`/manager + extraction.
- **P1 - DONE** (407ca8f/24c4661/d8495f4 capture, 67ab136 correctness) - `ReflectionProbeSystem` + cube-array/
  metadata + 6-face capture + flip-blit orientation + decoupled prefilter + feedback guard + per-face sky slots.
- **P2 - DONE** (83a4c21/67ab136) - bind set-0 `t8` + forward samples the probe (raw R) + blends over global IBL.
  (Built as a direct forward path, **not** the froxel-cluster selection - that's deferred, see P4c below.)
- **P3 - DONE** (1e7e71f) - parallax box correction (Lagarde), toggleable per-probe (ImGui + F6).
- **GGX prefilter - DONE** (a510163) - roughness convolution (mip chain) + Karis firefly reduction (6adee00).
- **P4a/b - DONE** (e96487d/e68b920) - multi-probe metadata SRV (`t9`) + forward loops probes + blendDistance
  falloff + round-robin capture cadence + static caching (dirty-tracked). Sandbox: 2 blended probes.

## Deferred / backlog (probe core is complete; these are extensions, not blockers)

- **P4c - cluster (froxel) probe assignment.** Bin probes into froxels via a compute pass (reuse `ClusterSystem`
  `SphereVsAABB`) so the forward reads only its cluster's probes (set-3 `t2/t3,space3`) instead of looping all.
  Pure **scalability** for many probes; with a handful the forward loop is already cheap.
- **P5 - per-probe SH9 diffuse.** Localized indirect *diffuse* GI from probes (reuse the IBL SH machinery); more
  visible quality than P4c. + **OBB proxies** (rotated probe boxes, vs the current world-AABB) + **dynamic
  capture budget / priority scheduling** (cap probes/faces captured per frame; the round-robin is the seed).
- **Self-reflection exclusion.** A single shared probe captures ALL geometry incl. the reflective objects, so
  each reflective object sees itself in the probe. Standard mitigation: skip flagged reflective meshes during
  probe capture (filter the capture draw list). Noted + deferred with the user.
- **Two-tier prefilter** (Sedulous gem): Realtime probes use cheap irradiance + raw mip-0 specular; Static get
  the full GGX. Optimization for many Realtime probes.

## Constraints

- Bind-group budget: probes fit set 0 (`t8`/`t9`, reuse `s1`) + set 3 (`t2`/`t3,space3`). No new SETS. No
  `SV_InstanceID`, no sync GPU readback on the main thread (WASM target).
- Capture reuses the multi-view + offscreen + per-array-layer machinery already proven by shadows/split-screen.

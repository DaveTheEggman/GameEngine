# Raptor Renderer — Design

Status: **IMPLEMENTED / done.** The renderer specified here is built and shipping in
`draconic.render` — the full §13 plan landed: extraction/view core, resource + submission
rebuild, RenderGraph passes (depth prepass + forward MRT), clustered lighting + PBR, CSM
shadows (cascades + spot + point + caching), IBL + sky, post (TAA/GTAO/bloom/tonemap/FXAA),
skinning, reflection probes, decals, debug draw, sprites, multi-view/multi-scene, plus SSR
and view-frustum culling. This document is the **design record** of what was built; it reads
in the present/future tense of its authoring but describes the delivered system.

**Future optimization & extension work has moved to [renderer-improvements.md](renderer-improvements.md)** —
the CPU→GPU-bound parity work (parallel command recording + draw-list sort, shadow instance
reuse), the deferred GPU-driven/indirect path, the spatial acceleration structure, and the
instanced-mesh (MultiMesh) primitive ([instanced-mesh.md](instanced-mesh.md)). Anything in the
sections below marked "designed in, build later" is tracked there, not here.

_(Historical framing preserved below. Original status: proposed design phase; the spinning-cube
thin slice was the throwaway that proved the foundation integrates.)_

Grounded in: the thin-slice ([[renderer-thin-slice]]), a critical assessment of
Sedulous.Renderer (quality/correctness/performance — not fit), and design references
from PlayCanvas (composition + uniform delivery) and Babylon-Lite (PBR depth,
fragment composition). Raptor already owns the load-bearing pieces: `raptor.rhi`
(explicit, bind groups + PSOs + dynamic rendering + barriers), `raptor.rendergraph`
(automatic barrier solving, transient aliasing, topo-sort + culling, tested),
`raptor.materials` (data-driven; `MaterialSystem` infers set-2 bind-group layouts; PSO
cache with version-poll hot reload), `raptor.shaders(.system)` (DXC + compile-time
`ShaderFlags` variants), `raptor.geometry` (GPU-ready mesh streams), and `raptor.scene`
(entities/transforms/components + `ISceneAware` injection).

---

## 1. Goal & non-goals

**Goal.** A clustered forward+ renderer that is *correct by construction* in its shading,
*extensible* (new renderable types and whole subsystems plug in without touching the
core), and *concurrency- and GPU-driven-ready* in its submission — so it scales past the
point where Sedulous.Renderer silently caps out.

**Scale target.** Tens of thousands of draws and hundreds of lights without falling off a
cliff or *silently dropping* work. (Sedulous is correct but tops out: single-thread
command recording, no GPU-driven path, hard caps that drop draws/shadows silently.)

**Non-goals (v1).** Deferred/visibility-buffer rendering, ray-traced GI, virtual
shadow maps, mesh shaders as the primary path. We design seams for these but don't build
them. Editor-specific rendering (gizmos/picking) is a thin pass, not a focus.

---

## 2. Principles

1. **The renderer is scene-agnostic.** Render data is *extracted and pushed*; the renderer
   never reads a scene/entity. One-way dependency: `raptor.render.subsystem` → `raptor.render`.
   (Already true in the slice; non-negotiable. Sedulous honors the same boundary.)
2. **Extensible by category, not by editing the core.** A renderable type contributes a
   `RenderData` subclass tagged with a `RenderCategory`, an extractor, a per-category
   `Renderer` (drawer), and optionally a `Pass`. The core sorts by category and dispatches.
   Proven by Sedulous's particles/sprites/decals/UI all plugging in from *separate libraries*.
3. **Keep the correct parts, rebuild the compromise layer.** The Sedulous shading/IBL/post
   math, the clustered-light compute, the CSM math, and the resource-pool patterns are
   genuinely good — port them close to verbatim. The submission frame loop, the
   pointer-identity batch cache, the shadow scheduler, and reflection probes are the
   compromise layer — rebuild them modern. (See §3 boundary.)
4. **Data-driven materials; custom materials need no renderer changes.** Materials declare
   properties → `MaterialSystem` infers the set-2 bind group; the renderer binds it as an
   opaque handle. A front-end/back-end shader split lets custom materials reuse the lighting
   back-end (PlayCanvas idea, serves the material-model decision already made).
5. **Concurrency- and GPU-driven-ready from the architecture, even if filled in later.**
   Double-buffered extraction (extract frame N while frame N-1 renders), parallel command
   recording (secondary/per-thread encoders), and an indirect-draw seam are designed in;
   the first implementation may be single-threaded/CPU-driven but must not *preclude* them.
6. **Barriers/transitions are the RenderGraph's job, not hand-rolled.** All passes declare
   reads/writes; `raptor.rendergraph` inserts barriers + aliases transients. No manual
   ping-pong state tracking (a documented bug-seam in Sedulous).
7. **No silent drops.** Every cap degrades gracefully (LRU/importance) and logs; never
   render-black-and-move-on (a class of bug Sedulous's comments admit it hit).
8. **Views are isolated; the renderer renders a *set of views* per frame, not "the frame."**
   Multiple scenes side-by-side (editor docked pages), the same scene from multiple cameras,
   and derived views (shadows, probe faces) are all the same mechanism. The ONLY state shared
   across views is the immutable per-scene extraction + shared GPU resources; everything
   mutable-per-frame is per-view. No view can trash another. (§9 is the full design; this fixes
   Sedulous's multi-scene-stomping class of bug by construction.)

---

## 3. The keep-vs-rebuild boundary (the heart of this design)

Earned from the critical assessment, not asserted. "Port" = translate Beef→C++23/Raptor
idioms close to verbatim. "Rebuild" = take the idea, write a modern implementation.

| Subsystem | Verdict | Why |
|---|---|---|
| **PBR/IBL/post shaders** (`forward.frag`, cluster, shadow, TAA, bloom, tonemap, prefilter/irradiance/BRDF-LUT) | **PORT** | Correct + modern: height-correlated Smith, energy-conserving, geometric specular AA, correct split-sum, ghosting-aware TAA, COD-grade bloom. Rewriting reproduces it. |
| **Clustered-light compute** (log-Z froxels, view-space AABB, GPU cull) | **PORT** (fix 32-cap + dead `CLUSTER_MAX_LIGHTS`) | Real GPU compute, correct math + NDC/Y-flip handling. |
| **CSM math** (practical split, sphere-fit cascades, cube-face seams) | **PORT** (ADD light-space texel snapping) | Fundamentals right; only missing the shimmer fix. |
| **GPU resource layer** (ring/dynamic UBO allocators, pooled bone/skinned-vertex storage, generation-handle deferred deletion, deferred bind-group destroy) | **PORT (pattern)** | Production-shaped; this is what prototypes lack. |
| **RenderGraph** | **ALREADY OURS** | `raptor.rendergraph` already ported + tested. |
| **Extraction data model** (`RenderData` + radix sort on inline 64-bit keys + category dispatch + provider boundary) | **PORT (model), REBUILD (frame-level)** | Model is excellent; single-buffering is the rebuild. |
| **Skinning** (GPU, pooled bones, prev-frame for motion vectors) | **PORT** | Complete + perf-aware. |
| **Submission frame loop** (extraction sync with sim, single-buffered, single-thread recording, no GPU-driven) | **REBUILD** | The core scale weakness — double-buffer + parallelize + indirect seam. |
| **Batch cache** (pointer-identity keys + retry loop) | **REBUILD** | Brittle under pool pointer reuse; use stable IDs. |
| **Shadow scheduler + atlas** (full re-render every frame, fixed 22-slot atlas, silent shadow drop) | **REBUILD** | Add static-shadow caching, importance/screen-size atlas, graceful fallback. |
| **Reflection probes** (nearest hard-swap, no parallax/blend) | **REBUILD** | Least-finished; add parallax-corrected box/sphere + blend weights. |

---

## 4. Architecture & module layout

Two libraries, one-way dependency, mirroring the slice but grown:

```
raptor.render            (SCENE-AGNOSTIC renderer core)
  :data        — RenderData base + RenderCategory + ExtractedScene (per-scene snapshot)
  :views       — RenderView (the isolation boundary) + RenderViewPool + view-list assembly
  :extract_ctx — RenderContext, FrameArena, per-worker arenas, double-buffer
  :resources   — GpuResourceManager (mesh/texture pools, bone pool), PerFrameResources (ring UBOs)
  :pipeline    — PassList (the pass-list TEMPLATE applied per view) + PipelinePass + Renderer
                 (per-category drawer) registry + the per-frame Frame (one RenderGraph, all views)
  :passes      — DepthPrepass, ForwardOpaque, ForwardTransparent, Sky, Decal, Post, Overlay
  :lighting    — ClusterSystem (per-view grids), LightBuffer
  :shadows     — ShadowSystem, ShadowAtlas, CSM matrices, ShadowScheduler
  :ibl         — IBLSystem (prefilter/irradiance/BRDF LUT), ProbeSystem
  :post        — TAA, SSAO, Bloom, FXAA, Tonemap
  :skinning    — BoneMatrixPool, SkinnedVertexPool
  deps: raptor.rhi, raptor.rendergraph, raptor.materials(+pso), raptor.shaders(.system),
        raptor.geometry, raptor.core

raptor.render.subsystem  (SCENE-COUPLED integration)
  :components  — MeshComponent, CameraComponent, LightComponent, ReflectionProbeComponent + managers
  :scene_system— RenderSceneSystem (per-scene SceneSystem; the parallel to Sedulous's
                 RenderSceneModule): holds the scene's ONE Environment (skybox, IBL source,
                 ambient, fog) + per-scene render config; coordinates the providers
  :extract     — providers: turn ComponentManagers + RenderSceneSystem → RenderData pushed in
  :subsystem   — RenderSubsystem (runtime::Subsystem + ISceneAware): owns the renderer,
                 injects the managers + the RenderSceneSystem, registers Renderers + Passes,
                 drives extract→render
  deps: raptor.render, raptor.scene, raptor.runtime, raptor.materials, raptor.geometry

  Source-of-truth split: things that are MANY-per-scene are components (mesh, camera, light,
  reflection probe); things that are ONE-per-scene live on the RenderSceneSystem (environment
  / sky / IBL / ambient / fog). If multi-environment-per-scene ever becomes real it promotes to
  a component, but 1-per-scene is the norm.
```

Other renderable subsystems (a future `raptor.particles`, the existing `raptor.vg` for UI)
depend on `raptor.render` only for the extension points (`RenderData`/`RenderCategory`/
`Renderer`/`PipelinePass`) — never on the scene. They register with the `RenderSubsystem`.

---

## 5. Extraction model

The contract between "the world" and "the GPU". Ported model, rebuilt frame-level.

- **`RenderData`** — base for a unit of renderable work, allocated from a per-frame
  **frame arena** (bump allocator), trivially destructible, valid one frame. Subclasses:
  `MeshRenderData`, `LightRenderData`, `DecalRenderData`, `SpriteRenderData`,
  (later) `ParticleRenderData`. Carries a `RenderCategory` + a precomputed 64-bit sort key.
- **`RenderCategory`** — a `u16` tag (Opaque, Masked, Transparent, Sky, Decal, Light,
  ReflectionProbe, GUI, Particle, …). The dispatch key.
- **Providers** (`raptor.render.subsystem`) read ComponentManagers and write `RenderData`
  into the frame's `ExtractedRenderData`. The renderer core defines the boundary
  (`IRenderDataProvider`-equivalent) and never sees a component.
- **Sort** — LSD **radix sort** over `{u64 key, RenderData*}` pairs, O(N), keys computed
  *inline during extraction* (material/PSO id in the high bits for opaque state-change
  minimization; view-space depth in the low bits, front-to-back opaque / back-to-front
  transparent — the PlayCanvas sort-key idea, also what Sedulous does).

**Extraction is per-SCENE, once per frame (not per-view).** Each active scene extracts once
into an immutable `ExtractedScene` (world-space renderables + lights + environment). Every
view of that scene — N cameras, shadow views, probe faces — shares that one snapshot read-only
and does its own per-view cull+sort against it. So "same scene from 5 cameras" extracts once
and renders 5 views; different scenes get different `ExtractedScene`s. Read-only-during-render
is what makes the snapshot safe to share across views *and* across worker threads. (§9.)

**The rebuild — double-buffered, decoupled extraction.** Sedulous extracts synchronously
inside the render call (sim↔render lockstep). We hold **two** `ExtractedScene` sets per scene
and extract frame N while the GPU submits frame N-1, so simulation and rendering overlap.
Extraction is parallel *within and across* providers and scenes via the job system (per-worker
arenas, single-threaded merge — Sedulous does the within-provider part well; we extend it
across providers, scenes, and shadow views).

---

## 6. Extensibility: categories + Renderers + Passes (the particles model)

The single most important architecture to adopt — validated by Sedulous's particles being
a separate 5k-LOC library that contributes draws with **zero** renderer-core changes. A
renderable type adds four pluggable pieces:

1. a **`RenderData` subclass** tagged with a `RenderCategory` (arena-allocated);
2. an **extractor** that fills it during extraction;
3. a **`Renderer`** (per-category drawer) registered with the `RenderSubsystem`
   (`GetSupportedCategories() → [Particle]`, records the draws for its category);
4. optionally a **`PipelinePass`** added to the pipeline to schedule it in the graph.

The core `Pipeline` sorts `RenderData` by category and dispatches each to its registered
`Renderer`. Meshes, sprites, decals, particles, and world-space UI all extend the renderer
identically, from outside. **This is the keeper architecture regardless of how much shading
code we port.** Our current flat `ExtractedView`/`Renderable` is the slice's simplification;
it gets replaced by `ExtractedRenderData` + categories.

### 6.1 Category definition: closed constants now, open registry later

`RenderCategory` is a `u16`, but the **set of categories is currently closed** — a static
list of named constants (`RenderCategories::Opaque … Particle`, `Count`-sized), ported
verbatim from Sedulous's `RenderCategories` static class. Sorting is likewise a central
`GetSortFunc`-style switch on the value. Adding a category means editing the core list + the
sort switch; a plugin **cannot** introduce one. This is deliberate for now: it's simpler, and
the ported passes + `Count`-sized arrays already assume a compile-time-constant, dense set.

The open alternative is **ezEngine's model**: categories are *registered* at runtime —
`RegisterCategory(name, sortKeyFunc) → Category` into a global table, looked up by hashed
name, with the **sort function stored per-category** (no central switch). Built-ins are just
pre-registered entries; plugins add their own without touching core. The cost is indirection:
the category value is no longer a compile-time constant (no `switch`, no static array sizing
by `Count`, name-hash lookups).

**Decision: keep the closed model** until a third-party renderer needs to introduce a
category without editing core. That's the trigger to migrate to the registry; the four
pluggable pieces above (RenderData/extractor/Renderer/Pass) already point that direction, so
the move is additive — replace the constant block + sort switch with a registry, leave the
extension points unchanged.

---

## 7. Frame loop & concurrency

**Threading model: a stackless work-stealing task/job system on a fixed worker pool —
NOT fibers. (Locked: WASM is a target.)** Fibers are off the table because stackful switching
on Emscripten/WASM needs Asyncify (heavy code bloat + slowdown) or the experimental Wasm
stack-switching/JSPI proposals (not production-universal) — even though desktop/mobile support
fibers fine (boost.context-style, not the deprecated `ucontext`). The task graph is also simply
the better fit: the renderer's parallelism is broad *data-parallel fan-out* (ParallelFor over
draws/components/shadow views) plus a few dependent stages, not the deep irregular
task-with-mid-task-waits graph fibers exist to serve. Normal threads work everywhere — WASM
via Web Workers + SharedArrayBuffer with three constraints we must honor in the design: a
**pre-sized worker pool**, the **main thread never blocks** (no indefinite joins/waits), and
cross-origin isolation. We evolve the existing `raptor.core` `JobSystem` (worker pool +
`WaitForAll`) into a work-stealing scheduler with `ParallelFor` + task dependencies. (Sedulous
went *fully* single-threaded for WASM, which is over-conservative — threads are fine on WASM;
only fibers and blocking-the-main-thread aren't.)

The rebuilt submission half (Sedulous's weakest area).

- **Frames-in-flight ring** (2–3): per-frame command pools, ring UBOs, deferred-destroy
  queues. (`raptor.runtime` GraphicsDevice already provides the frame ring + deferred
  destruction substrate.)
- **Double-buffered extraction** (§5): extract N ∥ submit N-1.
- **Parallel command recording** — passes that record many draws (forward opaque, shadows)
  split their draw list across worker threads recording into **per-thread encoders / secondary
  command buffers**, joined in pass order. Designed in from day one; the first cut may record
  single-threaded but the pass interface takes a thread-range. (Sedulous is 100% single-thread
  here — the hard scale wall.)
- **Deferred destruction** — generation-handle resources + a delay-N-frames free queue;
  bind groups destroyed at next BeginFrame, never while a command buffer references them.
- **No per-frame `WaitIdle`** on the hot path (only resize/shutdown). The slice already
  learned this (depth transition + shutdown WaitIdle).

---

## 8. GPU resource layer

Port Sedulous's patterns (the best part of it), on our RHI:

- **Ring/dynamic UBO allocators** — per-view (set 0) and per-object (set 2) data written into
  256-byte-aligned slots of a mapped `CpuToGpu` ring, bound with dynamic offsets. (Our slice's
  `ForwardRenderer` does a crude grow-the-UBO; this replaces it with a proper ring — the one
  thing the slice does worse than Sedulous.)
- **Pools** — a single large `BoneMatrixPool` (host-staging → device-local mirror copied once
  per frame), a skinned-vertex pool, mesh vertex/index sub-allocation. No per-mesh buffers.
- **`MeshGpuCache`** — keep (slice already has it), upgrade to sub-allocation from a pool.
- **PSO cache** — already ours (`raptor.materials.pso`, keyed by config × shader-variant ×
  RT-signature, version-poll hot reload). The renderer feeds it the assembled pipeline layout.
- **Descriptor lifetime** — persistent per-view/per-material bind groups updated in place where
  possible; avoid Sedulous's per-frame full frame-bind-group recreation.
- **Stable batch identity** — batches keyed by *stable IDs* (mesh resource id, material id),
  NOT pointer casts. Kills Sedulous's pointer-reuse hazard + the retry loop outright.

---

## 9. Views, scenes & the frame graph

The renderer renders a **set of views** per frame. A view is the unit of work: *render one
scene's extracted data, from one camera, into one target.* This single abstraction makes
multi-scene side-by-side (editor docked pages), multi-camera (split-screen / picture-in-picture
/ the same scene from N angles), and derived views (shadow cascades, probe faces) all the same
mechanism — and `RenderView` is the **isolation boundary** that guarantees views don't trash
each other. This is the explicit replacement for Sedulous's `Pipeline`/`ShadowPipeline`/
`ProbePipeline` god-objects.

**`RenderView`** (first-class data, pooled per frame from a `RenderViewPool`):
- `camera` (view+proj+frustum), `target` (an offscreen render target, or a backbuffer region),
  `scene` (→ the `ExtractedScene` it draws), `settings` (post config, layer mask, clear, viewport).
- per-view **transient** state, allocated from pools/rings *keyed by the view*, never shared:
  the culled+sorted draw list, the view-uniform ring slot (camera matrices), the **cluster grid**
  (clusters are view-space — per-view by construction), the shadow allocations, and the view's
  HDR/depth/normal/motion targets. **RenderView owns all per-frame-mutable render state.**

**`ExtractedScene`** (per-scene, once per frame, immutable — §5): world-space renderables +
lights + environment. Views of the same scene share it read-only. N cameras of one scene = one
extraction.

**The isolation rule (fixes Sedulous's multi-scene stomping class of bug by construction):**
the only state shared across views is (a) the immutable `ExtractedScene`(s) and (b) shared GPU
resources in `GpuResourceManager` (meshes/materials/textures — uploaded once, used everywhere).
*Everything* mutable-per-frame — culled lists, view uniforms, cluster grids, shadow atlas slots,
HDR/depth targets — is per-`RenderView`, from pools/rings. No mutable singleton is shared across
views. (Sedulous's bug was one cluster system shared across scenes; here clusters live on the
view.)

**One frame graph composes ALL views.** There is a single `raptor.rendergraph::RenderGraph` per
frame (a per-frame `Frame` object owns it). Each view — primary (cameras) and derived (shadow
cascades, probe faces) — contributes its **pass-group** into that one graph, targeting its own
resources. The graph schedules everything, inserts barriers, and **aliases transient targets
across views** (view A's HDR memory reused for view B if lifetimes don't overlap). There is **no
per-view driver object running its own mini-frame**: the "pipeline" is a **`PassList` template**
applied per view, the Shadow/Probe/Lighting **Systems contribute passes** for a view, and the
graph drives execution + barriers + aliasing. This is the decision that keeps the renderer from
re-growing the three-god-object shape.

Per-view pass-group (a `PassList` instance per view, each pass a graph node declaring its
targets/reads so barriers + aliasing are automatic):
```
ClusterBuild (compute, per-view) → [Shadow depth passes for this view's lights] →
DepthPrepass → [Decals] → ForwardOpaque (MRT: HDR, normals, motionvec) →
Sky → ForwardTransparent → Post (SSAO→Bloom→TAA→Tonemap→FXAA) → resolve to view target
```
MRT layout (color0 HDR, color1 packed normals, color2 motion vectors) follows Sedulous (feeds
SSAO/TAA). The graph owns the HDR/depth/normal/motion targets as transients with proper barriers
— **no hand-rolled ping-pong** (the manual SceneDepth state tracking is exactly the seam we let
the graph own). Shadow/probe captures are pass-groups in the *same* graph, not separate frames.

**View sources, assembled per frame:**
- *Primary views* = `CameraComponent`s across all active scenes (each camera names its target)
  **plus** explicit registrations (an editor docked page registers `view = {scene, camera, page
  target}`).
- *Derived views* are spawned by Systems for a primary view: `ShadowSystem` spawns shadow
  cascade/atlas views for the primary view's lights; `ProbeSystem` spawns probe-capture views
  (amortized — a few faces/frame). All are `RenderView`s over the same `ExtractedScene`, into
  different targets (shadow atlas, probe cubemap).

**Targets & presentation — decoupled.** Views render into **offscreen render targets**; window
presentation is a separate composite step: a window samples/blits the offscreen targets it
shows (editor: each docked panel samples its view's texture; game: the single view blits to the
backbuffer). A single-view-to-backbuffer **fast path** skips the offscreen RT. This is what makes
editor side-by-side, split-screen, and PiP fall out of one model, and it cleanly separates
"render N views" (one graph) from "present M windows" (the runtime's per-window job).

**View-independent shadow sharing (optimization, later):** point/spot shadow maps are light-space
(view-independent), so two views of the *same scene* can share them keyed by (scene, light);
directional CSM cascades fit the camera frustum and stay per-view. v1 is per-view-correct; sharing
view-independent shadows is a later optimization, never a correctness requirement.

---

## 10. Materials & shaders

- **Data-driven** — `MaterialSystem` infers the set-2 bind group from declared properties
  (already built); the renderer binds it opaquely. `PipelineConfig` drives render state;
  `ShaderFlags` drive compile-time variants via DXC; the PSO cache keys on both.
- **Front-end/back-end shader split** (PlayCanvas idea, serves the "custom materials without
  renderer changes" decision) — surface chunks *produce* `albedo/normal/roughness/metallic`;
  shared lighting chunks *consume* them. Custom materials swap the front end, reuse the
  clustered-lighting + IBL + shadow back end. Realized as shared `.hlsli` includes + the
  variant system, not a runtime string preprocessor (ours is compile-time, strictly better
  than PlayCanvas's runtime assembly).
- **Forward shader** — port Sedulous's `forward.frag.hlsl` (the assessed-correct PBR loop) as
  the standard surface→lighting path; adapt bindings to our set convention + TEXCOORDn vertex
  inputs (slice learned this) + `row_major` matrices (slice learned this).

---

## 11. Lighting & shadows

**Clustered, designed for MANY lights (port the math, scale the data path).** Port the
compute cluster build (16×16 tiles, log-Z slices, view-space AABB, GPU sphere-cull) and the
forward light loop, but design the data path for *many many lights* from the start:
- **Storage buffers throughout** (lights, per-cluster index lists) — drop the WebGL
  float-texture packing entirely; 32-bit light indices, per-cluster `(offset, count)` into a
  *compacted* global index list (not Sedulous's fixed 32-slot-per-cluster reservation).
- **Two-phase GPU culling** so cost isn't O(clusters × allLights): a coarse pass culls the
  global light set to the view frustum (and bins by depth) → only candidate lights reach the
  per-cluster assignment. This is where the GPU-driven seam (§7) pays off — light culling is a
  compute job, not a per-cluster loop over every light.
- Parameterized per-cluster cap with real overflow handling (priority/spill, never silent
  truncation); delete the dead `CLUSTER_MAX_LIGHTS`.

**Shadows (port math, rebuild scheduler + atlas).**
- Port: CSM practical-split + sphere-fit cascades, smooth cascade blend, cube-face point
  shadows with anti-seam FOV, the bias scheme. **Add light-space texel snapping** (the missing
  shimmer fix).
- Rebuild the **atlas** (importance/screen-size-driven slot sizing, LRU, **graceful fallback**
  instead of silent shadow disappearance) and the **scheduler** (static/dynamic split with
  **static-shadow caching** — render static casters once, re-render only dynamic — instead of
  Sedulous's full re-render-every-frame). This is the biggest perf win available.
- Filtering: start with PCF (port), seam in PCSS/contact-hardening later.

---

## 12. IBL, probes, post, skinning

- **IBL** — port split-sum (GGX prefilter, irradiance convolution, pre-baked BRDF LUT). Bump
  the conservative 256² env later; add roughness-aware specular occlusion as a refinement.
- **Reflection probes** — **rebuild**: parallax-corrected box/sphere projection + blend weights
  (Sedulous's `InfluenceRadius` hook exists but is unused), instead of nearest-probe hard-swap.
- **Post** — port the stack (TAA, SSAO, Bloom, FXAA) shaders; they're assessed correct. Adopt
  the refinements from the start since we're doing the most-correct thing: **YCoCg TAA
  neighborhood clamp** (better chroma-edge stability than RGB) and **blue-noise SSAO** (no
  hash banding).
- **Color management & tonemapping — the most-correct path, not ACES-approx.** Strict linear
  working space throughout (HDR render targets), exposure before tonemap, and a real filmic
  operator: **AgX** (Sobotka) as the default — it preserves hue and handles bright saturated
  colors without the hue-shift/desaturation artifacts of the Narkowicz ACES fit Sedulous uses;
  proper OETF for the display (sRGB now, with a clean seam for Display-P3 / HDR10 scRGB output
  later). Tonemap stays the last *color* step before FXAA's LDR luma pass. (This upgrades the
  one place the assessment flagged Sedulous as "the cheap approximation.")
- **Skinning** — port the GPU-skinning + pooled-bone design (incl. prev-frame bones for motion
  vectors). `raptor.geometry`'s `SkinnedMesh` (static stream + parallel skin stream) feeds it.

---

## 13. Phasing (each milestone shippable + tested on Null RHI, verified on Vulkan via Sandbox)

**All phases below are DELIVERED** (0–9, plus SSR and view-frustum culling added after). The
one item explicitly deferred — the spatial acceleration structure — moved to
[renderer-improvements.md](renderer-improvements.md); culling is still linear scans today.

0. **(done)** Thin slice: extraction boundary + forward draw of a cube.
1. **Extraction + view core**: replace `ExtractedView`/`Renderable` with per-scene
   `ExtractedScene` + `RenderData`/`RenderCategory` + radix sort + provider boundary + the
   `Renderer`/`Pass` registry, **and `RenderView` + the per-frame `Frame`/one-graph composition**
   (even the single camera is "one view" — the isolation boundary exists from day one). Mesh
   path, single-view→backbuffer fast path. (Establishes the keeper architecture + view isolation.)
2. **Resource + submission rebuild**: ring UBOs, mesh/bone pools, stable-id batching +
   instancing, frames-in-flight, double-buffered extraction, parallel-record seam.
3. **Pipeline + RenderGraph passes**: depth prepass + forward opaque/transparent + MRT via the
   graph (automatic barriers). Material set-2 binding (data-driven materials drive shading).
4. **Lighting**: clustered compute + light buffer + the ported forward PBR loop.
5. **Shadows**: CSM (with texel snap) + the rebuilt atlas/scheduler (static caching).
6. **IBL + sky**, then **post** (TAA/SSAO/bloom/tonemap/FXAA), then **skinning**. **(done)**
   - **spatial acceleration structure** (BVH/octree over `MeshRenderData` world bounds) — DEFERRED,
     moved to [renderer-improvements.md](renderer-improvements.md). Culling is linear scans today; the
     world center+radius on `MeshRenderData` are the ready-made index, and it drops in behind the
     existing `BuildDrawList` / `BuildShadowCasterList` cull seam when a profile demands it.
7. **Reflection probes (rebuilt)**, **decals**, **debug draw**. **(done)**
8. **Multi-view / multi-scene**: offscreen view targets + window composite, the view-list
   assembled from cameras across active scenes + explicit (editor) registrations. Proves
   side-by-side scenes and same-scene-multiple-cameras with no cross-view trashing. (The
   `RenderView` isolation built in phase 1 makes this additive, not a rearchitecture.)
9. **Extension proof**: a second renderable subsystem (sprites or particles) plugging in with no
   core change.

---

## 14. Decisions (resolved) + remaining open question

1. **GPU-driven path** — RESOLVED: **design the seam in now, build later.** Batches produce
   indirect-draw args; a compute cull job is droppable-in; the submission interface assumes it.
2. **Parallel command recording** — RESOLVED: **design in now.** Pass/record interfaces take a
   thread-range from day one; the first implementation may be single-threaded but the seam is
   load-bearing in the API.
3. **Port fidelity** — RESOLVED: the §3 keep/rebuild table stands (port shaders near-verbatim;
   rebuild shadow scheduler + probes + batch cache; keep our graph/materials/variants).
4. **Color/tonemapping** — RESOLVED: **most-correct path** — strict linear workspace, **AgX**
   tonemapper (hue-preserving, vs the ACES Narkowicz fit), YCoCg TAA clamp, blue-noise SSAO,
   clean seam for Display-P3 / HDR output. (See §12.)
5. **Lighting scale** — RESOLVED: **many many lights** — storage-buffer light data, compacted
   per-cluster index lists, two-phase GPU light culling (not O(clusters × allLights)). (See §11.)
6. **Transparency** — RESOLVED: sorted forward-blended v1; OIT later.
7. **Environment** — RESOLVED: **NOT a component** — it is one-per-scene, so it lives on the
   per-scene **RenderSceneSystem** (the Sedulous `RenderSceneModule` parallel). Many-per-scene
   render inputs stay components: `MeshComponent`, `CameraComponent`, `LightComponent`,
   `ReflectionProbeComponent`. (See §4.)

**WASM is a target (resolved).** Concurrency = stackless task graph, no fibers (§7). Sequencing:
the renderer is built **native-first**, then Emscripten bring-up proceeds **from core upward**
once the renderer lands. That makes WASM/WebGPU portability a *design constraint now* (§16) so
the bring-up is a port, not a rewrite.

---

## 16. WASM / WebGPU portability constraints (design-now, build-native-first)

The renderer is built and validated on Vulkan first, but WASM/WebGPU is a confirmed target
with bring-up to follow (core-upward). To keep that a *port*, the renderer must stay inside the
WebGPU-mappable subset from day one:

- **RHI feature subset.** Use only what WebGPU exposes: bind groups, render/compute pipelines,
  render passes (our dynamic-rendering usage maps to WebGPU passes), **dynamic-offset uniform
  buffers**, **storage buffers**, **compute**, **indirect draw**. Avoid geometry/tessellation
  shaders, and any bindless beyond WebGPU binding-array limits, in the core path. (Our RHI is
  already WebGPU-shaped, so this is mostly "don't reach for Vulkan-only extensions.")
- **Bind-group budget ≤ 4.** WebGPU guarantees only 4 bind groups. The set-frequency convention
  (view / material / object / pass) must fit in 4 — design the frequencies up front. (Sedulous's
  set0-frame / set2-material / set3-object fits; keep within that envelope.)
- **Shaders must cross-compile.** Author HLSL → SPIR-V via DXC for native; the same shaders must
  be WGSL-translatable (SPIR-V→WGSL via Tint, or a WGSL emit path the shader system gains at
  bring-up). Stay in the HLSL subset that cross-compiles — no constructs without a WGSL mapping.
- **Concurrency (§7):** pre-sized worker pool, main thread never blocks, no mid-frame thread
  spawn. The task graph is built to these from the start.
- **No synchronous GPU readback on the main thread** — picking/screenshot/occlusion results go
  through async readback (frames-in-flight latency), never a stall.
- **wasm32 address space** — 4 GB cap, 32-bit pointers by default; the large pools (bone/vertex)
  are fine but keep allocations bounded and avoid pointer-size assumptions.

These are cheap to honor while building native and ruinous to retrofit — hence design-now.

## 15. Bottom line

Not a wholesale port, not from-scratch. **Lift the assessed-correct, hard-to-get-right parts
(shading/IBL/post shaders, clustered compute, CSM math, resource-pool patterns) and the
extensibility model (category + RenderData + Renderer + Pass); rebuild the scale/concurrency
layer (frame loop, batching, shadow scheduler, probes) modern; keep our scene-agnostic
extraction boundary (cleaner than Sedulous's) and our already-superior compile-time materials
+ shader variants + PSO cache + RenderGraph.** The result is Sedulous's correctness without its
ceiling.

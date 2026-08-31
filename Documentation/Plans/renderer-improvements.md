# Renderer - Improvements & Optimizations

Status: **backlog.** The renderer itself is built and shipping ([renderer.md](renderer.md) is
the done design record). This document tracks the optimizations and extensions that remain -
what makes it *faster* and *broader*, not what makes it *exist*. Ordered roughly by value.

Grounded in a direct benchmark against Godot 4.7 (RTX 2060, same machine) and the profile data
from RenderStressTest / AnimStressTest.

---

## 1. CPU→GPU-bound parity (the priority)

### The finding

RenderStressTest was reproduced in Godot 4.7 (`p2` project). Same 2060, shadows on:

| shadows ON, RTX 2060 | count | frame | ms / 100k |
|---|--:|--:|--:|
| Godot MultiMesh | 184k | 37.04ms | 20.1 |
| Godot MeshInstance3D (per-node, auto-instanced) | 96k | 18.18ms | 18.9 |
| **Draconic** (per-entity) | 144k | 40.94ms | **28.4** |

Draconic is ~40% slower per sphere with shadows on - **but the GPU is fine.** At 144k the
render thread is **40ms CPU vs 29.6ms GPU** → CPU-bound. Godot's MultiMesh (O(1) CPU) and
per-node (O(N) CPU) run at basically the same speed, which means **Godot is GPU-bound in
both** - its per-frame CPU hides behind the GPU. Extrapolating Godot's per-node path to 144k
gives ~27ms, right next to Draconic's 29.6ms GPU. **If Draconic's CPU dropped below its GPU it
would be GPU-bound too, and land at parity (~20 ms/100k).**

So the whole gap is per-frame CPU exceeding GPU time. Three levers, all measured at 144k
shadows-on (from the P-dump):

### 1a. Parallel command recording + draw-list sort

- `AddView` / `BuildDrawList` is **single-threaded**: **6.47ms** to sort 144k draw items.
  §7 of renderer.md designed the parallel-record seam ("the pass interface takes a thread-range;
  first cut single-threaded") - it was never filled in. The sort is embarrassingly parallel
  (radix over independent keys); the pass record (forward, depth, each shadow cascade) splits
  the draw list across workers into per-thread encoders / secondary command buffers.
- Extraction is *already* job-parallel; the sort + record are the single-threaded holdouts.
- Godot parallelizes its cull across workers (`_scene_cull_threaded`). This is the direct analog.

### 1b. Shadow instance reuse - one buffer across all cascades

- `shadow.resolve` is **8.96ms** (far cascade 6.06ms) - the per-cascade instance-data fill.
  Each of the 4 cascades independently re-fills `InstanceData` (world matrices) for its survivors.
- **The right fix** is what Godot does and what [instanced-mesh.md](instanced-mesh.md) uses:
  build the caster instance data **once** into a buffer, and per cascade emit only a `DataOffsets`
  index list into it - never re-fill per cascade. The camera depth-prepass→forward already shares
  instance data (f63ea62); this extends the same idea to the shadow cascades.
- **Correction to the record:** the earlier `resolve-SoA` attempt (see `shadow-perf-investigation`
  memory) was reverted as a "wash," and that memory concludes "at the practical CPU floor." That
  is only true of *that* approach - it **relocated** the scattered `md->world` read (into a per-frame
  build + per-cascade world copy) instead of **eliminating** it. The real elimination is a
  persistent/shared instance buffer that the cascades index rather than rebuild. The cull side is
  already optimal (compact bounds SoA + inline frustum test, shipped 7f88b58: cull 10.6→1.9ms).

### 1c. De-duplicate the depth / forward / shadow fills

- `depth.prepass` record **6.14ms** + forward record **2.15ms** + Extract **4.40ms**. The
  prepass→forward sharing (f63ea62) already helps; the remaining double-touch is the shadow path
  (1b) and the fact that each pass still walks its own list. Once 1a + 1b land, this is mostly
  covered.

**Target:** get CPU record below the ~30ms GPU at 144k so the frame goes GPU-bound → ~20 ms/100k,
Godot parity. None of this needs new GPU work - the GPU is already competitive.

---

## 2. Instanced-mesh (MultiMesh) primitive - SHIPPED

The complement to §1: for **static** instanced content, a dedicated primitive whose per-frame
CPU is O(1) in the instance count (persistent GPU buffer, single-AABB cull, one draw shared
across camera + all cascades) - Godot-parity for static shadowed crowds, plus a skinned-crowd
edge Godot's MultiMesh lacks (per-instance bone base via `DataOffsets.y`).

**Done** - design record in **[instanced-mesh.md](instanced-mesh.md)** (§1-7 all shipped),
how-it-works + engine/VAT comparison in **[../Systems/instanced-mesh.md](../Systems/instanced-mesh.md)**. Static
`InstancedMeshComponent` + a persistent, N-buffered per-set instance buffer + a shared
`DataOffsets` ramp, drawn as one set across depth/forward/all CSM cascades with no per-frame
fill (§1-6). Skinned crowds via the `InstancedSkinning` companion - M shared pose palettes/
frame (instance i uses pose i%M), per-clip bucketing, per-instance tint, per-bone motion blur
(ping-pong prev pool), and an optional runtime mesh-merge (§7). Measured: 120k static spheres
shadows-off 47→60 fps (render CPU 20→1.3ms); ~5-6k skinned characters shadows-off, GPU-bound.
§1 (make the dynamic path GPU-bound) and §2 (give static content an O(1) path) are
complementary; §2 reused the shader, cull, and shadow-decoupling seams unchanged.

### Remaining MultiMesh optimizations (deferred)

- **Compact per-instance data.** The shipped `InstanceData` is full `World + PrevWorld + Tint`
  = 144B/instance; Godot stores a 3×4 affine (48B). Packing to 3×4 (or TRS) is a ~3× instance-
  buffer memory/upload/bandwidth win at the cost of a matrix rebuild in the vertex shader.
  Worth taking once a huge static set is bandwidth- or memory-bound.
- **Per-instance LOD selection.** A set draws one mesh for all instances. Distance-based LOD
  (swap mesh per instance or per sub-range) needs either multiple draws per set or GPU-side
  selection - pairs naturally with the GPU-driven path (§3).
- **Coarse spatial-split culling for huge sets.** A MultiMesh culls as one AABB (Godot-parity).
  Open-world-scale sets want per-cell sub-AABBs so an off-screen half culls, at the cost of
  per-cell CPU work - the MultiMesh face of the §4 spatial structure.
- **No-prepass mode for cheap-shading crowds.** The depth prepass (~6ms at crowd scale) can
  cost more than early-Z saves for simple-material crowds with little overdraw. Must stay a
  MODE (the prepass feeds SSAO + motion vectors), not a default.
- **Static shadow caching.** A static crowd re-renders into all four CSM cascades every
  frame; caching the cascade contributions for provably-static sets is the shadows-on win.
- General per-instance frustum culling / GPU compaction is the §3 (GPU-driven) endgame, not
  MultiMesh-specific.

---

## 3. GPU-driven / indirect-draw path

renderer.md §14 decision 1: the seam is designed (batches produce indirect-draw args; a compute
cull job is droppable-in; the submission interface assumes it), not built. This is the endgame
for both culling and submission cost - per-instance GPU culling + `DrawIndirectCount` removes the
CPU from the loop entirely. Build after §1 (parallel record) shows its ceiling; the two share the
"submission interface takes indirect args" assumption.

---

## 4. Spatial acceleration structure (BVH / octree)

From renderer.md §13.6, deferred on purpose. All culling is linear scans today: per-view frustum
cull O(objects/view), per-light shadow-caster cull O(casters×tiles). It drops in **behind the
existing cull seam** - replace the linear scans in `BuildDrawList` / `BuildShadowCasterList` /
the per-light sphere cull with a tree query; the `worldCenter`/`worldRadius` on `MeshRenderData`
are the ready-made index. Add it when a profile shows culling cost dominating (large object counts
× many views/lights → O(N×M)). Also unlocks spatial *queries*: picking rays, ray-traced
shadows/AO/reflections, overlap/nearest.

Note: for the RenderStressTest workload this is a **no-op** (everything is framed, nothing culls),
which is why §1 (record/sort/fill cost), not culling, is the lever there.

---

## 5. Shadow quality/perf knobs (shipped, tunable)

- **Shadow distance** is a runtime slider (`SetShadowDistance`, default 300) - the direct lever
  for shadowed-scene GPU cost (shrinks the far cascade, the dominant caster count).
- **Soft far-fade** (`SetShadowFarFade`, world-units) kills the diagonal coverage-boundary pop.
- Per-cascade frustum culling (e19d27b) + the cull SoA/inline test (7f88b58) are in.

Remaining shadow perf is either §1b (instance reuse) or accepting the GPU cost of a dense scene
at distance 300 (the far cascade's light-extended ortho legitimately sweeps a lot of ground).

---

## 6. Feature backlog (deferred, from the tracks so far)

- **SSR polish** - reflection-hit reproject, Hi-Z traversal, stochastic GGX (FidelityFX SSSR is
  the gold standard); de-double-count IBL under reflections; metallic tint. (Base SSR shipped.)
- **Transparency** - OIT (renderer.md §14 decision 6 shipped sorted-forward-blended v1).
- **Post** - additional passes as needed; the TAA/GTAO/bloom/tonemap/FXAA stack is in.
- **Reflection probes** - parallax/blend extensions (see reflection-probes.md / the probes track).
- **Surface-stack input** - viewport input layer extensions (see [[viewport-input]]).

---

## Ordering

1. **§1a parallel record + sort** - biggest, unblocks the dynamic path, uses the pre-designed seam.
2. **§1b shadow instance reuse** - kills `shadow.resolve`; shares mechanism with §2.
3. **§2 instanced-mesh primitive** - the static-crowd win; largely independent, reuses seams.
4. **§3 GPU-driven** - after §1 shows its ceiling.
5. **§4 spatial accel** - when culling cost shows in a profile (not the stress test).
6. **§6 features** - as product needs pull them.

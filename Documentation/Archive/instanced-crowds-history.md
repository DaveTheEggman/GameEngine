# Instanced Meshes & Animated Crowds (ARCHIVED 2026-09-01)

> Superseded: Documentation/Systems/instanced-mesh.md is the maintained
> how-it-works (and absorbed this doc's engine/VAT comparison as its §11-12);
> the future levers (§7 here) live in Plans/renderer-improvements.md §2. The
> code map below predates the Draconic/Foundation rename - paths are stale.

# Instanced Meshes & Animated Crowds

*How Draconic draws tens of thousands of static props — and thousands of animated characters —
in one draw call each, and why the CPU cost stops mattering.*

## TL;DR

Two primitives let a whole scatter of identical meshes (or a whole animated crowd) be treated as
**one render object** instead of N:

- **`InstancedMeshComponent`** (static "MultiMesh") — one shared mesh + material drawn at N
  transforms held in a **persistent GPU buffer**. Per-frame CPU is **O(1)** in the instance count.
- **`InstancedSkinning`** (skinned crowds) — the same, plus a **shared pose pool** of M skinning
  palettes so N animated instances cost **M** palette computes, not N.

The result is that CPU work **stops scaling with the instance/character count**, so the frame
becomes purely GPU-bound and the count you can hit rides whatever GPU you throw at it:

| Scene | Path | Result (RTX 2060 unless noted) |
|---|---|---|
| 120k static spheres, shadows off | per-entity | 21.1 ms (47 fps), **CPU-bound** (~20 ms CPU) |
| 120k static spheres, shadows off | MultiMesh | **16.7 ms (60 fps), GPU-bound** (~1.3 ms CPU) |
| Animated characters, shadows on | per-entity | ~1,683 @ 50 fps, **CPU-bound** |
| Animated characters (4 parts), shadows on | crowd | **2,350 @ 60 fps, GPU-bound** (~2 ms CPU) |
| Animated characters, shadows off | crowd | **~5,000 @ 60 fps** |
| Animated characters, shadows off (strong GPU) | crowd | **53,500 @ 59 fps**, total render CPU **~0.15 ms** |

At 53,500 animated characters the CPU spends **~3 nanoseconds per character** — extraction is 4.7 µs
and the draw-list sort is 2 µs, because the whole crowd is 4 render items, not 53,500 entities.

---

## 1. Why the naive path is CPU-bound

A "per-entity" renderer pays O(N) several times per frame for N objects:

1. **Extract** — walk N scene components into N `RenderData` snapshots.
2. **Sort** — radix-sort N draw items per view.
3. **Instance fill** — write N per-instance records (world matrix, tint, bone base) into a GPU
   buffer, **once per pass** — camera depth prepass, forward, and *every shadow cascade*.
4. **Animate** (skinned) — tick N `AnimationPlayer`s, each computing its own bone palette.

The draw *itself* is already cheap: Draconic batches a run of identical `(mesh, material)`
instances into one instanced draw (`ResolveInstanced`). But the O(N) machinery *around* the draw is
the wall. In the static stress test that machinery was ~20 ms at 120k; in the skinned test the
per-cascade instance fill (`shadow.resolve`) alone was ~9 ms — five passes each re-filling N
records. That is what caps the per-entity path.

The instanced primitives remove that machinery. They do **not** change the GPU work — the same
geometry is rasterized either way — they change how much CPU it takes to *issue* it.

---

## 2. Static instanced meshes (MultiMesh)

`InstancedMeshComponent` holds one mesh, one material, and an `Array<Matrix4>` of per-instance world
transforms. The whole set is drawn as one instanced draw, and the transforms live on the GPU
**persistently** instead of being re-filled every frame.

### The three moving parts

- **A persistent per-set instance buffer.** N `InstanceData{ world, prevWorld, tint }` records,
  uploaded to a GPU buffer **only when the set changes** (a monotonic `version` on the component).
  A static set uploads once and then costs nothing.
- **A shared `DataOffsets` ramp.** The vertex shader reads `Instances[DataOffsets.x]` (never
  `SV_InstanceID` — that is inconsistent across DX12/Vulkan). For a MultiMesh, `DataOffsets.x` is
  just the instance index `i`, so a **single** ramp buffer `[{0,…},{1,…},…]` serves *every* set.
- **One item through the pipeline.** Extraction emits **one** `MultiMeshRenderData` per set, with
  the merged AABB as its bounding sphere. So the draw-list builder culls and sorts *one* item, not
  N. The same persistent buffer is then bound for the depth prepass, the forward pass, **and all
  four shadow cascades** — no per-pass re-fill.

That last point is the big one for shadows: the ~9 ms of per-cascade instance fill collapses to
~0 because every pass reads the same immutable buffer.

### What the CPU does per frame

For a static set: **nothing** (after the one-time upload). Extract emits one item; the draw list
sorts one item; the passes bind an existing buffer. That is the O(1) claim, literally.

---

## 3. Skinned crowds (InstancedSkinning)

Animated characters can't share one immutable buffer — every character's bones move. The naive fix
(one bone palette per character) is back to O(N). The crowd trick is to make N instances **share M
poses**.

### The shared pose pool

`InstancedSkinning` (an animation-subsystem companion to the `InstancedMeshComponent`) samples the
clip at **M evenly-spaced phases** each frame into a pool of M skinning palettes, advancing on a
shared clock. Instance `i` uses pose `i % M`. So:

- The crowd is **spread across the clip** (M distinct phases — not lockstep), and
- The animation cost is **M palette computes per frame, independent of the crowd size**.

M defaults to 32. At 53,500 characters that is still 32 `SampleClip` + `ComputeSkinningMatrices`
calls per frame.

### How the GPU addresses it

The M palettes are uploaded into the engine's **shared bone pool** (the same buffer the per-entity
skinned path uses). Each instance's `DataOffsets.y` is set to its pose's base in that pool:

```
DataOffsets.y (current bone base) = poolBase + (i % M) * boneCount
```

Because the pool base moves each frame (it's ring-allocated), this per-instance offset buffer is
rebuilt each frame — but it's a **16-byte** record written once and shared across all passes, far
cheaper than the 144-byte `InstanceData` fill the per-entity path did five times.

### Per-bone motion blur

Motion vectors need each bone's *previous-frame* position. The pose pool is **ping-ponged**: last
frame's M palettes become this frame's "prev" pool (no re-sampling), both slabs are uploaded, and
`DataOffsets.z` points at the prev base. The skinned vertex shader deforms the mesh with both the
current and previous pose and emits the velocity — genuine per-bone motion blur, essentially free
(the extra cost is one buffer copy + doubling an O(M) upload).

---

## 4. The details that make it safe and complete

- **Frames-in-flight safety (N-buffering).** Any GPU buffer the CPU rewrites while the GPU still
  reads a previous frame is a hazard. The instance buffer uses **per-region bind groups** (one per
  frame-in-flight) with a "dirty for FiF frames after a change" upload — so a static set writes
  once and stops, while a per-frame-dynamic set writes only its own region each frame. The skinned
  offsets buffer is N-buffered by byte-offset region. Result: the whole path is hazard-free for
  static *or* per-frame-dynamic content. (A single-buffered offsets buffer was, briefly, a
  black-flicker bug — fixed by exactly this.)
- **Multi-part characters.** A character is usually several skinned meshes sharing one skeleton.
  Each part is its own `InstancedMeshComponent`; one `InstancedSkinning` feeds them all (a `targets`
  list) from the single pose pool.
- **Per-instance tint.** The `InstanceData` element already carried a tint, so the component gained
  an optional parallel `tints` array. Works for any instanced set, static or skinned.
- **Per-clip variety.** Mixing clips (walk/idle/run) is **app-level composition**: one
  `InstancedSkinning` per clip, each driving its own subset of the crowd. Cost stays
  **O(clips × M)** — 6 clips × 32 poses = 192 palettes/frame *regardless of count*. The engine
  primitive stays deliberately single-clip; the partition is a gameplay decision.

---

## 5. The cost model

This is the whole story in one table. N = instance/character count.

| Work | Per-entity path | Instanced (static) | Instanced (skinned crowd) |
|---|---|---|---|
| Extract | O(N) | **O(1)** | **O(1)** |
| Draw-list sort | O(N) | **O(1)** | **O(1)** |
| Cull | O(N) | **O(1)** (one merged AABB) | **O(1)** |
| Instance upload | O(N) × passes | O(1) (once, static) | O(1) positions + O(N)×16 B offsets (once, shared) |
| Animation | O(N) palettes | — | **O(M)** shared palettes |
| Draw calls | O(1) (already batched) | O(1) | O(1) per part |
| **GPU raster** | O(N) | O(N) | O(N) |

Everything CPU-side is O(1) or O(M) except a cheap 16-byte-per-instance offset write for skinned
crowds. Only the **GPU raster** is O(N) — and that's the point: once the CPU is out of the way, the
frame is limited purely by how much geometry the GPU can push.

---

## 6. Why the numbers come out the way they do

Read the profiler for **53,500 animated characters, shadows off** (a strong GPU):

```
Render.Extract      0.0047 ms      GPU:  forward        10.59 ms
Render.AddView      0.0020 ms            depth.prepass   6.02 ms
Compose.Execute     0.16   ms            (everything else < 0.05 ms)
total pass record   0.14   ms            GPU total       16.71 ms
Render.Acquire     16.29   ms  <- the render thread waiting on the GPU
```

The CPU issues 53,500 animated characters in **~0.15 ms** and then spends 16 ms *waiting for the
GPU*. The frame is 100% geometry raster (`forward` + `depth.prepass` = 214,000 skinned instances,
4 parts each). That is what "GPU-bound" means, and it's the goal: the count you hit is now a
function of the GPU, not the engine. On an RTX 2060 that's ~5,000 shadows-off; on a stronger card
53,500 — with **identical, negligible CPU** on both.

The static A/B is the same shape. At 120k spheres shadows-off, the per-entity path is CPU-bound
(~20 ms CPU: Extract 3.9, sort 6.0, record 8.2) while the GPU sits at ~15.8 ms; MultiMesh drops the
CPU to ~1.3 ms and the frame falls to the 15.8 ms GPU ceiling — 47 → 60 fps for *the same GPU work*.

---

## 7. Where the remaining cost is (and the next levers)

Since the frame is now pure GPU raster, the levers are GPU-side:

- **Skip the depth prepass for cheap-shading crowds.** The 6 ms prepass lays depth for early-Z, but
  for a simple-material crowd with little overdraw it can cost more than it saves. A "no prepass"
  mode would reclaim most of it (it feeds SSAO/motion vectors, so it's a mode, not a default).
- **Static directional-shadow caching.** With shadows on, the crowd re-renders into all four CSM
  cascades every frame. Static crowds could cache those and reuse them — the shadows-on win.
- **LOD / impostors / fewer parts.** Merge a 4-part character to one skinned mesh, drop distant
  characters to billboards — straight vertex-count reductions.

---

## 8. Relationship to other engines

Flax describes "storing skeleton bones in a global shared GPU buffer and batching the same skinned
meshes into a single draw call" — that is exactly Draconic's **existing per-entity skinned
instancing** (shared bone pool + per-instance bone range via `DataOffsets.y` + `ResolveInstanced`
batching), where every character animates independently (**O(N)** unique poses).

`InstancedSkinning` is the step *beyond* that for crowds: **O(M) shared poses + O(1) scene**. The
trade is explicit — Flax keeps 5,000 fully-independent actors at O(N) animation; Draconic's crowd
shares M poses at O(M) to reach far higher counts. You pick per-entity for a few dozen distinct
characters and `InstancedSkinning` for thousands of same-clip agents. (Godot's `MultiMesh` is the
static analogue; it has no per-instance skeleton, so its crowds need the technique below.)

---

## 9. vs. Vertex Animation Textures (the other main technique)

The classic technique for *massive* crowds (100k+) is **Vertex Animation Textures (VAT)**: bake each
frame of the animation into a texture (a texel holds a vertex's position or offset at that frame),
and in the vertex shader sample the texture at `(vertexID, phase)` to get the deformed vertex. There
are no bones and no skinning matrices at render time — per-vertex is one or two texture fetches, and
per-instance is just a phase offset.

VAT and the shared pose pool solve the *same* problem — collapse a crowd to one draw with
per-instance addressing — but sit at opposite ends of a trade-off:

| | Shared pose pool (Draconic) | Vertex Animation Textures |
|---|---|---|
| Animation compute | **O(M) on the CPU** (M palettes/frame) | **O(0)** — fully baked, GPU-only |
| Per-vertex GPU | 4-bone matrix blend | 1–2 texture fetches (cheaper) |
| Per-instance data | bone base (`i % M`) | a phase / frame offset |
| Memory | M × boneCount matrices (~100 KB) | verts × frames × channels (often large) |
| Runtime skeleton | **yes** — any clip, blends, IK, bone sockets | **no** — animation is baked |
| Blend / mix clips | pose lerp at runtime | multiple VATs + blend, or pre-baked |
| Authoring | none (uses the rig directly) | an offline bake per clip |

The shared pose pool is deliberately the **middle ground**: it gets VAT-like **CPU scaling** (O(M),
not O(N)) while keeping a **live skeleton**. That matters — a pooled crowd character can still blend
clips, run IK, or hold a weapon socketed to a bone, none of which a baked VAT supports without extra
machinery. The price is the per-vertex bone blend (a GPU cost) instead of a texture fetch — and
since the crowd is already GPU-bound, that is a deliberate *"spend GPU to keep flexibility"* choice.

**When would VAT win?** At the extreme end — 100k+ (or high-poly) characters where the per-vertex
cost dominates and the crowd genuinely needs no runtime flexibility (identical baked loops). Because
Draconic already addresses instances by a per-instance base + phase, a VAT-style path drops into the
*same* instanced draw (bake to a texture, sample instead of blending bones) — so it's a future
option for that regime, not a fork. In short: the pose pool covers "thousands of flexible agents"
without giving up the skeleton; VAT is the escape hatch for "hundreds of thousands of baked ones."

---

## 10. Code map

| Piece | Where |
|---|---|
| `MultiMeshRenderData` + `multiMesh` discriminator | `Code/Draconic/Render/RenderData.cppm` |
| `InstancedMeshComponent` (+ tint, pose pool fields) | `Code/Draconic/Render/Subsystem/Components.cppm` |
| Extraction (one item per set, merged AABB) | `Code/Draconic/Render/Subsystem/Extract.cppm` |
| Persistent buffer pool, N-buffering, skinned resolve | `Code/Draconic/Render/MeshRenderer.cppm` |
| `InstancedSkinning` companion (shared pose pool + prev) | `Code/Draconic/Animation/Subsystem/Components.cppm` |
| `AnimatedCrowd` sample (per-clip bucketing, tint toggle) | `Code/Samples/AnimatedCrowd/main.cpp` |
| `RenderStressTest` MultiMesh A/B (M key) | `Code/Samples/RenderStressTest/main.cpp` |

Design rationale and phasing: `docs/design/instanced-mesh.md`.

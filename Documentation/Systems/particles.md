# Particle / VFX System — Design

Status: **design (pre-implementation).** Synthesized from an 8-engine architecture survey
(ezEngine, SedulousEngine, FlaxEngine, LumixEngine, ogre-next ParticleFX/FX2, Godot 4,
PlayCanvas, Babylon.js). This is the "come up with the best" writeup + the recommended plan.

Grounded in Draconic's shipped substrate: the sprite/billboard renderer (blended forward pass,
additive, depth-sort — [[sprites-and-dynamic-categories]]), the instanced-mesh primitive
(persistent per-set `StructuredBuffer<InstanceData>` + `DataOffsets` addressing —
[[instanced-mesh]]), clustered forward+ lighting, the scene-agnostic extract→resolve→draw
pipeline, value-pool ECS (`ComponentManager<T>`), compute-capable RHI (Vulkan/DX12), the PSO
cache + shader hot-reload, and the **RTTI / reflection** system ([[reflect-core-types-for-scripting]]).

> **Note on scripting.** This design leans on the **RTTI/reflection** layer for module
> registration + param exposure — that layer is durable. The `Script` module builds on RTTI, and
> **Wren is one replaceable scripting backend** on top of `Script` that we're *not* committed to
> (may be dropped). So everywhere below, "reflection-registered" means the RTTI layer, backend-
> agnostic — not Wren specifically.

---

## 1. Decision (TL;DR)

Build a **GPU-compute-first** particle system whose **render path is the instanced-mesh
primitive we already ship** — particles are just another producer of `InstanceData`. Author it
with the **ez/Sedulous module model** (Emit → Init → Behavior → Finalize → Type) and
**parameterized curves + min/max ranges** (NOT a visual node graph for v1).

Concretely, a **two-phase strategy** the survey made obvious:

1. **Port Sedulous's CPU particle system** (~50 files, Beef → C++23 modules) near-verbatim. It
   already runs on Draconic's exact idioms (extract→resolve→draw, `ComponentManager<T>`,
   resource/material), already embodies the ez module taxonomy + curve authoring, and already
   ships the editor + `.particlefx` serializer. This gets a working, artist-authorable system
   fast and low-risk.
2. **Fill in the GPU-compute simulator Sedulous only stubbed.** Sedulous shipped the seam
   (`SimulationMode{CPU,GPU,Auto}`, per-behavior `BehaviorSupport{CPUOnly,GPUOnly,Both}`) but
   `GPUSimulator` falls back to CPU. Implement it using Godot/Flax's proven design: a per-effect
   compute update over a `ParticleData` SSBO → a small **copy/compact compute** that writes our
   existing `InstanceData` buffer → **indirect draw** with the GPU particle counter as
   `instanceCount`. CPU sim stays as the authoring reference + fallback for tiny/gameplay-reactive
   emitters; GPU is the ceiling-breaker.

This is where we **beat** most of the field: ez, Lumix, Ogre-FX2, and Sedulous are all
CPU-only; PlayCanvas is a WebGL fragment-texture hack; only Godot and Flax are modern
GPU-compute — and both draw through an instancing path we already have, *cleaner* (our
`DataOffsets`→`StructuredBuffer` beats their raw vec4-row transpose and avoids `SV_InstanceID`).

---

## 2. Cross-engine survey (compressed)

| Engine | Sim | Data | Authoring | Standout idea | For us |
|---|---|---|---|---|---|
| **ezEngine** | CPU, SoA, task/effect | named on-demand streams | visual editor, factories | **Emit→Init→Behavior→Finalize→Type** taxonomy; auto stream/finalizer dep resolution | module taxonomy backbone |
| **Sedulous** | CPU (GPU stubbed) | SoA `ParticleStreamId` | code/data/editor, RangeValue+Curve | faithful-ez on **Draconic's exact idioms** + a CPU/GPU **seam** | **port-of-least-resistance skeleton** |
| **Flax** | CPU **or** GPU from one graph | named AoS, demand-add, const-fold | Visject node graph | double-buffer + GPU counter + **indirect draw**; attribute access-mode tracking | concepts (skip transpiler+graph) |
| **Lumix** | CPU, **bytecode-VM over SoA** | 16 SoA channels | node graph + text script | compiled emit/update/**output** program; SoA→instance **transpose** | compiled-behavior idea (defer) |
| **Ogre FX2** | CPU **SIMD** | AoS→**SoA** rewrite | `.particle` scripts | def/instance split + **bounded FIFO pool**; **GPU vertex-pull** from packed buffer | pool discipline; validates our render path |
| **Godot 4** | **GPU compute** (+ CPU twin) | AoS `ParticleData` SSBO | process-material **generates shader** | **copy-compute → MultiMesh instance buffer**; phase-deterministic emission | **the GPU sim blueprint** |
| **PlayCanvas** | GPU **fragment** texture-hack | texture-encoded state | **dual-curve per property** + rnd-lerp | quantized curve→lookup; stateless id+seed RNG | authoring model only (skip exec) |
| **Babylon** | CPU + GPU (TF/compute) | CPU objects / GPU buffers | **gradient tracks** + sub-emitters | single `IParticleSystem` over CPU+GPU backends | interface seam; gradient authoring |

---

## 3. What every engine agrees on (the convergence)

1. **Def/descriptor ↔ instance split.** An authored asset (systems + emitters + modules) vs a
   lightweight per-entity runtime instance. Universal (ez, Sedulous, Flax, Godot, Ogre-FX2).
2. **Module pipeline in fixed phase order:** Emit → Init(once, per new particle) → Behavior/Update
   (per frame) → Finalize(integrate velocity+age) → Type(produce render data). ez/Sedulous are
   the reference; Flax (Spawn/Init/Update/Render) and Godot (process-material) are the same shape.
3. **Render is instanced, sim writes instance data.** Godot's copy-compute, Flax's indirect
   draw, Ogre-FX2's vertex-pull, Sedulous's per-frame instance buffer — all the same: *simulate
   points, emit per-instance records, draw them instanced.* **This is exactly our instanced-mesh
   path.** No new render infrastructure.
4. **Authoring = parameterized curves + min/max random ranges over normalized lifetime.** Godot
   (min/max + curve texture), Babylon (gradient tracks), PlayCanvas (dual-curve + rnd-lerp),
   Sedulous (`RangeFloat`/`RangeColor` + 8-key Hermite `ParticleCurve`). **Not a node graph** —
   that's a deferred offline baker, not the runtime model.
5. **Named, on-demand attribute channels.** A module declares the fields it needs; storage is
   allocated lazily so memory scales with feature use (ez, Sedulous, Flax, Lumix).
6. **Deterministic, stateless per-particle RNG** from `hash(id, seed)` — enables trails,
   fixed-seed replay, no stored RNG state (Godot, PlayCanvas, ez).
7. **Bounded pool + swap/compaction** on death; a full pool simply stops emitting (everyone;
   Ogre-FX2's bitset FIFO is the tightest).
8. **Sub-emitters via event routing / atomic-append** (birth/death → spawn into a child
   system, inheriting position/velocity/color) — ez, Sedulous, Godot, Babylon.

---

## 4. Recommended architecture for Draconic

### 4.1 Module split — runtime / resource / editor (the bake boundary)

Follow the established triad ([[textures-port]], geometry/materials/texture all do this): edit-time
**assets** are separate from cooked runtime **resources**, and an editor **builder** bakes one into
the other. **The runtime never carries edit-time data and is never queried by the editor** — the
bake decides how edit-time authoring becomes a runtime resource. Three modules:

- **`draconic.particles`** (runtime types): the module/behavior runtime classes, the simulator
  (CPU + GPU), the `ParticleEffectInstance`, and the extract/render integration. Consumes a *cooked*
  effect; knows nothing about curves-as-editable or authoring trees.
- **`draconic.particles.resource`**: the **cooked `ParticleEffectResource`** (output DB record) —
  flattened system list, each with an emitter + ordered module records (a stable **type-id** +
  baked params), **curves already sampled to fixed-size LUT arrays**, gradients baked to color
  LUTs, `RangeValue` min/max kept (runtime needs it for per-particle random), and GUID refs to
  cooked texture/mesh/material resources. No Hermite control points, no editor metadata. Plus a
  **factory** that instantiates the runtime `ParticleEffectInstance` (reconstructing modules by
  type-id from a runtime registry).
- **`draconic.particles.editor`**: `ParticleEffectAsset : editor::Asset` (edit-time: the system
  tree, editable Hermite curves / gradients / `RangeValue`s, module property trees surfaced via
  **reflection** for the inspector) + `ParticleEffectAssetBuilder : editor::DefaultAssetBuilder`
  whose `Build()` **is the bake** — sample curves→LUTs, bake gradients, resolve asset refs to
  cooked GUIDs, flatten the module tree to cooked records. The editor page lives here too.

Curve→LUT baking is a *feature*, not just a cook step: PlayCanvas/Godot quantize curves to lookup
textures at runtime; we do it **at cook time**, so the runtime resource (and the GPU compute sim)
just samples a baked array by normalized lifetime — no Hermite eval, no edit-time data, ever.

### 4.2 Runtime data model
- `ParticleEffectComponent` (value-pool ECS, thin handle → the cooked resource) → runtime
  `ParticleEffectInstance` (transform, time, spawn accumulators, live particle buffer(s)). Effect
  **outlives the component** (stop emitting → live until particles die → finisher reaps) —
  ez/Sedulous lifecycle.
- **CPU path:** SoA streams keyed by a `ParticleStreamId` (Position/Velocity/Age/Lifetime/Color/
  Size/Rotation/…/Custom), lazily allocated, swap-remove compaction — port Sedulous verbatim.
- **GPU path:** an AoS `ParticleData` SSBO (`xform`/`velocity`/`color`/`custom`/`userdata[]` à la
  Godot), double-buffered, version-stamped pool ([[bind-group-cache-versioning]]) — the named
  streams become named fields packed into the struct.

### 4.3 Module taxonomy (the behavior contract)

> **Port reconciliation (Phase 1 shipped):** the ez-style "Emit → Init → Behavior → Finalize →
> Type" is the *conceptual* pipeline, but Sedulous (and therefore our faithful port) collapses it:
> the module base classes are only **Initializer** (once per spawn) and **Behavior** (per frame).
> Emit is `ParticleEmitter` (spawn timing → a count), **Finalize** is a hardcoded
> `IntegrateVelocityAndAge` step on `ParticleSystem` (not a module), and **Type** is the
> `ParticleRenderMode` enum consumed by the render extractor (not a module). New behavior = an
> `Initializer`/`Behavior` subclass registered by string type-id. Emitter state reaches the
> Position/Velocity initializers via a `SetEmitterState` virtual hook (not an RTTI cast).
`Emit` (spawn count/timing) → `Init` (per-particle spawn state: position from emitter shape,
velocity, lifetime, color, size) → `Behavior` (per-frame: gravity, drag, wind, turbulence,
vortex, attractor, force, *-over-lifetime) → `Finalize` (integrate velocity + age; always last) →
`Type` (Billboard / Stretched / Mesh / Trail-Ribbon / **Light**). A behavior declares its required
streams and its `BehaviorSupport{CPUOnly,GPUOnly,Both}`. Adding a behavior touches both sides of
the bake: a **runtime** module subclass with a stable **type-id** in the runtime factory registry
(so the cooked resource's module records reconstruct — Sedulous's string-keyed registry idiom), and
its editable params surfaced **editor**-side via reflection for the inspector. The two are
independent — the runtime never needs the reflection metadata.

### 4.4 Simulation
- **CPU simulator** (phase 1): iterate behaviors over SoA streams, on the task-graph (batch
  across effects, not one-task-per-effect like ez — that won't scale). SIMD-friendly (Ogre-FX2's
  stateless `run(batch, dt)` shape).
- **GPU-compute simulator** (phase 2): per-effect (or per-material) generated **update compute
  shader** — splice behavior HLSL into a template and cache by key, exactly like our
  material→PSO story and Godot's process-material. Phase-based deterministic emission (no spawn
  atomics for the common case). Sub-emitters via atomic-append to a destination buffer.

### 4.5 Rendering — the key leverage
**Reuse what we shipped.** A **copy/compact compute** (Godot-style) reads the sim buffer and
writes our existing `InstanceData` (folding billboard/align/stretch in there), then:
- **Billboard/Stretched** particles → the **sprite/billboard** blended-forward path (additive +
  depth-sort already exist).
- **Mesh** particles → the **instanced-mesh** path directly (`DataOffsets`→`StructuredBuffer<InstanceData>`).
- **Light** particles → feed the **clustered-forward** light list (ez/Sedulous do this).
- Draw via **indirect** with the GPU particle counter as `instanceCount` (Flax/Godot). Keep our
  depth-sort for alpha-blended particles (Ogre-FX2 dropped sorting → additive-only; we don't have to).
- A `ParticlePass` after ForwardTransparent, depth bound read-only for **soft particles**
  (Sedulous). Particles are just render-data items with a `rendererId` — no bespoke pass plumbing.

### 4.6 Authoring (editor-side only)
All authoring lives in `draconic.particles.editor`, never the runtime. The `ParticleEffectAsset`
holds the edit-time value primitives — `RangeValue<T>` (min/max random), Hermite **curves**, color
**gradients** — plus the system/module tree; port Sedulous's `RangeFloat/RangeColor/ParticleCurve`
and its editor page (tree + inspector + curve/gradient controls + live preview). Module editable
params are surfaced through **reflection** for the inspector. The `Build()` bake then samples curves
→ LUTs, bakes gradients, resolves refs, and flattens the tree into the cooked `ParticleEffectResource`
— so `RangeValue`s survive (runtime random needs them) but Hermite control points do not. Iteration
loop: edit asset → rebuild (re-bake) → the resource system hot-swaps the cooked resource in place,
preserving live particles (config-diff reinit of only changed cooked modules). Runtime hot-reload
reacts to a *new cooked resource*, not to edit-time edits directly.

---

## 5. Non-goals (v1)

- **Visual node graph** (Flax Visject, Lumix graph). Huge; the runtime is param+curve driven.
  A graph is a later *offline baker* to the same param format (Babylon's Node Particle Editor model).
- **CPU-or-GPU-from-one-source transpiler** (Flax). Every report flagged it as the heaviest
  piece; Godot's alternative — maintaining two full sims — is itself a warned-against maintenance
  tax. We instead share the *authoring model + module contract*, and write the two sim backends
  behind the `BehaviorSupport` seam, per-behavior, only as needed.
- **GPU bitonic sort** initially (lean additive / CPU depth-sort we already have).
- **SDF/heightfield collision, volumetric-fog particles, GPU global-illumination hooks** — Flax/Godot
  extras; defer.
- **Lumix's bytecode-VM.** Elegant, but the module-composition model ships sooner and the
  generated-compute-shader path gives the same "artist behavior, no per-effect C++" without a VM.

---

## 6. Phasing

1. **Runtime core + cooked-resource shape** (`draconic.particles` + `.resource`) — runtime
   Effect/System/Emitter/Instance, `ParticleStreamId` SoA container, Initializer/Behavior/Finalizer
   split + runtime type-id registry, `RangeValue`, **baked** curve/gradient LUT sampling, LOD, and
   the cooked `ParticleEffectResource` + factory. Bootstrap with a **hand-cooked resource built in
   code** (no editor yet) to drive tests — the same way other systems came up before their editors.
2. **Wire rendering onto existing paths** — Billboard type → sprite forward pass; Mesh type →
   instanced-mesh; `ParticlePass` (soft-particle depth read). One draw per material/blend bucket.
3. **Component + extract** — `ParticleEffectComponent` + `ComponentManager`, extract →
   `ParticleRenderData`, resolve to draws. First particles on screen (driven by the code-cooked resource).
4. **Editor asset + bake** (`draconic.particles.editor`) — `ParticleEffectAsset` (system tree,
   editable Hermite curves / gradients / `RangeValue`, reflection inspector) +
   `ParticleEffectAssetBuilder::Build()` cooking asset → `ParticleEffectResource` (sample curves→LUTs,
   bake gradients, resolve refs, flatten tree). Editor page + live preview. Now artists author; the
   code-cooked resource from phase 1 is retired.
5. **Sub-emitters, Trail/Ribbon, Light type** — event routing; ribbon ring-buffers; light-list feed.
6. **GPU-compute simulator** — `ParticleData` SSBO (double-buffered, versioned pool), generated
   update compute (template + key cache), copy/compact compute → `InstanceData`, indirect draw
   with counter. Flip heavy emitters to `SimulationMode::GPU` behind the seam.
7. *(Later)* GPU sort, collision/attractor SDF, node-graph baker (an editor-side authoring frontend
   that still bakes to the same cooked resource).

Phases 1–5 are a Sedulous port **adapted to Draconic's runtime/resource/editor triad** — Sedulous
combined authoring + runtime, so the port splits its model across the runtime (1–3, 5) and the
editor asset + bake (4); still mostly mechanical. Phase 6 is the net-new "beat the field" work,
de-risked by Godot/Flax's blueprint and our existing instancing + compute.

---

## 7. Resolved decisions

- **Particle data layout → SoA on BOTH CPU and GPU** (not the SoA-CPU/AoS-GPU split the survey
  first suggested). The CPU and GPU simulators are alternative backends behind the `BehaviorSupport`
  seam — a given effect runs on exactly ONE, so there is never a per-frame CPU↔GPU state
  translation. Make both SoA: named, demand-allocated attribute buffers (CPU stream pointers / one
  GPU SSBO per active attribute), keyed by `ParticleStreamId`. This keeps **behaviors
  backend-symmetric** — a behavior reads/writes the same named attributes whichever path runs it,
  which is the single biggest maintainability win when shipping two simulators — and SoA also
  coalesces cleanly for the GPU update pass. The only "pack" is at *output*: the Type stage (CPU) /
  copy-compact (GPU) writes the shared `InstanceData`. Layout is internal, behind that output
  contract, so AoS-GPU (Godot/Flax) stays a drop-in fallback if managing N attribute buffers proves
  painful — but SoA is the default, for symmetry.

- **Generated update compute → cache by module-composition signature**, not per-effect or
  per-material. The generated shader depends only on WHICH behaviors run (ordered) + their codegen
  flags (which attributes touched), never on their param VALUES (those are uniforms + baked LUTs).
  Key the compute-PSO cache by an ordered `(behavior type-id, codegen-flags)` hash, so every effect
  that shares a behavior stack shares one compute shader. Maximises sharing beyond Godot
  (per-material) and Flax (per-emitter); mirrors our existing PSO cache.

- **Instance record → start with the full `InstanceData`** for both mesh and billboard particles.
  Uniform with the instanced-mesh path = zero new render infra (mesh particles literally reuse its
  resolve; `PrevWorld` gives particles TAA/motion-blur vectors for free). A packed billboard record
  (pos+size+rot+color, expanded in the VS) is a bandwidth optimization deferred until particle
  bandwidth actually dominates — and folded into the shared instanced-mesh 3×4-compaction item
  (renderer-improvements.md §2) so both paths benefit. Not built speculatively.

- **Copy/compact → particle-specific compute writing into the shared `InstanceData` buffer.** It
  reads particle attributes and applies billboard/align/stretch — logic a static-scatter path
  wouldn't have — so build it particle-specific first. But hold its *output* half to a minimal
  contract ("write `InstanceData[slot]` + bump the indirect count") so a future GPU-driven static-
  scatter path (renderer-improvements.md §3) can share that half. Extract the generic seam on the
  SECOND use, not speculatively on the first.

---

## 8. Taken from Sedulous vs owned / improved

The port is **not wholesale**. Sedulous bundled authoring + runtime in one CPU library; we split it
across the triad (§4.1) and replace the parts our shipped substrate already does better. Be explicit
about the line so the port doesn't drag in what we mean to own.

| Concern | Port/adapt from Sedulous | We own / improve |
|---|---|---|
| **Object model** | Effect → System → Emitter → Instance four-tier; effect-outlives-component + finisher reaping | adopt as-is |
| **Module taxonomy** | Initializer/Behavior/Finalizer split; `ParticleStreamId` stream-declaration; the concrete modules (emitter shapes; gravity/drag/wind/turbulence/vortex/attractor/force; *-over-lifetime) | **backend-symmetric SoA** so each module runs CPU *or* GPU behind `BehaviorSupport` (Sedulous only ran CPU) |
| **Simulation** | CPU simulator + swap-remove compaction | the **real GPU-compute simulator** Sedulous only stubbed (generated compute keyed by composition hash, double-buffered SSBOs, phase-deterministic emission, atomic-append sub-emitters, indirect draw w/ counter); **task-graph batched** across effects (Sedulous was single-threaded) |
| **Rendering** | render TYPES (Billboard/Stretched/H/V/Mesh/Trail); soft-particle `ParticlePass` after ForwardTransparent | draw through the **shipped instanced-mesh path** (persistent `InstanceData` + `DataOffsets`, N-buffered, shared across passes) instead of Sedulous's per-frame `ParticleRenderer`; **Light-type feeds our clustered-forward** light list |
| **Authoring primitives** | `RangeValue` (RangeFloat/RangeColor); 8-key Hermite `ParticleCurve`; gradients; sub-emitter event routing (inherit pos/vel/color + probability); per-system LOD; editor page shape (tree/inspector/curve/gradient/preview) | **reflection-driven inspector** (Draconic reflection, not Sedulous `[Property]`); **curves baked to LUTs at cook** |
| **Asset ↔ runtime** | the `.particlefx` authoring-data concepts | the **runtime/resource/editor triad + bake** (Sedulous had no source/product split): `editor::Asset` + `Build()` → cooked `ParticleEffectResource` carrying no edit-time data |
| **Type registry** | string/id-keyed registry for tolerant deserialization | **split** into a runtime type-id→factory (reconstruct cooked modules) vs editor reflection (inspector) |

**Explicitly NOT ported:** the vestigial `GPUSimulator` stub (we port the *seam*, `BehaviorSupport`,
and build the real thing); Sedulous's per-frame CPU `ParticleRenderer` instance-buffer packing (use
the instanced-mesh path); its combined asset+runtime serialization (replaced by the bake); and
CPU-sort-in-the-extractor as the *only* sort (we keep the renderer's depth-sort, add GPU sort later).

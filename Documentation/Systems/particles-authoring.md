# Particle authoring + cooked runtime resource

Companion to [particles.md](particles.md). That doc set the direction (§4.1 bake boundary, §4.6
authoring, §6 phasing). This doc is the **implementation-ready design** for the two tiers that turn
the shipped CPU runtime into an artist-authorable, cooked-at-build asset:

- **`draconic.particles.resource`** — the cooked `ParticleEffectResource` (runtime input), its
  factory + resource manager, content-DB GUID keying, and the (bidirectional) serializer.
- **`draconic.particles.editor`** — `ParticleEffectAsset : editor::Asset` (edit-time),
  `ParticleEffectAssetBuilder : editor::DefaultAssetBuilder` whose `Build()` **is the bake**, and the
  editor page (tree + reflection inspector + curve/gradient controls + live preview).

The shipped `draconic.particles` (CPU sim + all modules) and `draconic.particles.subsystem` (render
integration) are the **runtime** and stay as-is except for the small serialize/type-id hooks in §6.

> **Status of the runtime going in:** CPU sim + render integration are complete and at
> Sedulous-parity-or-better (all render/blend modes, shapes incl. Arc, trails, soft particles,
> collision planes/spheres/boxes, local space, flipbook, looping/duration/prewarm, seeded RNG,
> sub-emitter inheritance, per-particle sort, LOD, profiler). Only the GPU-compute sim (Phase 6) is
> deferred, and it's independent of authoring.

---

## 0. Status — IMPLEMENTED (reconciled with the build)

The triad is built, tested, and demoed. Two design choices changed during implementation from the
first draft below; the affected sections (§2.3, §6, §8) are updated to match, and this summary is
authoritative where it differs from older prose:

- **Modules are reflected `ISerializable`s, not hand-rolled.** The initializer/behavior base classes
  derive `ISerializable` (`DRACONIC_OBJECT`); each concrete module has `DRACONIC_DEFINE_OBJECT` + an
  overridden `Serialize(ISerializer&)`. Reflection supplies the type-id (`GetType()->id`), polymorphic
  reconstruction (`GlobalSerializableRegistry().Create(id)`), and the serialize hook — the same
  machinery `TextureResource` uses. The planned bespoke `TypeId()` virtual + `CreateInitializer/
  CreateBehavior` string registry were **removed**. Module storage moved `UniquePtr → RefPtr`
  (`ISerializable` is `RefCounted`). Trivial value types (ranges/curves/shapes/settings/collider
  structs) keep plain free `Serialize` overloads.
- **Ref resolution + hot-reload Proxies are done, not deferred.** `ParticleSystem` carries an opaque
  `Guid textureRef` (Sedulous-style ref-on-the-system); the editor stores a per-system asset **path**;
  `Build()` resolves path → cooked GUID (via the content DB, added to `editor::AssetBuildContext`); the
  factory binds each to a `Proxy<Texture>` on the resource (`SystemTexture(i)`), recording the
  dependency edge. Mesh/material refs follow the same pattern when needed.

Proof: `ParticleEffectFactoryTests` (cook→Bind→reconstruct→simulate), `ParticlePipelineTests`
(authored asset → `Build` → cook → Bind → run, **+ texture path → GUID → bound Proxy**), the component
`SetEffect(Proxy<ParticleEffectResource>)` path, and a live `ParticleFX` cell (author → bake →
`Data/Output/ParticleFX/*.rasset` → load → render). Still deferred: LUT curve baking (§1.1) and the
visual editor page/inspector (§4.3).

---

## 1. The bake boundary — what's edit-time vs cooked

Principle (from particles.md §4.1): **the runtime never carries edit-time data, and the editor never
queries the runtime.** For particles the gap is thin — the runtime `ParticleEffect` is already a
compact, evaluatable structure (fixed-size Hermite curves, `RangeValue` min/max, enums, module
trees). So the split is mostly about **references** and **editor metadata**, not the value model.

| Data | Edit-time asset | Cooked resource |
|------|-----------------|-----------------|
| System/module tree, emitter, enums | ✅ (authored) | ✅ (flattened records) |
| `RangeValue` (min/max) | ✅ | ✅ — runtime needs it for per-particle random |
| Hermite curves / gradients | ✅ (editable control points) | see **LUT decision** below |
| Texture / mesh / material refs | by **asset path / soft ref** | by **cooked content-DB GUID** (resolved at bake) |
| Editor UI state (tree expansion, selection, preview cam) | ✅ | ❌ never |
| Module **type-id** (string) | ✅ | ✅ (reconstruct via registry) |

### 1.1 LUT decision (curves)

particles.md §4.1/§4.6 proposed baking curves → fixed-size LUT arrays at cook time (so the runtime —
and later the GPU sim — samples an array by normalized lifetime instead of doing Hermite eval).

**v1 decision: keep curves as-is in the cooked resource; defer LUT baking.** Rationale:
- The CPU runtime already Hermite-evals `ParticleCurveFloat/Color/Vector2` (8 keys) cheaply, per the
  shipped over-lifetime behaviors. Baking to LUTs now would mean **changing those behaviors** to
  sample LUTs — runtime churn for no CPU win.
- The cooked curve (8 keys + tangents) is *evaluatable runtime data*, not editor metadata (no undo
  history, no UI state) — so keeping it does not violate "no edit-time data in the runtime."
- LUT baking is a **GPU-sim enabler** (a compute shader wants a texture/array, not a Hermite eval).
  Do it in Phase 6 as a cook option, when there's a consumer. The cooked-resource format reserves a
  `curveEncoding { Hermite | LUT }` tag so adding LUTs later is additive, not a format break.

So the **cook is mostly: resolve refs (path → GUID) + flatten the tree to serializable records.** The
value model is shared verbatim between asset and resource.

---

## 2. `draconic.particles.resource` (cooked)

### 2.1 `ParticleEffectResource`

A cooked, immutable runtime record. Shape (mirrors the runtime `ParticleEffect` minus edit metadata):

```
ParticleEffectResource
  name
  systems[]:                       // per ParticleSystem
    maxParticles, seed
    simulationMode, simulationSpace, blendMode, renderMode
    sortParticles, softParticles, softDistance
    trail (TrailSettings), flipbook (FlipbookSettings), prewarmTime, LOD(3)
    emitter (mode, rate, burst*, duration, looping)
    initializers[]:  { typeId (string), params-blob }
    behaviors[]:     { typeId (string), params-blob }
    curveEncoding:   Hermite (v1) | LUT (later)
    meshRef, materialRef, textureRef: cooked GUIDs (resolved at bake; runtime-null-safe)
  links[]: SubEmitterLink
  fileVersion
```

This is deliberately the runtime types (`TrailSettings`, `FlipbookSettings`, `EmissionShape`,
`RangeValue`, `ParticleCurve*`) — no parallel "cooked" duplicates. The only cooked-specific fields
are the **resolved GUID refs** and the `curveEncoding` tag.

### 2.2 Serializer — port Sedulous `ParticleEffectSerializer` onto `ISerializer`

The engine's `core::ISerializer` is exactly Sedulous's model: **mode-aware, format-agnostic, one
`Serialize()` runs both directions** (`Mode() == Read|Write`, `Key/BeginObject/BeginArray/Scalar/
Text/Blob/GuidValue`). `ParticleEffectResource` derives `ISerializable` (`virtual void
Serialize(ISerializer&)`), and field-level work uses the free helper `core::Serialize(ar, "key",
value)`. We already have the string factories (`CreateInitializer`/`CreateBehavior` in `Modules.cppm`)
— the read half of the type registry.

Plan: `ParticleEffectResource::Serialize` ported near-verbatim from `ParticleEffectSerializer.bf`:
- name → systemCount (`BeginArray`) → per-system → linkCount → per-link.
- per-system → fields + initializer/behavior arrays → per-module `{ Text(typeId); module->Serialize(ar) }`.
- Reading reconstructs each module via the registry, then `module->Serialize(ar)` reads its params.

**Two backends, one `Serialize()` (confirmed against the texture triad):**
- **Cooked resource → `BinarySerializer`**, stored as a content-DB `Instance`: a binary `.rasset`
  envelope (`WriteObject`) + optional `.<stream>.bin` sidecars for heavy blobs (`WriteData`). This is
  what the runtime loads.
- **Edit-time `.particlefx` asset → `XmlSerializer`** ([[xml-port]]) — human-readable/diffable, same
  `Serialize()` code, path-refs instead of GUIDs.

### 2.3 Module polymorphism via reflection (IMPLEMENTED)

Modules are reflected `ISerializable` objects (see §0), so the serializer needs no bespoke type-id
machinery:
- **Type-id** is `module->GetType()->id` (a `u64` from `ComputeTypeId(ns, name)`), written per module.
- **Reconstruction** is `GlobalSerializableRegistry().Create(id)` → `RefPtr<ISerializable>` →
  `Cast<ParticleInitializer/Behavior>` → `AddInitializer/AddBehavior(RefPtr)`.
- **Params** are the module's own `Serialize(ISerializer&)` override (each writes its fields via the
  free value-type overloads).

`RegisterParticleModules()` registers every module's `StaticType()` + a `RegisterSerializable<T>()`
factory. The originally-planned `TypeId()` virtual and `CreateInitializer/CreateBehavior` string
registry were removed in favour of this. (Reflection param-tables driving the inspector, §4.3, remain
future work — the runtime doesn't need them.)

### 2.4 Factory + resource manager + GUID (confirmed against `draconic.texture.resource`)

Three types, mirroring `TextureResource` / `Texture` / `TextureFactory`:
- **`ParticleEffectResource : ISerializable`** — the cooked *record* (§2.1), GUID-keyed in the content
  DB as an `Instance` (`.rasset` binary envelope).
- **Product `Object`** — the runtime-ready effect the factory produces (a small `Object` wrapping the
  instantiable `ParticleEffect` + attached refs). The `ParticleEffectComponent` binds this.
- **`ParticleEffectFactory : resource::IResourceFactory`**:
  ```
  const TypeInfo* ProductType() const override;           // the product Object type (NOT the record)
  RefPtr<Object> Create(ResourceManager& mgr, content::Instance& inst) override {
      auto rec = Cast<ParticleEffectResource>(inst.ReadObject().Get());   // deserialize record
      // for each ref: mgr.Bind<TextureResource>(rec->textureRef) -> Proxy<T>  (auto dependency edge)
      // build the runtime ParticleEffect from the record; return the product
  }
  ```
  A **data factory** (model-B) — no GPU upload; referenced texture/mesh/material resources own their
  own GPU data, attached via `mgr.Bind<T>` which records the dependency edges (so a referenced-texture
  reload transitively reloads the effect).
- **Refs** are `resource::ResourceId<T> { Guid id; }` fields on the record, serialized via `GuidValue`,
  resolved at runtime by `mgr.Bind<T>(rid) -> Proxy<T>`. `Build()` fills them; the factory resolves.
- **GUID** comes from the content DB: `Group::CreateInstance(name, ParticleEffectResource::StaticType())`
  mints a fresh GUID (deterministic when the DB RNG is seeded — reuse the idiom;
  [[sprites-and-dynamic-categories]] notes the deterministic-GUID collision gotcha, watch for it).

---

## 3. Runtime consumption

- `ParticleEffectComponent` today holds a borrowed `ParticleEffect*` + `SetEffect(fx)`. Add a
  **resource-handle** path: `SetEffect(resource::Proxy<ParticleEffectProduct>)` (obtained from
  `mgr.Bind<ParticleEffectProduct>(guid)`) → the component spins up a `ParticleEffectInstance` over
  the product's `ParticleEffect`. The existing in-code `SetEffect(ParticleEffect&)` stays for
  tests/samples — the two coexist, exactly like other systems kept a code path beside the cooked one.
- The instance is unchanged; it already consumes a `ParticleEffect`. The product wraps a
  `ParticleEffect` (plus resolved refs), so no instance changes.

---

## 4. `draconic.particles.editor`

### 4.1 `ParticleEffectAsset : editor::Asset`

**`editor::Asset` is minimal** (confirmed): it derives `ISerializable` (an `Object` with reflected
identity) and carries only `String fileName` + a `Serialize` hook. **No GUID and no dirty flag on the
asset** — identity/GUID lives on the content-DB `Instance`, not the asset.

**Modelling difference from textures:** a `TextureAsset` is *import settings* pointing at an external
`.png` (`fileName`) that `Build()` loads. A particle effect has **no external source** — it is
authored in-tool. So `ParticleEffectAsset` **embeds the full effect description** (systems/modules/
curves/ranges + **soft refs by path** to texture/mesh/material assets) and *is* the `.particlefx`
(serialized via `XmlSerializer`). `Build()` cooks the in-memory asset directly (no source-file load).

```
class ParticleEffectAsset final : public editor::Asset {
    DRACONIC_OBJECT(ParticleEffectAsset, editor::Asset)   // or DRACONIC_REFLECT if inspector-reflected
    // the authored effect (edit-time value model) + path-refs + editor UI state
    void Serialize(ISerializer& ar) override { editor::Asset::Serialize(ar); /* + effect fields */ }
};
```

### 4.2 `ParticleEffectAssetBuilder : editor::DefaultAssetBuilder` — `Build()` is the bake

As-built signature: `Status Build(const editor::Asset&, editor::AssetBuildContext&)`, where
`AssetBuildContext = { StringView assetRoot; content::Instance* output; IContentDatabase* db; }`. It
**writes into `ctx.output`**, it does not return a resource. Steps:
1. `static_cast` to `ParticleEffectAsset` (guarded by `AssetType()`).
2. **Deep-clone** the authored effect into a fresh `ParticleEffectResource` (`CloneEffect`, a serialize
   round-trip — the real transform, producing an independent cooked object).
3. **Resolve refs**: each system's edit-time texture PATH → `ctx.db->GetInstance(path)->Id()` written
   into the cooked system's `textureRef` (fails `NotFound` if the dependency isn't cooked yet).
4. *(Future)* sample curves → LUT arrays if `curveEncoding == LUT`; today curves copy through as Hermite.
5. `ctx.output->WriteObject(cooked)` (binary `.rasset`).

No GPU work (data-only cook). At load, the factory `Bind`s each `textureRef` (dependency edge), so a
re-cooked texture reloads the effect.

### 4.3 Editor page / inspector — **net-new, no existing framework**

**There is no editor-page or inspector subsystem in the engine yet** (confirmed: reflection
(`DRACONIC_REFLECT` / `TypeBuilder::Property`) exists but nothing consumes it as an inspector, and
`draconic.editor` is purely the asset/builder cook seam). So the tree/inspector/curve-editor UI is a
**separate, larger effort** with no framework to build on — not part of getting `.particlefx`
authoring working.

Therefore **the editor tier v1 = `ParticleEffectAsset` + `ParticleEffectAssetBuilder` + registration
+ an importer/preset helper** (mirroring `TextureImporter`). Authoring for v1 is programmatic (build
an asset in code / hand-write the `.particlefx` XML) → `Build()` → cooked resource. The visual editor
page (tree + reflection-driven inspector + curve/gradient controls + live preview) is deferred to
its own track — and it likely wants a general reflection-inspector framework built first (which would
serve every asset type, not just particles). Flag for scheduling: **do we build the inspector
framework now, or ship code/XML authoring first and add the page later?**

---

## 5. Hot-reload

Runtime reacts to a **new cooked resource**, not to edit-time edits. On re-bake, the resource
manager hot-swaps the cooked `ParticleEffectResource` in place; live instances **config-diff** and
reinit only changed cooked modules (preserve live particles where possible), falling back to a full
reinit on structural change. Uses the version/generation-keyed invalidation rule
([[bind-group-cache-versioning]]) — never raw pointers.

---

## 6. Runtime changes made (as built)

1. `ParticleInitializer`/`ParticleBehavior` now derive **`ISerializable`** (`DRACONIC_OBJECT`); each
   concrete module has `DRACONIC_DEFINE_OBJECT` + a `Serialize(ISerializer&)` override for its params.
   `RegisterParticleModules()` registers types + `RegisterSerializable<T>()`. (Replaces the planned
   bespoke `TypeId()` virtual + string registry.)
2. Module storage moved `Array<UniquePtr<…>> → Array<RefPtr<…>>` (`ISerializable` is `RefCounted`);
   `AddInitializer/AddBehavior` gained pre-built `RefPtr` overloads + `Get/Count` accessors.
3. Value types (ranges/curves/shapes/`Trail`/`Flipbook`/`SubEmitterLink`/collider structs) got free
   `Serialize(ISerializer&)` overloads (decomposed fields).
4. `ParticleSystem` gained an opaque `Guid textureRef` (ref-on-the-system); the effect serializer
   (in the resource tier) walks systems/emitter/modules/links.
5. `editor::AssetBuildContext` gained `IContentDatabase* db` (for cross-asset path→GUID resolution).

The sim and render behavior are otherwise untouched; the render integration additionally gained the
cooked-resource consumption path (`SetEffect(Proxy)` + per-system resolved textures).

---

## 7. Sequence

1. ✅ **Serializer + cooked resource** — `ParticleEffectResource` + reflection-based effect serializer
   + factory + `RegisterParticleEffectResource()`; `ParticleEffectFactoryTests` round-trip.
2. ✅ **Editor asset + `Build()`** — `ParticleEffectAsset` + builder (deep-clone bake + path→GUID ref
   resolution); `ParticlePipelineTests`.
3. ✅ **Component resource path** — `SetEffect(Proxy<ParticleEffectResource>)` + per-system resolved
   textures + a live `ParticleFX` cook-at-startup cell.
4. **Editor page** *(future)*: tree + reflection inspector + curve/gradient editors + live preview —
   net-new (§4.3); wants a general reflection-inspector framework first.
5. **LUT curve encoding** *(future, with GPU sim)*: a `Build()` option behind the `curveEncoding` tag.

Steps 1–3 shipped; 4–5 remain.

---

## 8. Decisions (resolved)

- **Reflection-driven, not manual** — modules are reflected `ISerializable`s; the reflection/
  serializable registry supplies type-id + construct + serialize (§0, §2.3). The runtime does gain an
  `ISerializable`/`RefCounted` base + `Serialize` overrides, which we accepted as using the engine's
  universal serialization contract (not bespoke authoring code) and RefPtr storage.
- **One serializer, two ref-encodings** — the effect serializer is shared; the asset writes path-refs
  (XML `.particlefx`), the cooked resource writes GUIDs (binary `.rasset`). ✅
- **`ParticleEffectResource` reuses the runtime `ParticleEffect` + refs** (no parallel cooked struct);
  `curveEncoding` tag reserved for the LUT seam. ✅
- **Refs stay as GUIDs the factory binds** (dependencies own their cook + GPU data); resolved handles
  are `Proxy<Texture>` on the resource for hot-reload. ✅
- **Editor page deferred** — shipped code/XML authoring first (proven by tests + the live cell); the
  visual page is its own track. ✅

---

## 9. Module layout + CMake (mirrors the texture triad exactly)

```
Code/Draconic/Particles/                 draconic.particles            Draconic::Particles          (runtime; shipped)
Code/Draconic/Particles/Subsystem/       draconic.particles.subsystem  Draconic::ParticlesSubsystem (render; shipped)
Code/Draconic/Particles/Resource/        draconic.particles.resource   Draconic::ParticlesResource  (NEW)
Code/Draconic/Particles/Editor/          draconic.particles.editor     Draconic::ParticlesEditor    (NEW, tooling-only)
```

- **`Draconic::ParticlesResource`** links `Draconic::Core Draconic::Particles Draconic::Content
  Draconic::Resource` (+ `Draconic::TextureResource`/`GeometryResource`/`MaterialsResource` for the
  refs it binds). Imports: `draconic.core, draconic.particles, draconic.content, draconic.resource`.
  Files: `ParticleEffectResource.cppm` (record + product + factory + `Serialize` + `RegisterParticleEffectResource()`).
- **`Draconic::ParticlesEditor`** links `Draconic::Core Draconic::Editor Draconic::Particles
  Draconic::ParticlesResource Draconic::Content`. Imports add `draconic.editor,
  draconic.xml.serialization`. Files: `ParticleEffectAsset.cppm` (asset + builder + importer +
  `RegisterParticleEffectAsset()`). **Not linked by the runtime/app** — tooling only.
- `Particles/CMakeLists.txt` adds `add_subdirectory(Resource)` + `add_subdirectory(Editor)` after the
  shared lib (like `Texture/CMakeLists.txt`).

Registration mirrors `RegisterTextureResource()`: `GlobalTypeRegistry().Register(...)` +
`RegisterSerializable<ParticleEffectResource>()` (so the content DB reconstructs the record by type
tag). Round-trip bring-up follows `TexturePipelineTests.cpp`: cook a code-built asset into a
`ContentDatabase` `Instance`, then `ResourceManager::Bind` it back and run.

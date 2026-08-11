# Reflection track — reflect the useful surface for scripting + tooling

Status: **planned (user directive, 2026-07-28).** Trigger: the generic asset page had to be
built SERIALIZE-driven because the assets carry no reflected properties — "too much of the
whole code-base is not reflected. Go through the codebase and reflect the useful things so
they are usable for scripting and tooling."

## The measured gap (2026-07-28)

| Facility | Sites | Where |
|---|---|---|
| `DRACONIC_REFLECT(...)` (intrusive + properties) | 12 | facades, subsystem impls, net |
| `DRACONIC_REFLECT_VALUE(...)` (value types + properties) | 46 | Core math, scene COMPONENTS (per-subsystem impl units), replication |
| `DRACONIC_REFLECT_ENUM(...)` | 25 | of ~261 `enum class` declarations |
| `DRACONIC_DEFINE_OBJECT` (identity ONLY, no properties) | 329 | everything else: assets, particle modules, sources, documents |

The reflected island exists exactly where a consumer demanded it: the scene inspector
(components), the replication field codec, and the script backends' type harvest. Everything
without a consumer stayed identity-only — and then new consumers (the generic asset page, the
particle inspector) had to be built WITHOUT reflection.

## Consumers that light up as coverage grows

1. **Generic asset page** — upgrade to reflection-FIRST (serialize-scan stays as the fallback
   for still-unreflected types): real labels via `displayName`, ranges/sliders via the `range`
   attribute, tooltips via `description`, conditional rows via `visibleWhen`, and — the big
   one — **enum dropdowns by NAME** instead of raw int rows.
2. **Script backends** (Wren + AngelScript) — both harvest reflected types; every newly
   reflected type/enum becomes scriptable surface without hand bindings.
3. **Bespoke inspectors** — hand-built `Cast<T>` dispatches (particle modules, graph data)
   stop being the only option for NEW types; future pages get auto-grids.
4. **Replication** — the field codec works over reflection; reflected gameplay types become
   replicable without custom serializers.

## Conventions (already-standing rules that govern the pass)

- **Bodies live in module IMPLEMENTATION units**, never interface partitions
  (gcc gcm-cluster hygiene). Each module grows/extends a `Register<X>Types()` entry point
  called from the existing startup registration sites (editor Main, cook, export, player).
- Reflection describes the AUTHORED/public surface — never editor-only state (that stays on
  editor assets) and never internal runtime scratch.
- Attributes carry tooling metadata where it exists today: `displayName`, `description`,
  `range` (Float4 {min,max,step,_}), `visibleWhen` — the scene inspector already consumes
  all four; the generic page will too.
- Accessor-gated state (e.g. `ParticleSystem::maxParticles`) is NOT member-reflectable; expose
  via reflected METHODS where scripting wants it, or leave to bespoke UI.
- Every phase lands with tests (property enumeration + get/set round-trip per type is cheap).

## Phases (consumer-driven, each pays off on landing)

- **P1 — Editor assets + their enums.** The ~21 asset types (Texture, ParticleEffect,
  AnimationGraph/Clip/Skeleton, AudioBusLayout/Clip/Cue, Physics pair, Image, InputMap, UI
  pair, Script, Model manifest...) + source structs where useful. Payoff: the generic page
  goes reflection-first; asset authoring becomes scriptable later.
- **P2 — Particle modules + animation graph/emitter data.** The 23 particle module types
  (revisits the decision made during the particle page - the page keeps its bespoke editors,
  but the types stop being tooling-invisible), GraphNode/Transition/Condition data, emitter
  settings. Payoff: auto-grids for new modules, script-driven effect authoring.
- **P3 — Runtime gameplay surface for scripting.** Audio engine knobs, physics descs
  (CharacterDesc, RayHit, joint params), animation parameters, camera/light knobs not yet
  covered, net-adjacent types. Plus the ENUM sweep: reflect the gameplay-meaningful subset of
  the ~236 unreflected enums (blend modes, comparison ops, emission shapes, ...).
- **P4 — Consumer upgrades.** Generic page reflection-first mode (+ enum dropdowns), script
  harvest breadth audit (both backends), optional dedupe of hand-built inspectors where the
  auto-grid now suffices.

## P1 progress + the established recipe (Opus, 2026-08-03)

**Pattern-setter shipped: `TextureAsset` + its texture/image enums (commit 8da015c3).**
90 assertions, clang+gcc green, editor/cook/export link on both. The recipe every
remaining P1 asset follows:

1. **Enums -> owning module, once.** Reflect each enum in the impl unit of the module that
   DEFINES it (e.g. `TextureShape/Filter/Wrap` in `draconic.texture`, `ImageColorSpace` in
   `draconic.image`), via `DRACONIC_REFLECT_ENUM` + an idempotent `Register<Module>Reflection()`
   (declared in an interface partition, `static bool once` guard in the body). Owning-module
   placement is load-bearing: several assets share enums (ImageColorSpace is used by both
   TextureAsset and ImageAsset) - reflecting per-editor would emit duplicate
   `DraconicRegisterEnum_*` symbols and fail to link. New impl unit needs the module's
   CMakeLists `target_sources(... PRIVATE <X>ReflectionImpl.cpp ...)`.
2. **Asset TYPE -> its `.editor` module, impl unit.** Replace the interface's
   `DRACONIC_DEFINE_OBJECT(X, NS)` with `DRACONIC_REFLECT(X, NS){ builder... }` in a NEW
   `<Asset>Impl.cpp` (`module <mod>.editor;`). The class body already carries `DRACONIC_OBJECT`;
   identity (name/base/data-version) is preserved because the `TypeBuilder` ctor takes the same
   `#Type, NS, &Super::StaticType()`. List every serialized field as `.Property<&X::field>("name")`
   + `.PropAttribute("displayName"/"description"/"range", ...)`. Reflect bodies are the
   free-function `DraconicReflect_X`, so **fields must be public** (all editor Asset fields are).
3. **Wire transitively.** Have `RegisterXAsset()` (already called from editor/cook/export) call
   the owning-module enum registrars first, then `GlobalTypeRegistry().Register(X::StaticType())`.
   No Main.cpp edits. `image::` etc. namespace-qualify cross-module registrar calls.
4. **Test.** A `<Asset>ReflectionTests.cpp` in the `.editor` Tests target: property enumeration
   (count + names), attribute reads (`FindAttribute(prop, u8"displayName")` - keys are u8
   StringView; identifiers `type.name`/`EnumValueName` are plain `const char*`, compare directly),
   get/set round-trip through `Instance::From(&asset)`, and `IsEnum`/`EnumValueName` on the enum
   property types.

**Deferred within P1:** the base `editor::Asset::fileName` (a `vfs::SourcePath`) is not yet
reflected - doing it once on the base lights up `fileName` for EVERY asset via the base-chain
`FindProperty`, but needs `SourcePath` reflected as a value type first. Worth an early follow-up.

**Remaining P1 flat targets** (same recipe): `PhysicalMaterialAsset` (scalars, no enum),
`AudioClipAsset` (bools/scalars), `FontAsset` (+ bakeMode enum), `ShaderAsset` (strings),
`ImageAsset` (ImageColorSpace - already reflected, just consume), `UIDocumentAsset`/`UIThemeAsset`
(strings), `ScriptClassAsset` (string). **Harder** (nested domain structs behind accessors, do
after the flats): `ParticleEffectAsset`, `InputMapAsset`, `MaterialAsset`, `Static/SkinnedMeshAsset`.

## BLOCKER (Opus -> Fable review, 2026-08-03): nested non-copyable Object members

The flat P1 pass + the base `Asset::fileName` are shipped (commits 8da015c3, 2bd96f7e, b82f8a01,
9052ff02, 0506b576, fa332385). The **nested-struct assets are blocked on a reflection-system gap**,
and I want Fable's call on the design before building it.

**The problem.** The remaining P1 assets wrap a domain source struct by VALUE:
`MaterialAsset{ MaterialSource source; }`, `ParticleEffectAsset{ ParticleEffect m_effect; }`,
`InputMapAsset{ InputMap m_map; }`, `Static/SkinnedMeshAsset{ *MeshSource source; }`. To reflect
the asset you'd add `Property<&MaterialAsset::source>("source")` and let the generic page recurse
into the nested type's properties. **That does not compile.** `TypeBuilder::Property<Member>`
generates a getter `detail::PropertyGet` that does `Variant::From<M>(object->*Member)` -
Reflection.cppm:39-42 - i.e. it **copies** the member into a Variant. But every one of these nested
structs derives `ISerializable : Object : RefCounted`, and `RefCounted(const RefCounted&) = delete`
(RefCounted.cppm:34). So the member type is non-copyable and `Property<>` fails to instantiate. The
copyable-field path (all the flat assets) is exactly why they worked; nested Object members hit the
wall.

**What already exists that helps.** `PropertyInfo` already carries an `address` accessor
(Reflection.cppm:91-95) - `void* (*address)(const Instance&)` - the type-erased escape hatch editors
use for fields a Variant can't hold (it's how reflected enums are read/written). A nested Object
member could be exposed the same way: the page gets the `MaterialSource*` via `address` and recurses
into `Properties(TypeOf<MaterialSource>())`. The nested struct itself reflects fine (its own fields
are copyable: `MaterialSource` = name/shaderId/shaderName/flags + u8-encoded enum fields + arrays).

**Proposed fix (needs Fable's blessing).** Add a **reference/nested property kind** to the
reflection system:
- A `TypeBuilder::Reference<&T::member>("name")` (or a `PropertyFlags::Nested` variant of
  `Property`) whose `get`/`set` are null or a no-op (nested Objects aren't get/set by value) but
  whose `address` and `type` (`&TypeOf<M>()`) are populated, plus a `Nested` flag on `PropertyInfo`.
- The generic asset page (P4 consumer) checks the flag: for a nested property it recurses into
  `Properties(*prop.type)` using a sub-`Instance` built from `prop.address(instance)`, instead of
  rendering a leaf editor. The scene inspector's property loop needs the same guard (skip `GetProperty`
  when `Nested`, recurse instead).
- Scope: ~1 Core reflection addition (TypeBuilder method + PropertyInfo flag + a `PropertyGet` that
  returns empty for nested) + the two consumers (generic page, scene inspector) + then reflect the 4
  nested assets by their `source`/`m_effect`/`m_map` members. The nested structs get `DRACONIC_REFLECT`
  in their owning modules first (e.g. `MaterialSource` is `DRACONIC_DEFINE_OBJECT_VERSIONED` today ->
  becomes `DRACONIC_REFLECT` with `.DataVersion(2)`).

**Open questions for Fable:**
1. Reference-property kind vs. a different mechanism (e.g. reflected accessor METHODS returning `M&`,
   which the "accessor-gated state -> reflected METHODS" convention already hints at)? Methods avoid a
   new property kind but make the page/inspector follow methods, not properties.
2. u8-encoded enum fields (`MaterialSource::blendMode` is `u8`, not `BlendMode`) reflect as raw ints -
   no enum-name dropdown unless the field is retyped to the enum. Retype (wire-compatible?) or accept
   raw u8 for these?
3. Is the nested-recursion depth worth it for P1, or defer the nested assets to P2 (which already
   revisits particle modules + graph data) and close P1 at "flat assets + base fileName"?

Until this is decided, the nested assets stay `DRACONIC_DEFINE_OBJECT` (identity-only). The flat P1
surface + base `Asset::fileName` are the shipped deliverable.

## Non-goals

- Reflecting private/internal state or editor-only fields.
- Replacing the working bespoke pages — they keep their specialized editors (curve canvases,
  node graphs); reflection makes types VISIBLE to generic tooling, it does not flatten UX.
- A serialization rewrite — Serialize stays the wire; reflection is the tooling/scripting view.

## Fable DECISION on the nested-member blocker (2026-08-03)

Verified: PropertyGet copies into the Variant (Reflection.cppm ~39), the
`address` escape hatch exists and is the enum precedent (~95), RefCounted copy
is deleted (~106). The analysis is right and the proposed fix is the right
mechanism. Blessed, with these calls:

**Q1: the nested property KIND - approved (not accessor methods).** Properties
are the one currency both consumers already iterate; routing STRUCTURE through
methods would fork the traversal model, and a method returning `M&` cannot
marshal through Variant anyway (same deleted copy) - methods do not even dodge
the problem. The Nested kind formalizes the existing `address` pattern rather
than inventing a parallel one. Details:
- Name it `TypeBuilder::Nested<&T::member>("name")` + `PropertyFlags::Nested`
  ("Reference" collides mentally with resource::Ref). get returns an empty
  Variant; set returns a clear error Status (not silent Ok); address + type
  populated.
- POINTER members (Static/SkinnedMeshAsset's `MeshSource* source`): define the
  semantics now - `address` yields the POINTEE (deref, null when the member is
  null); consumers must null-check before recursing. Add a test for the null
  case.
- REQUIRED IN THE SAME COMMIT as the mechanism: guards in BOTH existing
  property consumers (scene inspector + generic page: skip GetProperty, recurse
  via address) AND in the script-harvest path (SKIP Nested properties for now -
  otherwise scripts see empty Variants as garbage surface). Script-side nested
  objects later ride the bound-object direction (scene-scripting decision), not
  Variants.
- Recursion depth: consumers guard against cycles (a nested type nesting its
  ancestor) with a simple visited-type or depth cap; assert-log, don't hang.

**Q2: retype the u8 enum fields - yes, with a wire test per field.** Declare
the enum `: u8` (or verify it already is), store the enum in the struct, keep
the Serialize site writing the same byte (explicit cast). That is
wire-identical, and it buys name dropdowns + type safety engine-wide. Rule:
each retyped field needs a wire round-trip test proving old bytes load
unchanged - add the test BEFORE the retype where one does not exist. Any field
that is genuinely packed/flags-encoded stays raw u8 with a comment; do not
force it.

**Q3: land the mechanism NOW, close P1 pragmatically.** The Core addition is
small and P2 (particle modules, graph data) needs it regardless - building it
later just moves the blocker. P1 closes at: flats + base fileName (shipped) +
the Nested mechanism + at least MaterialAsset reflected through it as the
living proof (it is the most-edited nested asset). InputMapAsset if it falls
out the same way. The accessor-gated (ParticleEffectAsset) and pointer-member
(mesh) assets MAY fold into P2's opening without blocking P1 closure - note
which moved in the gap table.

## Adjacent addition: `ComputedProperty` (Opus, 2026-08-03) - NOT the Nested kind

While actioning the scene-scripting `.scene` getter deferral I added a SEPARATE small
Core primitive: `TypeBuilder::ComputedProperty<&getter>(name)` - a read-only property
backed by a const zero-arg getter that returns BY VALUE. get marshals the return through
a Variant; set returns NotSupported; address is null; flagged ReadOnly.

This is distinct from the Nested kind decided above and does not touch it:
- **Nested** (Q1): address populated, get returns EMPTY, for RECURSING into a non-copyable
  Object member; script-harvest SKIPS it.
- **ComputedProperty**: get returns a REAL copyable value, no address, for reading a DERIVED
  value as a parens-less property in scripts + tooling.

Motivation: facade accessors that should read as properties (`entity.scene`) rather than
methods (`entity.scene()`). The script backends already bind property GETs through
`core::GetProperty` -> `property.get` and emit them parens-less, so no backend change was
needed. Covered by `rtti: a ComputedProperty is a read-only getter-backed property`
(RttiTests) + the both-backend `.scene` end-to-end cases in ScriptSceneTests. Flagging for
the record since it lives in the same Core reflection surface Fable just reviewed.

## BLOCKER (Opus -> Fable review, 2026-08-03): two container primitives for the particle sweep

The particle-module reflection (P2) reached the limit of the current primitives. 15 of 20
module types reflect (flat scalars + Nested value structs + the emission-shape/collision-shape
leaves). The remaining 5 modules + the ParticleEffect wiring each need a NEW Core reflection
primitive. Both are small; I want Fable's call before building, since they touch the same Core
reflection surface as Nested/container.

### Gap 1: fixed C-array member reflection (`T member[N]`)

**The problem.** The particle curves store keys in a fixed C-array: `ParticleCurveFloat` has
`CurveKeyFloat keys[kMaxCurveKeys]; i32 keyCount;` (same for `ParticleCurveColor`, and
`ParticleCurveFloat2` uses parallel `f32 times[N]` / `Float2 values[N]` arrays).
`CollisionBehavior` likewise has `CollisionPlane planes[4]` / `spheres[4]` / `boxes[4]`.
`Property<&T::member>` cannot hold a C-array in a Variant, and `RegisterArrayType` is for
`Array<T>`, not `T[N]`. So the curve keys (the actual authored data) are unreflectable today -
which blocks the 5 curve-driven OverLifetime behaviors (Color/Alpha/Size/Rotation/Speed) and the
shape lists on CollisionBehavior.

**What exists that helps.** The container reflection (ContainerInfo: size/getAt/setAt over an
Instance) is exactly the right consumer shape. A C-array is a container with a COMPILE-TIME size
and trivial element addressing (`base + i*sizeof(T)`).

**Proposed fix.** A `RegisterCArrayType<T, N>()` (or a `TypeBuilder::CArray<&member>` marker) that
builds a ContainerInfo whose `size` returns N, `getAt`/`setAt` index by `base + i*sizeof(T)`, and
`elementType = TypeOf<T>()`. Reuses the whole IsContainer/ContainerGetAt/ContainerSetAt consumer
path already tested. The `keyCount` stays a normal scalar property (the logical length; the
container is the physical capacity). The curve struct then reflects: `keys` as a C-array
container + `keyCount` scalar.

### Gap 2: polymorphic-container reflection (`Array<RefPtr<Base>>`)

**The problem.** `ParticleSystem` holds `Array<RefPtr<ParticleInitializer>>` +
`Array<RefPtr<ParticleBehavior>>` - the modules are polymorphic (concrete types derive the base).
The existing container reflection is HOMOGENEOUS: `elementType` is a single fixed `TypeOf<T>()`.
For a polymorphic module list the element's reflected type is the ELEMENT's dynamic type
(`element->GetType()` / the RefPtr's pointee StaticType()), which varies per entry. So
ParticleEffect (and thus ParticleEffectAsset) can't be traversed into its modules' concrete
reflected properties.

**What exists that helps.** Objects already carry dynamic type via `GetType()`, and Nested +
container reflection already handle the addressing/recursion once the element type is known.

**Proposed fix.** A container flavor for `Array<RefPtr<Base>>` whose `getAt` yields an Instance
keyed on the ELEMENT's dynamic type (`ptr->GetType()`), not a fixed elementType - so a consumer
recurses into `Properties(*elem.Type())` for the concrete module. Registered e.g.
`RegisterPolymorphicArrayType<Base>()`. With this, ParticleEffect reflects its systems -> module
lists -> each concrete module's reflected props, end to end.

**Open questions for Fable.**
1. Two separate registrars (`RegisterCArrayType<T,N>`, `RegisterPolymorphicArrayType<Base>`) vs
   folding both into the existing container model with flags on ContainerInfo?
2. C-array: expose the physical capacity N as the container size, or clamp to the logical
   `keyCount` (the consumer would need the count field's name)? I lean capacity-N + keyCount scalar.
3. Priority: neither blocks anything shipped (particle page is bespoke; 15/20 modules already
   reflect). Build now to finish the particle types + unlock ParticleEffectAsset, or defer both to
   a "reflection containers" follow-up after the rest of P2/P3?

Until decided: the 5 curve OverLifetime behaviors stay identity-only, and ParticleEffect's module
arrays stay untraversed. Everything reflectable with today's primitives is done.

---

## Wiring step DONE (Opus, commit f1713d32) - full effect traversal

The effect graph is reflected end to end and traversal is proven by test (ParticlesTests batch 7):

    ParticleEffectAsset --Nested--> ParticleEffect { name, systems }
        systems : Array<UniquePtr<ParticleSystem>>  (homogeneous UniquePtr container)
            --> ParticleSystem { name, config scalars, emitter(Nested),
                                 initializers, behaviors }
                emitter      : ParticleEmitter (Nested value; EmissionMode enum reflected)
                initializers : Array<RefPtr<ParticleInitializer>>  (polymorphic container)
                behaviors    : Array<RefPtr<ParticleBehavior>>     (polymorphic container)
                    --> each concrete module's reflected props --> ranges / curves

### One new primitive: address-based element descent (`ContainerInfo.addressAt`)

The outer level forced a small, additive extension. `ParticleSystem` / `ParticleEmitter` are plain,
NON-Object, NON-copyable classes held only in `Array<UniquePtr<ParticleSystem>>`. The existing
`getAt` returns a `Variant`, and a Variant carries only copyable values or `RefPtr<Object>` - so a
move-only, non-Object pointee cannot come out through `getAt`.

Fix (mirrors the already-blessed Nested mechanism exactly, one level down): a nullable
`Instance (*addressAt)(const Instance&, usize index)` slot on ContainerInfo that returns a BORROWED
Instance `{ pointee, elementType }`. Consumers `IsContainer` -> `ContainerAddressAt` -> recurse into
the element's properties, no copy. `RegisterUniquePtrArrayType<T>()` fills it for the systems array
(getAt returns empty, like Nested); the polymorphic RefPtr container fills it too (dynamic type), so
both flavors share ONE uniform descent path. No new imports, crosses no layer - it is the Nested
idea at the element level, not a new design axis. Flagged here for awareness, not as a blocker.

Private `m_initializers` / `m_behaviors` (on ParticleSystem) and `m_systems` (on ParticleEffect)
are reached through a `static void T::BuildReflection(TypeBuilder<T>&)` member hook declared in the
interface and defined in the impl unit (ParticleEffectReflectionImpl.cpp) - private access without
widening the public surface, and it keeps DRACONIC_REFLECT bodies out of the interface (GCC hygiene).

### Deferred (not needed for the traversal proof, low risk to add later)
- ParticleSystem's nested value config `trail` (TrailSettings) / `flipbook` (FlipbookSettings) are
  not yet reflected (would be `Nested<>` + their own DRACONIC_REFLECT_VALUE). Enums beyond
  EmissionMode reflect as plain identity-typed Property (no value-name metadata yet).
- The systems container has no default-emplace (ParticleSystem has no default ctor - it needs a
  capacity); the flavor reports that cleanly. Editor "add system" calls the native AddSystem.

---

## Collections-in-scripts lift - progress (Opus)

The "full lift, both backends" the user approved, in checkpointed units:

- **Unit 1 DONE (4451f118)** - constructor-less reflected types bind as RETURNED HANDLES. A facade
  method can hand a script a handle to a reflected Object that is not itself script-constructable, and
  its reflected properties are gettable/settable (write-through). Wren: a reachability-closure emitter
  (seed = constructor-having types, close over method return/param + property + container-element-base
  + EnumerateDerived edges) keeps the emitted set - and the 256->1024 dispatch pool - bounded to what
  scripts can receive. AngelScript already bound properties without a factory (locked by a test).

- **Unit 2a DONE (349b612c)** - container MEMBERS bind as ops on the owner: NAME_count / NAME_at(i) /
  NAME_add(typeName|_) / NAME_removeAt(i) / NAME_move(a,b). Computed transiently on the owner, so
  element handles come back OWNED (object element addrefs, value element copies) - NO borrowed-handle
  machinery. This is the Fable-D core: `system.behaviors_add("Gravity")` end to end in both backends.
  add-by-name -> EnumerateDerived (displayName, then type name) -> the container create-by-type factory.

## BLOCKER (Opus -> Fable review): Unit 2b - a Variant "borrow mode" for nested-value / non-Object element handles

User picked 2b as the next unit and asked for Fable review before implementing (the established bar for
a core reflection primitive). Here is the full spec.

### The problem
After 2a, still returning null / unbound to scripts: NON-Object value elements (`Array<UniquePtr<
ParticleSystem>>`, reached only by `addressAt`) and plain NESTED-VALUE members (`ParticleEmitter
emitter;`, an OverLifetime behavior's curve struct). Each is a BORROW - a handle over a member/element
ADDRESS that must keep its owner alive. The particle graph needs it for effect -> systems ->
system.emitter / curve descent from a script (and for editing a nested value's scalar props in place).

Both backends store an OWNED payload in the foreign instance (Wren: a heap `Variant*` deleted on
finalize; AngelScript: a refcounted `BoxedVariant { Variant; refCount }`). Every getter / setter /
method / container op already turns that payload into a reflection `this` via `core::ToInstance(const
Variant&)` (Reflection.cppm ~347). A `Variant` today has exactly two shapes: value mode (SBO/heap copy
of a copyable T) and object mode (`RefPtr<Object>`, `m_dynamicType` = dynamic type). Neither can express
a borrowed `{addr, type}` into a parent's storage. So the borrow needs a new Variant shape.

### Proposed primitive: a third Variant shape, "borrow mode" (minimal + additive)
Reuse object mode's machinery and add ONE pointer field. A borrow is an object-mode Variant (its SBO
holds a `RefPtr<Object>` = the KEEP-ALIVE, vtable = `kVariantVTable<RefPtr<Object>>`, `m_dynamicType`
= the BORROW type) PLUS a new `void* m_borrowAddr = nullptr`. When `m_borrowAddr != nullptr`:
- `ToInstance(v)` returns `Instance(v.m_borrowAddr, v.m_dynamicType)` instead of the object/value form.
  This is the ONLY consumer change - both script backends inherit borrow descent for free (no box
  surgery, no new BoxedVariant/ForeignBox layout).
- `Type()` already returns `m_dynamicType` = the borrow type. `IsObject()` stays true (harmless: the
  keep-alive really is an object; add `IsBorrow()` for call sites that must distinguish).

Storage / lifetime: the keep-alive `RefPtr<Object>` is the ROOT owner object (the object-mode ancestor
the borrow lives inside - e.g. the ParticleEffectAsset the whole effect graph hangs off). It rides the
existing object-mode vtable, so copy / move / destroy already addref / release it correctly; the ONLY
additions are carrying `m_borrowAddr` in the copy ctor, move ctor, and both assignments, and clearing
it in `Reset()`. Everything is unchanged when `m_borrowAddr == nullptr`.

Factory:
```
static Variant Borrow(void* addr, const TypeInfo* type, const Variant& parent);
// parent object-mode  -> keepAlive = RefPtr<Object>(parent.AsObject())  (addref the root)
// parent borrow-mode   -> keepAlive = parent's keepAlive                 (propagate the same root down)
// parent value-mode    -> no keepAlive (see open question 2)
// then m_borrowAddr = addr; m_dynamicType = type;
```
Descending N levels always pins the SAME root object, so the whole subobject graph stays valid as long
as any borrow into it lives - no per-level ref juggling.

### Alternatives considered (and why this wins)
1. Per-backend borrow box (Wren `ForeignBox { Variant owned; Instance borrow; WrenHandle* ownerRoot }`
   + AngelScript twin). Rejected: duplicates the concept in two places, rewrites `SelfOf` / finalize /
   every dispatcher's `ToInstance` in both backends, and Wren would need `wrenGetSlotHandle` GC-root
   juggling for the keep-alive. The Variant-mode change is ~1 field + 1 `ToInstance` branch and both
   backends get it for free.
2. Make ParticleSystem/Emitter/curve into Objects (RefPtr, refcounted). Rejected: broad runtime blast
   radius for a scripting-surface convenience; they are deliberately value/UniquePtr-owned.

### Open questions for Fable
1. `m_borrowAddr` as a normal member vs folding a discriminator into `m_dynamicType`/a flag bit - the
   plain pointer is clearest and Variant is not size-critical here; OK?
2. Value-mode parent (no object root to pin): borrow with a null keep-alive (valid only while the
   parent Variant lives - fine for a transient getter that immediately descends), or refuse the borrow
   (return empty)? Our facade roots are all object-mode, so this is an edge; I lean null-keep-alive +
   a doc note.
3. Write-through: a borrowed value member is editable in place (set a scalar prop -> writes the real
   subobject). Any concern vs. treating nested values as read-only snapshots? (I think in-place is the
   right, useful behavior and matches the editor.)
4. Should `IsObject()` stay true for a borrow (keep-alive really is an object) or should borrow be a
   fully distinct predicate so nothing mistakes it for an owned object handle? I added `IsBorrow()`
   either way; question is whether `IsObject()` should exclude borrows.

### Once blessed, the implementation is
- Core: the `m_borrowAddr` field + `Borrow()` + `IsBorrow()` + the `ToInstance` branch + a Variant test
  (borrow round-trips an in-place edit; keep-alive outlives the parent handle).
- Wren + AngelScript: in `Dispatch`/`ContainerDispatch`, a nested-value member getter and a container
  `_at` whose element is non-Object return `Variant::Borrow(addr, type, self)` (address via
  `PropertyInfo::address` / `ContainerAddressAt`), and emit those getters/`_at` that 2a currently skips.

### Note: the generic LIST EDITOR (Fable C, "unit 4") does NOT need 2b
It consumes the C++ reflection container API directly (ContainerSize/AddressAt/GetAt/createElement/
EnumerateDerived - all shipped), so it is unblocked independent of the script borrow.

## Fable RULING on Unit 2b - borrow mode APPROVED, with a generation guard added (2026-08-04)

The shape is right: reuse object mode's keep-alive machinery, one added
field, one ToInstance branch, both backends inherit descent for free. The
alternatives analysis is also right (backend boxes duplicate the concept;
Object-ifying value types is runtime blast radius for a scripting
convenience). Approved - with ONE addition the proposal is missing, and it
is the difference between "safe" and "gotcha":

### The missing piece: pinning the root does NOT keep the ADDRESS valid

The keep-alive guarantees the graph is not freed wholesale. It does NOT
guarantee the borrowed address survives STRUCTURAL MUTATION of anything
between root and pointee - and Unit 2a just handed scripts the mutators:

- `systems_removeAt(i)` destroys the UniquePtr pointee a borrow points into.
- `bindings_add(...)` on an Array<T> value container reallocs the buffer a
  value-element borrow points into.
- A hot-reload rebuild repopulates the arrays under a pinned root.

`var em = system.emitter; effect.systems_removeAt(0); em.rate = 5` is
NATURAL script code that would UAF under the proposal as written. This is
exactly the pattern class the codebase already has a RULE for
(bind-group-cache-versioning: never cache a raw pointer, validate by
generation - it bit us as resize light-flicker). A borrow IS a cached raw
pointer; it follows the same rule.

### The guard: a reflection mutation generation (coarse, cheap, safe)

- One `u64 GlobalReflectionMutationGeneration()` in the reflection partition
  (main-thread; reflection mutation is main-thread by the async rules).
- BUMPED centrally in the reflection container-mutation ENTRY POINTS
  (ContainerEmplaceByType/EmplaceDefault/RemoveAt/MoveElement wrappers - so
  registrants cannot forget), which also covers every script `_add/_removeAt/
  _move` since 2a routes them through the same ops.
- `Variant::Borrow` captures the current generation into `m_borrowGeneration`;
  the `ToInstance` borrow branch validates `stored == current`, else returns
  an EMPTY Instance - the script op fails cleanly ("stale handle" error, the
  same path as a null), never dereferences.
- Coarse by design: ANY structural mutation invalidates ALL live borrows.
  False invalidation is acceptable - borrows are transient descent handles;
  code holding one across a structural mutation is precisely what must fail
  safely. Per-root counters (space on every object) are not worth it.
- BOUNDARY, documented: the generation guards REFLECTION-DRIVEN mutation
  (scripts + generic editors - the concurrent users that matter). NATIVE
  code rebuilding a graph (hot-reload internals calling Clear/AddSystem
  directly) is the same hazard class as native teardown of any
  script-visible object and stays governed by the existing reload discipline
  (script objects re-instantiated on reload). If a native rebuild path is
  found to run while scripts can hold borrows, bump the generation there
  explicitly.

Cost: one u64 field on Variant + one compare per borrowed ToInstance.

### The four open questions

1. **Plain `m_borrowAddr` member: yes.** Clarity wins; Variant is not
   size-critical. (Now plus `m_borrowGeneration`.)
2. **Value-mode parent: REFUSE (empty), not null keep-alive.** "Valid while
   an engine-internal temporary lives" is a lifetime no script author can
   reason about - it buys nothing but a UAF path for an edge that does not
   occur (all facade roots are object-mode). Fail closed; revisit only with
   a concrete need.
3. **Write-through: yes.** In-place editing is the point, matches the
   editor, and a read-only snapshot is impossible anyway (non-copyable).
   The generation guard makes a stale write fail cleanly.
4. **`IsObject()` EXCLUDES borrows.** A borrow is not an owned object
   handle: any call site doing `AsObject()`-style extraction on an
   IsObject() Variant would silently get the KEEP-ALIVE ROOT - the wrong
   object entirely (marshal it back to script as the root's type and you
   have a type-confusion bug). `IsBorrow()` distinct, `IsObject()` false for
   borrows, `AsObject()` returns null on a borrow; the root is reachable
   only through an explicitly named accessor (`BorrowRoot()`) if anything
   needs it. The internal vtable reuse is fine - the PREDICATES are the API.

### Tests to add beyond the proposed ones

- Stale-borrow: take a borrow, `_removeAt` its element, get AND set through
  the borrow -> clean failure, no crash (ASAN run).
- Realloc: borrow an Array<T> value element, `_add` until realloc, access ->
  clean failure.
- Generation does NOT invalidate on pure VALUE writes (setting a scalar prop
  through another borrow) - only structural ops bump.
- Keep-alive: root released by script, borrow still held -> graph alive,
  edit lands (the proposal's test, kept).

Unit 4 (generic list editor) proceeding independently on the C++ API is
confirmed correct - it holds no borrows across frames.

---

## Unit 2b SHIPPED (Opus, commits 01fb9a9b + 4537d312) - Fable ruling implemented in full

Core (01fb9a9b): Variant borrow mode + the mutation-generation guard, exactly per the ruling -
IsObject() excludes borrows, value-mode parent refused, write-through, generation bumped centrally in
the four structural container wrappers, revalidated in ToInstance. Tests: in-place edit + root pinning,
stale-borrow on removeAt/emplace (ASAN-clean, no UAF), value-mode-parent refusal. All four Fable test
cases covered. clang + gcc + ASAN green.

Backends (4537d312): nested-VALUE members bind as a getter returning a borrow; non-Object container
elements come back from _at/_add as a borrow. Both backends' out-marshalling already box/wrap any
Variant and defer to ToInstance, so producing the borrow in Dispatch/ContainerDispatch (+ emitting the
skipped getters/_at) was the whole change - no marshalling surgery, no per-backend box layout. Tested
nested-value edit-in-place + UniquePtr element add/edit/read on BOTH backends. clang + gcc + ASAN green.

### The collections-in-scripts lift is COMPLETE (units 1 + 2a + 2b)
A script can now traverse and edit the ENTIRE reflected graph in both Wren and AngelScript: owned
object handles, constructor-less returned handles, container members (count/at/add-by-name/removeAt/
move), nested-value members, and non-Object container elements - all generation-guarded against stale
borrows. Fable D (effect.behaviors_add(...) etc.) is delivered as the generic container binding; a
particle-specific facade + registering the effect value types (unit 3) is the remaining convenience
so a script can REACH a ParticleEffect. The generic list editor (unit 4) is the remaining editor UI.

---

## Units 3 + 4 SHIPPED (Opus) - the user's three tasks are all done

- **Unit 3 (bae7a1a5)** - ParticleEmitter/System/Effect value types published to the global type
  registry so the script harvest + reachability closure can reach them. Inert until a gameplay facade
  hands a script an effect handle (no constructor => not a script-emit seed). The gameplay facade
  itself (a ParticleComponent method returning an effect handle) is a separate gameplay-integration
  task, not a reflection-track item.

- **Unit 4 (776996e9)** - the generic reflected LIST EDITOR (Fable C) in the component inspector: a
  container property renders as element blocks (each recursing into its dynamic type's leaf rows) +
  per-element move/remove + an EnumerateDerived category-grouped "Add..." menu (polymorphic) / default
  button (homogeneous). Leaf editing: f32/Float3/bool/String/enum/int (others deferred). Undo via a
  generic MutateComponent (snapshot/restore/PasteComponent - the generic twin of the typed
  MutateMeshMaterials/MutateScriptComponent). Hidden shape-watcher forces rebuild on count/type change.

  IMPORTANT - it is DORMANT today: no scene COMPONENT carries a reflected container property, and the
  generic ASSET page (where ParticleEffectAsset's systems/modules containers live) is still
  serialize-driven. The list editor lights up on-screen once the asset page goes REFLECTION-FIRST
  (this track's P4). The mechanism is fully wired + tested-compile (52 scene-editor tests green,
  clang + gcc) and inert for container-less components (no regression).

### Follow-ups that light up the shipped mechanisms (separate tasks)
1. Reflection-first generic asset page (P4) - renders ParticleEffectAsset through reflection, so the
   Unit-4 list editor becomes visible/verifiable (systems -> modules -> ranges/curves).
2. Particle gameplay facade - a ParticleComponent script method returning an effect handle, so gameplay
   scripts reach an effect and use the shipped collections-in-scripts surface.

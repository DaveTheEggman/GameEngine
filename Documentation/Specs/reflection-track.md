# Reflection track (task #110)

Size: L (3+ phases, each independently landable). AUTHORITATIVE DESIGN:
`docs/design/reflection-track.md` - read it fully before starting; this spec
only adds build-execution notes. The measured gap: 83 reflected sites vs 329
identity-only `DRACONIC_DEFINE_OBJECT` types.

## Execution notes on top of the design doc

1. Phase order is P1 (editor assets + enums) -> P2 (particle modules + anim
   graph data) -> P3 (runtime gameplay surface). Do not start P(n+1) before
   P(n) is merged and its consumer visibly works (P1's consumer: the generic
   asset page goes reflection-first with enum dropdowns BY NAME).
2. Placement rule is non-negotiable: `DRACONIC_REFLECT_*` bodies go in module
   IMPLEMENTATION units (a `RegisterXTypes()` per module), never interface
   units - gcc gcm-cluster blowup. Build gcc EARLY and often during this
   track; it is the compiler that punishes mistakes here (the web track's
   clang-first drift lesson).
3. Type domains: runtime registrations go in the core "Runtime" domain;
   editor-only registrations pass `TypeDomain(u8"Editor")` so editor-only
   script bindings stay marked (see memory `type-domain-string-hash`; the
   TypeRegistry API exists).
4. Attributes: carry `displayName`, `description`, `range`, `visibleWhen`,
   plus the component-menu `category` where applicable - the scene inspector
   and component menu already consume them. Missing displayName/category on a
   new component is a review reject (standing rule).
5. Scripting methods audit (user-added scope): while reflecting each type,
   audit which METHODS scripting should reach and reflect those too (the
   facade-numerics rule applies: natural C++ types i32/i64 in method
   signatures, not f64 - the method path narrows by reflected type).
6. Do NOT reflect editor-only state onto runtime types, and do not reflect
   internal scratch - the design doc's conventions section governs.

## Tests (per phase, standing rule: nothing lands without them)

- Per newly reflected type: property enumeration + get/set round-trip
  (the cheap pattern the design doc names); enum name<->value round-trip for
  each newly reflected enum.
- A registration-completeness test per module: `RegisterXTypes()` called ->
  the expected type list resolves via GlobalTypeRegistry (this catches the
  "forgot to call the registrar at a startup site" class; registration sites:
  editor Main, cook, export, player).
- Script-surface smoke per phase: one Wren + one AngelScript test touching a
  newly reflected type through the harvest (the script test batteries have
  precedents).

## Acceptance per phase

- Both compilers green (gcc especially - see note 2), all script batteries
  green, the phase's named consumer works (P1: generic asset page shows real
  labels/dropdowns - user visual check; P2: a particle module renders an
  auto-grid; P3: script can drive the named runtime knobs).
- The measured-gap table in the design doc updated with new counts.

---

## State (appended 2026-08-03; original content above is unchanged)

**IN PROGRESS.** P1 flat assets + base Asset::fileName + SourcePath shipped
(8da015c3..fa332385). ComputedProperty getter-property primitive added (3d66a6d6,
distinct from the Nested kind). Fable BLESSED the nested-member design (see
docs/design/reflection-track.md Q1-Q3). NEXT (building now): the TypeBuilder::Nested
mechanism + consumer/harvest guards + MaterialSource/MaterialAsset as living proof.

### Update (appended 2026-08-03): P1 CLOSED

The Nested mechanism (ef383b9d) + MaterialAsset reflected through it (9edd2a45) landed,
which is Fable's Q3 bar for closing P1: flats + base fileName (shipped) + Nested mechanism +
MaterialAsset as the living proof. So P1 is CLOSED.

- Nested mechanism: TypeBuilder::Nested + PropertyFlags::Nested + IsNested; both script
  backends skip Nested props; Core rtti test (value/pointer/null).
- MaterialSource reflected (10 scalars, DataVersion 2 preserved) in a new impl unit;
  MaterialAsset carries Nested<&source>; headless recurse-via-address proof test; scene
  inspector has a defensive IsNested skip (generic asset page is serialize-driven, no guard).

DEFERRED (not P1 blockers): the u8->enum retype for render-state fields (Fable Q2 dropdowns -
real blast radius, do as a focused follow-up); InputMapAsset via Nested (if it falls out the
same way); the accessor-gated (ParticleEffectAsset) + pointer-member (Static/SkinnedMeshAsset)
nested assets fold into P2. Consumer RECURSION rendering (generic page reflection-first / a
nested sub-grid in the inspector) is the P4 consumer upgrade, not done yet - today nested
props are skipped by reflection consumers, shown by the serialize-driven page.

### Update (appended 2026-08-03): enum retype done; P1 fully closed; P2 entry mapped

Enum retype shipped (23516bf3, Fable Q2): MaterialSource's blendMode/depthMode/cullMode/
vertexLayout are now their enum types (each `: u8`, wire-identical via core::Serialize's
underlying-type path); the 4 enums are reflected (new draconic.materials impl unit) so the
property types are IsEnum with named values (dropdown-ready). samplerU/V stay u8 (AddressMode
is u32 - wire mismatch). Wire round-trip test + enum-reflection test added; bespoke Material
page aliases the enum byte for its existing dropdown. So P1 is FULLY closed.

P2 entry analysis (nested assets, now unblocked by TypeBuilder::Nested):
- **InputMapAsset** = value-member `InputMap m_map` (a plain struct). CLEANEST P2 opener -
  mirrors MaterialAsset exactly (reflect InputMap's authorable fields + Nested<&m_map>). Do first.
- **Static/SkinnedMeshAsset** = POINTER-member `MeshSource* source` - exercises the pointer path
  (Core-tested, not yet on a real asset), BUT MeshSource is mostly cooked binary geometry (low
  authorable surface); reflect only the metadata, or defer.
- **ParticleEffectAsset** = accessor-gated (ParticleEffect behind accessors) - needs the
  reflected-METHOD path, not Nested; heaviest, do last / its own unit.
- Plus the 23 particle module types + animation graph/emitter data (the bulk of P2).

Note: a pre-existing, unrelated VG.Renderer color-threshold test failure (Fable's VG track) is
present; not touched by any reflection work.

### Update (appended 2026-08-03): P2 opened - input leaf types reflected

Reflected the input-map FLAT-SCALAR leaves + enums (8e372576): Binding (17 scalars),
Interaction, ActionProcessors + BindingSource/ActionKind/InteractionKind, via a new
draconic.input impl unit + RegisterInputTypeReflection().

KEY FINDING on InputMapAsset: InputMap is a nested LIST-OF-LISTS (InputMap.sets ->
ActionSet.actions -> Action.bindings), NOT a flat struct like MaterialSource. So the full
asset reflection needs: (1) container reflection registered for each level (RegisterArrayType
<ActionSet/Action/Binding> - the IsContainer/ContainerGetAt machinery exists and is Core-tested),
(2) a Nested member on InputMapAsset for m_map, (3) consumers that RENDER a container property as
a list editor (add/remove/edit elements, recursing per element) - that last part is P4. The leaf
reflection here is the foundation; the container tree + list editor is the next input step.

Distinction worth keeping: MaterialSource was the clean flat-nested case (Nested -> scalars);
InputMap is the list-structured case (Nested -> container -> element structs). MeshSource is the
binary-blob case (low authorable surface). ParticleEffect is accessor-gated (reflected methods).

### Update (appended 2026-08-03): input reflection COMPLETE + the collections-in-scripts note

InputMap tree fully reflected (5f7dc9c1): Action/ActionSet/InputMap + Array containers
(RegisterArrayType<Binding/Action/ActionSet>) + InputMapAsset Nested<&map>; new
Draconic.Input.Editor.Tests proves end-to-end reflection traversal (asset.map -> sets ->
actions). The input editor page stays bespoke - this reflection is for SCRIPTABILITY.

**Design note - lifting the Array<Struct>/nested harvest-skip (answer to a design question):**
Nested + container members are currently SKIPPED by the script-harvest (an empty-Variant getter
would be garbage script surface). This is NOT a permanent limit. The reflection layer is being
built with exactly the primitives to lift it: Nested gives address+type (bind a nested struct as
a foreign-object handle over the address, like Entity/Scene already are), and container reflection
gives IsContainer/ContainerSize/ContainerGetAt/SetAt (map to a Wren list / AngelScript array<T>).
Lifting it is a SCRIPT-BACKEND binding feature ("reflected collections + nested access in scripts"),
not a reflection change - feasible now, not yet scheduled. Until then the skip is the safe interim.

### Update (appended 2026-08-03): particle sweep batch 1

Particle P2 opened (97d96d83): RangeFloat/RangeFloat2/RangeColor leaves + 10 flat/range modules
reflected (Lifetime/Color/Size/Rotation/MeshOrientation initializers + Gravity/Drag/Wind/
Turbulence/Vortex behaviors) via a new ParticleModulesImpl.cpp; RegisterParticleModules wires the
range registrar. Range fields Nested, Core-math (Float3) Property. Particle editor page stays bespoke.

REMAINING particle batches:
- Batch 2: EmissionShape modules (Position/Velocity) - needs EmissionShape (a tagged variant) reflected.
- Batch 3: curve-driven OverLifetime behaviors (Color/Alpha/Size/Rotation/Speed) - needs ParticleCurve
  Float/Color/Float2 + curve-key structs reflected; plus Attractor/RadialForce/Collision (+ Collision
  Plane/Sphere/Box, FlipbookSettings, TrailSettings).
- Then: the polymorphic ParticleEffect module-array wiring (Array<RefPtr<ParticleInitializer/Behavior>>)
  needs POLYMORPHIC-CONTAINER reflection (dynamic element type per entry) - a mechanism that does not
  exist yet; likely a small design decision (its own step), not needed to reflect the module types.

### Update (appended 2026-08-03): particle batches 2-3 done; TWO reflection-primitive gaps surfaced

Batch 2 (b81a7fd6): EmissionShapeType enum + EmissionShape (flat struct) + Position/Velocity
initializers. Batch 3 (1e8800ac): Attractor/RadialForce/CollisionBehavior (flat scalars) +
CollisionPlane/Sphere/Box value types. 15 of 20 module types now reflect.

The particle reflection has reached the limit of the CURRENT primitives. The rest needs two new
Core reflection primitives - each a small design decision (Opus -> Fable, like the Nested blocker):

1. **Fixed C-array member reflection (`T member[N]`).** Needed for: ParticleCurveFloat/Color/Float2
   (their `CurveKeyFloat keys[kMaxCurveKeys]`), CollisionBehavior's planes/spheres/boxes lists.
   Property<> can't hold a C-array in a Variant; RegisterArrayType is for Array<T>, not T[N]. Blocks
   the 5 curve-driven OverLifetime behaviors (Color/Alpha/Size/Rotation/Speed) + full CollisionBehavior.
   Shape: a container-style ContainerInfo over a fixed-size C-array (compile-time N; element address =
   base + i*sizeof(T)), reusing the existing IsContainer/ContainerGetAt consumer path.

2. **Polymorphic-container reflection (`Array<RefPtr<Base>>`).** Needed for the ParticleEffect module
   wiring (ParticleSystem holds Array<RefPtr<ParticleInitializer>> + <ParticleBehavior>). The existing
   container reflection is HOMOGENEOUS (fixed elementType = TypeOf<T>); a polymorphic container's element
   type is the ELEMENT's dynamic type (element->GetType()). Shape: a container flavor whose getAt returns
   a Variant/Instance keyed on the element's runtime type, so tooling recurses into the concrete module's
   reflected properties.

With those two, ParticleEffect/ParticleEffectAsset become fully reflection-traversable (scriptable). Until
then: 15/20 flat module types reflect; the particle page stays bespoke regardless.

---

## Fable DECISION on the two reflection primitives (2026-08-03)

Both approved - and the taxonomy note (flat-nested / list-structured /
binary-blob / accessor-gated) is worth keeping; carry it into the design doc's
conventions when P2 closes. First, a housekeeping fact: the "pre-existing
VG.Renderer color-threshold failure" was MY stale test, not a mystery - it
still asserted the CPU-side sRGB decode that 16107056 deliberately removed.
Fixed + pushed (bae42563); VG.Renderer.Tests green both compilers again.

### Primitive 1: fixed C-array members - approved, but COUNT-BOUND is the real primitive

Verified the actual shapes: `keys[kMaxCurveKeys]` pairs with `i32 keyCount`
(ParticleTypes.cppm ~147), and CollisionBehavior's planes/spheres/boxes each
pair with a live count whose Serialize already clamps against the max
(ParticleModules.cppm ~582-605). These are BOUNDED INLINE VECTORS, not fixed
arrays - reflecting plain N would surface garbage elements beyond the count
to tooling and scripts. So:

- Primary flavor: `TypeBuilder::BoundedArray<&T::member, &T::countMember>` -
  ContainerSize reads the count member CLAMPED to [0, N] (corrupt wire data
  must clamp, never read out of bounds - the Serialize sites already set this
  precedent); GetAt/SetAt bounds-check against the count; expose capacity N on
  ContainerInfo so a future list editor knows the add limit; add = write slot
  at count then increment, remove = shift down + decrement, both within [0,N].
- Degenerate flavor: `FixedArray<&T::member>` (size == N always) for any
  genuinely count-less array - none of the current targets need it, add it
  only if one appears.
- Element address = base + i * sizeof(T); reuse the IsContainer consumer path
  unchanged.
- Tests: size-follows-count; clamp-on-corrupt-count; GetAt at/beyond count
  fails cleanly; add/remove round-trip against the Serialize wire.

### Primitive 2: polymorphic containers - approved, READ/TRAVERSE-ONLY v1

- Shape as proposed: a container flavor (new flag, e.g.
  `ContainerFlags::PolymorphicElements`) whose static elementType stays
  TypeOf<Base> and whose getAt derefs the RefPtr and returns an Instance
  carrying the element's DYNAMIC type (Object::GetType()) + pointer.
  Consumers recurse into the concrete type's properties. Null elements
  return an empty Instance - consumers null-check (same rule as Nested
  pointer members).
- MUTATION IS OUT of v1 - explicitly. Add-element needs create-by-type
  (registry factory) + an eligible-concrete-types-of-base query + ordering
  semantics: that is its own small design (it is also exactly what a future
  generic "add module" dropdown needs), and it must not ride in as a side
  effect. The bespoke particle page keeps authoring; this primitive delivers
  traversal + scriptability.
- Script harvest keeps skipping polymorphic containers for now - the
  collections-in-scripts design note stands (bound foreign-object handles
  over address+type is the right lift, aligned with the bound Entity/Scene
  direction; schedule it as a script-backend feature, not reflection).
- Tests: heterogeneous array (two different concrete module types resolve to
  their own property sets), null element, empty array, and a full
  ParticleEffect traversal (effect -> modules -> ranges/curves) as the
  living proof.

Order: primitive 1 first (unblocks the 5 curve-driven behaviors + full
CollisionBehavior = 20/20 module types), then primitive 2 (makes
ParticleEffect/ParticleEffectAsset traversable end to end).

---

## Fable DESIGN: polymorphic-container MUTATION ("add module") - the follow-up, fully specified (2026-08-03)

User directive: complete the track with no unnecessary deferrals - so here is
the full design for the mutation side. Verified foundations, all existing:

- `TypeInfo::base` is a walkable single-inheritance chain (TypeInfo.cppm ~40)
  and `TypeRegistry::All()` enumerates every registered type - the derived-of
  query needs no new registry state.
- `GlobalSerializableRegistry().Create(typeId)` is ALREADY how the particle
  LOAD path materializes modules from their wire type tags
  (ParticleEffectResource.cppm ~49). Creating an element through the same
  factory is wire-round-trippable BY CONSTRUCTION - the save path just writes
  the new module's tag like any other.
- displayName/category attributes + the component-menu dropdown are the exact
  UI precedent to reuse.

### A) Derived-type query (Core, TypeRegistry)

`TypeRegistry::EnumerateDerived(const TypeInfo& base, Array<const TypeInfo*>& out)`
- walk All(), follow each type's base chain to match. ELIGIBILITY for
creation = the type resolves in GlobalSerializableRegistry (creatable) - which
is exactly "loadable from wire", the correct predicate; the abstract bases
fall out automatically (never registered as creatable). Sort by category then
displayName for stable UI ordering. No caching until a profile asks.

### B) Container mutation ops (Core, ContainerInfo)

Function-pointer slots alongside the existing getAt machinery - Variant-free
(RefPtr elements are non-copyable; everything goes by Instance/address):

- `emplaceByType(instance, index, const TypeInfo& concrete) -> Instance` -
  POLYMORPHIC containers: create via GlobalSerializableRegistry, insert at
  index, return the new element's Instance (dynamic type) for immediate
  editing. Fails cleanly (empty Instance) for a non-creatable type.
- `emplaceDefault(instance, index) -> Instance` - HOMOGENEOUS Array<T> and
  BoundedArray: default-construct in place. This is what the input-map list
  editor needs (add binding/action/set) - same feature, no extra design.
- `removeAt(instance, index)`, `moveElement(instance, from, to)` - both
  container flavors. Move matters: module arrays are ORDER-SENSITIVE
  (initializers/behaviors run in array order), so reorder is part of the
  authoring contract, not a nicety.
- BoundedArray: ops respect capacity (emplace at count, fail at N; the
  count member updates as ruled above).

Tests (Core): emplace-by-type inserts a live element whose dynamic type
reflects; emplace of a non-creatable type fails cleanly; remove/move
round-trip against Serialize (save -> load -> identical wire); order
preserved across the round-trip; BoundedArray capacity edge.

### C) Consumer: the generic list editor (the P4 page + inspector)

For a container property: rows render the element grid (recursing per
element's dynamic type), each row gets remove + move up/down; the header
gets "Add..." - for polymorphic containers a dropdown from
EnumerateDerived(elementBase) grouped by `category` attribute showing
`displayName` (the component-menu pattern verbatim); for homogeneous ones a
plain add button. Editing goes through the existing property-edit path so
dirty-marking/undo ride whatever the page already does - the ops themselves
are raw; the PAGE wraps them in its edit-command pattern (same as every
other inspector mutation). Attribute sweep: the 20 module types need
displayName + category attributes as part of this step (review-reject rule
already covers new components; extend it to module types here).

### D) Script surface (defined now, ships with the collections lift)

Shape decided so nothing dangles: on the bound-object path,
`effect.addBehavior("Gravity") -> handle` / `addInitializer(...)` /
`removeBehavior(handle|index)` - name resolves via EnumerateDerived +
displayName/type name match, creation through the same emplaceByType, the
returned bound handle carries address+dynamic type (the foreign-object
mechanism from the collections-in-scripts note). Lands WITH the
collections-in-scripts feature, not before it - but this section is its
spec, so that feature is now specified, not deferred-vague.

### E) Explicitly OUT (with reasons, not vagueness)

- The bespoke particle page is NOT rewritten onto this - it keeps its curve
  canvases and specialized editors (the design doc's non-goal stands). The
  generic path is for the generic page, scripts, and future module types
  that have no bespoke UI yet.
- No "duplicate element" / copy op: elements are non-copyable by design
  (RefCounted); duplication = serialize-element -> create -> deserialize,
  which is a save-format-level feature to add only if authoring demands it.

### Order

B (ops + Core tests) -> A (query) -> C (list editor consumer, closes the
input-map editor gap too) -> D rides the collections-in-scripts feature.
With primitives 1+2 and this, the reflection track has NO remaining
deferred-undesigned pieces: every remaining item is specified work.

---

## Fable CORRECTION mid-flight: invert the emplaceByType dependency (2026-08-03)

User spotted a layering wrinkle in the in-progress work and is right:
`Reflection.cppm` now does `import :serializable_registry` and calls
`GlobalSerializableRegistry()` directly (the eligibility Contains + the
emplaceByType Create). My design named the registry as the factory, which
invited this - the correction is on the DESIGN, not the implementation
reading of it.

Why it matters: serialization is a CONSUMER of reflection
(:serializable_registry imports :type_info; data versions live on TypeInfo).
Reflection importing serialization back inverts the substrate/consumer
direction inside Core, half-closes an import cycle between the partitions,
and welds "reflected + creatable" to "wire-registered" forever - a reflected
type with no wire presence could never be emplaced by any future consumer.

The fix - same behavior, dependency inverted (the function-pointer pattern
the reflection system already uses everywhere):

1. `ContainerInfo` gains two SLOTS, defined and CALLED by reflection, filled
   by whoever registers the container:
   - `createElement: Instance (*)(const Instance& container, u32 index, const TypeInfo& concrete)`
   - `canCreateElement: bool (*)(const TypeInfo& concrete)`
   Reflection.cppm loses the serializable_registry import entirely; empty
   slot = container is read-only (emplace fails cleanly), which also gives
   read-only polymorphic views for free.
2. The standard factory lives WITH the serialization partition (the correct
   direction - it may import :type_info freely, and an adapter there may use
   the registry): a small
   `SerializableRegistryElementFactory` helper next to SerializableRegistry
   returning the slot pair. The polymorphic RegisterArrayType flavor takes
   the factory as a PARAMETER; registrants (particles' impl unit et al.)
   pass the standard helper - one line each. The eligibility filter for the
   editor dropdown routes through canCreateElement, so the derived-type
   query (pure RTTI) stays layering-clean.
3. Tests unchanged in intent; add one: a polymorphic container registered
   WITHOUT a factory rejects emplace cleanly.

RULE (goes with the gcc-hygiene rule in CONVENTIONS): Core reflection
partitions import RTTI/base partitions only - never serialization, never
consumers. Capability flows INTO reflection through registration-time
function pointers.

### Update (appended 2026-08-03): Fable's two primitives + add-element ACTIONED

Both primitives Fable approved, plus the mutation follow-up (Fable A+B), are built + pushed:
- BoundedArray (0a50d638): count-bound C-array container -> particle modules 20/20.
- Polymorphic container (204cb9f7): Array<RefPtr<Base>>, read/traverse v1; module arrays register.
- Mutation ops + EnumerateDerived (a8ba3eec): emplaceByType/emplaceDefault/removeAt/moveElement on
  ContainerInfo + the creatable-derived-type query; homogeneous/bounded/polymorphic all wired.
  Tests green clang+gcc, ASAN clean on the emplaceByType refcount path.

REMAINING (both consumer-layer, not the mechanism):
- Fable C: the generic LIST-EDITOR consumer (P4 page + inspector) - renders a container property
  as add/remove/reorder rows, recursing per element; "Add..." dropdown from EnumerateDerived
  (component-menu pattern). Needs the module types' displayName+category attributes. Visual work.
- Fable D: the SCRIPT surface (effect.addBehavior(...) etc.) - rides the queued collections-in-
  scripts feature ([[script-collections-queued]] equivalent), lands with it, not before.
- ParticleEffect/ParticleSystem member wiring (private members + the systems Array<UniquePtr<>>)
  for the full effect-level traversal - a small wiring step on top of the primitives.

### Update (appended 2026-08-03): Fable's layering correction ACTIONED (77a754f0)

My add-element impl imported :serializable_registry into reflection (a partition-cycle wrinkle).
Fable's fix applied: capability flows into reflection as registration-time function pointers, never
an import. ContainerInfo.emplaceByType -> createElement + canCreateElement slots (reflection calls,
never fills; both null = read-only container, a free feature); RegisterPolymorphicArrayType<Base>
(create, canCreate) takes the factory as params; the standard serialization adapter
(Create/CanCreateSerializableElement<Base>) lives WITH the serialization partition and the particles
impl unit passes it; EnumerateDerived is now pure RTTI (dropdown eligibility routes through
canCreateElement). Reflection imports RTTI/base partitions only again. Added test: no-factory
container rejects create cleanly. Fable added the binding rule to CONVENTIONS.md.

### Update (appended 2026-08-04): collections-in-scripts + list editor SHIPPED; remaining = weekend (Fable review first)

Big arc since the last update is landed. State of the track:

**DONE**
- P1 CLOSED (Nested + ComputedProperty + enum retype/dropdowns + flat editor assets + base fileName +
  MaterialAsset proof).
- P2 input DONE (full InputMap tree + InputMapAsset; page stays bespoke) and particles DONE (all 20
  modules reflected; whole effect graph traverses effect -> systems -> modules -> ranges/curves).
- The two Fable-reviewed primitives SHIPPED: BoundedArray (count-bound C-array) + polymorphic container
  (create-by-type via registration-time function pointers, never an import). Plus ContainerInfo.addressAt
  (borrowed-Instance element descent), RegisterUniquePtrArrayType, and addressAt on HOMOGENEOUS value
  containers (commit f864454e - needed so the list editor can reach/edit each element).
- COLLECTIONS-IN-SCRIPTS lift SHIPPED, BOTH backends (Fable D as the generic mechanism, not a one-off):
  constructor-less returned handles (Wren reachability-closure emitter; AngelScript already did it);
  container ops NAME_count / NAME_at / NAME_add(typeName) / NAME_removeAt / NAME_move; and Fable-approved
  Variant BORROW MODE (nested-value + non-Object elements) with a mutation-generation guard (a borrow is
  a cached raw pointer; structural mutation invalidates it -> clean empty, never a UAF). IsObject()
  excludes borrows; value-mode parents refuse; write-through in place. Green clang+gcc, ASAN-clean.
- Particle effect-graph value types published to the global registry (so the harvest/closure reaches
  the modules once a facade returns an effect handle).
- GENERIC LIST EDITOR (Fable C) SHIPPED + polished into real UI this session: a ContainerListEditor
  (one grid row = header add icon over a slot row per element: a picker slot that fills + move-up /
  move-down / remove ICON buttons), driven by the reflection container ops through a generic
  MutateComponent (one undo step each), with a content-diff refresher for undo/redo. New reusable
  controls: ui::IconButton (themable + tested + markup-registered) and app::AssetPickerSlot. Component
  Copy/Remove moved into the collapsible section HEADER (right-aligned icons, non-toggling); a dedicated
  Paste Component button (shown only when the clipboard holds one) with an overwrite-confirmation modal.
  Live on-screen for mesh materials (materials reflected as a container; kUseReflectedMaterialSlots flag
  switches reflected<->bespoke, both rendering through the same ContainerListEditor). Also fixed a
  pre-existing bug: RemoveComponent undo did not re-bind resource proxies (mesh went invisible).

Session commits (local, unpushed): f864454e reflection addressAt; b4a5a8a8 IconButton; 1f03db02
Expander/PropertyGrid header actions; 1a999a94 AssetPickerSlot + icons; 337495d9 remove-undo fix;
d716af78 materials-as-container; ee45207e the inspector list editor + header actions + paste.

**REMAINING (build on the weekend, after Fable review)**
1. P4 REFLECTION-FIRST GENERIC ASSET PAGE (highest value). The generic asset page is still
   serialize-driven; making it reflection-first lights up the list editor + enum-by-name dropdowns for
   ParticleEffectAsset (systems -> modules -> curves) - the fuller showcase and the list editor's home
   beyond scene components.
2. LIST-EDITOR POLISH: the polymorphic add-by-type menu (category-grouped from EnumerateDerived) +
   struct-element leaf editing (the particle-module case). All primitives exist; UI wiring in
   ContainerListEditor. The element-pick is currently specialized to Ref<Material>.
3. PARTICLE GAMEPLAY FACADE so a script can REACH a ParticleEffect handle (value types registered; the
   closure reaches the modules once a facade returns an effect). Out-of-tree per [[facade-pattern]].
4. P3 runtime gameplay surface + enum sweep (not started): audio/physics/animation/camera-light knobs +
   the gameplay-meaningful subset of the ~236 unreflected enums.
5. Small deferrals: ParticleSystem trail/flipbook nested config; non-EmissionMode enums stay
   identity-typed Property (dropdown-ready once reflected as enums).

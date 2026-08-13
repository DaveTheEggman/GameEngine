# EntityRef - a typed, inspectable entity reference

> Status: P1 IN PROGRESS - the type + JointComponent migration landed; inspector picker + prefab
> remap open. See "Build state" below.
> Track: editor / scene / reflection
> Author: Opus, 2026-08-13 (from a design exchange). Land in pieces; verify the editor picker on screen.

**Motivation.** Components reference other entities by a bare `Guid` (today only
`JointComponent::targetEntity`). A bare `Guid` is ambiguous - the inspector can't tell an
entity reference from an asset guid or an arbitrary id, so it renders **no editor at all** for
such fields (its dispatch keys off `resource::Ref<T>` types, `f32`, `Color`, `bool`, `Float3`,
enums - `Guid` falls through). And prefab instancing only remaps component *owners*, not guids
stored *inside* fields, so a raw-`Guid` cross-entity reference silently breaks when a prefab is
instanced (it keeps pointing at the template entity). `JointComponent` works around that with a
"nil = nearest ancestor via hierarchy" convention precisely because its `Guid` target isn't
remapped.

A dedicated **type** fixes both at the root, mirroring how `resource::Ref<T>` already gives
assets a type the inspector and tooling dispatch on.

## The type (Decision)

`foundation::scene::EntityRef` - a thin value type wrapping the persistent `Guid`, living beside
`EntityHandle` in `Foundation/Scene/Entity.cppm`:

- **Persistent vs live split** mirrors `resource::Ref<T>` (persistent) vs a live resource pointer:
  `EntityRef` = the serialized reference (a `Guid`); `EntityHandle` = the transient live
  index+generation. Resolve `EntityRef` -> `EntityHandle` at the point of use via
  `Scene::FindEntity(ref.id)`.
- **Dumb holder, no cached handle.** It does not resolve or cache - consistent with the already-
  decided scripting rule that entity access *re-resolves* rather than borrows (game-ready-scripting
  spec). Runtime and inspector agree.
- **Wire-identical to a bare `Guid`.** Its serialization writes only the inner guid, so migrating a
  field from `Guid` to `EntityRef` is a SOURCE-only change - the on-disk/wire format is unchanged.
- **Implicit from `Guid`** (like `Ref<T>`'s implicit constructors) so `ref = scene.GetEntityId(h)`
  and existing assignments keep compiling.

## Phases

**P1 - the type + first migration (LANDED).** Define `EntityRef` (+ identity-only `Serialize`);
migrate `JointComponent::targetEntity` from `Guid` to `EntityRef` and its use sites; wire-compatible;
both compilers green. No behavior change yet (the inspector still renders nothing for the field until
P2 - but nothing regresses, since the bare `Guid` rendered nothing either).

**P2 - the inspector entity picker.** A `BuildPropertyRow` dispatch branch on `TypeOf<EntityRef>`
that renders the same `ResourceRefEditor` widget the asset pickers use, but populated from a
**scene-entity menu** (`ForEachEntity` -> name items, `(none)` first) - reusing the exact pattern the
script-behavior entity picker (`BuildScriptEntityPropertyRow`) already uses, but writing a reflected
component property instead of a script override. Write goes through a new undoable
`SetEntityRefCommand` (mirror of `SetResourceRefCommand<T>`: re-derive the property address each
apply, cast to `EntityRef*`, set `.id`; no resource rebind) exposed as
`SceneEditContext::SetComponentEntityRef`. **Needs an in-editor visual check** (headless builds only
compile it). Once landed, `JointComponent::targetEntity` is pickable in the inspector.

**P3 - reflection-driven prefab remap.** Teach prefab instancing to walk reflected component
properties, find `EntityRef`-typed fields, and remap their guids through the instance's
old->new guid table (the same table that already remaps owners). This makes cross-entity references
prefab-safe *generally* - and lets `JointComponent` drop its "nil = ancestor" workaround (or keep it
as an explicit convenience, no longer a necessity). Requires `EntityRef` reflection registration so
the remapper can discover fields by type.

## What this deliberately does NOT touch

- The live `EntityHandle` path (unchanged; `EntityRef` is only the persistent reference).
- Script-behavior entity properties (already have a picker via `ScriptPropertyType::Entity`); P3's
  remap could later unify with them, out of scope here.
- Multi-entity references / entity lists (a container of `EntityRef` would ride the existing
  collections-in-inspector path once the scalar type exists; not in this track).

## Build state (2026-08-13)

**Done (P1):**
- `foundation::scene::EntityRef` (value type + implicit-from-`Guid` + `IsNil`/`Id`/`==`) and its
  identity-only `Serialize`.
- `JointComponent::targetEntity` migrated `Guid` -> `EntityRef`; the three use sites
  (subsystem resolve, hand-written `Serialize` via `.id`, test assignment via implicit ctor) updated;
  wire format unchanged.
- Both compilers green.

**Open (resume here):**
- **P2** - the inspector `EntityRef` picker (`SetEntityRefCommand` + `SetComponentEntityRef` +
  `BuildEntityRefRow` + the `TypeOf<EntityRef>` dispatch branch). Then visual-verify in the editor:
  select a jointed entity, pick its target, confirm the row shows the target's name and undo works.
- **P3** - reflection-driven prefab-remap of `EntityRef` fields; then retire the joint's forced
  nil/hierarchy workaround.

## Acceptance (track)

Both compilers + wasm green; `EntityRef` serializes byte-identical to the prior `Guid` (existing
scenes with a set `targetEntity` load unchanged); the inspector renders an entity picker for the
joint target (P2, visual); a prefab containing an intra-prefab `EntityRef` instances with the target
remapped to the instance's copy (P3).

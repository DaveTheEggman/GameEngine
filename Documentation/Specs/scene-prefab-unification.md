# Scene / prefab model: unification, and the Scene -> World rename

Status: WIP DESIGN QUESTION (Opus, 2026-08-18). NOT a build spec - an evaluation
for Fable to weigh in on. Origin: the week-2026-08-15 "weigh unifying scenes and
prefabs" question, plus the user's proposal to rename `Scene` -> `World`. No
decision is made here; unification is explicitly NOT a foregone conclusion. The
one thing the user leans toward regardless of the outcome is the rename (Part A).

The concrete trigger was the model importer gaining a "Generate scene" toggle
(9f84e205): the scene and prefab generators now share `BuildModelScene`, and the
two encoders differ by so little that it raised the question of whether the two
concepts should be one.

## The two questions (they are independent)

We kept collapsing three separate decisions into one. Separating them is the
main contribution of this doc:

- **Part A - Rename `Scene` -> `World`.** Naming only. The user wants this
  regardless of B/C. It is the cheapest and least controversial change and it
  removes most of the "instantiate a scene into a scene" circularity that makes
  the other two questions hard to even discuss.
- **Part B - Should scene-global settings become components?** ("everything is a
  component"). This is what we assumed was REQUIRED to unify. Prior art says it
  is not (see below).
- **Part C - Should `SceneDocument` and `PrefabDocument` become one instanceable
  graph type?** This is the actual "unify scenes and prefabs" question.

B and C are separable. You can do C without B (prior art does exactly this). You
can do A without either.

## What exists today (verified against code, 2026-08-18)

A `SceneSystem` (Code/Foundation/Scene/SceneSystem.cppm) plays up to three roles,
and this is the whole crux:

1. **Component storage** - `AsComponentManager()` returns non-null
   (SceneSystem.cppm:41). Per-entity component data (mesh, light, transform,
   collider, ...). Serialized in BOTH scenes and prefabs, keyed by owning entity.
   No divergence here.
2. **Scene-global settings** - `SettingsType()` returns non-null
   (SceneSystem.cppm:68). A per-scene singleton config struct. Present on:
   the render env system + the post-process system
   (Code/Engine/Engine.Render/RenderComponents.cppm:532, 634),
   `PhysicsSubsystem` (Code/Engine/Engine.Physics/PhysicsSubsystem.cppm:92),
   `ScriptSubsystem` (Code/Engine/Engine.Script/ScriptSubsystem.cppm:1210).
3. **Pure logic** - neither of the above. Update / fixed-update behavior, no
   serialized state.

`Scene` (Code/Foundation/Scene/Scene.cppm:33) owns the systems
(`m_systems`, Scene.cppm:484); `AddSystem<T>` requires `T : SceneSystem`
(Scene.cppm:280).

The ENTIRE data-model difference between a scene and a prefab is role #2.
`SerializeScene` (Code/Foundation/Scene.Resource/SceneResource.cppm:868) takes a
`ScenePrefabMode { Referenced, Expanded }` (SceneResource.cppm:845) and an
`includeSettings` flag. The comment at SceneResource.cppm:865 states the rule
directly:

> prefab payloads write an EMPTY system-settings section (a prefab is a subtree
> template, not a world - and SpawnPrefab must be able to walk PAST the section
> to reach the nested-instance records without applying settings to the target
> scene).

So a prefab is a rooted subtree of entities+components with NO global settings; a
scene is a forest of entities+components PLUS the global settings. Everything
else about prefabs (single root, `Expanded`/`Referenced`, per-instance overrides,
placement transforms, `RebuildPrefabInstances` re-sync) is about INSTANCING, not
about systems.

`SceneDocument` and `PrefabDocument` both live in foundation.scene.resource and
both back onto the same `SerializeScene` core (one serializer, two encoders,
two document primaries).

## Prior art

### Bevy

- **`World`** - the live runtime container: entities, components, and
  **resources**. Resources are singleton global data, NOT components on an
  entity; they live in the World as their own kind.
- **`Scene` / `DynamicScene`** - a serializable graph of entities+components
  (DynamicScene can also serialize resources). You SPAWN it into a World. The
  spawned occurrence gets an `InstanceId` and is called a **scene instance**.
  A scene can be spawned many times, and scenes nest (a scene's entities can
  themselves be scene instances).
- Bevy has **no separate prefab type** - a scene IS the prefab; "prefab" is a
  usage, not a type. This is the unification we are considering.
- Bevy did NOT make everything a component to get there: it kept Resources as
  global singletons. There is a long-running community discussion about folding
  resources into components-on-a-singleton-entity, but as of this writing it is
  a debate, not the shipped default.

### ezEngine

- **`ezWorld`** - the runtime container, holding `ezGameObject`s and per-type
  component managers. This is the live simulation. **It maps exactly onto our
  `Scene`.**
- **Scenes and prefabs** - serialized object graphs (`ezPrefabResource` etc.)
  instantiated INTO an `ezWorld`. A prefab instantiates a subtree; a scene loads
  as the world's contents.
- **World modules (`ezWorldModule`)** - where world-level config and global
  systems live. Global data stays attached to the world, NOT turned into
  components.

### What the prior art actually tells us

Both engines land on the same shape: **live World + serialized instanceable graph
+ live instantiated occurrence**, and BOTH keep global data global (resources /
world modules). Neither reached unification by making everything a component.

This directly challenges the premise we started from ("the only way unifying
works is if everything is a component"). The prior art says: keep global data
global, keep the World-vs-template split, and make the TEMPLATE side uniform. In
other words, **Part C is reachable without Part B.**

And our existing "empty settings section for prefabs" rule is already the
correct prior-art behavior, just stated awkwardly: when a graph is INSTANCED into
a world you must NOT re-apply its global settings (a nested prop scene should not
reset gravity); when a graph is LOADED AS the world root, you DO apply them. That
is a per-role application rule on ONE optional settings block, and it needs zero
settings-as-components work.

## Part A - Rename `Scene` -> `World`

We named the live container `Scene`. Bevy and ezEngine reserve "Scene" for the
serialized asset and call the live thing "World". That single choice is why
"instantiate a scene into a scene" reads as circular for us and does not for
them, and it is why Parts B/C are hard to even name.

Proposed vocabulary (three layers, matching Bevy's terms):

| Layer | Bevy | ezEngine | Us today | Proposed |
|---|---|---|---|---|
| Live runtime container | `World` | `ezWorld` | `scene::Scene` | `World` |
| Serialized graph asset | `Scene`/`DynamicScene` | ezScene / ezPrefab | `SceneDocument` + `PrefabDocument` | `Scene` (asset), `Prefab` (asset) |
| Live instantiated occurrence | scene instance (`InstanceId`) | prefab instance | prefab instance | `SceneInstance` |

Note `SceneInstance` belongs to layer 3 (the live occurrence with a root +
override tracking), NOT to the asset. Using it for the asset would collide with
this meaning.

Pros:
- Removes the terminology knot before we decide B/C; the rest of the discussion
  gets easier to have.
- Aligns with the two engines we are drawing from, so future contributors map
  concepts for free.
- `World` is the accurate word for a thing that owns simulation lifetime and
  holds the component managers.

Cons / cost:
- Large mechanical rename: `scene::Scene` -> `world::World`? or keep the
  `scene::` namespace and rename just the type? This touches a very large
  surface (Scene, SceneSubsystem, SceneManager, per-scene APIs, docs, script
  facades where `Scene` is a bound value type - see scene-scripting tier).
- The SCRIPT surface exposes `Scene` as a reserved bound name (scene-scripting
  tier). Renaming ripples into user-facing script API and every sample/behavior.
- Churn in ~60 memory/doc references and the Systems docs.
- Risk of a half-rename that leaves both terms in the tree (worse than either).

Open sub-question: rename the TYPE only (`Scene` -> `World`, namespace stays
`scene::`), or the namespace too? Type-only is far cheaper and still buys the
clarity. Recommendation leans type-only, staged, with a mechanical pass + a grep
tripwire for the old name.

## Part B - Settings as components ("everything is a component")

Fold role #2 (per-scene settings singletons) into role #1 (components on
entities). A well-known singleton entity would carry `EnvironmentComponent`,
`PostProcessComponent`, `PhysicsWorldComponent`, `ScriptConfigComponent`.

Pros:
- Uniform serialization, inspection, undo/snapshot, and reflection - settings
  ride the same machinery components already use.
- Settings become OVERRIDABLE per instance for free (a prefab/scene instance
  could carry an environment override), which is a real capability we do not
  have today.
- Deletes the bespoke settings-section wire and its `BeginVersionedPayload`
  handling.

Cons:
- Trades a guaranteed invariant (`SettingsType()` = exactly one, always present,
  O(1) access from the owning system) for a runtime-enforced convention (a magic
  singleton entity that may be zero or two; every system must defend against
  both, and re-find its settings each time or cache a handle).
- Muddies queries / iteration: a "settings entity" shows up in the hierarchy and
  in component iteration where it does not belong, or you special-case it out -
  reintroducing the special-casing you were deleting. This is precisely why Bevy
  kept Resources separate.
- Editor UX regression risk: the scene-settings panel is discoverable today; a
  component on a hidden entity is not, unless you build bespoke UI (which partly
  undoes the "uniform" win).
- Serializer churn under the strict-versioning rules: every `SettingsType`
  (env, post-process, physics, script) migrates wire format with back-compat
  readers for existing scenes.
- Prior art evidence is AGAINST this being necessary (Bevy Resources, ezEngine
  world modules). It is a legitimate design, but it is not the price of
  unification.

## Part C - One instanceable graph type (unify Scene and Prefab)

Merge `SceneDocument` and `PrefabDocument` into a single serialized-graph type.
"Scene vs prefab" becomes a USAGE (loaded-as-world-root vs instanced-as-subtree),
not a type. Settings live on the graph as one optional block, applied only at
root load, ignored on instance (the existing rule, generalized).

Pros:
- Any graph can be instanced into any world (nested / streamed sub-scenes) - a
  real new capability, and the Godot/Bevy model users may expect.
- One import path, one edit path, one wire; the "generate scene vs prefab"
  branch collapses.
- Per-placement overrides on what used to be scenes come along with the prefab
  override machinery.

Cons:
- Every graph inherits prefab complexity (re-sync, overrides, Expanded/
  Referenced, placement) even a top-level level that will never be instanced.
  More invariants to hold for the common case.
- Rooting mismatch: prefabs are single-root, scenes are forests. Unifying forces
  a synthetic root on scenes, or you allow multi-root instanceables (which
  complicates instancing and the re-sync identity model).
- Lifecycle: a World owns simulation lifetime; a template is inert. One type
  still needs a clear "loaded-as-world vs instanced" mode, so you partly
  reintroduce the split as a flag and save less than it appears.
- The practical DRY win is ALREADY largely banked: `BuildModelScene` is shared,
  and the two encoders differ only by the settings section + the root wrapper.
  A full merge buys new capability (nesting, per-instance overrides on scenes),
  not much new code-sharing.

## Options on the table

- **Option 0 - Do nothing (beyond Part A).** Keep two document types. Legitimate:
  we already share the builder; the split is not debt. Cost: none. Loses:
  nested/streamed scenes, per-instance scene overrides.
- **Option 1 - Part A only.** Rename now; defer B and C. Buys clarity, commits to
  nothing.
- **Option 2 - Part A + Part C, NOT Part B** (the prior-art path). One graph type,
  settings stay system-owned and apply only at root load. Reaches unification
  without the settings migration. This is the Bevy/ezEngine shape.
- **Option 3 - Part A + B + C** (full "everything is a component" + one type). The
  maximal version. Highest cost; the singleton-component ergonomics and settings
  wire migration are the price.

## Tentative recommendation (Opus, for Fable to challenge)

- Part A (rename): worth doing, type-only and staged, largely independent of the
  rest. The main risk is the script-surface `Scene` name; that needs its own
  small migration plan.
- Between the unification options: if we move at all, **Option 2** is the one the
  prior art supports - unify the template side, keep global settings global,
  apply-at-root-only. Do NOT reach for Part B just to unify.
- BUT I would not commit to Option 2 on elegance alone. Nothing forces it today.
  Pull the trigger when a concrete feature asks: nested/streamed sub-scenes, or
  per-placement overrides on scenes. Absent that, Option 1 (rename now, split
  stays) is a defensible resting state, not technical debt.

## Open questions for Fable

1. Rename scope: type-only (`Scene` -> `World`, keep `scene::`) vs namespace too?
   And how do we stage the script-surface `Scene` name without breaking user
   behaviors / samples in one commit?
2. Do you agree Part C is reachable without Part B (prior-art claim), or is there
   a reason in OUR wire/instancing model that ties them together?
3. Is there a near-term feature that actually WANTS nested/streamed scenes or
   per-placement scene overrides? That is the trigger that would move us off
   Option 0/1.
4. Rooting: if we unify, synthetic single root on every graph, or first-class
   multi-root instanceables? The re-sync identity model (RebuildPrefabInstances)
   is the constraint to check here.
5. Anything in the destroy-undo snapshot path (serializable managers) that the
   settings-as-components move (Part B) would simplify OR break?

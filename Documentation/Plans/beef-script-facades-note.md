# Note for the Beef agent: put facades under `scene.Physics`

Written 2026-09-19 after reviewing 392cda66..2bd83666. A recommendation, not a review finding:
the machinery is right, the target is wrong. Keep the spelling, change what it binds to.

## The observation

The runtime surface (`EngineScriptSurface`, 156 types, 815 members bound, 9 blocked) is the
engine's own types filtered by `[Scriptable]`. That makes the script API the engine API. Three
consequences, each small now and large once game scripts exist:

- **Stability.** A script-only project and a mod author program against this surface. Every
  engine refactor (a rename, a manager split, a field that becomes a property) is a script
  compatibility question, answered one `[ScriptName]` at a time. There is no layer to version.
- **Shape.** Engine methods are engine-shaped: `(EntityHandle, ...)` on a manager, a `Guid` where
  a script wants "the asset", `List<T>` members the language cannot take (the nine Blocked
  members, five `List<T>` skips in the AngelScript binding). A facade is written in script shape
  once and the generator never has to skip or block anything.
- **Composites.** A verb that touches two systems (set a position AND wake the physics body)
  has no owner on a role-derived surface; it lands on whichever manager is nearest. Raptor's
  three "remaining facade verbs" commits on your side (e5128067, 081c1e44, 8cae0adb) already
  ARE facade bodies - they sit on the managers only because there was nowhere else.

Raptor's answer is a hand-written facade set (Entity, Scene, ScenePhysics, SceneAnimation,
SceneAudio, SceneRender, Input, Audio, DebugDraw, Time, Random, Log, Net, Ui, plus the
per-component `.of` views), spelled `ScenePhysics.of(scene).rayCast(...)`. Your spelling,
`scene.Physics.RayCast(...)`, is better. The two are separable: keep yours.

## Keep everything that is binding machinery

Nothing below touches: `ScriptSurfaceWalker` and its comptime walk, the domains and the
build-time domain gate, `ScriptValue`/`ScriptCallFrame`/`ScriptCallContext`, the thunks and
their frame checks, `ScriptOverloadResolver`, `ScriptValueMap`, the AngelScript trampoline and
its type mapping, the cook binding the runtime subset, the Null runtime's listing, the count
tripwire. All of that is better than Raptor's hand-written registration and stays as it is.

## The change: a facade role, and a facade closure

1. **A `Facade` role** beside SceneSystem/ComponentManager/Service. A facade is a plain marked
   class the engine does not otherwise know: `[Scriptable(.AllPublic), SceneFacade("Physics")]
   class PhysicsFacade`. Its self resolves the way a scene-system thunk's does today, but to
   the facade instance for the scene: one per scene, created on first use and owned by the
   run's script host (or the `ScriptSceneSystem`, which already knows its scene), holding the
   borrowed `Scene` and looking its systems up as it needs them. `SceneFacade("Physics")` is
   the property name on `Scene`; a `ServiceFacade("Audio")` variant is the global handle. The
   AngelScript binding needs no new mapping: it already binds a scene system as a read-only
   property on Scene and a service as a global; the facade role reuses both.
2. **Facade bodies** in script shape: `RayCast(Float3 from, Float3 direction, float maxDistance)
   -> PhysicsHit`, `SetMesh(ScriptEntity entity, Guid mesh)`, `Play(ScriptEntity entity)`, and
   the composites. They call the managers and systems exactly as the marked verbs do now; the
   marked verbs on the managers move here and lose their `[Scriptable]`.
3. **The runtime root's closure becomes the facades plus the value types they pass** (Float3,
   Quaternion, Color, Guid, ScriptEntity, PhysicsHit, SplineHit, VoiceHandle, ActionRef, the
   enums). Engine subsystems drop out of the closure, so `EngineScriptSurface` shrinks to a few
   dozen types and the inspector's `[Scriptable]` marks on components keep their other job
   (the walker can keep reading them for the editor surface; they simply are not in the
   runtime root any more). Components a script needs reach it through `entity`-shaped facade
   verbs or a per-component facade (`MeshComponent(entity)` is already close to that).
4. **Blocked = 0 becomes an assertion** on the runtime root, not a listing note: a facade with
   a member the frame cannot carry is a facade bug.

## Migration, in order

- Add the role to the walker and the resolver to the thunk emitter (one new case in
  `SelfPrologue`, one attribute), with a fixture facade in `Sedulous.Script.Fixture` and the
  walker/thunk/AngelScript tests extended by one case each.
- Write the facades from the verbs already on the managers (physics, animation, audio, render,
  input, debug draw) plus Entity and Scene shapes; each one lands with its cook-time check and
  a script in `Engine.ScriptSurface.AngelScript.Tests` that drives it (Raptor's rule: a facade
  lands only with something that uses it).
- Re-root `EngineScriptSurface` on the facades; re-pin the count; regenerate the listing.
- Unmark the manager verbs; leave `[Scriptable]` on components/value types where the editor
  reads it.

## What you decide

Whether `ScriptEntity` stays a value the facades take (I would keep it: scene-bound, 16 bytes)
or becomes a facade itself with methods; whether the per-component facades are generated from
the component marks (a walker role: a component's marked fields become a facade over
`manager.Get(entity)`) or written; and the names. None of that blocks the first step.

The reason to do it now: no game script exists yet, so the reversal costs a day. After the first
PaperKid-sized game it costs every script.

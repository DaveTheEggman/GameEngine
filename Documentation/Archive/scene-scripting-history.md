# Scene-level scripting (the third tier)  (archived)

> Status: ARCHIVED - fully built. Non-authoritative: this is the original build spec, kept
> as the record of what was built and why; present-tense truth is the code + tests.

Size: M. Modules: `Code/Draconic/Engine/Draconic.Engine.Script/` (the scene
system + dispatch), `Draconic.Foundation/Draconic.Scene` (nothing - the seam
exists), editor scene-settings inspector (should light up automatically),
script batteries.

## Verdict (Fable, 2026-08-03): strong yes - user's proposal, endorsed

This is the Unreal Level Blueprint / Godot scene-root-script tier, and the
architecture already has the exact seam. The user's framing is right: scene
settings are one-instance-per-scene, and `SceneSystem` (SceneSystem.cppm)
already affords EVERYTHING the tier needs:

- `SettingsType/SettingsInstance/SettingsId/SerializeSettings` - the
  one-per-scene authored settings block (the PostProcessSettings precedent),
  serialized with the scene, edited in the scene-settings inspector.
- `ResolveResources(manager)` - binds the settings' `Ref<ScriptResource>`,
  which means the scene script ALSO rides the async level-load path
  (AsyncBindScope wraps ResolveSceneResources) with zero extra work.
- `OnSceneStarted/OnSceneStopped/OnUpdate(phase, dt)/OnFixedUpdate` - the
  whole lifecycle, already simulation-gated the way behaviors are.

The tier table it completes (each with a distinct time semantic):

| Tier | Unit | Lifecycle | Time |
|---|---|---|---|
| GameInstance script | the run | launch/update/exit, scene-agnostic | always ticks |
| **Scene script (NEW)** | one per scene | scene start/stop | scene dt, pauses with sim |
| Behaviors | per entity | component lifetime | scene dt, per-entity |

The gap it fills is real: level logic (wave spawners, door/trigger logic,
cutscene sequencing, per-level rules) currently needs a dummy entity carrying
a behavior. A discoverable slot in scene settings is the honest home, and a
level loaded via SceneLoader brings its own logic with it.

## Design

1. **`SceneScriptSettings`** on a new `SceneScriptSystem` (Engine.Script):
   `{ resource::Ref<script::ScriptResource> script; bool enabled = true; }`.
   Reflected with displayName/description attributes so the scene-settings
   inspector renders it (Ref picker needs its dispatch entry if the settings
   inspector shares the InspectorView table - verify). Scene wire version
   bumps per the wire-symmetry rules.
2. **Contract class: `class Level`** with `onStart()`, `onUpdate(dt)`,
   `onStop()` - all optional (dispatch by presence, like behaviors).
   `Level` joins the reserved-name list in docs/design/adding-facades.md
   (the SceneLoader name-clash lesson: no facade may ever take this name).
3. **One script OBJECT per scene**: instantiate on `OnSceneStarted` from the
   bound resource, on the OWNING INSTANCE's run host (one gameplay context
   per instance; the behavior re-bind machinery in GameInstance::CreateScene
   already routes managers to the right host - follow it). Two additive
   scenes sharing one script resource get independent instances. Destroy the
   object on OnSceneStopped - no state survives a stop (same rule as
   behaviors; Simulate stop/start = fresh object).
4. **Dispatch semantics**: `onUpdate(dt)` gets SCENE gameplay dt (host x
   context x instance x scene scale) and ticks ONLY while simulation is
   enabled - the deliberate difference from the instance script. Runs in the
   same phase as behavior updates, BEFORE the entity behaviors of its scene
   (the level orchestrates, entities react). Coroutines work exactly as they
   do for behaviors (same run host).
5. **Facade routing**: during Level callbacks, the Scene facade's
   currentScene must be the OWNING scene (the same routing swap behavior
   dispatch performs) - Scene.spawn/find from a Level target its own scene,
   including in additive multi-scene setups.
6. **Editor**: no dispatch outside Simulate/play (behaviors' rule). Hot
   reload rides the script-resource reload path behaviors already use.
   The scene-settings inspector shows the block with a script picker - the
   authoring surface falls out of the settings machinery.
7. **Error handling**: a faulting onUpdate disables THIS scene's script and
   logs (mirror GameInstance::TickScript's fault path) - never the instance
   script or other scenes.

## PREREQUISITE - the Scene facade's ambient-currentScene safety (Opus -> Fable review, 2026-08-03)

User directive: scripts must be safe "without gotchas", for the NEW Level tier AND
the existing behaviors; a breaking change is acceptable if it is the best way.
This must be settled BEFORE the Level tier is built (design point 5 depends on it),
because the tier would otherwise inherit an existing fragility.

### What the Scene facade is today

`Scene` (ScriptFacades.cppm:255) is a STATIC, AMBIENT facade: scripts call
`Scene.spawn(prefab,x,y,z)` / `Scene.find(name)` / `Scene.findByPath(path)`, and
each call resolves `ScriptRuntimeBinding::currentScene` (a mutable `scene::Scene*`
on the per-context `"script.runtime"` service). It means "operate on whatever
scene is executing my code right now." Entity handles, by contrast, are
scene-BOUND (each `Entity` carries its own `scene` pointer), so entity ops are
already safe; only the static `Scene.*` calls depend on the ambient value.

### The bug (existing, not just the new tier)

`currentScene` is set in EXACTLY ONE place - ScriptSubsystem.cppm:465 - coarsely
around the Update-phase tick (`binding.currentScene = m_scene;` then
TickBehaviors / DrainMessages / coroutine resume). But behavior methods are all
invoked through one choke point, `InvokeHandler` -> `instance->Invoke(...)`
(ScriptSubsystem.cppm:884), from contexts BEYOND that tick window:

- `onUpdate` / `onEnable` / `onStart` - inside TickBehaviors -> covered today.
- `onDestroy` (ScriptSubsystem.cppm:925) - entity removal, can run outside the tick.
- Physics / contact events (`onContactBegin`, delivered via InvokeHandler) - may
  fire on the FIXED step, a different phase than the Update swap.
- Editor property setters (ScriptSubsystem.cppm:815) - inspector-driven, no tick.

`Scene.spawn/find` from any of those reads a STALE or NULL `currentScene` today.
So this is a real behavior-scripting footgun, and the Level tier would copy it.

### Options

**A - Invocation-scoped ambient (minimal, non-breaking).** Move the currentScene
set out of the per-tick line and INTO `InvokeHandler` (and the coroutine-resume
path), as an RAII push/pop keyed to the invoked script's OWN scene:
`prev = binding.currentScene; binding.currentScene = <this script's scene>;
Invoke(...); binding.currentScene = prev;`. Because it wraps the actual
invocation, every entry point (update/destroy/physics/message/editor) gets the
right scene automatically; nesting/re-entrancy restore correctly. No script-facing
change - `Scene.*` stays static and ambient, but the ambient value is always
correct AT THE MOMENT script code runs. RESIDUAL gotcha: any mechanism that
executes script code WITHOUT going through a scene-aware seam (a stored delegate
the engine calls back later, a scheduler/timer callback) would still misroute
unless that seam also pushes a scene. Safety is "correct as long as every
execution entry is scoped" - a discipline the codebase must keep holding.

**B - Bound scene handle (breaking, structurally safe).** Remove the ambient
model. Each script instance receives its scene as data - e.g. `self.scene` (a
bound `Scene` object carrying its `scene::Scene*`) injected at bind time - and
calls `self.scene.spawn(...)` / `self.scene.find(...)`. No ambient
`currentScene`, no push/pop, so misrouting is IMPOSSIBLE regardless of who calls
the script or when (coroutine, stored callback, timer, cross-scene). Cost: a
breaking API change (every script using `Scene.*` migrates to `self.scene.*`) +
the runtime must inject a per-instance bound scene into both backends (Wren +
AngelScript) at behavior/Level bind, and the Entity-return `Wrap` stays as-is.
This is the only option with NO residual gotcha.

**C - Hybrid.** Ship B's `self.scene` bound handle as the always-safe primitive
AND keep static `Scene.*` as invocation-scoped (option A) convenience sugar,
documented as valid only during direct invocation. Keeps the footgun available,
so it does not fully meet "no gotchas" - listed for completeness.

### Recommendation

Given the user's explicit "no gotchas" priority AND acceptance of a breaking
change, I lean **B (bound scene handle)**: it removes ambient state entirely, so
safety is a structural property rather than a discipline the dispatch layer must
never break. A is a clean, cheap fix that covers every entry point that exists
today, but its correctness is invariant-based (it can regress the day someone adds
a new callback seam that forgets the push) - exactly the class of "gotcha" the
user wants gone. If Fable prefers to avoid the migration, A is the safe minimum
and should ALSO land now (it fixes a live behavior bug independent of the Level
tier).

### Edge cases to verify either way

1. Coroutine resume (ScriptSubsystem.cppm:471+) - under A it must push the resumed
   handler's scene, not rely on the enclosing tick; under B it is automatically
   safe (scene is on the object).
2. Physics/contact event delivery - confirm the phase it runs in and that it
   routes through the same seam that would carry the scene.
3. Multi-scene additive: two scenes sharing one instance run host must never see
   each other's scene through `Scene.*` (both A and B handle this; the test is
   design point Additive below).

### Impact on this spec

Design point 5 ("Facade routing: currentScene must be the OWNING scene") is
subsumed by whichever option is chosen: under A the Level tier wraps its
onStart/onUpdate/onStop invocations with the same push/pop; under B the Level's
`self.scene` is its owning scene by construction. The reserved-name rule
(`Level`) and the fault-isolation rule are unaffected.

## Not in v1

- Physics/collision events on the Level class (entities own those; a Level
  wanting them uses a behavior relay or a later design pass).
- A separate Level facade - existing facades (Scene/SceneLoader/Ui/Net/
  Input/Time/Random) cover it.
- Per-scene script PARAMETERS (inspector-authored fields on the Level
  class) - that is the reflection track's parameter story; do not invent a
  one-off here.

## Tests

- Battery (both backends): Level instantiates on scene start, onUpdate ticks
  with scene dt and STOPS when simulation is disabled, onStop fires on scene
  stop, fresh object on restart; fault-in-onUpdate disables only that scene's
  level script.
- Additive: two scenes, same script resource -> two independent objects,
  each spawning into ITS scene via the facade routing.
- Settings round-trip: scene save/load preserves the script ref (wire
  version test); ResolveSceneResources binds it (and a BindAsync-scope load
  leaves it pending until pumped - the async-path test).
- Reserved-name guard: a facade registration named `Level` must be caught
  (extend the adding-facades battery check if one exists, else assert in
  RegisterUiScriptFacade-style registrars).

## Acceptance

Both compilers + full script batteries green; a demo scene (PhysicsPlayground
or WebScene) moves one piece of its C++ setup logic into a Level script as
the on-screen proof; user visual verify.

---

## Fable DECISION: option B, shaped on the existing bound-object machinery (2026-08-03)

Verified the bug report: the single coarse set-site (ScriptSubsystem.cppm
~465) vs the InvokeHandler entry points is real, and the stale/null ambient
is a live behaviors footgun today. Good find - this had been latent since
behaviors shipped.

Decision: **B** - and it is much cheaper than the writeup prices it, because
the injection machinery already exists: behaviors are CONSTRUCTED with their
bound entity (`construct new(entity)` - ScriptSceneTests.cpp ~169), and
bound Entity objects already carry their scene::Scene* internally. B is not
"new injection plumbing"; it is one new bound type plus one property:

1. **New bound `Scene` instance type** (both backends), the mirror of bound
   Entity: instance methods `spawn(prefab, x, y, z)`, `find(name)`,
   `findByPath(path)` - each routes through the SAME host-installed
   function pointers (spawnPrefab et al.) but passes ITS OWN scene::Scene*
   explicitly. No ambient state anywhere.
2. **Entity gains a `.scene` property** returning the bound Scene it already
   carries. A behavior needs NO contract change - it already stores
   `_entity` from its constructor; `_entity.scene.spawn(...)` is the
   migration target. Coroutines, stored callbacks, timers, physics events,
   editor setters: all structurally safe (the scene is data on an object
   the script holds).
3. **Level is constructed with its scene**: `construct new(scene)` -
   mirrors the behavior constructor contract exactly. Design point 5's
   "facade routing" section is DELETED by construction.
4. **The static ambient `Scene.*` facade is REMOVED**, along with
   `ScriptRuntimeBinding::currentScene` and the tick set-site. Removing the
   static frees the `Scene` NAME for the bound instance type, so scripts
   still read naturally (`scene.find("boss")`). This is the breaking part
   the user pre-approved; migration is confined to in-repo demo scripts +
   test scripts (sweep them in the same commit). Do NOT keep an
   invocation-scoped static as sugar (option C) - it re-introduces exactly
   the gotcha class we are paying to delete.
5. **The orchestrator's door**: the GameInstance script has no entity and no
   constructor-injected scene. It gets `SceneLoader.currentScene() -> Scene`
   (bound object for the instance's current scene, resolved explicitly AT
   THE CALL) plus the activated scene from its load flow. Explicit query of
   instance state is data flow, not ambient dispatch state - deterministic
   regardless of who invoked the script. Null-scene-safe: returns a null/
   invalid Scene pre-scene (guarded methods no-op, consistent with the
   facade rules).
6. Tests to add on top of the spec's list: the FORMER footgun paths as
   regression cases - spawn from onDestroy, from a physics contact event,
   from a resumed coroutine, from an editor property setter; each must
   target the correct scene on both backends. Plus cross-scene: an entity
   of scene A stored by a behavior in scene B spawns into A via
   `thatEntity.scene.spawn` (the capability the ambient model never had).

Order: land B as its own commit sweep (bound Scene type + Entity.scene +
static-facade removal + migration + regression battery) BEFORE the Level
tier commit builds on it.

---

## Opus: B IMPLEMENTED (2026-08-03) - one small syntax deviation flagged

Landed exactly as Fable shaped it, in one commit sweep:
- `Scene` is now a bound VALUE type (`DRACONIC_REFLECT_VALUE`, mirrors `Entity`)
  carrying `scene::Scene* scene`, with `spawn/find/findByPath` on ITS OWN scene
  (still routing prefab spawns through the host-installed `spawnPrefab`, just
  passing its own ptr). The static `Scene` facade, `ScriptRuntimeBinding::
  currentScene`, and the tick set-site (ScriptSubsystem.cppm:465) are GONE.
- `Entity` gained a bound-scene accessor; `SceneLoader.currentScene()` returns a
  bound Scene for the orchestrator, backed by `GameInstance::GetScene()`.
- Migrated the in-repo scripts (test literals + both editor starter templates).
- Regression battery: a pure-facade cross-scene isolation test (an A-entity finds
  in A, never B) + spawn-from-onDestroy + spawn-from-a-resumed-coroutine (both
  former footguns) + an AngelScript spawn through the bound Scene (2nd backend).
  All green both compilers; existing script + game-instance suites pass migrated.

**Deviation RESOLVED (2026-08-03, commit pending):** Fable's decision wrote
`entity.scene.spawn(...)` (getter, no parens). The initial ship used `entity.scene()`
/ `self.scene()` because a reflected zero-arg METHOD emits with parens, and only
reflected PROPERTIES emit parens-less - but the existing `Property<&member>` builder
needs a member pointer and `.scene` is a COMPUTED accessor, not a field.

Investigation of the (now unblocked) reflection work found this is NOT the same
missing piece as the nested-struct blocker. Fable's `Nested` kind is address-based
(get returns empty, script-harvest SKIPS it) - for recursing into non-copyable Object
members. What `.scene` needs is the opposite: a COMPUTED-VALUE getter (get returns a
real copyable Scene via Variant; no address). The script backends already bind property
GETs through `core::GetProperty` -> `property.get` (Wren WrenScript.cppm:547, AngelScript
AngelScriptScriptImpl.cpp:1798) and emit properties parens-less, so the only gap was a
builder method. Added `TypeBuilder::ComputedProperty<&getter>(name)` (Core reflection):
get marshals the getter's return value, set returns NotSupported, address null, flagged
ReadOnly. Entity's `scene` now uses it, so the shipped form IS `entity.scene` /
`self.scene` - matching the letter of the decision. Verified on both backends
(migrated the test literals + both editor starters). Distinct from Fable's Nested kind;
noting for the reflection-track record.

---

## Implementation status (Opus, 2026-08-03) - the Level tier + templates SHIPPED

Commits: 4262b689 (Level tier) + 7c579dc5 (per-tier starters).

**Level tier (4262b689):**
- `SceneScriptSystem` + `SceneScriptSettings { Ref<ScriptClass> script; bool enabled }`
  on the scene (draconic.engine.script). Sim-only, UpdateOrder -10 (before entity
  behaviors). Instantiates on OnSceneStarted from the bound resource on the OWNING
  instance's run host (GameInstance re-binds it alongside the behavior system), so
  additive scenes sharing one class get independent objects. Destroyed on stop; a
  faulting handler disables that scene's Level only.
- Contract class `Level`: `construct new(scene)`, `onStart` / `onUpdate(dt)` /
  `onFixedUpdate(dt)` / `onStop` - all optional, dispatched by presence. onUpdate +
  onFixedUpdate tick only while the scene simulates. onFixedUpdate rides the scene's
  fixed lane (Scene::FixedUpdate), the distinct hook you flagged.
- Settings reflected (displayName/description) so the scene-settings inspector renders
  the Level-script Ref picker (added the Ref<ScriptClass> dispatch branch) + enable toggle.
- Reserved-name guard: `RegisterExtraFacadeName` now REFUSES `Game` and `Level` (logs,
  adds nothing) so a user's contract class always wins. Documented in adding-facades.md.
- Battery both backends: lifecycle + sim-gating, onFixedUpdate, fault isolation,
  two-scene independence, an AngelScript Level, settings round-trip, reserved-name guard.

**Per-tier starters (7c579dc5):** New Asset now offers Behavior / Level / Game per
backend (was one behavior template). `ScriptTier` enum drives `NewAssetTemplate(tier)`;
each cook seeds a tier-appropriate starter; three creators per backend. Cook tests
assert each tier is distinct and cooks to its contract class.

All green on clang + gcc; full editor executable links on both.

---

## State (appended 2026-08-03; original content above is unchanged)

**COMPLETE.** Level tier (4262b689), per-tier New-Asset starters (7c579dc5),
ComputedProperty + parens-less `entity.scene` (3d66a6d6), facade-naming
normalization (a6baf40e), and the physics-contact->script bridge extraction
(12f90896). Green clang+gcc; awaiting user visual verify of the Level inspector.

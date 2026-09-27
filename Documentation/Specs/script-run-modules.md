# Script run modules: one module per run, and hot reload that migrates

> STATUS: PROPOSED 2026-09-26, not scheduled. Sized M (P0-P1), L with P2. Origin: a fault in
> Sedulous's PaperKid. Its port of `ScriptRunHost` discarded the previous `behaviors#N` module
> after each rebuild, and AngelScript frees a discarded module's globals, so the Bike's first
> throw read `kPaperPrefab` as a null pointer. Sedulous now matches Raptor and keeps old
> generations (Sedulous 17dbbf66). The review that followed asked whether keeping them is
> right; this spec is the answer. Raptor has no crash, but has the latent problems below in
> both backends. Every citation was checked against `editor-mcp` at 7c835966. Read
> CONVENTIONS.md first.

## Goal

A game run has ONE live set of script globals and ONE definition of each script type, shared
by the Game orchestrator, every Level and every behaviour:
- A global one script writes is the global every other script reads.
- A class first used mid-run joins the run without disturbing anything already running.
- A hot reload keeps the state of every live instance it can carry across, rather than leaving
  instances on stale code or resetting them.

Not goals:
- Script objects crossing `Send` / emit. Not supported today (`ValueFromArg` falls through to an
  empty Variant for a script-class handle, `Code/Foundation/Script.AngelScript/AngelScriptScriptImpl.cpp:1004-1160`),
  and this spec does not add it.
- Changing the property harvest or the override apply.

## What happens today (checked, cited)

**The run host** (`Code/Engine/Engine.Script/ScriptSubsystem.cppm`):
- `EnsureClassLoaded` (412-483) keeps `m_loadedClasses`. When a class is new, or a reloaded
  product replaces one of the same name (431-437), it rebuilds: every loaded class, not just the
  new one, goes into a fresh `behaviors#N` module (456-482).
- Its only caller is `Instantiate` (394-408), reached from:
  - a behaviour's first tick (`TickBehavior` 1038-1040 → `InstantiateBehavior` 1106): lazy, not
    at spawn or load;
  - a Level at `OnSceneStarted` (1333-1336 → 1397).

  There is no runtime AddBehavior in Raptor. Sedulous has one (`ScriptsFacade.AddBehavior`).
- The class comment (246-251) states the intent: "live instances of old generations keep
  running".

**The Game orchestrator is a separate module.**
- `GameInstance::StartScript` (`Code/Engine/Engine.GameInstance/GameInstanceImpl.cpp:95-148`)
  calls `m_scriptContext->Load(source, name)` (119), not `LoadBehaviorModule`, then
  `NoteExternalLoad()` (120-121) and `CreateInstance("Game")` (128).
- It always compiles from source, even in the player (`PlayerApplication.h:487-501`).

**AngelScript** (`Code/Foundation/Script.AngelScript/AngelScriptScriptImpl.cpp`):
- A context is "a family of asIScriptModules ... one fresh module per Load" (18-19).
- `LoadBehaviorModule` (2831-2903) builds the source classes into the new module. A class with
  bytecode loads as its own module (2785-2811, 2908-2919), again on every generation.
- A previous generation is never discarded during a run: `Discard()` runs only on a failed
  build (2750, 2806, 2887) and in the destructor (3366-3376).
- `CreateInstance` searches every owned module newest first (3824-3840), so new instances come
  from the newest generation while old ones keep their own module's type (`Invoke` resolves
  through the instance's object type, around 3218-3246).
- Globals are per module. `GetGlobal` / `SetGlobal` read only the newest (around 2922-2958).

**Luau** (`Code/Foundation/Script.Luau/LuauScript.cppm`):
- One `lua_State` per context (2137-2138). `LoadBehaviorModule` (2244-2269) ignores the module
  name and runs each class as its own chunk into the one global table ("A reload redefines the
  class global in place").
- The Game is `Load`ed into the same state.

**Hot reload of a ScriptClass product:**
- The editor reloads each cooked product (`Code/Editor/Editor.App/ApplicationImpl.cpp:2500-2508`).
- A behaviour whose bound class changed is stopped without `onDestroy`, re-instantiated and has
  its overrides re-applied on its next tick (`ScriptSubsystem.cppm:1014-1020`, 1091-1133).
  Transient state resets by the "documented v1 contract" (21-24).
- Instances of other classes are untouched.
- Levels and the Game are never reloaded: there is no bound-class comparison in
  `SceneScriptSystem` (1333-1346) and no Game reload path.
- Tests: `Code/Engine/Engine.Script.Tests/ScriptSceneTests.cpp:777-837` (AngelScript) and
  1579-1624 (Luau).

**State capture:**
- There is none at runtime: `ScriptObject` exposes only `Invoke` (`Code/Foundation/Script/IScriptContext.cppm:53-61`).
- AngelScript's `CSerializer` add-on is not vendored; `ThirdParty/angelscript/add_on` holds
  scriptarray, scriptbuilder and scriptstdstring.
- The cook harvests property defaults (`AngelScriptScriptCookImpl.cpp:508-541`,
  `LuauScriptCookImpl.cpp:34-51`), and instantiate applies defaults plus overrides
  (`ScriptSubsystem.cppm:625ff`). Nothing reads live values back.

**No test covers generations.** The "old generations keep running" contract appears only in
comments (`ScriptSubsystem.cppm:250`, `AngelScriptScriptImpl.cpp:2909`, `ScriptAsset.cppm:559, 773`).

## The problems

1. **AngelScript: globals split by generation.** A class first used mid-run builds `behaviors#2`
   with fresh globals, while instances from `behaviors#1` keep theirs. A global written by one
   instance is invisible to an instance of the other generation. When the first use happens
   depends on timing (a spawned entity's first tick, a scene start), so the split varies run to
   run. PaperKid only reads its globals, which is why it has not shown here.
2. **AngelScript: the Game's globals are always separate** from the behaviours' and the
   Levels', even with a single generation, because the Game is its own `Load`.
3. **AngelScript: types split by generation.** `Bike` in `behaviors#1` and `Bike` in
   `behaviors#3` are different types. It is latent today, since nothing passes script objects
   between instances or casts between script types (no `shared`, no cross-script `cast<>`),
   but it is a trap for the first feature that does.
4. **Luau: a class's first use resets other classes' globals.** Each rebuild re-executes every
   loaded class's chunk (`LuauScript.cppm:2244-2269` over the full list the run host passes,
   `ScriptSubsystem.cppm:456-466`). A top-level `score = 0` in one class's file is re-run
   whenever any other class is first used, and resets the run's state.
5. **Both backends: rebuild cost grows with every class.** Each first use recompiles or reloads
   every class seen so far, about n²/2 class compiles for a run that meets n classes one at a
   time. AngelScript also accumulates every generation's modules, and every bytecode class's
   module per generation, until teardown.
6. **Hot reload leaves stale code running.** Only instances of the reloaded class are rebuilt.
   Instances of other classes keep running an old generation. Levels and the Game are never
   rebuilt, so their reload silently does nothing.

## Decisions

### D1. One module per run, compiled up front

At run start (`GameInstance::StartScript` for a run, the editor's Play and Simulate brackets
for the default host), the run host compiles the run's whole script set into one module:
- the Game class;
- every ScriptClass that can be instantiated in the run.

The Game joins that module rather than being a separate `Load`, so Game, Levels and behaviours
share one set of globals and types. `NoteExternalLoad` goes away.

**The script set** [DISCUSS]:
- Recommended: every ScriptClass asset in the cooked content database. It is simple and always
  complete: the content database is a walkable tree (`Code/Foundation/Content/ContentModule.cppm:118-131`),
  and a shipped pak holds exactly what was cooked.
- The alternative is the reachable set from the export scan (`Code/Editor/Editor.Core/Export.cppm:207-290`).
  It is smaller, but it misses anything a script names dynamically; script-literal roots are
  noted but not implemented (Export.cppm:59).

A class outside the set that appears later (a pack loaded mid-run, or runtime AddBehavior on a
backend that has it) is added through D2's rebuild, not by a second live generation.

### D2. A rebuild migrates, then discards

Any rebuild (hot reload, or a class outside the set) builds the new module and then:
1. **Captures** every live instance's state (all classes, not only the changed one) and the run's
   globals. Captured values: primitives, strings, the registered value types (Float3, Guid,
   Entity, and so on), handles to engine objects, and handles to other script instances, which
   are remapped to their migrated counterparts.
2. **Recreates** each instance from the new module's class of the same name and restores the
   captured fields matched by name and type. A field that no longer exists or changed type
   keeps its new default, and the migration logs it.
3. **Re-points** every holder (behaviour, Level, Game, pending coroutine targets) at the new
   instance, then **discards** the old module.

Exactly one generation is live after any reload.

AngelScript implementation: vendor `CSerializer` (`add_on/serializer`), which exists for this
flow. It captures an object graph and globals across a module rebuild, with user types for
registered engine types. The engine-side capture of registered value types and engine handles
is a small user-type table.

Luau implementation: the single state already shares globals. Migration is re-pointing each
live instance's metatable at the reloaded class table; the fields stay on the instance. D3
stops the other chunks re-running.

Lifecycle during migration [DISCUSS]:
- Recommended: `onEnable` / `onStart` do not re-fire for migrated instances, since they are the
  same logical object. That replaces the v1 "transient state resets" contract with "state
  survives where it can".
- A class that wants a clean restart on reload can opt in with an `onReload()` handler, which
  would be a new handler name. The alternative is keeping the v1 reset for the changed class
  only while migrating the others.

### D3. Luau loads only what changed

For Luau, `LoadBehaviorModule` runs a class's chunk only when the class is new or its product
changed; unchanged classes are not re-executed. That removes problem 4 without any migration,
and fits the per-class chunks already in place.

### D4. Levels and the Game reload too

Under D2 every live script instance migrates, including each scene's Level and the run's Game,
so a reload of their classes takes effect instead of being ignored.

## Phases

- **P0: one module per run** (M).
  - D1's up-front compile of the script set.
  - The Game in the shared module.
  - D3 for Luau.
  - Tests, run on both backends where the backend has the feature:
    - a behaviour and the Game share a global: one writes it, the other reads the value;
    - a class first used mid-run (a spawned entity's first tick) does not build a second
      generation, and a global written before it survives;
    - in Luau, a class's first use does not reset another class's top-level global;
    - the rebuild count across a run that meets n classes is zero after the up-front compile.
- **P1: migrating hot reload** (M).
  - D2 with `CSerializer` vendored for AngelScript and the metatable re-point for Luau.
  - D4.
  - The lifecycle ruling from D2.
  - Tests:
    - a reload of class A migrates live instances of A and of an unrelated class B, keeping a
      counter field on each;
    - a renamed field falls back to its default and is logged;
    - a script instance handle held by another instance points at the migrated object;
    - a Level and the Game pick up a reloaded class;
    - after a reload exactly one module is live (AngelScript: `m_ownedModules` holds one
      behaviour module);
    - `ScriptSceneTests.cpp:777` and 1579 are updated to the new contract.
- **P2 (optional): runtime additions without a rebuild**, for a backend and host that add
  classes mid-run: per-class modules with `shared` types in AngelScript, so an addition
  compiles alone. Only worth it if P0's up-front set proves too slow for large projects.

## Acceptance

- A global written by any script in a run is the value every other script in that run reads,
  on both backends.
- After any hot reload, every live instance runs the reloaded code and keeps its fields where
  the name and type still match.
- AngelScript holds one live behaviour module after any reload.
- Green on clang and gcc; ASAN clean for the new tests, including a reload under ASAN (the
  migration frees the old module while instances were holding it).

## For the Beef port (Sedulous)

Sedulous's AngelScript `ScriptRunHost` (`Code/Engine/Sedulous.Engine.Script/src/ScriptRunHost.bf`)
follows the same generation scheme, and after 17dbbf66 it keeps old generations as Raptor
does, so it has problems 1, 2, 3, 5 and 6 today. Sedulous has no Luau backend, so D3 does not
apply.

Sedulous has runtime `ScriptsFacade.AddBehavior(entity, scriptClass)`, which Raptor lacks. With
P0's up-front set, a class it names is almost always already compiled; one that is not goes
through D2's rebuild. The tests carry over as written. The `CSerializer` port needs its
user-type table in Beef, over the AngelScript C shim.

# Draconic Scripting — backend-agnostic gameplay layer (design; Wren = first backend)

Status: PLANNED (surveyed 2026-07-17; not yet approved to build)
This is the fifth and final piece of the game-ready subsystem plan (physics/audio/input/
game-ui docs sit beside this one). Unlike those, scripting already has locked decisions and
shipped foundations — this doc consolidates them and designs the missing layer.

## 1. What already exists (do not re-decide)

- **Two-layer model, locked in the roadmap (2026-07-12, §Milestone/3):**
  (a) the **GAME SCRIPT** — a project-level coordinating Wren script, the scripted
  `IApplication` counterpart, orchestrating ABOVE scenes (which scene to load, game states);
  (b) **ENTITY BEHAVIORS** — per-entity scripts ticked under simulation. A native game
  module later replaces the game script at the same seam (static-link; DLL hot reload is
  phase-never).
- **Tier (a) is BUILT**: `DraconicPlayer` resolves `startupScript` from project settings,
  loads it through the VFS root (works from the pak), compiles, instantiates the `Game`
  class, and drives `construct new()` / `launch()` / `update(dt)` / `exit()`. Compile and
  runtime faults log and stop the script without killing the player.
- **Substrate** (`draconic.script` + `draconic.script.wren`): `IScriptManager` /
  `IScriptContext` (VM-agnostic; everything crosses as `Variant` + `TypeInfo`),
  `ScriptObject::Invoke(method, Span<Variant>)`, `IScriptErrorHandler` (kind/module/line/
  message), and `RegisterReflectedTypes` exposing the entire reflection registry in one
  call. Wren backend proven end-to-end (memory: reflection→Script→Wren working).
- **PIE lifecycle, decided**: Play = fresh script context + fresh scene from cooked
  content; Stop = context teardown, total cleanup. Run-only for MVP (no edit-time hooks).

So this doc's actual design surface = **tier (b): entity behaviors** — attachment,
lifecycle, properties, engine API, assets, hot reload, editor.

## 2. Reference survey — conclusions (Lumix Lua, Godot ScriptInstance, ez AngelScript)

- **Lumix** (`src/lua`): multiple scripts per entity in ONE component (reorderable array);
  per-instance VM thread + environment with `this` seeded; lifecycle by named lookup
  (`awake/start/update/onDestroy/onInputEvent`); properties discovered from the instance
  and stored as **name-HASHED override blobs** re-applied after hot reload (rename-safe,
  editor values survive); deferred start queue; physics/UI call INTO scripts through one
  generic `beginFunctionCall`. Awkward: global update arrays = no deterministic
  cross-entity order; any stray global becomes an inspector field.
- **Godot** (`core/object/script_language.h`): the clean triad — `Script` (asset) /
  `ScriptInstance` (per-object binding: set/get/callp/property-list, all Variant +
  PropertyInfo) / `ScriptLanguage` (VM). Exports are ordinary object properties → scenes
  serialize only values differing from the script's default (diff + inspector revert for
  free). **Placeholder instances** keep property values alive across compile failures and
  reloads. Awkward: one script per object; huge language interface.
- **ez** (`Core/Scripting` + AngelScriptPlugin): script class must derive the reflected
  component base → reuses the whole reflection/message/serialization stack; **exposed
  parameters harvested from the ASSET at cook** so the inspector is data-driven and only
  overrides serialize (untyped Variant map on the component); collision/trigger events
  arrive as ordinary MESSAGES handled by script methods; **coroutine scheduler** with
  creation-collision modes + built-in Wait/MoveTo/TweenProperty; per-component update
  interval + only-when-simulating gating. Reload = full re-instantiate, parameters
  re-applied.

Combination we adopt: Lumix's array-of-behaviors-in-one-component and hash-keyed override
blobs, ez's cook-time property harvest + simulation gating + coroutines, Godot's
asset/instance split (we already have it: resource + `ScriptObject`) and its
default-diff property semantics.

## 3. Entity behaviors — design

### 3.1 Attachment

**`ScriptComponent`** (serializable, value-pool — one per entity, like all our managers)
holding an ordered array of behavior entries:

```
ScriptComponent
 └─ Behavior[]                        // multiple behaviors per entity, ordered
     script    Ref<ScriptClass>       // the cooked script-class resource
     enabled   bool
     overrides Array<{nameHash u64, Variant}>   // property overrides ONLY
     [runtime] instance RefPtr<ScriptObject>, started bool
```

Execution order = array order within an entity; entity iteration order across entities
(documented as unspecified, like every surveyed engine in practice). Behaviors on prefab
members work automatically: overrides live in the component payload, so the existing
component-granular prefab delta machinery (baseline blob compare) covers script property
overrides with zero new code.

### 3.2 Script classes (the asset payload)

A behavior is a Wren class following a convention:

```wren
class Mover {
    static properties { {
        "speed":  ["float", 4.0, "units per second"],
        "target": ["entity", null],
        "clip":   ["asset:AudioClip", null],
    } }

    construct new(entity) { _entity = entity }
    onStart() { ... }
    onUpdate(dt) { ... }
    onContact(other, point, normal) { ... }
    onDestroy() { ... }
}
```

- **Property declaration** via the `static properties` map (name → [type, default,
  description?]). Explicit declaration, not Lumix's reflect-over-globals (no accidental
  inspector fields). Types v1: `float, int, bool, string, color, vec3, entity`
  (guid-remapped like other entity refs), `asset:<TypeName>` (typed resource ref).
- The **cook harvests** this map by compiling the class in the cooker's Wren VM and
  invoking `properties` — the resource carries the metadata so the EDITOR never needs a VM
  to render the inspector (ez model). Lifecycle/handler methods are also probed and
  recorded (dispatch avoids per-frame method-missing costs).

### 3.3 Lifecycle & ticking

Named methods, looked up once at instantiate (recorded in the resource, verified live):
`onStart` (simulation start, or first tick after spawn during play — deferred-start queue,
Lumix), `onUpdate(dt)`, `onFixedUpdate(dt)` (once the physics fixed-update lane exists),
`onEnable/onDisable`, `onDestroy`. Ticking is **gated to simulation** (`OnlyWhenSimulating`;
the editor's Simulate toggle and PIE both count). Optional per-behavior `updateInterval`
(ez's throttling) is a phase-2 nicety.

**Coroutines**: Wren has first-class fibers — the scheduler is thin. `this.wait(seconds)`,
`this.waitUntil(fn)` yield the behavior's fiber; the subsystem resumes due fibers each tick
and stops all fibers of an instance on destroy/disable (ez teardown rule). Ship
`Wait`/`MoveTo`/`Tween` helpers in the script stdlib module.

### 3.4 Events into scripts

One convention, one dispatch path — named handler methods invoked by the subsystems that
already deliver main-thread events (each subsystem doc reserves this seam):
- Physics: `onContactBegin/End(other, point, normal, impulse)`,
  `onTriggerEnter/Exit(other)` (from the buffered contact dispatch).
- UI: wired per game-ui.md (named-view events; later declarative `onClick="..."`).
- Input: behaviors normally POLL actions (`Input.action("jump").pressed`); no per-event
  push needed in v1.
- **Between behaviors**: `entity.send("heal", [amount])` invokes `onHeal(amount)` on every
  behavior of the target entity that declares it (Lumix's generic call-in, ez's message
  spirit without a typed message registry).

### 3.5 Engine API surface (what scripts can touch)

- Already: every reflected type via `RegisterReflectedTypes`.
- Curated gameplay facade (a Wren stdlib module backed by native bindings, grown WITH the
  subsystems): `entity` (transform get/set, name, destroy, findChild, components),
  `Scene.spawn(prefabRef, transform)` (the prefab runtime spawner — exists),
  `Input.action(...)` (input.md §6), `Audio.playOneShot(...)` (audio.md §6),
  `Physics.rayCast(...)` (physics.md §6), `UI` (game-ui.md §6), `Log`, `Time`, `Random`.
- Rule (the recurring survey lesson): a facade lands only WITH a sample that uses it —
  no API-first surface.

## 4. Runtime resources

- **`ScriptClass`** (cooked product): Wren source text + harvested metadata {className,
  properties (name, hash, type, default Variant, description), declared handler set,
  version}. No bytecode (Wren has no stable serialized bytecode; compilation is fast).
  Factory = metadata parse; actual VM compilation happens per script CONTEXT on first use,
  cached per context.
- **Contexts**: the player/PIE run owns ONE gameplay `IScriptContext` hosting the game
  script AND all behavior instances (shared module space — behaviors can share utility
  modules; isolation between games isn't a goal, isolation between RUNS is: Stop tears the
  context down, the locked PIE rule). Behavior instances = `CreateInstance(className,
  [entityHandleVariant])`.
- Cross-module imports: Wren `import` resolves through the VFS (script assets by path) —
  utility modules are ScriptClass-less script assets, cooked the same way.

## 5. Editor-side assets

- **`ScriptClassAsset`** (source): a `.wren` file copied into Sources/ (drop-import via
  `IFileImporter`, extension `wren`, no options dialog) or created from a New Asset
  template (starter behavior with the convention pre-filled).
- **Builder**: compiles in a cooker-owned Wren VM → surfaces compile errors as cook errors
  (file/line — `ScriptError` already carries them); harvests `static properties` +
  handlers; writes source + metadata. A behavior that fails to compile keeps its LAST good
  cooked resource (placeholder spirit, Godot) — the editor shows the error, instances keep
  running the old class.
- **Inspector**: the ScriptComponent section renders one sub-block per behavior — script
  picker (AssetPickerDialog filtered to ScriptClass) + rows generated from harvested
  metadata (reusing the attribute conventions: type → editor widget, description →
  tooltip, defaults shown, overrides tracked with the standard revert affordance). Add/
  remove/reorder behaviors is undoable through the page command stack.
- **Editing**: v1 external editor + file watch (source change → recook → hot reload);
  a `ScriptPage` (text page like UIDocumentPage, with inline error surfacing) is phase 2.
- **Hot reload** (edit-during-play/simulate): resource reload → per-instance re-instantiate
  → re-apply hash-keyed property overrides (Lumix; editor-set values survive, transient
  runtime fields reset — documented; Godot-style live-state migration is explicitly a
  later refinement). Fibers of old instances stop.

## 6. Errors, debugging, profiling

- Compile + runtime errors → editor Console + sticky error toast (existing notify rules),
  with module/line from `ScriptError`. Player logs and disables the faulting BEHAVIOR
  (not all scripting — finer than the game-script tier's stop-everything).
- `Log.info/warn/error` from scripts land in the engine log with a `Script` category.
- Per-behavior `DRACONIC_PROFILE_SCOPE` around Invoke so script cost shows in the P-key
  CPU tree from day one.
- Step debugging: out of scope (Wren has no debug protocol worth building against now).

## 7. Phasing

- **P1 — behaviors core**: ScriptClassAsset + builder (compile + harvest) + cooked
  resource, `draconic.script.subsystem` (context ownership, ScriptComponent + manager,
  lifecycle dispatch, deferred start, simulation gating), property overrides + inspector
  rendering, hot reload with override re-apply, `entity` facade + Log/Time/Random,
  sample: a Mover/Spinner behavior in Sandbox + player. Tests: harvest round-trip, override
  re-apply after reload, lifecycle over a scripted scene (headless Wren VM — no device
  deps at all).
- **P2 — events + fibers + facades**: physics/UI event dispatch into handlers,
  `entity.send`, fiber scheduler + Wait/Tween stdlib, `Scene.spawn`, Input/Audio/Physics
  facades as those subsystems land their P1s, ScriptPage editor.
- **P3 — polish**: per-behavior update interval, live-state-preserving reload
  (Godot placeholder model), script-defined editor tooling hooks (deferred with the
  edit-time-Configure questions from the roadmap).

## 7.5 Backend neutrality (decided with the user 2026-07-19)

The intent from day one: reflection-driven scripting where a language backend is a
LIBRARY (draconic.script.wren), never the architecture. The contract (IScriptManager/
IScriptContext/Variant/services/CurrentScriptContext + facades as reflected classes) is
already neutral - a new backend inherits the ENTIRE engine API surface from the
reflection registry for free. What was missing is the wiring that makes "add
AngelScript like Wren was added" true, and the proof:

- **B1 — backend registry + language dispatch.** `ScriptBackendRegistry` in
  draconic.script: `{languageId, displayName, fileExtensions, factory}`; resolve by
  language or file extension. DefaultApplication registers the Wren backend as part of
  its batteries-included defaults (exactly like default subsystems - subclasses/entry
  points register more) and RESOLVES through the registry by the script file's
  extension. No consumer names a backend type again.
- **B2 — conformance contract + shared test battery. SHIPPED** (Script/Tests/
  BackendConformance.h; contract finding: a later Load need NOT preserve an earlier
  load's globals - Wren replaces the module - only live ScriptObjects survive). A documented REQUIRED capability
  set (reflected-type emission incl. statics-on-constructible-classes, Variant
  round-trips, per-context services + CurrentScriptContext nesting, error propagation,
  GC hook) + a shared doctest battery in draconic.script/Tests that every registered
  backend must pass. Wren = the first certified backend. A backend is DONE when the
  battery is green - never "hopefully it works".
- **B3 — asset language.** ScriptClassAsset (P1) carries `language` (defaulted from the
  imported file's extension); the cook's property-harvest VM and the runtime both
  resolve the backend through the registry.
- **B4 — capability flags. SHIPPED.** Optional features (fibers/coroutines for the P2
  scheduler, debugger/profiler seams) are declared per backend and consumed
  contract-first, so a backend without them degrades cleanly instead of breaking the
  model. `ScriptCapabilities` flags + `IScriptManager::Capabilities()` (default None;
  the battery certifies REQUIRED behavior - flags cover only optional features): Wren
  declares Fibers; AngelScript declares None until its coroutine/debugger surfaces are
  wired. Single source of truth is the MANAGER, not the registry desc - consumers with
  a backend in hand ask it directly.

**Two-phase type registration (the AngelScript lesson, from the user's Traktor
experiment):** AngelScript requires ALL object types to be DECLARED before any of
their members are registered - otherwise everything must arrive in strict dependency
order. The contract therefore treats `RegisterType` as COLLECTION and adds
`IScriptManager::FinalizeTypes()` (called by RegisterReflectedTypes after the full
walk, and defensively before the first CreateContext): a backend may defer all
emission to the finalize step and do declare-all-types then bind-all-members. Wren
finalizes as a no-op (its emitter materializes lazily). Facade authors write against
the contract, not a backend's quirk (the `builder.Constructor()` requirement is now
part of the DOCUMENTED contract, not Wren trivia).

**Second backend = AngelScript — SHIPPED + CERTIFIED** (draconic.script.angelscript, vendored 2.39.0-WIP; opt-in via RegisterAngelScriptBackend(), ext .as; Wren stays the DefaultApplication default). (user decision; replaces the earlier Lua suggestion).
It is the stricter validator: static typing, the two-phase registration, different
coroutine model - if the contract survives AngelScript, it is real. Added right after
B2, before building further tiers on an unexercised contract.

**Traktor findings** (ours is partly inspired by it; re-read 2026-07-19): their
IScriptManager is Lua-backed behind the same shape (registrar + createContext + GC
stepping) and carries three seams worth adopting when their phases arrive:
`IScriptCompiler` -> `IScriptBlob` (compile at COOK, ship bytecode - slots into our
ScriptClassAsset builder), `IScriptDebugger`/`IScriptProfiler` (B4 capability flags),
and explicit incremental `collectGarbage(full)` stepping for frame-budgeted GC (their
rationale comment is worth keeping verbatim: step at high frequency to keep the heap
small rather than paying full collections mid-game).

## 8. Open questions

1. `static properties` map convention vs annotation comments parsed without executing
   (`// @property speed: float = 4.0`)? Recommendation: the static map — it's real code
   (defaults can compute), the cook VM exists anyway, and Wren has no annotation syntax to
   lean on.
2. One shared gameplay context vs context-per-scene? Recommendation: one per RUN (matches
   the locked PIE teardown rule); scene unload destroys that scene's instances but keeps
   the context (game script outlives scene swaps by design).
3. Does the game script gain access to the behavior facades (`Scene.spawn` etc.)? Yes —
   same context, same modules; the tiers differ only in lifecycle, not capability.


## Addendum (2026-07-18): host-object injection & the Input facade

The Wren backend cannot inject host objects as module globals: `WrenScriptContext::
SetGlobal` is a stub because Wren's C API has no `wrenSetVariable` (get/has only). The
input P2 `Input` facade therefore ships as a foreign class with STATIC methods over a
process-bound runtime pointer - correct today (one InputSubsystem, one game context).

**DONE (same day):** per-context host services. The backend already stores its context in
the VM user data (`wrenSetUserData`) and every foreign shim fetches it - add a
`context->SetService(type, ptr)` registry and route the `Input` statics through it.
Script-side API unchanged; each VM resolves its own runtime.

**Optional:** a faithful `SetGlobal` despite the missing API - interpret `var Name = null`
once, plus a script-side setter closure (`Fn.new {|v| Name = v }`; module vars ARE
assignable from closures), then wrenCall the setter with the marshalled value. Argument
injection (`Game.new(obj)` / `launch(obj)`) also works today via MarshalOut if a contract
change is ever preferable.

## 9. Status & follow-ups (2026-07-20)

The scripting track is at a wrap for its core feature set. What SHIPPED (all merged +
pushed, verified clang+gcc + smokes throughout):

- **Behaviors P1** (all of §3-§6): ScriptClassAsset + per-language cook + harvest, cooked
  ScriptClass, `draconic.script.subsystem` (ScriptComponent, lifecycle, deferred start,
  simulation gating), property overrides + inspector, hot reload, `entity`/`Log`/`Time`/
  `Random` facades, sample + player.
- **Behaviors P2** (beyond the original plan): `entity.send` (DEFERRED delivery - Wren
  forbids re-entrant wrenCall, so messages queue + drain at the tick's top level);
  **coroutines** as a backend-NEUTRAL capability on BOTH backends (`startCoroutine`/`wait`/
  `waitUntil` - Wren fibers, AngelScript own-context scheduler; `AdvanceCoroutines`/
  `CancelCoroutinesFor` contract; optional Wren `Behavior` base); `Scene.spawn` +
  `Scene.find`/`findByPath` (+ core `Scene::FindEntityByName`/`FindEntityByPath`); Input/
  Audio/Physics facades exposed to the behavior context; **physics events**
  (`onContactBegin/End`, `onTriggerEnter/Exit`) via a composition-root bridge (script does
  NOT depend on physics - `DefaultApplication` owns a `ScriptContactBridge`; entity-packed
  body user word so contacts resolve to the exact entity; approach `speed` not impulse);
  **ScriptPage** editor (save→recook→hot-reload + inline errors + language-neutral New Asset).
- **Behaviors P3**: per-behavior `updateInterval` (done).
- **Backend neutrality B1-B4** (§7.5): registry, conformance battery, capability flags,
  two-phase FinalizeTypes; **AngelScript = the second CERTIFIED backend** (vendored
  2.39.0-WIP). Wren stays batteries-included default; both registered in DefaultApplication.
- **AngelScript property harvest**: typed member fields + `[metadata]` via the vendored
  CScriptBuilder add-on (`[4.0, "desc"] float speed;`).
- **Delegates + introspection**: `IScriptDelegate` (script fn as native callback, certified
  both backends; a `DelegateSignal` event facade uses it) - ADDITIVE, does not replace the
  contact bridge. `IScriptManager::DescribeBoundApi()` (the accurate, backend-reported API
  surface) + a reflection-vs-backend diff conformance check.
- **Neutrality refactor**: zero language syntax in the neutral libs; `IScriptLanguageCook`
  + per-language editor libs (`draconic.script.{wren,angelscript}.editor`); runtime
  behavior-module framing on the backend (`LoadBehaviorModule` - per-source AngelScript
  sections so debugger breakpoints line up).
- **Debugger P1 + P1.5** (see [script-debugger.md](script-debugger.md)): AngelScript
  in-process suspension debugger (breakpoints/step/stack/locals), game-pause, editor UI
  (gutter + panel), certified in the battery; per-source sections so editor breakpoints
  hit in a live PIE run.

### Follow-ups (deferred, not blocking — the durable list)

- **Behaviors P3.2 — live-state-preserving reload** (Godot placeholder model): preserve a
  behavior's declared-property LIVE values across hot reload. BLOCKED on a **getter
  convention** - behaviors declare setters only (`speed=(v)`), so the runtime can't read a
  live value back. Needs a read-side convention before this is doable.
- **Behaviors P3.3 — script-defined editor tooling hooks**: scripts declaring gizmos /
  inspector customization. Fuzzy; tied to the roadmap's edit-time-Configure questions.
- **Debugger track** (script-debugger.md §5): **P2** profiler (inclusive/exclusive per-fn +
  editor grid); **P3** REMOTE TRANSPORT (serialized messages - snapshots already serialize;
  loopback + socket; needs a minimal `draconic.net`, we have none; serves web/WASM +
  Android); **P4** multi-session breakpoint multiplexing, Wren debugger (needs VM hooks -
  deferred), step-out, conditional breakpoints, watch expressions.
- **ScriptClassesView** — the API-browser / autocomplete in ScriptPage driven off
  `DescribeBoundApi()`. The data source exists; the UI is unbuilt.
- **Delegate richer AngelScript signatures** — currently one general `double(double)`
  funcdef; per-signature funcdefs are an additive extension (not a mechanism change).
- **Bytecode blob** — the `IScriptBlob` seam is committed but stubbed. Fill for AngelScript
  (`SaveByteCode`) to ship compiled bytecode; Wren stays source (no stable bytecode).
- **rayHitEntity / Entity placement** — `physics.subsystem` depends on `ScriptFacades` only
  for the `Entity` return type of `rayHitEntity`. Move `Entity` down into the script CONTRACT
  lib so no producer subsystem depends on the facades lib (revisit-later from the physics
  decoupling).
- **AngelScript coroutine global naming** — only `waitUntil` is namespaced (`Coroutine::`);
  `wait`/`startCoroutine` are host-registered globals. Namespace them too for consistency if
  collisions matter (minor).
- **KNOWN_ISSUES.md** tracks: AngelScript vendored `asPWORD` UBSan misalignment (benign,
  upstream/patch/suppress); AngelScript coroutine `$func` GC warning at engine shutdown
  (handle-release audit); intermittent first-run ctest flake (unidentified, clears on rerun).

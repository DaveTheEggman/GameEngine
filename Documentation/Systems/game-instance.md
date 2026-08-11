# Draconic — GameInstance (design)

Status: **Phase 1 SHIPPED (8f808e2), Phases 2-5 designed (2026-07-21).** Specced against the code as it
exists today — every "current state" claim carries a file:line and was read, not assumed. **Correction
(§4.4): a `GameInstance` is a game RUN that manages a *set* of scenes with a mutable current scene — NOT
one scene** (the one-scene framing was an artifact of the current player). User approved the full track
with 4 locked decisions (§9.1 per-instance run host, §6 OnLaunch/OnExit app-once, §9.2 per-instance fixed
accumulator, scope through Phase 5). **The target model is §11** — a `SceneManager` (scene lib) owns a
group of scenes + ticks it; `GameInstance` owns a `ScriptRunHost` + a `SceneManager`; the dependency
points DOWN (runtime → scene), so the scene layer never learns about the runtime layer.

Purpose: make **"a running game" a first-class object** so the standalone player and the editor share
one run bracket, and so the editor can host **more than one** simultaneously (multi-instance
play-in-editor, and an in-editor dedicated server for the networking track).

Related: [runtime-host.md](runtime-host.md), [scripting.md](scripting.md) (the locked one-context rule),
[networking.md](networking.md) (multi-client PIE), [editor.md](editor.md).

---

## 1. The finding: the run bracket already exists twice

The player and the editor's Game tab **hand-roll the same sequence**. This design does not invent a
concept — it *names and extracts* an existing duplicated one.

| step | Player (`Code/Tools/Player/main.cpp`) | Editor (`Editor/Scene/GamePage.cppm`) |
|---|---|---|
| create scene | `scenes->CreateScene(...)` :192 | `m_scenes->CreateScene(...)` |
| load scene | `LoadScene(*instance, *m_scene)` :193 | `LoadScene(*instance, *m_scene)` |
| bind resources | (via app's manager) | `ResolveSceneResources(...)` |
| camera | — | `EnsureCamera()` |
| start | `m_scene->Start()` :217 | `m_scene->Start()` |
| simulate | `SetSimulationEnabled(true)` :218 | `SetSimulationEnabled(true)` |
| pair script↔scene | `SetPrimaryScene(m_scene)` :299 | `m_app->SetPrimaryScene(m_scene)` |
| app launch hook | `OnLaunch(host)` :160 (is the hook) | `m_app->OnLaunch(*m_host)` |
| error sink | — | `SetGameScriptErrorHandler(&m_scriptErrors)` |
| start script | `LoadAndStartGameScript()` :300 | `StartGameScriptFromProject()` |
| **stop** | `SetPrimaryScene(nullptr)` :324 | `StopGameScript()` → `SetGameScriptErrorHandler(nullptr)` → `SetPrimaryScene(nullptr)` → `OnExit()` → `scene->Stop()` → `DestroyScene()` |

**`GameInstance` = this bracket, owned.**

---

## 2. Current ownership (verified)

### 2.1 `DefaultApplication` — shared services **and** singular run state

`DefaultApplication` is "an opinionated `IApplication` base that registers the standard engine
subsystems… the editor embeds THIS same class against its runtime context… and owns the GAME-SCRIPT
lifecycle (the Wren `Game` class bracket) - the player and the editor's Game tab both consume it
instead of hand-rolling copies" (module header, :1-14).

**Legitimately shared (must stay app-level):**
- All engine subsystems, registered in `Configure` — "Entry points (player, editor) do NOT register
  gameplay subsystems - this is the one place" (:106-110).
- Resource infrastructure, already presettable per host: `SetResourceManager` (editor **borrows** its
  existing manager — "a second manager over the same cooked DB would load every product twice") vs
  `SetContentDatabase` (player builds its own) (:213-233). **This is the precedent that shared vs
  owned is already a solved, deliberate distinction here.**
- All the resource factories (mesh/material/animation/audio/physics/UI/script…) (:460-485).

**Singular per-run state (the blocker):**
```
m_primaryScene           :491   the scene whose time scale the script follows
m_scriptErrorHandler            per-run error sink
m_scriptManager / m_scriptContext / m_game    the run's script + `Game` object
```

### 2.2 The script run context — "one gameplay context per run, the locked rule"

- `ScriptSubsystem` owns a **single `m_runHost`**; `AcquireRunContextForFile(path)` →
  `m_runHost.EnsureContextForFile(path)` + sets `m_gameScriptHold`; `ReleaseRunContext()` clears the
  hold and `MaybeTeardownRunContext()` (`Script/Subsystem/Subsystem.cppm` :874-890).
- **Every scene's entity behaviors share that same run host** — `OnSceneCreated` does
  `system->SetRunHost(&m_runHost)`.
- So today: *one* gameplay context is shared by the game script **and all scenes' behaviors**
  ("one gameplay context per run - the locked rule", :146).

### 2.3 Time model — three verified levels, correct as designed

- `Scene.cppm:519` documents the contract: **`effective frame dt a scene sees = host dt × context
  TimeScale × scene TimeScale`**.
- `Context::TimeScale()` (`Context.cppm:89-91`) — app/context-wide; the input runtime's per-action
  `timeScale` flag reads it.
- `Scene::TimeScale()` (`Scene.cppm:523`) — per scene.
- `TickGameScript` applies **the same formula** to the script:
  `dt × host.Ctx().TimeScale() × m_primaryScene->TimeScale()` (:93-97).

This is a coherent multiplicative model, **not** a hack. `m_primaryScene` exists only to answer
*"which scene does the single game script pair with?"* — documented as "The scene whose time scale the
script's update(dt) follows… Set by the launch flow; null = context time" (:299-302).

---

## 3. What actually blocks multiple instances

1. **`StartGameScript()` begins with `StopGameScript()`** (:314-316) — starting a second run kills the
   first.
2. **Singular run state on the app** — `m_game`, `m_scriptContext`, `m_scriptErrorHandler`,
   `m_primaryScene` (:491-495).
3. **One `m_runHost` in `ScriptSubsystem`**, shared by the game script *and every scene's behaviors* —
   two instances of the same script would share globals. **This is the real work.**
4. **`m_gamePage` is an editor singleton** — `OpenGamePage()` focuses the existing tab; one persistence
   id `"game-page"` (`Editor/App/Application.cppm` :404-434, :1947). Trivial to change.
5. **`OnLaunch`/`OnExit` are `IApplication` hooks, but the editor calls them per Play**
   (`GamePage::Start/Stop`). With N instances, invoking app-level hooks N times is wrong — see §6.

**Already fine:** per-scene subsystem state (physics worlds, audio via multi-listener), per-surface
input binding, per-viewport rendering, and the borrowed-vs-owned resource-manager split (§2.1).

---

## 4. Specification

### 4.1 Ownership

`DefaultApplication` **owns `Array<GameInstance>`** and keeps the shared services.

**`GameInstance` owns (moved off the app):**
- **its own script run context + `Game` object + error handler** (the moved `m_scriptContext` /
  `m_game` / `m_scriptErrorHandler`) — *this is the durable core*: the game's "brain" persists across
  scene transitions (see §4.4).
- **the set of scenes active for this run**, with a mutable **current scene** (the one the script's
  `update(dt)` time-pairs with). Scenes are memory-owned by the `SceneSubsystem`; a `GameInstance` owns
  *which* scenes belong to its run (load / switch / unload) and their lifecycle within the run.
- its **instance time scale** (§5)
- its render target + input-surface binding (editor: a viewport RT; player: the backbuffer)
- flags: `headless` (simulate, don't render, no listener → **in-editor dedicated server**)

**`GameInstance` borrows (never owns):** subsystems, ResourceManager/content DB, resource factories,
reflection registries.

### 4.2 Lifecycle

`GameInstance::Start()` / `Stop()` = exactly the §1 bracket, once, in one place. Both the player and the
editor call it; neither hand-rolls the sequence again.

### 4.3 Standalone vs editor

- **Player: exactly one instance.** `PlayerApplication::OnLaunch` creates it and starts it. Behaviour
  is unchanged — the current code *is* the N=1 case.
- **Editor: 0..N instances.** "Play" appends one; "Play New Instance" appends another (Zero's
  `PlayGameOptions::SingleInstance | MultipleInstances`, Ctrl+F5 — §8).

One code path, N=1 for shipping. This directly serves the recovered original PIE intent ("PIE = run the
game exactly like the shipped player").

### 4.4 A run manages a *set* of scenes, not one (correction)

An earlier draft tied a `GameInstance` to a single `Scene`. That is wrong — it was an artifact of the
current player, which loads one startup scene and never switches. The engine already supports **multiple
active scenes**: `SceneSubsystem` owns an `Array<Scene>` (`CreateScene`/`DestroyScene`/`ActiveScenes`),
and `DefaultApplication::OnRenderWindow` already iterates *all* active scenes. And per
[scripting.md](scripting.md), the game script's whole job is to **orchestrate above scenes** — which
scene to load, game states, transitions.

So the correct model:
- The **durable thing is the script run context** (the `Game` brain + globals). It persists across
  level transitions — `MainMenu → Level1 → Level2` is *one* game, *one* context, accumulated state
  intact. This is why **"one gameplay context per instance"** (§9.1) is the right rule, not "per
  scene": all of an instance's scenes' behaviors **and** its game script share that one context.
- A `GameInstance` holds a **set** of active scenes and a **current scene** the script repoints as it
  transitions. `m_primaryScene` therefore does **not** disappear (§5 corrected) — it becomes the
  per-instance *current* scene, mutable.
- **Scene ownership → run-host binding (Phase 2):** a scene binds to the run host of the instance that
  **loaded it**. Multi-scene-per-instance is fine — they all bind to the same (per-instance) run host.

**Gap this exposes — a scene-management facade.** Today the game script *cannot* switch scenes: there is
no `loadScene`/`Scene.load`/`SwitchScene` in the facades (only `Scene.spawn`/`find`/`findByPath`). A
scene-management seam (e.g. `Game.loadScene(id)` / unload / set-current, routing through the owning
`GameInstance`) is required for the script to actually drive transitions. Tracked as its own item
alongside the run-host work (§10); not a blocker for the Phase-1 extraction (which the player exercises
at one scene).

---

## 5. Time model generalization

Add an **instance** term between context and scene:

```
effective dt = host dt × context (app-wide) × instance (this game) × scene (this scene)
```

- **context** — app/editor-wide (pause everything, including editing pages). Unchanged meaning.
- **instance** — *this game run's* global scale. In the player this is what a game means by "global
  time scale."
- **scene** — unchanged.

**At N=1 (the player) context and instance collapse**, which is exactly why today's two-level model is
correct for shipping and why the gap only appears in the editor with N>1. `m_primaryScene` does **not**
disappear (correcting an earlier draft — see §4.4): it becomes the per-instance **current scene**, the
one the script's `update(dt)` time-pairs with, and the game repoints it on a scene transition. Each
instance has its own current scene; the term is per-instance, not global.

`Scene.cppm:519`'s documented contract must be updated in the same change.

---

## 6. `OnLaunch`/`OnExit` semantics — must be resolved

`IApplication::OnLaunch/OnExit` are documented as the play bracket ("standalone: once; editor: Play",
`runtime-host.md`). Today the editor calls them per Play (`GamePage`), which is fine at N=1 but
ill-defined at N>1.

**Proposal:** keep `OnLaunch/OnExit` as **app-level, once** (they configure the app), and let
`GameInstance::Start/Stop` be the **per-run** bracket. A future native game module then hosts its
`OnLaunch/OnExit` **per instance**, which is the seam MVP item 5 reserved. *Decide explicitly — this
changes an existing contract.*

---

## 7. Editor: `ScenePage` ≠ `GameInstance`

Do **not** model editing/preview pages as "instance 0." They differ structurally:

| | Editing `ScenePage` | `GameInstance` |
|---|---|---|
| script context / `Game` | none | **owns one** |
| lifecycle | snapshot → simulate → restore | fresh-from-cooked → teardown |
| source | live authored scene | cooked content |
| selection / gizmos | yes | no |

Collapsing them reintroduces an "is this the editor one?" branch everywhere. The editor holds N
ScenePages **and** M GameInstances. `m_gamePage` becomes `Array<GameInstance>` (or views onto them).

---

## 8. Precedent: Zero's `GameSession`

Zero achieves Ctrl+F5 multi-instance **in-process on one shared engine**:
- `GameSession` is the first-class game-instance object; the editor holds `GameArray mGames`
  (`Editor.hpp:259,272`) — a **collection, not a singleton**.
- `Editor::PlayGame(PlayGameOptions::Enum, takeFocus, startGame)`: `SingleInstance` (F5) quits existing
  games first; `MultipleInstances` (Ctrl+F5, `Data/Commands.data`: *"Play the game in an additional
  window"*) **skips that step and appends**.
- Each instance gets its own `GameWidget` tab; engine services stay process-wide.

Zero does **not** instantiate N engines — confirming the cut at the game-instance boundary rather than
the application/context boundary.

---

## 9. Open decisions

1. **Per-instance run host (the real work).** `ScriptSubsystem::m_runHost` must become one **per
   GameInstance**, with `OnSceneCreated` binding a scene's `ScriptSceneSystem` to the run host of the
   instance owning that scene. The "one gameplay context per run" rule becomes *per instance* — a
   deliberate amendment to `scripting.md`'s locked rule.
2. **Fixed-step × instance time scale.** An instance at 0.5× should fixed-step half as often; the
   `FixedStepper` accumulator is context-level today. Per-instance accumulator, or scale only variable
   dt? *Subtlest item.*
3. **Audio listener policy** with N instances — focused instance only, or mix? (multi-listener exists).
4. **Fault isolation** — a script fault must disable *that instance*, not all (today the fault path is
   app-level, :381+).
5. **Player multi-scene rendering** — `OnRenderWindow` notes "Single-scene for now - multiple active
   scenes would each clear; compositing is a later concern" (:404+). The editor is unaffected
   (per-viewport RTs), but N instances in the *player* would hit this.

---

## 10. Phasing

1. **Extract the bracket** — ✅ **SHIPPED (8f808e2).** `draconic.runtime.gameinstance`: `GameInstance`
   owns the script run state (context + `Game` + error sink) + a current-scene pointer + instance time
   scale, with `StartScript`/`StopScript`/`TickScript`. `DefaultApplication` owns one and delegates.
   Still N=1, pure refactor, tested clang+gcc.
2. **Per-instance run host** — the §9.1 change; two instances no longer share script globals. **Binding
   rule (per §4.4): a scene binds to the run host of the instance that loaded it**; an instance's many
   scenes share its one run host. `OnSceneCreated` binds lazily (adds the `ScriptSceneSystem`, leaves
   the run host unset) and the owning `GameInstance` binds `scene → its run host`.
3. **`Array<GameInstance>` + editor multi-instance** — un-singleton `m_gamePage`, add "Play New
   Instance" (Zero's enum shape). Fold in the **§6 `OnLaunch`/`OnExit` → app-level-once** contract
   change (callers switch to `GameInstance::Start/Stop` directly; the editor stops calling
   `m_app->OnLaunch/OnExit` per Play).
4. **Instance time scale** (§5) + **per-instance fixed accumulator** (§9.2, decided).
5. **Headless instances** — enables the in-editor dedicated server for [networking.md](networking.md).

**Cross-cutting (surfaced by the §4.4 correction): scene-management facade.** A `Game.loadScene(id)` /
unload / set-current seam routing through the owning `GameInstance`, so the script can drive scene
transitions (today it can't). Sequence it with Phase 3+ (multi-scene runs), independent of the run-host
mechanics. Not a Phase-1 blocker.

---

## 11. Target runtime model (the clean model — supersedes the retrofit framing above)

Decided 2026-07-21 (user: "no users, no backwards-compat — do it right, not incremental compromises").
§§1-10 above diagnosed the problem and the phasing; this section is the **target the implementation
builds to**. Where they conflict, this wins.

### 11.1 Two layers that today are conflated

- **Engine host** — "the engine is running": the Context + subsystems + resource infra + shell/graphics.
  Configured ONCE. No run state.
- **GameInstance** — "a game is running": a script brain + its scenes + its time/pause/headless state.
  Dynamic, 0..N, owned by the host.

`DefaultApplication` is both today, and `ScriptSubsystem` owns *the one run* (`m_runHost`). That single
ownership is the root of every N=1 compromise. The fix moves **run ownership** up to `GameInstance` — and
introduces a **`SceneManager`** so the tick loop moves *without* the scene layer ever learning about the
runtime layer (11.3).

### 11.2 The `SceneManager` — a group of scenes as a first-class scene-lib object

The linchpin (user's design, replacing an earlier "make `SceneSubsystem` instance-aware" draft that
would have inverted the dependency — `draconic.scene` must NOT know about `GameInstance`).

**`SceneManager` lives in the scene lib and knows only scenes.** It owns a *group* of scenes + the
group's shared state, and it *ticks its own group*:
- **membership** — the set of scenes in this group + the **current scene**; create / adopt / load /
  switch / unload / destroy.
- **group config** — the group's time scale, running/paused, headless, and its **fixed-step
  accumulator** (§9.2 — per group, i.e. per instance, since a group is fed one frame dt).
- **ticking** — `SceneManager::Update(hostDt)` / `FixedUpdate` drives *its* scenes' variable + fixed
  lanes at the group's rate. No global ticker needs to understand grouping.
- **ISceneAware notification** — it holds a reference to the aware-registry (owned by `SceneSubsystem`,
  still in the scene layer) and fires `OnSceneCreated`/`OnSceneDestroyed` as it manages scenes. So
  physics/audio/render/script still get injected — the registry stays where it is; only *who calls it*
  moves.

Crucially the dependency arrow points **down**: `GameInstance` (runtime) *owns* a `SceneManager` (scene
lib); the scene lib never references `GameInstance`. The coupling the earlier draft would have created is
gone by construction. `SceneManager` is also literally the multi-scene model of §4.4 — "the set of scenes
a run manages, with a current one" — so `Game.loadScene`/`switchScene` are methods on the owned manager.

### 11.3 Ownership

| Owner | Owns |
|---|---|
| **Engine host** (`IApplication`) | Context + subsystems + resource infra + shell/graphics; `Array<GameInstance>`. Configured once (`Configure`/`OnStartup`); **no run state**. |
| **GameInstance** | its `ScriptRunHost` (context + `Game` + binding + error sink — *moved out of `ScriptSubsystem`*) **and its `SceneManager`** (its scenes + current + group config). `Start`/`Stop`; scene ops delegate to the manager. |
| **`SceneManager`** (scene lib) | a group of scenes + current scene + group config (time scale / running / headless / fixed accumulator); ticks its group; fires ISceneAware via the registry. |
| **SceneSubsystem** | the Context-level **aware-registry** owner + a **default `SceneManager`** for loose / editor-editing scenes. No longer the global ticker of *all* scenes. |
| **ScriptSubsystem** | run-agnostic **machinery**: the `ScriptRunHost` type, `ScriptSceneSystem`, backend registry, reflection registration, contact/message *routing helpers*. No longer owns a run. |

### 11.4 Who ticks

**Each `SceneManager` ticks its own group** — the `GameInstance` drives its manager; the editor's
default manager drives loose/editing scenes. No orphans (every scene belongs to exactly one manager), no
global-vs-instance tension, and — the point — **no upward dependency**: a manager reads its *own* group
config, never a `GameInstance`.

- **Variable lane**: `dt a scene sees = host dt x context x GROUP(=instance) x scene` (§5). The group term
  is the manager's; the editor default manager uses 1.0 (today's behaviour).
- **Fixed lane** (§9.2): the accumulator is the **manager's**; it advances + steps its scenes at the
  group rate.
- **Script run** (GC stepping + binding clock + teardown) is per-`ScriptRunHost`, driven by the owning
  `GameInstance` (each instance steps its own run host). No global run-host tick.

### 11.5 Scene <-> run-host binding (through the manager)

Because a `GameInstance` owns both its `SceneManager` and its `ScriptRunHost`, binding is internal and
trivial: as the manager adopts a scene, the instance binds that scene's `ScriptSceneSystem` to its run
host. `OnSceneCreated` no longer force-binds a subsystem-global host (there isn't one). Contacts/messages
route via *that scene's* `ScriptSceneSystem` -> its instance's run host + binding. An instance's many
scenes share its one run host ("one gameplay context per instance", §4.4/§9.1).

### 11.6 Lifecycle contracts

- `OnLaunch`/`OnExit` become **app-once config** (§6); most of the player's current `OnLaunch` body
  moves into `GameInstance::Start`.
- `GameInstance::Start(descriptor)` = the run bracket: create its `SceneManager`, load scene(s) into it,
  bind the run host, start the script, simulate. `Stop()` = script exit + the manager stops/clears its
  scenes + release the run host. The player creates ONE instance at launch; the editor creates 0..N.
- Teardown accounting (today's `MaybeTeardownRunContext` + `m_gameScriptHold`) becomes **per run host**,
  owned by the instance.

### 11.7 What Phase 1 already got right / what grows

Phase 1's `GameInstance` (script state + current-scene pointer + instance time scale, with
`DefaultApplication` delegating) is the seed. It **grows** to own a `ScriptRunHost` + a `SceneManager`;
`DefaultApplication` **sheds** its run state to become the host owning `Array<GameInstance>`. No Phase-1
work is discarded.

### 11.8 Bounded rip / replace

- **Rip:** `ScriptSubsystem::m_runHost` + `m_gameScriptHold` singular ownership; the immediate
  `SetRunHost(&m_runHost)` in `OnSceneCreated`; `OnLaunch`/`OnExit` as the play bracket; `m_primaryScene`
  as "the" scene; the singular teardown accounting; `SceneSubsystem` as the global scene-ticker.
- **Add:** `SceneManager` (scene lib) — group ownership + tick + ISceneAware fan-out; `GameInstance` owns
  one + a `ScriptRunHost`; per-run-host teardown driven by the instance; the host owns
  `Array<GameInstance>`.
- **Keep:** the ECS/scene machinery, component managers, physics/audio/render per-scene systems, the
  ISceneAware *registry*, resource infra, the borrowed-vs-owned resource split — none care which manager
  owns the scene or who owns the run.

### 11.9 Implementation sequence (revised for `SceneManager`)

1. **`SceneManager` in the scene lib** — extract group ownership + tick + ISceneAware fan-out from
   `SceneSubsystem` (which keeps the registry + a default manager). Pure refactor at N=1 (the default
   manager reproduces today's global tick); testable headless.
2. **`GameInstance` owns a `ScriptRunHost`** — move run ownership off `ScriptSubsystem`; the host wires
   the instance's run host (configurator + spawner + message route); isolation + teardown tests.
3. **`GameInstance` owns a `SceneManager`**; scene<->run-host binding internal (11.5); `ScriptSubsystem`
   sheds `m_runHost`/`m_gameScriptHold` → machinery + routing.
4. **Engine host owns `Array<GameInstance>`**; `OnLaunch`/`OnExit` app-once; player = 1 instance, editor
   un-singletons `m_gamePage`.
5. **Editor multi-instance** ("Play New Instance") + **headless** (dedicated server for networking.md).

### 11.10 Run-host ownership resolved (step 3c design — investigated 2026-07-21)

The open question was "what run host does editor editing-scene Simulate use?" Investigated:
`ScenePage::StartSimulation` un-freezes ALL the scene's systems including its `ScriptSceneSystem`, so
**editing-Simulate runs script behaviors today** (a real feature - not to be regressed). So the answer
is NOT "only GameInstances have run hosts". The resolved model:

- **Run host <-> scene manager are paired.** `SceneSubsystem` owns a run host for its DEFAULT manager
  (editor / editing scenes - reuses its existing `m_ownedRunHost`); each `GameInstance` owns one for its
  manager (game scenes + the game script). At N=1 in the editor these are DISTINCT (editing-Simulate on
  the subsystem's host, the Game tab on the instance's) - cleaner than today's shared-host state.
- **Binding.** `OnSceneCreated` binds a scene to the DEFAULT run host; a `GameInstance` re-binds ITS
  scenes' `ScriptSceneSystem` to its own host after adopting them (legit: the instance is in the script
  layer; `SceneSubsystem`, being lower, cannot reach `ScriptSceneSystem`).
- **Driving.** Each owner drives its own run host (binding clock + GC in its update; teardown). The
  subsystem drives its own; the `GameInstance` drives its own (via `DefaultApplication`).
- **Game-script hold** moves ONTO the `ScriptRunHost` (a per-host flag). The game script talks to the
  INSTANCE's run host directly (`GameInstance::StartScript` uses `m_runHost.EnsureContextForFile`), not
  through `ScriptSubsystem::AcquireRunContextForFile` (which is retired).
- **Teardown is per-host.** `MaybeTeardownRunHost(host)` scans `m_systems` for systems whose bound host
  IS `host`; tears the host down when `!host.hold` AND none of those scenes simulate AND none have live
  behavior instances. The subsystem calls it for its own host; the instance's teardown is checked for
  its host (the subsystem still owns `m_systems`, so it runs the scan - the instance requests it).
- **Routing is per-host but shared logic.** `ScriptSubsystem::ConfigureRunHost(host)` installs the
  message-route + prefab-spawner + service-configurator on ANY host. The message route scans `m_systems`
  by scene, so one lambda works for every host; contacts route via the scene's `ScriptSceneSystem` ->
  its host. `SetContextConfigurator`/`SetPrefabSpawner` become stored defaults applied by `ConfigureRunHost`.
- **`ScriptSubsystem` sheds:** the single-`m_runHost` ownership (keeps only `m_ownedRunHost` for the
  default manager), `m_gameScriptHold` (-> per-host), `AcquireRunContextForFile`/`ReleaseRunContext` (the
  game script uses the instance's host). It keeps: `m_systems`, contact/message routing, ISceneAware,
  reflection registration, `ConfigureRunHost`, `MaybeTeardownRunHost`.

### 11.11 `SceneSubsystem` is a PURE registry — no default manager (SHIPPED 2026-07-22, commit 2231caf)

The final structural cleanup. `SceneSubsystem` no longer owns ANY scenes (the "default manager" in
§11.3/§11.10 is gone). It is purely: the app-wide `ISceneAware` registry + a list of registered
`SceneManager*` it ticks (`BeginFrame`/`Update` fan the Context time-scale/fixed-step to each) + two
read-only sweeps (`ForEachManager`, and a registry-wide `ForEachScene` for prefab-instance rebuild /
export scans across all managers). Every owner of scenes creates its OWN `SceneManager` over
`scenes->AwareRegistry()` and `RegisterManager`s it. `DefaultApplication` renders only instance scenes
(the default-manager render loop is gone) and exposes `PrimaryScenes()` (returns `SceneManager&`, so
callers reach the primary instance's group without naming `GameInstance` — also sidesteps an LLVM 21.1
frontend crash that materialising `GameInstance` in huge sample TUs hit).

**Editor scene managers — DECISION: per-page (SHIPPED) vs one shared editor manager (alternative).**
The shipped design gives EACH editor page its OWN `SceneManager` member:
- `ScenePage` — owns one; registers on open, `DestroyScene` + `UnregisterManager` on close. Its editing
  scene lives there. Export transcode/scan scratch scenes use a transient LOCAL manager over the aware
  registry (so `OnSceneCreated` still injects the full component-manager set).
- `MaterialPage` — owns one for its preview scene, same open/close lifecycle.
- `GamePage` — uses its `GameInstance`'s group (a no-embedded-app placeholder manager as the fallback).

Trade-off vs a single shared editor-owned manager:
- **Per-page (shipped):** cleanest lifetime (closing a page unregisters + destroys ONLY its group,
  locally); mirrors "each owner owns a manager" (a `GameInstance` owns one, each page owns one); no
  shared-group churn. COST: "all editing scenes" sweeps go through the registry-wide `ForEachScene`,
  which is broader than "editing scenes" — during play it also visits game-instance scenes (harmless
  for prefab-rebuild/export-scan, but semantically wider than one editing group).
- **One shared editor manager (alternative):** the editor (e.g. `EditorApplication`) owns ONE
  `SceneManager`; pages create/destroy scenes in it. Simpler mental model + the cross-page sweep is
  exactly the one editing group. COST: one extra long-lived owner; pages add/remove into shared state;
  slightly more coupling. This is what a reader might first expect.
- **Verdict:** neither is clearly better; per-page shipped for lifetime/isolation cleanliness. Switching
  to one-shared is a bounded change (a shared manager on the editor + pages create in it + drop the
  page-owned members) if the registry-wide sweep breadth ever becomes a real problem.

Run-host binding is UNCHANGED by this: `OnSceneCreated` still binds a scene to the subsystem's
`m_ownedRunHost` regardless of which manager owns the scene; a `GameInstance` re-binds its own scenes to
its host (§11.10). The manager a scene lives in is orthogonal to which run host drives its behaviors.

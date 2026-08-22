# Messaging: one bus per run scope, owned by the scope, borrowed by scenes

Status: COMPLETE (P1-P2 Opus, P2-revision cleanup + P3 Fable, 2026-08-22; full
battery + ASAN green). Origin: Documentation/Ideas/scripting-runtime-shape.md §7 + the
follow-up exchange. Spec 2 of the three-spec cut (networking-extraction.md is spec 1;
the script-surface spec is spec 3).

## The decisions (locked)

1. **The bus TYPE moves out of foundation.scene into its own module** (user direction):
   `Code/Foundation/Messaging`, module `foundation.messaging`, target
   `Foundation::Messaging` - a LEAF under foundation (imports foundation.core only).
   `scene::EventBus` becomes `messaging::EventBus`; foundation.scene imports messaging.
   No-compat: every `scene::EventBus` usage updates in the same commit (there is no
   alias shim). EventBus tests move to `Messaging.Tests`. Follows the standing
   folder==target==module convention (Process/CONVENTIONS.md).
2. **ONE bus per RUN SCOPE, owned by the scope, borrowed by scenes** (the
   instance-less-scene resolution, user-confirmed; REVISED 2026-08-22 - user ruling:
   NO owned fallback bus):
   - `Scene` holds ONLY a borrowed `messaging::EventBus*` (`SetEventBus`), null until a
     scope injects. There is NO owned fallback - the original fallback existed only so
     bare test scenes worked unwired, which is a production branch maintained for
     tests (and worse: tests would exercise the owned path while production runs the
     borrowed one - zero fidelity on the drain topology). Test fixtures create a bus
     and inject it - one line, and the tests now exercise EXACTLY the shipping path.
   - `Scene::Events()` returns the borrowed pointer (nullable). The script-facing
     `scene.events` handle already no-ops safely on a null scene; it extends the same
     contract to a null bus. Native subscribers subscribe at SystemsReady, BY WHICH
     TIME the scope has injected (injection is part of scene creation/adoption, before
     the composition installer fires SystemsReady - an ORDER the wiring must keep and
     a test must pin). A bus-less scene (headless scratch: export transcode,
     scene_validate) simply never emits - and an emit attempt through a null Events()
     in native code is a loud contract violation, not a silent no-op.
   - `GameInstance` owns THE bus for its run (what `m_runEvents` already is) and
     injects it into every scene it creates/adopts/loads. Result: `scene.events.emit`
     and the run bus are THE SAME BUS in an instance - the explicit scene->run relay
     is DELETED as a concept (a Level may still re-emit as a deliberate
     translation/encapsulation boundary; it is never required).
   - The EDITOR scene page is the run scope for edit-mode Simulate: it owns a
     page-scoped bus and injects it into its scene (the page already owns the command
     stack / selection / edit context - this is the same ownership shape). PIE Game
     tabs keep the GameInstance bus.
3. **Drain ownership is trivial now: ONLY scopes drain. Scenes NEVER drain.** (The
   original spec's "behaviorally delicate" conditional drain rule existed only because
   of the owned fallback; with the fallback deleted, the rule is one sentence and the
   double-dispatch hazard is unrepresentable.) The owning scope drains once per frame
   at its tick top - GameInstance before its scene updates, the editor page in its
   Simulate tick - so events emitted last frame arrive this frame. `Scene::Update`
   stops touching the bus entirely.
4. **Subscriptions are OWNER-HELD tokens.** `Subscribe` returns a token; the
   subscriber's owner releases it on its own teardown. The script subsystem is the
   owner for all script subscriptions (behaviors, Level, Game) and releases in the
   hooks it already has (StopBehavior / ReleaseComponentInstances / scene-system
   teardown / run teardown). No bus-level weak refs; the bus never learns about
   scenes or entities (it stays scope- and domain-agnostic).
5. **Untagged in v1.** Multi-active-scene IS supported (additive loads), and
   game-semantic events want cross-scene delivery - the common case. A scene-tag/topic
   convention is added on the FIRST real cross-scene collision, not speculatively
   (recorded deferral).
6. **`entity.send` stays directed and separate.** Broadcast = the scope bus; directed
   = entity.send. Unchanged.

## What this deletes

- The scene->run relay convention and its documentation (`GameInstance.cppm:433`'s
  "there is NO implicit scene->run relay" comment inverts: there is no relay because
  there is nothing to relay ACROSS).
- Per-scene bus instances in instance scenes (the owned fallback exists but sits idle
  once a scope injects).
- The Game tier's separate run-bus inbox plumbing collapses onto plain subscription to
  the one bus (the `on<Event>` dispatch surface is unchanged for script authors).

## Phasing

- **P1 - the module move.** foundation.messaging created; EventBus + its tests move;
  all usages update (scene, gameinstance, script bridge, facades). Pure relocation -
  zero behavior change; the battery proves it.
- **P2 - borrowed-bus wiring.** Scene::SetEventBus + nullable Events() (NO owned
  fallback - revised decision 2); GameInstance injects into created/adopted/
  async-loaded scenes (every path through its scene group) BEFORE SystemsReady fires;
  the editor ScenePage injects its page bus for Simulate; scene tests gain the
  one-line bus fixture; the relay deletes; `run.events()` and `scene.events` now
  resolve to the same object in a run; Scene::Update's drain deletes.
- **P3 - subscription tokens.** Subscribe returns a token; the script subsystem holds
  and releases per owner in its existing teardown hooks; native subscribers
  (C++ systems) hold their own tokens. Behaviors may subscribe directly (locked
  decision 3 of the ideas doc) - through the same lifecycle-bound tokens.

## Tests (the P2 set is the gate)

- Relay-free delivery: a behavior emits in an instance scene; the Game tier's
  `on<Event>` fires with NO Level re-emit.
- Editor scope: in a page-scoped Simulate, physics-contact -> behavior -> emit ->
  a Level `on<Event>` in the same scene fires (the instance-less path).
- Bus-less scene: a scratch scene with no injection ticks, loads, and validates
  cleanly (never emits); the script `scene.events` handle no-ops safely on it.
- Injection-before-SystemsReady ORDER: a native system subscribing at SystemsReady
  sees the scope's bus already present (the wiring-order pin).
- Cross-scene delivery: two active scenes sharing the scope bus; an event emitted in
  scene A is dispatched exactly once per subscriber, and subscribers in scene B hear
  it. (Double-dispatch is structurally impossible now - no scene drains - but the
  exactly-once assertion stays as the regression guard.)
- Token teardown: a behavior subscribes, its entity is destroyed mid-run, the next
  emit dispatches without touching the dead subscriber (ASAN on this test - it is the
  lifetime-sensitive class).
- Additive scenes: load a second scene into the instance; both hear the bus; unloading
  one releases its subscriptions (scene-system teardown tokens).

## Progress

- **P1 DONE (Opus, 2026-08-22).** Pure module move, zero behavior change. `EventBus` relocated from the
  `foundation.scene:events` partition into its own leaf module `foundation.messaging`
  (`Code/Foundation/Messaging`, `Foundation::Messaging`, over Core only). `foundation.scene` now
  `export import foundation.messaging` (so existing scene importers still see the type) + links it PUBLIC;
  `Scene` uses `messaging::EventBus`. All `scene::EventBus` usages updated to `messaging::EventBus`
  (GameInstance, ScriptSubsystem, ScriptFacades via Scene::Events, GameInstance tests) - no alias shim.
  EventBus tests moved to `Messaging.Tests` (Scene.Tests drops them). Drain log tag `Scene`->`Messaging`.
  Verified: clang + gcc DEBUG green (Messaging 4/4, Scene 55/55, GameInstance 25/25); Script.Facades /
  SceneSurface / Editor.App link clean. NEXT: P2 (borrowed-bus wiring + drain ownership + relay delete).

- **P2 DONE (Opus, 2026-08-22).** Borrowed-bus wiring + drain ownership + relay deleted. `Scene` gained
  `SetEventBus(messaging::EventBus*)` + an owned fallback; `Events()` returns the borrowed scope bus when
  set, else the owned one; `Scene::Update` drains ONLY the owned fallback (a borrowed bus is drained by
  its owning scope - never every borrowing scene). `GameInstance` injects its run bus into every scene it
  creates - done via `SceneManager::SetSceneEventBus(&m_runEvents)` (set in the GameInstance ctor) so the
  bus is applied BEFORE assembly, i.e. before the script systems bind in `OnSceneCreate` (the ordering
  hazard: binding to the owned bus then switching would have stranded the bridge on a never-drained bus).
  `SetScene` also injects (adopt path). So `run.events()` and `scene.events` resolve to the same object in
  a run; the Game inbox + behaviors + Level all subscribe to that one bus; the instance's `DrainRunEvents`
  is the single per-frame drain. The scene->run relay is gone as a concept (RunEvents() doc updated).
  Editor edit-mode Simulate + bare test scenes use the owned fallback (a scene is its own scope), drained
  by `Scene::Update` unchanged. Tests: Scene.Tests borrowed-vs-owned resolution + drain-only-owned;
  GameInstance.Tests cross-scene delivery + single drain (emit in scene B, subscriber in scene A fires
  once via DrainRunEvents, scenes don't drain). Verified: clang + gcc DEBUG green (Messaging 4/4, Scene
  56/56, Engine.Scene 5/5, Engine.Script 91/91, GameInstance 26/26); Editor.App / SceneSurface link.
  NEXT: P3 (subscription tokens - owner-held, released by the script subsystem's teardown hooks).

## Docs (same-commit duties)

Systems/game-instance.md (the run-bus paragraph), Systems/scripting.md (event flow),
Shipping/Scripting.md if it names the relay, and the game-ready-scripting2 spec gains
a superseded-by-messaging note on its §1a relay design.

- **P2 REVISION CLEANUP + P3 DONE (Fable, 2026-08-22).** Opus's P2 was built to the
  PRE-revision spec (owned fallback + edit-mode-on-fallback); the cleanup aligned it to
  revised decisions 2/3: the owned fallback DELETED (Scene::Events() returns the
  nullable borrowed pointer; Scene::Update never touches a bus), the editor ScenePage
  became a real run scope (page-owned bus, SetSceneEventBus before CreateScene, drained
  once per page frame), the script bridge + facades went null-safe, and every affected
  fixture now injects a bus - exercising the exact shipping topology. CONSEQUENCE
  (documented in the tests): event delivery is uniformly the SCOPE cadence - emitted
  frame N, delivered at frame N+1's scope drain; the Roll Call tests' same-frame
  expectation was an owned-fallback artifact production never had. P3: the owner-held
  token discipline verified end to end (the bridge owns handles, Clear on scene stop /
  Level teardown; dispatch resolves LIVE components at drain time) + the token-teardown
  test (destroyed subscriber untouched, survivors fire) green under ASAN.
  DEFERRED with its own decision needed: DYNAMIC script subscription
  (scene.events.subscribe(name, delegate)) - IScriptDelegate has no owner accessor and
  Luau delegates can be ownerless closures, so correct per-behavior lifecycle release
  needs a cross-backend delegate-owner design first. Declared on<Event> handlers remain
  THE behavior subscription surface. Docs swept same-commit (scripting.md scope
  wording, game-ready-scripting2 relay superseded note).

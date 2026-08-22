# Messaging: one bus per run scope, owned by the scope, borrowed by scenes

Status: SPEC - ready to build (Fable, 2026-08-22; the run-scope ownership model
user-confirmed). Origin: Documentation/Ideas/scripting-runtime-shape.md §7 + the
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
   instance-less-scene resolution, user-confirmed):
   - `Scene` gains `SetEventBus(messaging::EventBus*)` (borrowed) and keeps an OWNED
     fallback bus; `Scene::Events()` returns the borrowed bus when set, else the owned
     one. A scene with no scope is semantically its own scope - bare unit-test scenes
     work unwired, unchanged.
   - `GameInstance` owns THE bus for its run (what `m_runEvents` already is) and
     injects it into every scene it creates/adopts/loads. Result: `scene.events.emit`
     and the run bus are THE SAME BUS in an instance - the explicit scene->run relay
     is DELETED as a concept (a Level may still re-emit as a deliberate
     translation/encapsulation boundary; it is never required).
   - The EDITOR scene page is the run scope for edit-mode Simulate: it owns a
     page-scoped bus and injects it into its scene (the page already owns the command
     stack / selection / edit context - this is the same ownership shape). PIE Game
     tabs keep the GameInstance bus.
3. **Drain ownership follows bus ownership - exactly one drain per bus per frame.**
   Today each scene drains its own bus at its tick top and GameInstance drains the run
   bus; a shared borrowed bus MUST NOT be drained by every scene (double-dispatch).
   Rule: `Scene` drains ONLY its owned fallback bus; a borrowed bus is drained by its
   OWNING scope (GameInstance at its tick top, before scene updates so events emitted
   last frame arrive this frame; the editor page in its Simulate tick). This is the
   one behaviorally delicate point of the spec - see Tests.
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
- **P2 - borrowed-bus wiring.** Scene::SetEventBus + Events() resolution + the
  drain-ownership rule; GameInstance injects into created/adopted/async-loaded scenes
  (every path through its scene group); the editor ScenePage injects its page bus for
  Simulate; the relay deletes; `run.events()` and `scene.events` now resolve to the
  same object in a run.
- **P3 - subscription tokens.** Subscribe returns a token; the script subsystem holds
  and releases per owner in its existing teardown hooks; native subscribers
  (C++ systems) hold their own tokens. Behaviors may subscribe directly (locked
  decision 3 of the ideas doc) - through the same lifecycle-bound tokens.

## Tests (the P2 set is the gate)

- Relay-free delivery: a behavior emits in an instance scene; the Game tier's
  `on<Event>` fires with NO Level re-emit.
- Editor scope: in a page-scoped Simulate, physics-contact -> behavior -> emit ->
  a Level `on<Event>` in the same scene fires (the instance-less path).
- Standalone fallback: a bare Scene with no injection emits + drains its owned bus
  (existing EventBus tests keep passing relocated).
- SINGLE-DRAIN: two active scenes borrowing one bus; an event emitted in scene A is
  dispatched exactly once per subscriber, and subscribers in scene B hear it
  (cross-scene delivery is the feature, double-dispatch is the bug).
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

## Docs (same-commit duties)

Systems/game-instance.md (the run-bus paragraph), Systems/scripting.md (event flow),
Shipping/Scripting.md if it names the relay, and the game-ready-scripting2 spec gains
a superseded-by-messaging note on its §1a relay design.

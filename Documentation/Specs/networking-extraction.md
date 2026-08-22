# Networking extraction: off the god object, off the app, onto the standard lanes

Status: SPEC - ready to build (Fable, 2026-08-22). Origin:
Documentation/Ideas/scripting-runtime-shape.md §8 (Fable-reviewed, user-cut as its own
spec with "no holdovers"). This is an EXTRACTION + RELOCATION onto lanes that already
exist - not a networking rewrite. The wire protocol, the replication model, and the
role presets do not change.

## The problem (verified in the ideas doc)

Networking is the one domain that breaks the house pattern twice over:

- `GameInstance : public net::INetworkController` - the god object IS the controller
  (~24 net refs in GameInstanceImpl): endpoint lifetime, StartServer/Connect/Stop,
  DriveNetwork, the per-SetScene "replicated scene" wiring.
- `DefaultApplication` carries app-resident net logic (~28 refs): the per-fixed-step
  `DriveNetwork` fan-out over instances, `MakeEndpointOnlineHook` + the content-DB
  net-spawn (prefab) resolver, `ApplyNetworkStartup` (role presets), facade
  registration. The editor's embedded drive mirrors the fan-out with its own stepper.
- `NetworkSubsystem` is an empty shell ("does no per-frame work") while every other
  sim domain self-ticks on a standard lane.

## The target shape (three pieces, all on existing lanes)

1. **`NetworkController`** - a composed object (engine.gameinstance or engine.net; see
   layout below) owning what GameInstance currently IS: the endpoint, the
   `INetworkController` implementation, `StartServer`/`Connect`/`StopNetworking`, and
   the replicated-scene selection (the SetScene/level-load wiring moves WITH it).
   `GameInstance` holds one by value/Ref and forwards; `GameInstance : public
   INetworkController` is deleted. Script/preset triggers call INTO the controller.
2. **Replication rides the per-scene fixed lane.** A `NetworkSceneSystem`
   (`SceneSystem::OnFixedUpdate`) does the state capture/apply/interp sampling per
   scene, exactly beside `PhysicsSceneSystem` - deterministic, physics-lockstep. The
   `NetworkSubsystem` shell becomes real: the net `SceneModule` installs the scene
   system (scene-composition model), and the subsystem is the `ISceneObserver` +
   controller-wiring home.
3. **The transport pump is a per-frame context-subsystem concern**: recv-drain in
   `BeginFrame`, send-flush in `PostUpdate`, connection lifecycle events in between -
   ticked uniformly by the Context like every subsystem, never hand-pumped. Endpoint
   LIFECYCLE (listen/connect/stop) stays trigger-driven through the controller; only
   the TICK moves here.

Explicitly NOT done: resurrecting the context-level fixed lane (deleted in the
FrameTime cutover for having zero consumers - replication gets its fixed step from the
per-scene lane, which is the correct clock for sim state).

## Timing semantics (deliberate, documented)

- Replication under the per-scene fixed lane inherits the scene's time chain: a paused
  or slow-mo scene replicates at its scaled rate. That is CORRECT for sim-state
  replication (a paused sim produces no state changes worth sending) and matches
  physics. The transport pump is per-frame and unaffected - connections stay alive,
  recv keeps draining, regardless of scene time.
- The entity-active rules already landed for net (state freezes for effectively
  inactive entities; entities stay in snapshots) are unchanged by the move - the same
  capture/apply code relocates.

## Phasing (each lands battery-green both compilers; ASAN on teardown-touching phases)

- **P1 - NetworkController extraction.** Move the endpoint + INetworkController impl +
  StartServer/Connect/StopNetworking + DriveNetwork + replicated-scene wiring off
  GameInstance into `NetworkController`; GameInstance composes + forwards (public API
  preserved this phase so callers/tests are churn-free; no-compat means the forwards
  can thin later). Delete the inheritance. Tests: existing GameInstance net tests pass
  unchanged; new controller unit tests for lifecycle (start/stop/reconnect, teardown
  under ASAN - socket + endpoint lifetimes are exactly the UAF-prone class).
- **P2 - replication onto the scene fixed lane.** `NetworkSceneSystem::OnFixedUpdate`
  performs what `DriveNetwork`'s replication half does today, per scene; the net
  `SceneModule` installs it; `NetworkSubsystem` becomes the observer/wiring home
  (controller pointer reaches the scene system the same way audio's engine pointer
  does - at the SystemsReady stage). `DriveNetwork` shrinks to the transport half.
  Tests: the replication round-trip tests re-targeted to scene fixed stepping
  (Scene::FixedUpdate drives capture/apply); a scene-time-scale test (paused scene =
  no state deltas; transport still pumped).
- **P3 - transport pump as the context subsystem.** BeginFrame recv-drain +
  PostUpdate send-flush on `NetworkSubsystem`; `DriveNetwork` deletes;
  `DefaultApplication::OnFixedUpdate`'s fan-out deletes (the override becomes empty -
  REMOVE the override; the host's app-level fixed hook itself stays, other apps may
  use it); the editor's embedded-drive stepper fan
  (Editor.App ApplicationImpl, the "OnFixedUpdate ... DriveNetwork" mirror) deletes
  with it. Tests: Engine.Net.Tests subsystem tests assert pump-per-frame behavior;
  the in-editor PIE server/client smoke stays green (the editor path loses its
  special-case, which is the point).
- **P4 - the app sheds the rest.** `MakeEndpointOnlineHook` + the net-spawn prefab
  resolver move behind a service the controller owns (injected at wiring, not
  app-inline); `ApplyNetworkStartup` becomes a thin call into the controller from the
  role preset. The `Net` facade REMAINS this spec (rewired to resolve the controller
  service) - its retirement into `.of` belongs to the script-surface spec
  (sequencing ruling: networking first, facades later).

## Layout decision

`NetworkController` lives in **engine.gameinstance** (beside its owner) in P1 -
extraction is about ROLES, not modules, and gameinstance already links net. If the
script-surface spec later wants `NetworkController.of(context)`, the controller can
migrate to engine.net then (one move, with that spec's own churn budget). The net
`SceneModule` + subsystem work lives in engine.net where the shell already is.

## Tripwires + docs (same-commit duties)

- The net `SceneModule` joins `FullSceneComposition` - the ModuleCount guard in
  SceneSurfaceTests bumps 9 -> 10 deliberately... unless the existing net managers
  module already counts (it does - `foundation::net::AddNetworkSceneManagers` is
  module #9); then the scene SYSTEM joins the EXISTING net module's install and the
  count stays 9. Verify at P2; either way the guard changes deliberately or not at
  all - never silently.
- Documentation/Systems/networking.md updates in the SAME commits as each phase (the
  doc-sweep lesson); game-instance.md's net paragraph updates at P1.
- kSubsystemFacadeNameCount unchanged (the facade stays this spec).

## Progress

- **P1 DONE (Opus, 2026-08-22).** `engine.runtime::NetworkController` extracted into the
  `engine.gameinstance:networkcontroller` partition (NetworkController.cppm + NetworkControllerImpl.cpp):
  owns the endpoint + `INetworkController` impl + StartServer/Connect/StopNetworking + DriveNetwork +
  the net script binding + the replicated-scene cache. `GameInstance` now COMPOSES it (`m_network`) and
  forwards; `GameInstance : public net::INetworkController` deleted; public net API preserved as inline
  forwards (callers/tests churn-free); `Network()` accessor added for later phases. Tests: existing
  server+client-over-UDP test green unchanged (comment updated to "composes"); new NetworkController
  unit tests (start/stop/reconnect + online-hook-not-consumed + fresh-endpoint-replicates-cached-scene
  + live destruct with a live endpoint). Verified: clang + gcc DEBUG batteries green (23/23), ASAN green
  (the only UBSan hit is the pre-existing ThirdParty AngelScript VM one, unrelated). Docs: game-instance.md
  + networking.md net paragraphs updated same-change. NEXT: P2 (replication onto the scene fixed lane).

## P2 layering question (OPEN - for Fable review before build)

P2 puts `NetworkSceneSystem` in **`engine.net`** (user ruling: it obviously belongs with the other
scene-integration net code, not in foundation). Confirmed facts that constrain the design:

- **The endpoint is instance-owned.** `NetworkController` (composed by `GameInstance`, P1) owns
  `UniquePtr<NetworkManager> m_net`, created per StartServer/Connect. One endpoint per running game.
- **Nothing needs to move out of `foundation.net`.** The `Update` split
  (`UpdateTransport`/`UpdateReplication`) stays on `NetworkManager` in `foundation.net.manager` - it is
  the endpoint's own method. `engine.net`'s `NetworkSceneSystem::OnFixedUpdate` just calls
  `endpoint->UpdateReplication()` (so `engine.net` gains an `import foundation.net.manager`, which is a
  normal engine->foundation edge).
- **The scene-system install joins the EXISTING net module** (ModuleCount stays 9): `engine.net` gets
  an engine-level installer `engine::net::AddNetworkSceneManagers(scene)` that calls
  `foundation::net::AddNetworkSceneManagers(scene)` (the component managers) + `scene.AddSystem<NetworkSceneSystem>()`.
  `SceneSurface`'s `kNetModule` switches to that engine installer (aligning net with physics/audio,
  which already use `engine::*` installers). No new module; the `== 9u` guard is unchanged.

The one genuinely open decision: **how the per-instance endpoint reaches the per-scene
`NetworkSceneSystem`.** No option has a cycle; the difference is a module edge + indirection:

1. **Direct edge (Opus recommends).** `NetworkController` stays in `engine.gameinstance` and calls
   `scene->GetSystem<engine::net::NetworkSceneSystem>()->SetEndpoint(m_net.Get())` on
   SetReplicatedScene / StartServer / Connect (and `SetEndpoint(nullptr)` on StopNetworking / scene
   change). Adds a benign, acyclic `engine.gameinstance -> engine.net` dependency. Simplest, and P1's
   layout note already anticipated `NetworkController` possibly migrating to `engine.net` in P4 anyway.
   Verified acyclic: `engine.net` imports foundation.* + engine.scene only; never engine.gameinstance.
2. **Dependency-invert (no gameinstance->engine.net edge).** `engine.net`'s `NetworkSubsystem`
   implements a foundation-level binder interface (e.g. `net::IReplicationSceneBinder{ BindEndpoint(Scene*,
   NetworkManager*) }`), exposed as a per-context service. `NetworkController` resolves the binder off
   the context and never names `engine.net`. Keeps `engine.gameinstance` off `engine.net` at the cost
   of one interface + a service indirection.
3. **Move `NetworkController` to `engine.net` now** (pull P4's migration earlier). Note this does NOT
   remove the edge: `GameInstance` composes the controller by value, so `engine.gameinstance ->
   engine.net` is still required to hold the member. It only relocates the controller.

Opus's lean: option 1 - the edge is benign, acyclic, and the least churn; revisit if the script-surface
spec's `NetworkController.of(context)` later argues for option 3's relocation. Fable: please rule.

### FABLE RULING (2026-08-22): Option 1 - and it is the RIGHT place, not merely the cheap one

The acyclicity claim is verified (engine.net imports foundation.* + engine.scene only). Build
option 1, with the reasoning sharpened and four requirements:

The deeper argument for 1: the audio-style domain-internal wiring (subsystem observer injects its
engine pointer at SystemsReady) CANNOT work here, because the audio engine is CONTEXT-GLOBAL while
the endpoint is INSTANCE-SCOPED. `NetworkSubsystem` is context-wide - it cannot know which scenes
belong to which instance or which endpoint they should see. The controller is the ONLY object that
owns the instance <-> endpoint <-> replicated-scene mapping, so the binding logic belongs there by
knowledge, not just by churn. The `engine.gameinstance -> engine.net` edge is the honest expression
of that ownership.

- Option 2 REJECTED: a foundation interface + service indirection whose only consumer is this one
  binding is the seam-for-one-consumer pattern (the tool_panel lesson; the action-row rule). If a
  second binder consumer ever appears, extract the interface THEN.
- Option 3 REJECTED as premature: it does not remove the edge (the by-value member requires it
  regardless) and it pre-spends the one-move relocation budget this spec explicitly reserved for
  the script-surface spec's `.of` decision. The budget stays reserved.

Requirements on the option-1 build:
1. Binding is EDGE-driven at exactly the controller's listed points (SetReplicatedScene /
   StartServer / Connect / StopNetworking / scene change), and ONLY the replicated scene's
   `NetworkSceneSystem` ever holds the endpoint - additive sibling scenes stay null/inert (matches
   today's single-replicated-scene model; widening is a future decision, not a default).
2. Both teardown ORDERS are handled and TESTED: endpoint-dies-before-scene (StopNetworking clears
   the scene system's pointer) and scene-dies-before-endpoint (the controller's replicated-scene
   cache clears - the Destroying observer or the SetScene path - so a later Start/Connect never
   touches a dead scene). The scene system itself dies with its scene, so the hazard is the
   CONTROLLER's cache and the SYSTEM's endpoint pointer; null both eagerly.
3. The ASAN pass for P2 covers both orders above plus stop-start-reconnect against a LIVE scene
   (the joint-UAF lesson: lifetime-sensitive teardown gets sanitized in both directions).
4. `SetEndpoint` is a plain setter on the scene system - no service, no observer hop for the
   binding itself; the SystemsReady observer stage remains for what it already does (the
   subsystem's own wiring), not for endpoint routing.

The engine-level installer fold (engine::net::AddNetworkSceneManagers wrapping the foundation
managers + the scene system, kNetModule switching to it, ModuleCount stays 9) is APPROVED as
described - it aligns net with the physics/audio installer shape and the guard changes not at all,
deliberately.

## Acceptance

The net demo scripts (net-demo-scripts.txt flow: in-editor dedicated server + PIE
client) run unchanged end to end; `git grep -c "net" Code/Engine/Engine.DefaultApp`
drops to link/registration-only mentions; GameInstance's net surface is forwards-only;
the full battery + the net teardown ASAN pass are green.

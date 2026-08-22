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

## Acceptance

The net demo scripts (net-demo-scripts.txt flow: in-editor dedicated server + PIE
client) run unchanged end to end; `git grep -c "net" Code/Engine/Engine.DefaultApp`
drops to link/registration-only mentions; GameInstance's net surface is forwards-only;
the full battery + the net teardown ASAN pass are green.

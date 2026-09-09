# Networking

> Status: CURRENT
> Verified: 2026-08-12 @ 9c9046f8
> Track: [[networking-track]] / [[game-instance-track]]

Reflection-driven, server-authoritative networking, shipped end to end (P0-P2) and wired into
`DefaultApplication` per game instance. A game sets a role, marks component fields `Replicated`,
and gets authoritative, fog-of-war-filtered, interpolated replication with prefab net-spawn +
late-join - no hand-written per-component net code. First target is real-time strategy; the
`IReplicationModel` seam keeps other genres open.

## Modules

- **`foundation.net`** - transport: reliable-UDP + TCP + loopback/sim + the wire codec. Raw
  sockets live in `Core/System` (the platform backend pattern - `System.cppm` + per-OS dirs),
  not here; `foundation.net` is the reliability/session layer above them.
- **`foundation.http`** - the sibling HTTP/1.1 layer over the TCP wrappers: incremental message
  parser, a pump-model `HttpServer` (one request per connection, `Connection: close`) with
  Server-Sent Events streams (`SseStream`, ref-counted, held past the response), and a blocking
  localhost `HttpFetch` client (no DNS, no TLS - the localhost trust domain). Consumers: the MCP
  streamable-HTTP host, the WebSocket upgrade path (a WS connection begins as an HTTP request -
  this server is the web-networking server's front door), loopback tooling/tests.
- **`foundation.net.manager`** - `NetworkManager` (a session endpoint) + `INetworkController` +
  the `Net` script-facade binding. A GameInstance owns its OWN networking and goes online at
  RUNTIME via the facade (no app-owned socket). The endpoint + role lifecycle + `INetworkController`
  impl live on `engine.runtime::NetworkController` (`engine.gameinstance`), which the GameInstance
  COMPOSES and forwards to (networking-extraction.md P1; GameInstance no longer inherits
  `INetworkController`). Later phases move the per-frame drive onto the standard lanes.
- **`foundation.net.replication`** - `StateReplication` + the `IReplicationModel` seam.
- **`engine.net`** - the net scene-integration: `NetworkSubsystem` (registers the reflected components)
  plus `NetworkSceneSystem` and the engine-level `AddNetworkSceneManagers` installer (the net
  `SceneModule` - [[scene-composition]]) that installs the `NetworkComponentManager` + the scene system.
  `NetworkSceneSystem::OnFixedUpdate` drives the endpoint's `UpdateReplication` on the per-scene FIXED
  lane (deterministic, physics-lockstep - networking-extraction.md P2); the per-instance
  `NetworkController` points it at the live endpoint only for the endpoint's current replicated scene.
  The subsystem also OWNS the per-frame TRANSPORT pump: `NetworkSubsystem::PostUpdate` visits every live
  endpoint (via an app-provided source) and drives `UpdateTransport` (socket recv/send) on the Context
  lane (networking-extraction.md P3) - replacing the app's former `OnFixedUpdate` `DriveNetwork` fan-out
  (deleted, including the editor's embedded mirror). `NetworkManager::Update` is split into
  `UpdateTransport` (per-frame) and `UpdateReplication` (per-scene fixed lane).

## Roles + startup

`NetworkStartup` carries the role (`NetworkRole` None/Server/Client) + preset; the app's role-preset
startup is a thin call into the instance's controller. Per-instance: each `GameInstance` has its own
endpoint and goes online at runtime via the `Net` facade (`Net.startServer` / `Net.connect`), so a
single process can host multiple instances. The prefab net-spawn resolver is injected once as a factory
onto each instance's `NetworkController` (`Network().SetSpawnResolverFactory` - networking-extraction.md
P4); the controller makes one per endpoint and installs it, so reconnect keeps it. The app provides only
the content-DB-backed factory (`MakeSpawnResolver`); it no longer wires the endpoint itself.

## Replication (`StateReplication`)

Mark component fields `Replicated` (a reflection attribute) and the model does the rest:
- the server builds a snapshot and a PER-PEER delta (reflection-driven field codec);
- **prefab net-spawn** replicates spawned entities; a joining client gets a full-snapshot
  **late-join** sync (first-class for this target - reconnect brings full state);
- clients **interpolate** replicated state for smoothness;
- **per-peer relevancy / fog-of-war** filters what each client sees - interest management is a
  SECURITY requirement here (a client never receives what it should not see), not just bandwidth.

No client prediction / reconciliation / lag compensation and no lockstep determinism are needed
for the current target (server-authoritative), which is what shipped.

## The model seam (`IReplicationModel`)

`StateReplication` is one model behind the `IReplicationModel` seam - the escape hatch that keeps
the design multi-genre without disturbing what shipped. A twitch-shooter unreliable-snapshot+ack
model, or a large-scale-RTS lockstep `CommandReplication`, can slot BESIDE it. Target-specific
choices (e.g. the reliable-ordered delta baseline) are POLICY of the current send path, not baked
into the wire format or the seam. A future lockstep `CommandReplication` would re-raise the
determinism caveat (lockstep needs bit-exact simulation - not promised by state replication).

## Script facade

The `Net` facade (registered by `RegisterNetScriptFacade`, resolved per script context through
the instance's `NetworkManager`): `Net.isServer()` / `isClient()` / `peerCount()` /
`startServer(...)` / `connect(...)`, plus SEND-side RPC (`Net.rpc(...)` / `rpcNumber(...)` /
`rpcText(...)`). `NetworkComponent.of(entity).authority` exposes ownership to script. Certified on
AngelScript + Luau (see `Integration.ScriptFacades`; Wren was retired 2026-08-19). The RECEIVE side (`Net.on(name, fn)`) is not
built - it is P3 slice 1 (see the commands plan).

## Deferred

- **P3 - commands/orders layer** (script-facing: receive-RPC-into-script, spawn-a-networked-entity
  from script, ownership + orders, demo content): SKETCHED, not started. Plan:
  `Documentation/Plans/networking-commands.md`.
- Backlog (web WebSocket client, replication refinements, transport/deployment, matchmaking/
  services hooks): `Documentation/Backlog/networking-followups.md`.
- **Beef port / unification with the legacy Sedulous.Net** (which stack is the foundation, what
  the port lifts from the legacy toolkit, the DNS gap on our side): ASSESSED, awaiting a
  ruling - `Documentation/Plans/net-unification.md`.

---

Design rationale (roll-your-own transport + the escape hatch, sockets-in-Core, HTTP-as-sibling,
the genre-tuning analysis, the Traktor references) is in
`Documentation/Archive/networking-design-history.md`.

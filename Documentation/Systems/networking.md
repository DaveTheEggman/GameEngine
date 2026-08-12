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
  not here; `foundation.net` is the reliability/session layer above them. (HTTP, if built, is a
  sibling `foundation.http`, not part of net.)
- **`foundation.net.manager`** - `NetworkManager` (a session endpoint) + `INetworkController` +
  the `Net` script-facade binding. A GameInstance owns its OWN `NetworkManager` and goes online
  at RUNTIME via the facade (no app-owned socket).
- **`foundation.net.replication`** - `StateReplication` + the `IReplicationModel` seam.
- **`engine.net`** - `NetworkSubsystem` (an `ISceneAware` runtime home that injects the
  `NetworkComponentManager` into scenes).

## Roles + startup

`NetworkStartup` carries the role (`NetworkRole` None/Server/Client) + preset; `StartNetworking`
enters it. Per-instance: each `GameInstance` has its own endpoint and goes online at runtime via
the `Net` facade (`Net.startServer` / `Net.connect`), so a single process can host multiple
instances. The primary instance carries the prefab net-spawn resolver hook.

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
`startServer(...)` / `connect(...)`. `NetworkComponent.of(entity).authority` exposes ownership to
script. Certified on Wren + AngelScript (see `Integration.ScriptFacades`).

## Deferred

- **P3 - commands/orders layer** (script-facing: receive-RPC-into-script, spawn-a-networked-entity
  from script, ownership + orders, demo content): SKETCHED, not started. Plan:
  `Documentation/Plans/networking-commands.md`.
- Backlog (web WebSocket client, replication refinements, transport/deployment, matchmaking/
  services hooks): `Documentation/Backlog/networking-followups.md`.

---

Design rationale (roll-your-own transport + the escape hatch, sockets-in-Core, HTTP-as-sibling,
the genre-tuning analysis, the Traktor references) is in
`Documentation/Archive/networking-design-history.md`.

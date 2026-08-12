# Networking - commands / orders layer (plan)

> Status: DRAFT
> Track: [[networking-track]]

The NOT-STARTED P3 of networking: the script-facing command surface on top of the shipped
transport + replication (`Documentation/Systems/networking.md`). Gameplay payoff - a client sends
ORDERS, the server executes them AUTHORITATIVELY and replicates the result. Demo target: a client
spawns a cube on the server, which it then controls. The propagation half (RPCs both directions,
prefab net-spawn + late-join + relevancy, `NetworkedTransform`) already shipped in P2; the new
work is the script command layer. Four independently-landable, loopback-tested slices.

## Slice 1 - receive-RPC-into-script (the keystone)

`RpcTable.On(name, handler)` is C++-only today and the `Net` facade only SENDS. Add the receive
side: `Net.on(name, fn)` registers a script delegate (`IScriptDelegate`) that fires
`fn(senderPeerId, value)` when RPC `name` arrives. `RpcTable.Dispatch` looks up the delegate and
invokes it with the sender `PeerId` (i32, [[script-facade-numerics]]) + the decoded arg. Arg shape
v1 mirrors the send side (`rpcNumber`->`fn(sender, num)`, `rpcText`->text, bare `rpc`->just
sender); multi-arg via a `:wire` reader is a later refinement. GOTCHA: RPCs dispatch on the fixed
lane inside `NetworkManager::Update` - the delegate call must push the instance's
`CurrentScriptContext` and tolerate re-entrancy (a handler that itself spawns/sends). This unblocks
the whole "server game logic reacts to client input" surface.

## Slice 2 - spawn-a-networked-entity from script (server side)

`NetworkId Net.spawn(prefab, x, y, z)` (server-only): spawn the prefab into the replicated scene
(reuse `Scene.spawn`), `AssignNetworkId`, set `NetworkComponent.prefab`. Client reconstruction is
already built (the per-instance `SetSpawnHandler` rebuilds from the spawn record; late-joiners via
full snapshot; relevancy filters). GOTCHA: runtime ids from `++m_nextNetworkId` are already past the
Guid-derived authored ids - no collision. SUB-GAP: how a script NAMES the prefab (a project-settings
prefab ref, or a script asset-ref facade) - smallest for the demo is a known project prefab id.

## Slice 3 - ownership + orders (the "controls it" part)

Server-authoritative control: add an owner `PeerId` to `NetworkComponent` (0 = server-owned);
`Net.spawn` tags owner = the requesting client. The client sends movement orders
(`Net.rpcNumber("move", dir)`); the server's `Net.on("move", ...)` resolves sender -> its owned cube
-> `SetLocalTransform` -> `NetworkedTransform` replicates back. Model: server-authoritative + input
RPCs (no prediction; the controlling client sees ~interpolation-delay lag - fine for the RTS
target). Client-authority (instant local control) stays a separate larger track. DECIDE: order
VALIDATION (does this client own that cube? is the order legal? - the anti-cheat point) and
disconnect policy (owner leaves -> despawn its cubes; `ForgetPeer` drops the peer).

## Slice 4 - demo content (proves it on-screen)

A cube prefab (mesh + `NetworkComponent` + `NetworkedTransform`); a client game script (spawn key ->
`Net.rpc("spawn")`, arrows -> `Net.rpcNumber("move", dir)`) + a server script (`Net.on("spawn")` ->
`Net.spawn`, `Net.on("move")` -> move owner's cube). Two editor tabs (host + join): spawn -> a cube
in both tabs; arrows -> it moves in both; a second client gets its own owned cube.

## Cross-cutting

- Command log = replay/spectate: orders are reliable-ordered RPCs; logging them feeds the
  replay/spectate idea for free (not needed for the demo).
- Lockstep seam intact: this is server-authoritative EXECUTION, not lockstep; a future large-scale
  `CommandReplication` slots beside `StateReplication` via `IReplicationModel`, untouched.
- Effort: slice 1 is the real work (script delegate on the receive path + context/re-entrancy);
  2-3 are small given P2's machinery; 4 is content. Sequence 1 -> 2 -> 3 -> 4, each loopback-tested,
  on-screen smoke on slice 4.

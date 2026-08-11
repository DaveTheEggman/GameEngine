# Draconic — Networking (design)

Status: **IMPLEMENTED — P0 + P1 + P2 shipped (2026-07-21).** Networking was the last major subsystem
gap; it now exists end to end. `draconic.net` (transport: reliable-UDP + TCP + loopback/sim + wire),
`draconic.net.subsystem` (NetSubsystem runtime home + `Net` script facade + `NetworkStartup`/
`StartNetworking`), and `draconic.net.replication` (StateReplication: reflection-driven field codec,
snapshot, per-peer delta, prefab net-spawn + full-snapshot late-join, client interpolation, per-peer
relevancy/fog-of-war) are all built, tested (clang+gcc), and wired into `DefaultApplication`. A game
sets a role in `NetworkStartup`, marks component fields `Replicated`, and gets authoritative,
fog-of-war-filtered, smoothly-interpolated replication with prefab spawn + late-join — no hand-written
per-component net code. See §7 for what shipped per phase and §9 for the remaining optional refinements.

> **Genre steer (user, 2026-07-21): first target is real-time strategy; other genres are NOT locked
> out.** The `IReplicationModel` seam (§5.1) is the escape hatch — a twitch-shooter unreliable-snapshot+
> ack model, or a classic large-scale-RTS lockstep `CommandReplication`, can slot BESIDE
> `StateReplication` without disturbing it. Target-specific decisions (e.g. the reliable-ordered delta
> baseline, §5.6) are POLICY of the current send path, not baked into the wire format or the seam. RTS
> makes lockstep/`CommandReplication` (and the §5.3 determinism caveat) re-relevant for a *future*
> model; server-authoritative `StateReplication` (what shipped) fits smaller-scale RTS / MOBA shape
> today. Relevancy-as-security and server-authority still hold. (The "real-time turn-based" framing
> below is the earlier decision; the shipped code is genre-neutral where it matters.)

> **Reviewed + verified against the codebase 2026-07-21.** The leverage this design assumes is
> real: `Code/Draconic/Core/System/` already runs the backend pattern (System.cppm + SystemBackend.h
> + Linux/ & Win32/ dirs; Time.cppm as precedent) — sockets slot in cleanly (§3.1 confirmed);
> `core::FixedStepper` + `Context::FixedUpdate` (per-scene) exist (§6 fixed-tick lane confirmed);
> reflection + `ISceneAware` subsystems + prefab ref/delta all exist; no net/http code yet (clean
> slate); the RHI **Null backend** (`RHI/Null`, "headless testing/CI/no GPU") is a real headless
> precedent for a dedicated server. **Corrections applied:** (1) §6's "dedicated server falls out of
> the export (platform,config) axis — already implemented" overstated — see §6; (2) **ZeroCore
> dropped as an L4 reference** (weak AOI + property-state shape). **Best reference is Traktor**
> (`/home/robert/Dev/CPP/traktor`, 2022, MPL-2.0): `code/Net` validates the sockets+http split,
> `code/Online` is the services/matchmaking-hooks reference, and `code/Jungle` has usable state-
> delta mechanics — but Jungle is P2P-only (wrong for this server-authoritative target). See §6.

**Target genre (decided 2026-07-20): real-time turn-based.** This is scoping-decisive — see §5.6.
Short version: **no client prediction / reconciliation / lag compensation needed**, **no lockstep
determinism needed**, but **interest management becomes a security requirement (fog of war)** and
**late-join / reconnect full-state sync becomes first-class**.

Related: [runtime-host.md](runtime-host.md) (the fixed-tick lane), [scene-ecs](../design/editor.md)
(component pools), [export-templates.md](export-templates.md) (the dedicated-server config).

---

## 1. Goals / non-goals

**Goals**
- A **multi-genre** networking stack: shooters, co-op, MMO-lite, and (later) RTS/fighting.
- **Portable** — desktop UDP today, **browser tomorrow** (a stated engine goal). Web cannot do raw UDP.
- Ride Draconic's existing leverage: **reflection** (replicated fields), **ECS component pools**,
  **prefabs** (network spawn), the **fixed-tick lane**, the **subsystem pattern**.
- **Headlessly testable** — reliability and replication must be unit-testable with simulated
  latency/loss/reorder, no sockets, no flake (the "adequate tests" rule applies).

**Non-goals (now)**
- Matchmaking / lobbies / relay / NAT punch-through services (hooks only).
- Cross-platform deterministic lockstep as a *promise* (see §5.3 determinism caveat).
- An MMO-scale sharded server architecture.

---

## 2. The decisive constraint

**Browsers cannot open raw UDP sockets.** Web needs WebSocket (reliable-ordered) or WebRTC
DataChannels (unreliable capable). Therefore the stack **must** have a transport abstraction with
swappable backends — this is not optional, and it is the same "interface + swappable backends"
pattern Draconic already runs twice successfully (**RHI**, and **Script**'s backend registry).

A key consequence, which drives §4: **because multiple transport backends are mandatory anyway,
integrating a transport library (ENet) does not save us the abstraction.** We pay for the seam either
way, so the marginal cost of owning the UDP transport is small.

---

## 3. Layer map

```
Core/System (backend)      raw sockets, addresses, poll        [OS primitive]
draconic.http              IHttpClient + backends (native | emscripten-fetch),
                           URL parsing, headers/chunked/keep-alive   [SIBLING, §3.2]
draconic.net               packets, endpoints, virtual connections,
                           INetTransport + reliable-UDP + loopback/sim
                           (later: websocket [-> draconic.http handshake], webrtc)
draconic.net.wire          bitpacking, quantization, delta
draconic.net.session       roles, peers, handshake, clock/tick sync
draconic.net.replication   IReplicationModel seam
   +- .state               StateReplication  (first-class; prediction configurable)
   +- .command             CommandReplication / lockstep (deferred, determinism-gated)
draconic.net.rpc           reflection-driven RPCs
draconic.net.subsystem     NetSubsystem (ISceneAware) + components + script facades
```

### 3.1 Why sockets live in `Core/System`, not `draconic.net`

- **Project rule** — platform `#if` / OS headers belong in Core/System backends (same category as
  files, threads, dynamic libraries, processes, `OpenPathInFileManager`).
- **Non-game consumers already exist** — the **script debugger's remote transport** wants sockets, as
  will tooling / asset-hot-reload servers. None of those should depend on the game networking module.
- The surface is bounded and boring: create/bind/connect/listen/accept/send/recv/sendto/recvfrom/
  close/setsockopt/non-blocking/poll + address resolution. A few hundred LOC per backend
  (Linux/Win32; Emscripten later). **This is not the hard part of networking.**

`draconic.net` then owns the low-level *networking* (packets, connections, reliability) on top.

### 3.2 Why HTTP is a sibling module (`draconic.http`), not part of `draconic.net`

**Decision: `draconic.http` is its own module, sitting beside `draconic.net` on the same Core/System
sockets.** Not inside it.

- **Different concern.** `draconic.net` is real-time game networking (UDP, reliability, replication);
  HTTP is request/response over TCP+TLS. They share essentially nothing but sockets and DNS.
- **Different consumers — and the split already exists.** [export-templates.md](export-templates.md)
  anticipates *"downloadable templates — fetch a versioned template archive"*: an **editor/tooling**
  consumer. Same for update checks, telemetry/crash reporting, and a dev asset/hot-reload server. None
  of those should depend on game replication; conversely a multiplayer game needs no HTTP client.
- **Different platform story.** On WASM you don't implement HTTP at all — you call the browser
  fetch/XHR via Emscripten; game net on web maps to WebSocket/WebRTC instead. Two different backend sets.

**Sub-decisions:**

1. **Do NOT roll TLS.** This is deliberately the *opposite* of the roll-our-own transport call (§4):
   reliable-UDP is bounded, documented territory; **crypto is the canonical "never roll your own."**
   Integrate — mbedTLS (small, embeddable), platform TLS (Schannel/SecureTransport), or **libcurl** if
   we want HTTP/2 + redirects + proxies handled. *(Precedent: ZeroCore vendors Curl for exactly this.)*
2. **Backend abstraction, same pattern as everywhere else** — `IHttpClient` with a native backend
   (curl, or a small HTTP/1.1 client + mbedTLS) and an **Emscripten fetch** backend. Fourth use of the
   interface+backends pattern (RHI, Script, INetTransport, this).
3. **One real seam with `draconic.net`** — a **WebSocket transport begins as an HTTP upgrade
   handshake**, so `draconic.net.websocket` reuses `draconic.http`'s URL parsing + handshake. A one-way
   edge (`net.websocket -> http`); it does not argue for merging the modules.
4. **Async by default, riding existing patterns** — requests run on a worker; in the editor, downloads
   ride **`EditorJobService`** for progress + cancel, exactly like export. A template download then gets
   a progress bar and completion toast for free.
5. **Possibly a minimal HTTP *server* later** — plausible consumers are the **script debugger's web UI**
   (ZeroCore shipped a browser-based debugger) and a **dev asset server** for hot-reload onto
   device/web. Not now, but it is why `draconic.http` should be a module that can grow a server rather
   than a client-only helper buried in net.

**Phasing note:** `draconic.http` is **not gated on the networking phases** — it has independent
tooling consumers (downloadable export templates first) and can land whenever that need becomes real.
Its only ordering constraint is that the P5 WebSocket transport wants item 3's handshake.

---

## 4. Decision: roll our own transport (with an escape hatch)

**Build:** Core/System sockets + `draconic.net`'s reliable-UDP transport.
**Rationale:**
- The abstraction is mandatory regardless (§2), so integrating ENet saves little.
- Reliable-UDP is well-trodden, bounded territory (~2-4k LOC): sequencing, ack bitfields, RTT
  estimation, fragmentation/reassembly, handshake, timeouts, keepalive, basic congestion control.
- No dependency; consistent semantics across backends; freedom on console/WASM.
- Matches the house pattern: own the *foundation* (Core, RHI, UI), integrate the *specialized heavy*
  (Jolt, miniaudio, AngelScript, DXC). Reliability is foundational-and-bounded, not specialized-and-huge.

**Escape hatch (documented, not built):** `INetTransport` keeps **ENet** or **Valve
GameNetworkingSockets** droppable if our transport underperforms or we want GNS's built-in encryption
and modern congestion control.

**What makes rolling our own safe — the loopback/sim transport (P0, not later).** An in-memory
`INetTransport` with injectable **latency / jitter / packet loss / reordering / duplication**. It turns
reliability and replication from "scary networking bugs" into deterministic headless unit tests, and
mirrors what already works elsewhere (RHI's Null backend, the script conformance battery).

### 4.1 `INetTransport` sketch

```
INetTransport
    Listen(port, options) / Connect(address, options)
    Poll(events&)                     // connected / disconnected / received
    Send(peer, channel, Span<const byte>, reliability)
    Disconnect(peer, reason)
    Stats(peer) -> { rttMs, lossPct, sentBps, recvBps, queuedBytes }

Channels/reliability: ReliableOrdered | UnreliableSequenced | Unreliable
Backends: udp (ours) | loopback (sim) | websocket | webrtc | [enet | gns]
```

---

## 5. L4 — Replication: models as backends, configuration within a model

Genre differences are **not all the same kind of difference**, so the answer is *both* mechanisms, at
different granularities.

### 5.1 Different architectures → **models behind a seam**
- **State replication** — server-authoritative property/snapshot sync + interpolation (+ optional
  prediction). Covers FPS, action, co-op, MMO-lite. **The first-class shipped model.**
- **Command replication** — deterministic lockstep (RTS) / rollback (fighting). Replicates *inputs*,
  not state; every peer simulates identically. **Deferred.**

These are not one algorithm with flags: lockstep syncs no state at all and imposes requirements on
**the simulation itself**. Hence `IReplicationModel` as the seam (the RHI/Script pattern, a third time).

### 5.2 Same architecture → **configuration**
Within `StateReplication`: tick rate, prediction on/off and for which components, interpolation buffer
depth, relevancy/AOI policy, per-field reliability / priority / quantization, authority policy.

### 5.3 Determinism caveat (write it down, don't promise it)
Deterministic lockstep is a **whole-engine commitment**: deterministic physics (Jolt is same-binary
deterministic; *cross-platform* determinism is genuinely hard), strict float discipline or fixed-point,
deterministic iteration order. **Do not advertise cross-platform lockstep until the simulation
determinism story is real.**

### 5.4 The escape hatch that actually delivers "multi-genre" on day one
Unreal and Godot both ship essentially *one* model and let lockstep/rollback games build on raw
messaging. Do the same: keep **L1/L3/L5 (transport, session, reliable messaging, RPC) clean enough that
a lockstep or rollback game can be built on them immediately**, without waiting for us to ship
`CommandReplication`. Multi-genre coverage without a god-config.

### 5.5 StateReplication contents
- **`NetworkId` + authority** (server-authoritative default; per-entity authority for
  client-authority/host-migration cases).
- **Reflection-driven replicated fields** — annotate component properties (`Replicated`, with
  priority / precision / condition) and generate the wire format from reflection. This is the *same
  leverage* that already powers the inspector, serialization, and scripting. **Do not hand-write
  per-component net code.**
- **Network spawn/despawn of prefabs** — prefabs already persist as ref+delta, so network spawn is
  "prefab id + initial state."
- **Snapshot + delta** with per-peer baselines and acks; client **interpolation buffer** by default.
- **Interest management / AOI**, priority, per-peer bandwidth budget.
- **Prediction + server reconciliation + lag compensation** — configurable, phased (see §7).

### 5.6 Tuning for the target genre — real-time turn-based

The decided target (§ status) reshapes priorities. Real-time turn-based means **discrete committed
orders** rather than continuous per-frame input, **high latency tolerance** (a few hundred ms is
invisible), **small player counts**, and **low state-change frequency**.

**What this REMOVES (major scope reduction):**
- **Client prediction / server reconciliation / lag compensation are NOT needed.** Those exist for
  twitch aiming and continuous movement. Orders are committed and resolved server-side; a short
  round-trip is acceptable. P4 drops off the critical path entirely.
- **Lockstep determinism is NOT needed.** The server is authoritative and broadcasts resulting state,
  so we never require cross-platform deterministic simulation. **The §5.3 caveat becomes moot for this
  target** — a significant risk removed.

**What this ELEVATES:**
- **Commands/orders are the primary gameplay channel** — closer to *reliable RPC + a validated command
  queue* than to snapshot streaming. This makes **L5 (RPC) more important and earlier** than in a
  shooter-shaped stack.
- **Reliable-ordered dominates.** Order submission must be reliable-ordered; unreliable channels carry
  only cosmetic/continuous data (smooth unit movement between resolved states). This *simplifies* the
  transport's hot path.
- **Interest management is a SECURITY requirement, not a bandwidth optimization.** With fog of war,
  the server must **never send a client information it shouldn't see** — a client that receives hidden
  state can be memory-read to cheat (maphacks). So relevancy/AOI is promoted from a P3 scale concern to
  a **correctness/anti-cheat requirement**, and must be designed into StateReplication's filtering from
  the start, not bolted on.
- **Late join / reconnect** — a dropped player rejoining mid-match must be re-synced from a **full
  authoritative snapshot** (not a delta baseline). First-class for this genre, where matches are long
  and disconnects are survivable.
- **Low tick rate** (10-20 Hz, or event-driven on order resolution) with a generous interpolation
  buffer for smooth visuals. Bandwidth and precision quantization pressure are low.

**Bonus synergy:** replicating *commands* means the command log **is a replay** — turn-based games
commonly want replays/spectating, and this comes nearly free if orders are recorded.

---

## 6. Cross-cutting

- **Tick on the fixed lane** — `FixedStepper` / `Context::FixedUpdate` already exist; networking must
  never key off frame time.
- **Dedicated server — partial reuse, not free** (corrected 2026-07-21). The export `(platform,
  config)` axis IS implemented, but `config` today is a build-OPTIMIZATION tag (`Debug`/`Release`/
  `RelWithDebInfo`), not a build VARIANT — there is no `Server` config, and a headless server is
  more than a config string: it needs a **runtime mode that creates no window/swapchain/input**
  (tick-only). The genuine head start is the RHI **Null backend** (headless rendering already
  exists for tests/CI) + the export axis (adding a `Server` platform/config entry is easy). So:
  reuse the Null backend + export axis, but plan for a small headless runtime mode — it does not
  entirely "fall out" of existing work.
- **`NetSubsystem`** follows the established `ISceneAware` + component-pool + asset/resource + editor
  pattern (like physics/audio), so it composes with scenes and PIE with no special cases.
- **Editor tooling** — replication inspector + bandwidth/RTT graphs on the **existing profiler**, and
  **multi-instance PIE** (launch N clients from the editor); a large DX win Godot and Unreal both ship.
- **References (assessed 2026-07-21).**
  - **Concepts / L4:** current writeups over old engines — Glenn Fiedler's reliable-UDP /
    snapshot-interpolation / networked-physics articles, and **Valve GameNetworkingSockets**
    (also our transport escape hatch, §4).
  - **Traktor (`/home/robert/Dev/CPP/traktor`, 2022, MPL-2.0) — the best structural reference:**
    - `code/Net` (Socket/Tcp/Udp/MulticastUdp/SocketAddress IPv4+IPv6/SocketSet/SocketStream +
      Http/Ftp/Smtp/Discovery/Stream) **validates this doc's `draconic.net`-sockets + `draconic.http`-
      sibling split** (§3.1/§3.2). Reference the API SURFACE — its impl uses inline `#if _WIN32/
      __LINUX__` per file, against our Core/System backend-fanout rule, so restructure to backends.
    - `code/Jungle` (Replicator/ReplicatorProxy + **State/StateTemplate**) — a modern state-DELTA
      mechanic worth a look, BUT it is **P2P-only** (`Peer2PeerTopology` is the sole
      `INetworkTopology`), which is the WRONG architecture for this server-authoritative fog-of-war
      target (P2P cannot hide state from peers → maphacks). Reference the State/StateTemplate delta
      mechanics, NOT the P2P topology.
    - `code/Online` (IMatchMaking/ILobby/ILeaderboards/ISaveData + Steam/Lan/Local providers) — a
      clean provider-abstraction, the reference for the DEFERRED matchmaking/services hooks (§1).
  - *(ZeroCore `Replication` ~21.5k LOC: competent reliable-UDP + property-state, but relevancy/AOI
    essentially absent — the security-critical piece here — and property-state (not command/RPC)
    shaped. **Not referenced.**)*
- **Security posture** — server authority by default; validate on the server, never trust the client.
  Encryption delegated to the transport (ours: later; GNS: built in).

---

## 7. Phasing

- **P0 — plumbing. SHIPPED (2026-07-21).** Core/System UDP+TCP sockets (Linux/Win32) +
  `draconic.net` `INetTransport` seam + **our reliable-UDP transport** (seq/ack/ackBits, RTT-timed
  resend, ordered reliable delivery, **fragmentation + reassembly** of large messages,
  handshake/keepalive/timeout) + **loopback + datagram sim** (deterministic loss/reorder/dup) +
  `:wire` (bitpacking, varint, quantization) + a real `UdpDatagramSocket`/`TcpSocket` backend + the
  **NetEcho** console sample. 30 net tests (loopback-deterministic + real-localhost) clang+gcc.

  **P0 principled deferrals (documented, NOT gaps in the shipped transport):**
  - **Congestion control** — for the real-time *turn-based* target (low message rate, high latency
    tolerance, small player counts) a real congestion controller is speculative work against
    requirements this genre doesn't have (see §8 Q3). The RTT-timed resend already prevents the
    obvious flood. Add a real controller only when a bandwidth-heavy target appears.
  - **IPv6** — the socket backend + `DatagramEndpoint` are IPv4 for v1 (localhost/LAN/most hosting
    work today). IPv6 is additive (a wider endpoint + `AF_INET6` paths), not a correctness gap;
    add it with the first deployment that needs it.
  - **A dev-conditions/relay simulator over real sockets** — the *sim* already injects
    loss/latency/reorder deterministically; a real-socket conditions shim is a nice-to-have, not P0.
- **P1 — session + messaging. SHIPPED (2026-07-21).** `NetSession` roles (client / listen-server /
  dedicated), peer registry, handshake, connect/disconnect lifecycle, **server-authoritative clock/tick
  sync** (reserved control channel 255, RTT-adjusted offset → `NetworkTimeMs`/`NetworkTick`);
  **`RpcTable`** (FNV-1a name-hashed ids, wire-serialized args, `Call`/`CallAll` on reserved channel
  254); **`NetSubsystem`** runtime home (owns session + RPC, driven on the fixed lane); the **`Net`
  script facade** proven on BOTH backends (Wren + AngelScript) via the `RegisterExtraFacadeName` prelude
  hook; **game-wiring** in `DefaultApplication` (`NetworkStartup`/`StartNetworking` open the socket +
  enter the role from config, driven on `OnFixedUpdate`).
- **P2 — StateReplication core. SHIPPED (2026-07-21) — `draconic.net.replication`.** All six slices:
  (1) the **reflection-driven field codec** (`Replicated` property attribute → `:wire`; bool / int
  widths / f32-f64 / Float2-4 / Quaternion; no hand-written per-component net code); (2) **`NetworkId`
  + `NetworkAuthority`**, the **`NetworkComponent`** tag, the **`IReplicationModel`** seam, and the
  **full snapshot** (server → client, keyed by `NetworkId`); (3) **per-peer delta** against a
  last-*sent* baseline (rides reliable-ordered delivery, §5.6 — no ack/snapshot-ring at this layer);
  (4) **prefab network-spawn** (`SetSpawnHandler` → `SpawnPrefab`) + **full-snapshot late-join** (a
  fresh peer = `ForgetPeer` + `CaptureDelta` from an empty baseline); (5) **client interpolation**
  (`InterpolationBuffer`, sampled at synced-network-time − delay; transforms lerp, discretes snap);
  (6) **per-peer relevancy / fog-of-war** (`SetRelevance`: an entity that leaves a peer's relevance is
  actively *removed* on that client — no hidden state to memory-read). Wire type tags use the string
  `SerializationTypeId` (not the process-address `TypeOf`); per-component length prefixes make unknown
  types skippable. **Integrated end to end:** `NetSubsystem` drives `CaptureDelta` per peer each fixed
  tick on reserved channel 253 (reliable-ordered) and `ApplyDelta` + interpolation on receive;
  `DefaultApplication` sets the primary scene as the replicated world and wires the spawn handler to
  the content DB.
- **P3 — scale + tooling. NOT STARTED.** Relevancy/AOI *policies* (the mechanism shipped in P2; the
  built-in distance/team/vision policies + priority + per-peer bandwidth budgeting are P3), net
  profiler, replication inspector, multi-client PIE, **command-log replay/spectate**. (The gameplay-facing
  **commands/orders layer** — the "client spawns + controls a cube on the server" payoff — is sketched
  separately in **§7.1**; it's independent of this infra track and can land first.)
- **P5 — web transports.** WebSocket + WebRTC backends, landing with the portability track.
- **Dropped from the critical path (target genre, §5.6)** — ~~P4 prediction / reconciliation / lag
  compensation~~: not needed for real-time turn-based. Revisit only if a twitch-action target appears.
- **Later / conditional** — `CommandReplication` (lockstep/rollback), gated on §5.3 determinism (moot
  for the current target); relay/NAT traversal; encryption hardening.

---

## 7.1 P3 — Commands / orders layer (script-facing) — SKETCH, NOT STARTED

> This is the **gameplay payoff** of the shipped stack: a client sends *orders*, the server executes
> them **authoritatively** and replicates the result. It is distinct from the "scale + tooling" P3
> bullet above (relevancy policies / profiler / inspector) — that is infra; this is the game-facing
> command surface. Demo target: **a client spawns a cube on the server, which it then controls.**
>
> Everything the *propagation* half needs already shipped in P2 — RPCs both directions, prefab
> net-spawn + late-join + relevancy, `NetworkedTransform`. The new work is the **script command layer**
> (receive-into-script + spawn/own facades) on top. This realises §5.6's "orders = reliable-ordered
> primary channel" note and gives the §7 `CommandReplication`/replay seam its first real feed.

Four slices, each independently landable + doctest-tested (loopback sim) before the on-screen smoke.

### Slice 1 — Receive-RPC-into-script *(the keystone)*
Today `RpcTable.On(name, handler)` is C++-only and the `Net` facade only **sends**. Add the receive side:

```
Net.on(name, fn)     // register: fn(senderPeerId, value) fires when an RPC 'name' arrives
```

- `fn` is a **script delegate** (we already have `IScriptDelegate` + delegate params — see the script
  behaviors work). On receipt, `RpcTable.Dispatch` looks up the registered script delegate and invokes
  it with the sender `PeerId` (`i32`, per the [[script-facade-numerics]] natural-type rule) + the
  decoded arg.
- **Arg shape v1** mirrors the send side: a client's `Net.rpcNumber("move", dir)` arrives as
  `fn(sender, dir)`; `rpcText` → `fn(sender, text)`; a bare `Net.rpc("spawn")` → `fn(sender)`. A richer
  multi-arg payload (exposing a `:wire` `BitReader`/struct to script) is a later refinement, not v1.
- **Gotcha — dispatch context.** RPCs are dispatched on the **fixed lane** inside `NetworkManager::Update`;
  invoking the delegate must push the instance's `CurrentScriptContext` (the same seam physics/behavior
  events use) and tolerate **re-entrancy** — a handler that itself spawns or sends another RPC.
- **Test:** loopback sim — client fires `rpcNumber`, the server's registered script delegate receives
  `(peerId, value)` exactly once with the right sender.

### Slice 2 — Spawn-a-networked-entity from script *(server side)*
So the "spawn" order actually creates a server cube that clients reconstruct:

```
NetworkId Net.spawn(prefab, x, y, z)   // server-only; returns the new entity's NetworkId
```

- Spawns the prefab into the replicated scene (reuse the existing `Scene.spawn` prefab spawner), then
  `AssignNetworkId` + set the `NetworkComponent.prefab` Guid.
- **Client reconstruction is already built** — the per-instance `SetSpawnHandler` (P2 slice 4) rebuilds
  it from the spawn record; late-joiners get it via full snapshot; relevancy filters it. This slice is
  *only* the server-side spawn+assign.
- The prefab is authored with `NetworkComponent` + `NetworkedTransform` (networked + a replicated
  transform).
- **Gotcha — id space.** Runtime-minted ids come from `++m_nextNetworkId`, already bumped past the
  Guid-derived authored ids (`DeterministicNetworkId`) — no collision.
- **Sub-gap — script→prefab reference.** `Scene.spawn` already resolves a prefab Guid, so there's
  precedent, but "how a script *names* the cube prefab" needs nailing (a project-settings prefab ref, or
  a script asset-ref facade). Smallest for the demo: a known project prefab id.
- **Test:** server script `Net.spawn` → the entity replicates to a connected client (extend the
  `NetworkedTransform` wiring test).

### Slice 3 — Ownership + orders *(the "controls it" part)*
Server-authoritative control: the client's input moves **its own** cube, executed on the server.

- Add an **owner `PeerId`** to `NetworkComponent` (0 = server-owned). `Net.spawn` tags owner = the
  requesting client's `PeerId` (from the slice-1 handler's `sender`).
- Client sends movement orders (`Net.rpcNumber("move", dir)` / a target). Server's `Net.on("move", …)`
  resolves `sender → the cube it owns → SetLocalTransform` → `NetworkedTransform` replicates back to
  everyone. Ownership resolution = a `NetworkComponent.owner` lookup or a game-script `peer→NetworkId`
  map; relevancy is already per-peer.
- **Model (recommended):** *server-authoritative + input RPCs* — the controlling client sees its cube
  with ~interpolation-delay lag (**no prediction**; fine for the RTS target, §5.6, where P4 was dropped).
  Client-authority (instant local control via `NetworkAuthority::Client` + client→server *upstream*
  replication) stays the separate, larger escape-hatch track — the current capture is server→client only.
- **Decide:** order **validation** (does this client own that cube? is the order legal? — the anti-cheat
  point, §5.6) and **disconnect policy** (owner leaves → despawn its cubes; `ForgetPeer` already drops
  the peer, owned entities likely get destroyed).
- **Test:** two sim endpoints — client sends "move," server moves the *owned* entity, both scenes
  converge; a non-owner "move" for that entity is rejected.

### Slice 4 — Demo content *(proves it on-screen)*
- A **cube prefab** (mesh + `NetworkComponent` + `NetworkedTransform`).
- Game script: **client** — key → `Net.rpc("spawn")`, arrows → `Net.rpcNumber("move", dir)`; **server** —
  `Net.on("spawn", …)` → `Net.spawn(cube, …)`, `Net.on("move", …)` → move owner's cube.
- Two editor tabs (host + join): client presses spawn → a cube appears in **both** tabs; client drives it
  with arrows → it moves in both (server-authoritative). A second client tab gets its own owned cube.

### Cross-cutting
- **Command log = replay/spectate.** Orders are reliable-ordered RPCs; logging them gives the §7 P3
  "command-log replay/spectate" bullet its feed for free (not needed for the demo).
- **Lockstep seam intact.** This is server-authoritative *execution*, not lockstep; a future large-scale
  `CommandReplication` (§7 "Later / conditional", gated on §5.3 determinism) slots in beside
  `StateReplication` via `IReplicationModel`, untouched by this.
- **Effort.** Slice 1 is the real work (script delegate on the net receive path + context/re-entrancy).
  Slices 2–3 are small given P2's prefab-spawn + relevancy machinery. Slice 4 is content. Client-authority
  (model b) is explicitly **out** of P3.
- **Sequencing:** 1 → 2 → 3 → 4, each committed + loopback-tested, then the on-screen smoke on slice 4.
  Slice 1 unblocks far more than this demo — it's the whole "server game logic reacts to client input"
  surface.

---

## 8. Open questions

1. ~~**Target genre first?**~~ **RESOLVED 2026-07-20: real-time turn-based; REFINED 2026-07-21: first
   target real-time strategy, other genres kept open via the `IReplicationModel` seam** (see the banner
   at the top). Consequences already folded into §5.6/§7. The shipped `StateReplication` is
   server-authoritative and genre-neutral where it matters; a future lockstep `CommandReplication`
   remains a seam away (§5.3 determinism caveat then applies).
2. **Encryption** — roll into our transport, or make it the reason to adopt GNS? *(Open — deferred,
   §9.)*
3. **Congestion control** — how far do we go in v1 (simple send-rate throttle + backpressure) before it
   needs to be real? *(Open — deferred, §9; RTT-timed resend covers the obvious flood today.)*
4. ~~**Authority model**~~ **RESOLVED 2026-07-21: server-authoritative in v1, with the per-entity hook
   present.** `NetworkComponent` carries a `NetworkAuthority` (`Server` default / `Client`); replication
   is server-authoritative throughout, and the `Client` value is the reserved seam for
   client-owned-avatar / host-migration cases (not yet exercised — §9).
5. ~~**Sockets in Core/System**~~ **RESOLVED 2026-07-21: yes, Core/System.** Verified the backend
   pattern already lives there (System.cppm + SystemBackend.h + Linux/Win32 + Time.cppm precedent);
   the script debugger's remote transport is the tiebreaker non-game consumer, exactly as argued.

---

## 9. Optional refinements / backlog

Everything below is **deferred, not a gap** — the shipped P0+P1+P2 stack is complete and correct for
the current target. Each item lists *what*, *why it's deferred*, and *where it plugs in* so picking one
up is a scoped change, not a redesign.

### 9.1 Replication refinements (P2-adjacent)

- **Per-field bitmask delta.** Replication is **component-level** today: a component ships whole if any
  of its replicated fields changed. *Why deferred:* transform-like components are small (a few fields
  that usually all move together), so a per-field mask saves little; it matters only for large
  components with many independently-changing fields. *Where:* `CaptureDelta`'s per-component diff emits
  a changed-field bitmask (one bit per `ReplicatedProperties` entry) + only the changed field values;
  `ReadReplicatedState` reads the mask. The baseline already stores per-component bytes to diff against.
- **Render-rate interpolation sampling.** `SampleInterpolation` runs on the **fixed lane** today
  (inside `NetSubsystem::Update`). *Why deferred:* fine when the fixed rate ≥ perceptible smoothness;
  motion can look stepped if the render rate is much higher. *Where:* the `InterpolationBuffer` is
  delay-agnostic — the host calls `StateReplication::SampleInterpolation(scene, buffer, renderTime)`
  from the **render tick** instead of (or in addition to) the fixed tick. No code change in the buffer.
- **Priority + per-peer bandwidth budget (P3).** When many entities are relevant to a peer, cap bytes/
  peer/tick and send the highest-priority deltas first. *Where:* `CaptureDelta` sorts candidate entries
  by a priority (a `Replicated` attribute value or a per-entity hint) and stops at a byte budget,
  carrying the rest to the next tick. The reliable-ordered baseline still holds (unsent = unchanged).
- **Built-in relevancy policies (P3).** The *mechanism* shipped (`SetRelevance` predicate, §7 P2/slice
  6); ready-made **distance / team / vision-cone** policies are P3. *Where:* library helpers that build
  a `RelevanceFn` from a per-peer interest volume + the entity's transform.
- **Quantization / precision per field.** The `Replicated` attribute value is reserved for a
  priority/precision descriptor; the codec writes full-width floats today. *Where:* `WriteFieldValue`/
  `ReadFieldValue` consult the property attribute to bit-pack ranged/quantized floats (`:wire` already
  has `WriteFloatRanged`). Low value for the turn-based target (bandwidth is not the constraint).

### 9.2 P1 messaging follow-ons

- **Receive-RPC-into-a-script-handler.** `RpcTable::On` binds **C++** handlers today; a script can
  *fire* RPCs via the `Net` facade (`rpc`/`rpcNumber`/`rpcText`) but cannot yet *receive* one into a
  script function. *Where:* a script-facing `Net.on(name, fn)` that registers a handler routing the
  wire args into a script call — pairs with the next item.
- **Reflection auto-marshaling of RPC args.** RPC args are hand-serialized today (the facade covers the
  number/text shapes). *Where:* reuse the replication field codec (`WriteFieldValue`/`ReadFieldValue`)
  to marshal typed args from reflection, so `RpcTable` gains typed multi-arg calls for free.
- **`project-settings → NetworkStartup`.** The launch flow sets `NetworkStartup` programmatically; it
  should read role/host/port from project/run settings. *Where:* the player/editor launch path fills
  `NetworkStartup` from the settings store before `DefaultApplication::Configure`.

### 9.3 Transport / deployment (later, conditional)

- **Congestion control** (§8 Q3) — RTT-timed resend prevents the obvious flood; a real controller is
  speculative until a bandwidth-heavy target appears.
- **Encryption** (§8 Q2) — transport-level; either roll into our reliable-UDP or adopt GNS as the
  hardened backend behind `INetTransport`.
- **Per-entity / client authority** (§8 Q4) — `NetworkComponent.authority` carries the `Client` value
  as the seam; exercising it (client-owned avatars, host migration) is future work.
- **`CommandReplication` / lockstep** (§5.1, §5.3) — the *second* `IReplicationModel`, for large-scale
  RTS (replicate inputs, deterministic sim). Determinism-gated; genre-conditional. The seam is in place.
- **IPv6** — the socket backend + `DatagramEndpoint` are IPv4 for v1; additive (`AF_INET6` + a wider
  endpoint), not a correctness gap.
- **Web transports (P5)** — WebSocket + WebRTC backends behind `INetTransport`, landing with the
  portability track (WebSocket reuses `draconic.http`'s handshake).
- **NAT traversal / relay + matchmaking/services** — deployment concerns; Traktor `code/Online` is the
  reference for the services/matchmaking hooks.

### 9.4 Validation debt

- **Win32 sockets need Windows validation.** The Winsock2 backend (`Core/System/Win32/Win32System.cpp`,
  `ws2_32`) is written but **not compiled on Linux**; it must be built + smoke-tested on Windows. The
  POSIX path is proven (real-localhost tests). This is the one item that is a genuine *unverified*
  surface rather than a scoped enhancement.

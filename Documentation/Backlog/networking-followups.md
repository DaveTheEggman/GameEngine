# Networking - deferred refinements + backlog

> Status: CURRENT
> Track: [[networking-track]]

Deferred, NOT gaps - the shipped P0-P2 stack is complete for the current target
(`Documentation/Systems/networking.md`). Each item is a scoped change, not a redesign.

## Replication refinements (P2-adjacent)

- **Per-field bitmask delta.** Replication is component-level (a component ships whole if any
  replicated field changed). Where: `CaptureDelta` emits a changed-field bitmask + only changed
  values; `ReadReplicatedState` reads the mask. Low value for small transform-like components.
- **Render-rate interpolation sampling.** `SampleInterpolation` runs on the fixed lane; the host
  can call it from the render tick instead (the `InterpolationBuffer` is delay-agnostic - no buffer
  change). Matters only when render rate >> fixed rate.
- **Priority + per-peer bandwidth budget.** Cap bytes/peer/tick, send highest-priority deltas
  first (`CaptureDelta` sorts by a `Replicated` priority attribute, carries the rest to next tick;
  reliable-ordered baseline holds).
- **Built-in relevancy policies.** The mechanism shipped (`SetRelevance` predicate); ready-made
  distance/team/vision-cone `RelevanceFn` helpers are future.
- **Quantization / precision per field.** The `Replicated` attribute is reserved for a
  precision descriptor; `WriteFieldValue`/`ReadFieldValue` could bit-pack ranged floats (`:wire`
  has `WriteFloatRanged`). Low value for the bandwidth-light turn-based target.

## P1 messaging follow-ons

- **Receive-RPC-into-script** + **reflection auto-marshaling of RPC args** - see the P3 plan
  (`Plans/networking-commands.md` slice 1); reuse the replication field codec for typed multi-arg RPCs.
- **project-settings -> NetworkStartup**: the launch flow should read role/host/port from the
  settings store before `DefaultApplication::Configure` (currently set programmatically).

## Transport / deployment (later, conditional)

- **Congestion control**: RTT-timed resend prevents the obvious flood; a real controller is
  speculative until a bandwidth-heavy target appears.
- **Encryption**: transport-level - roll into the reliable-UDP or adopt GameNetworkingSockets as a
  hardened backend behind `INetTransport`.
- **Per-entity / client authority**: `NetworkComponent.authority` carries the `Client` value as
  the seam; exercising it (client-owned avatars, host migration) is future.
- **CommandReplication / lockstep**: the second `IReplicationModel` for large-scale RTS (replicate
  inputs, deterministic sim). Determinism-gated, genre-conditional; the seam is in place.
- **IPv6**: the socket backend + `DatagramEndpoint` are IPv4 for v1; additive (`AF_INET6`).
- **Web transports**: WebSocket + WebRTC backends behind `INetTransport` (WebSocket reuses the
  http handshake) - lands with the portability track.
- **NAT traversal / relay + matchmaking/services**: deployment concerns; Traktor `code/Online` is
  the services/matchmaking-hooks reference.

## Validation debt

- **Win32 sockets need Windows validation.** The Winsock2 backend
  (`Core/System/Win32/`, `ws2_32`) is written but NOT compiled on Linux; build + smoke-test on
  Windows. The POSIX path is proven (real-localhost tests). This is the one genuinely UNVERIFIED
  surface (not a scoped enhancement).

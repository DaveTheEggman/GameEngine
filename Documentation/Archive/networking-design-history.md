# Networking - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/networking.md
> Track: [[networking-track]]

NON-AUTHORITATIVE. Design rationale for the 2026-07 networking build. Present-tense truth is
`Systems/networking.md`; the full original design doc (layer map, transport sketch, replication
tuning, all sections) is in git at the P0 commit 3b92560d. Kept for the "why".

## Key decisions

- **Roll our own transport, with an escape hatch.** Reliable-UDP + TCP built in-house behind an
  `INetTransport` seam, so GameNetworkingSockets (or another hardened backend) can slot in later
  without touching the game-facing layers. Rejected taking a heavy dependency up front.
- **Sockets live in `Core/System`, not the net module.** The platform backend pattern already
  lives there (`System.cppm` + `SystemBackend.h` + Linux/Win32 dirs, `Time.cppm` as precedent);
  the net module is the reliability/session layer above raw sockets. The script debugger's future
  remote transport is a second, non-game socket consumer - the tiebreaker for putting sockets low.
- **HTTP is a sibling module** (`foundation.http`), not part of net - different protocol, different
  consumers (asset fetch, services). The net module stays a game-session layer.
- **Replication models behind a seam** (`IReplicationModel`). `StateReplication` (server-authoritative
  snapshot/delta) shipped; a twitch snapshot+ack model or an RTS lockstep `CommandReplication` are
  future models beside it. Target-specific choices are send-path POLICY, not baked into the wire.
- **Genre steer** (2026-07-21): first target real-time strategy, others kept open via the seam.
  Consequences of the earlier "real-time turn-based" framing (no prediction/reconciliation/lockstep
  needed; interest management = a SECURITY requirement; late-join/reconnect first-class) still hold
  for the shipped server-authoritative model.
- **Determinism caveat** (write it down, do not promise it): `StateReplication` is
  server-authoritative and does NOT require bit-exact simulation; a future lockstep
  `CommandReplication` would re-raise determinism as a hard requirement.

## References

Traktor (`code/Net` validated the sockets+http split; `code/Online` = the services/matchmaking
hooks reference; `code/Jungle` = state-delta mechanics, but P2P-only, wrong for the
server-authoritative target). ZeroCore was dropped as an L4 reference (weak AOI + property-state
shape). Sedulous has no cook/net baseline worth copying here.

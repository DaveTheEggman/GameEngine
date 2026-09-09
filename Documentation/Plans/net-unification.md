# Net unification: Raptor's stack vs legacy Sedulous.Net (assessment)

> Status: RULED 2026-09-09 - Raptor's stack IS the foundation; the new Sedulous ports Net
> DIRECTLY from Raptor (the legacy Beef toolkit is reference material for the socket-line
> substrate, not a source to lift wholesale). Raptor's own gap is closed the same day:
> `Core::ResolveHostIPv4` (dotted-quad fast path, else getaddrinfo) behind `ResolveEndpoint`,
> `TcpSocket::Connect`, `Net.connect` and `HttpFetch`, on both backends. IPv6 and TLS stay
> deferred in both engines.
> Track: [[networking-track]] / the Beef port

The question: the Beef port is about to reach networking. Raptor has `foundation.net` +
`.replication` + `.manager` + `.websocket` + `foundation.http` + `engine.net`; the LEGACY
Beef engine (`~/Dev/Beef/SedulousEngine`) has `Sedulous.Net` + `Sedulous.Net.HTTP` that the
new Sedulous could lift as-is. Which is the better foundation, and can both engines unify on
it before anything is ported? The new Sedulous tree has EMPTY `Sedulous.Net*` / `Sedulous.Http`
directories (scaffolded from Raptor's layout, commit a2e6832) - nothing is committed yet.

## What each stack is

| | Raptor | Legacy Beef |
|---|---|---|
| Lines / tests | ~9.5k / 82 cases | ~5.5k / 135 cases (many are one-line enum/header checks) |
| Sockets | `Core/System` backend (Linux/Win32): UDP, TCP, listen/accept, non-blocking, dotted-quad IPv4 only, **no DNS**, no IPv6 | `UdpSocket` / `TcpClient` / `TcpListener` over BeefLang's `System.Net.Socket`; `IPAddress` (v4 + v6 parse/format), `IPEndPoint`, `DnsResolver` (getaddrinfo), `NetError` enum, `SocketInit` |
| Wire codec | `BitWriter` / `BitReader`: bit-packed, var-int, ranged floats, overflow-safe | `NetBuffer` / `NetWriter` / `NetReader`: big-endian BYTE codec (u8..u64, float, length-prefixed string) |
| Datagram seam | `IDatagramSocket` + `SimDatagramNetwork` (latency/jitter/loss/dup/reorder, seeded) + `LoopbackLink` | none |
| Reliability | `ReliableTransport`: protocol id, seq/ack/ackBits, RTT, reliable-ordered with resend backoff, fragmentation + reassembly, keepalive, timeout, connect/accept handshake; 9 headless tests over the sim | none |
| Session | `NetSession`: roles (client / listen / dedicated), peers, broadcast, server time sync + network tick | none |
| RPC | `RpcTable`: name-hash dispatch, `BitReader` args, reliable/unreliable | none |
| Replication | reflection-driven field codec, snapshot + per-peer delta baselines, prefab net-spawn, late-join, relevancy (fog of war), interpolation buffer, `IReplicationModel` seam; 16 tests | none |
| Endpoint + script | `NetworkManager` (per GameInstance), `INetworkController`, the `Net` script facade (AngelScript + Luau), `engine.net` fixed-lane driver + transport pump | none |
| HTTP | `foundation.http`: incremental parser, PUMP-model server (never blocks the loop), SSE streams (the MCP host rides this), blocking localhost `HttpFetch`; no DNS, no TLS | `HttpParser` / `Headers` / `Request` / `Response` / `StatusCode` / routes, `HttpClient` (URL parse, GET/POST/PUT/DELETE), `HttpServer` (routes, WS upgrade hook); no TLS |
| WebSocket | native ACCEPT gateway (RFC 6455 handshake + binary frame codec), `WebSocketHybridSocket` (UDP natives + WS browser peers behind ONE `IDatagramSocket`), `WebSocketClientSocket`; hand-rolled SHA-1 + Base64; 10 tests | `WebSocketFrame` (encode/decode, masking, 9 tests), `WebSocketClient` (handshake, ping/pong, close), server-side `WebSocketConnection`; `SHA1` (8 tests) |

They are not two versions of the same thing. Legacy Beef is a **socket + HTTP + WebSocket
toolkit** (the bottom layer). Raptor is a **game networking stack** whose bottom layer is
thinner than the legacy one and whose upper layers have no counterpart in Beef at all.

## Where each is stronger

Raptor, decisively, above the socket line: the datagram seam is what makes the reliability,
session and replication layers testable headless under simulated loss (all 82 cases run
without an OS socket); the bit-packed wire is what the replication field codec and the RPC
hash are written against; the WebSocket gateway maps browser peers onto the same seam so the
whole stack is transport-agnostic; the HTTP server is pump-model and carries SSE. Every piece
above the seam has been exercised end to end by a running game (two editor tabs host/join,
PaperKid's web build).

Legacy Beef, at the socket line: DNS resolution and hostname connects, IPv4 AND IPv6 address
types with parse/format, a typed `NetError`, a URL-parsing HTTP client, a WebSocket CLIENT
with masking and ping/pong, text frames, and a proper accept-and-upgrade server hook. All
idiomatic BeefLang over the standard `Socket`.

Legacy Beef's weaknesses are real and would have to be fixed before a game rode on it:
- `HttpServer.HandleClient`, `HttpClient.Send` and `WebSocketClient.Connect` spin on
  `Thread.Sleep(10)` for up to 5-30 s: a slow client STALLS THE GAME LOOP. Raptor's server is
  a per-frame pump by design (the MCP host and the WS front door depend on that).
- `HttpServer.Update` accepts and fully serves ONE connection per call.
- `WebSocketConnection.Receive` decodes one frame per call and ignores FIN/continuation.
- `TcpClient.Connect(host, port)` records a fake loopback remote endpoint.
- `NetBuffer` is a byte codec; the bit-packed wire (ranged floats, var-ints) is what makes
  replication deltas small, and the two are not interchangeable on the wire.
- No reliability, session, RPC, replication, sim network or loopback - the game layer would
  have to be written from scratch on top, which is exactly what Raptor already did.

## Recommendation

Unify on **Raptor's architecture and wire format** as the foundation, and let the Beef port
reuse the legacy Beef pieces WHERE THEY FIT UNDER RAPTOR'S SEAMS. Concretely:

1. **The seams and layering are Raptor's.** `IDatagramSocket` + sim + loopback, the bit wire,
   `ReliableTransport`, `NetSession`, `RpcTable`, `StateReplication` + `IReplicationModel`,
   the WS-as-datagram gateway, the pump-model HTTP server with SSE, `NetworkManager` + the
   `Net` facade. Ported faithfully like every other module, tests included (the sim-network
   tests are the valuable ones: they prove the reliability layer without sockets).
2. **The wire stays byte-identical across the two engines** for the transport/session/RPC
   layers (protocol id, packet header, ack bits, fragment records, control channel, the RPC
   name hash, `BitWriter` bit order). This costs nothing in a faithful port and buys shared
   test fixtures and mixed-engine peers on the transport level. Replication PAYLOAD
   compatibility is best-effort only (it needs identical reflected component layouts, which
   only holds when both engines run the same game) - consistent with the cooked-product
   ruling.
3. **Lift from legacy Beef into the port's substrate:** `IPAddress` / `IPEndPoint` /
   `DnsResolver` / `NetError` / `SocketInit`, the `UdpSocket` + `TcpClient` + `TcpListener`
   wrappers (adapted to the `IDatagramSocket` seam and made strictly non-blocking - no
   `Thread.Sleep` anywhere), `SHA1`, `WebSocketFrame` (Raptor hand-rolls the same codec),
   and the `HttpParser` / `HttpHeaders` / `HttpRequest` / `HttpResponse` / `HttpStatusCode`
   value types behind Raptor's pump-model server shape. Drop `NetBuffer` / `NetWriter` /
   `NetReader` (superseded by the bit wire) and the blocking client/server loops.
4. **Close Raptor's socket-line gaps so the foundation is the same on both sides:** add DNS
   resolution + hostname connects to `Core/System` (getaddrinfo on both backends) and route
   `Net.connect(host, port)` and `TcpSocket::Connect` through it; keep a typed error out of
   the socket backend instead of the bare `kInvalidSocket`. Small, and the legacy Beef code
   is the reference for the shape.
5. **Defer IPv6 in both**, recorded: `DatagramEndpoint` is a u64 (ip32 | port16, bit 63 =
   WebSocket peer). An IPv6 endpoint needs a wider handle and a hash-keyed endpoint table;
   nothing today needs it (LAN / localhost / browser-via-WS).
6. **Neither engine gets TLS** now (unchanged: the localhost / LAN trust domain, `ws://`).

What the Beef agent should NOT do: port the legacy `Sedulous.Net` as-is into the new tree
and build a game layer on it, or invent a Beef-side reliability layer. The reliability and
replication work is done and tested in Raptor; the port is a port.

## Sequence for the port (when the ruling lands)

`Sedulous.Net` (sockets substrate from legacy + Raptor's datagram/sim/loopback/wire/reliable/
session/rpc, with the Net.Tests) -> `Sedulous.Http` (parser types from legacy + Raptor's pump
server + SSE + fetch) -> `Sedulous.Net.WebSocket` (frame codec + SHA1 from legacy, Raptor's
gateway + hybrid socket + client socket) -> `Sedulous.Net.Replication` (needs Scene +
reflection attributes, both already ported) -> `Sedulous.Net.Manager` (needs the script
facade layer; may wait for scripting) -> `Engine.Net`.

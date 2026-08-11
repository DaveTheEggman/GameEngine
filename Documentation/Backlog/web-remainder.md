# Web platform remainder (task #112)

Size: L (three independent items; A is the big one). Design doc:
`docs/design/web-platform.md`. Everything below assumes the shipped state:
full engine + WebScene + player run in Chromium browsers on WebGPU; web export
preset + fetch-based player template + serve.py SHIPPED (5d800de0); build
stamp (abbc5348) is THE way to verify a wasm build is not browser-cached.

## A) Web networking: WebSocketTransport

Goal: a browser client can join a natively-hosted game session.

- New transport implementing `INetTransport` (NetTransport.cppm, module
  `draconic.net`) over `emscripten/websocket.h`. Browser sockets are TCP-based
  WebSocket ONLY: no UDP, no listen. Message-oriented (binary frames), which
  maps cleanly onto the existing packet transport seam.
- `CanHost()` (or equivalent capability on the transport): the web transport
  returns false; NetworkManager/NetSubsystem must consult it so Host/Listen
  paths are cleanly rejected in the UI/script API (no silent hang). Grep for
  where the host/join split happens in `draconic.net.manager` /
  `Engine.Net` and gate there.
- Native side needs a WebSocket ACCEPT path so a desktop host can accept
  browser clients. Decision to make (present both in the PR, pick one,
  justify):
  1. Native host embeds a minimal WebSocket server (RFC 6455 handshake +
     framing over the existing TCP socket layer; no TLS in v1 - localhost/LAN
     play) alongside its UDP listener. Browser clients connect via
     `ws://host:port`.
  2. A standalone relay tool that bridges WS <-> UDP (more moving parts;
     only if (1) is disproportionately hard in the socket layer).
  NO new third-party dependency without checking with the user first.
- Reliability semantics: WebSocket is reliable-ordered; the replication layer
  (draconic.net.replication) tolerates that fine, but the transport must not
  pretend to be lossy-unordered - expose transport characteristics if the
  replication scheduler cares (check how StateReplication consumes transport
  properties; keep v1 simple).
- Emscripten headless tests CANNOT run WS under plain node
  (`___syscall_listen` needs a proxy) - unit-test the framing/handshake on
  DESKTOP (the native accept path is plain C++), and make the browser proof a
  WebScene/host smoke the user runs: native WebScene --host, browser joins,
  the replicated cube moves. Wire that into the existing net demo flow
  (net-demo-scripts.txt has the desktop recipe).
- Security note: the native WS accept parses untrusted bytes; fuzz the
  handshake parser minimally (malformed header cases as doctests).

## B) Firefox compat

Known state: Firefox renders BLACK and throws an Uncaptured "render bundle
incompatible read-only flags" error; instrumentation PROVED our pass/bundle
depth+stencil read-only flags match exactly (forward depthRO=0/bundle 0,
transparent depthRO=1/bundle 1), so this is Firefox's wgpu-based WebGPU
strictness/maturity, not an engine flag bug. Firefox wheel deltaMode already
fixed (a138353c).

1. Re-test on current Firefox first - their WebGPU ships fast; the bug may be
   gone. Capture the exact error text + Firefox version in the task notes.
2. If still failing: bisect by disabling render bundles on web (the RHI has a
   bundle-vs-direct path; a `DeviceFeatures`/flag forcing direct encoding) and
   see if the scene renders. If it does, land "no bundles on Firefox" as a
   UA-gated (or option-gated) fallback and file the upstream repro (minimal
   HTML from WebTriangle + one bundle).
3. Keep Chromium pixel-parity: any fallback must not change Chromium output.

## C) Small shipped-path gaps

- Audio autoplay: "AudioContext prevented from starting" - add
  resume-on-first-user-gesture to the miniaudio web backend path (first
  pointer/key event resumes the context). Test: user smoke (sound after first
  click; >= 250Hz tone rule for audible sample tests).
- AngelScript browser smoke: an AS-authored game script in the browser
  (backend is built + node-tested; the end-to-end browser run + SetLineCallback
  path is unproven). Piggyback on the player export flow.
- P4 export polish only as needed: the Web export preset is SHIPPED; treat
  export bugs found during A-C as part of those items.

## Acceptance

- A: browser client joins a native host over WS on localhost; replication
  demo visibly works (user-verified); desktop UDP path untouched; doctests for
  handshake/framing + transport capability gating; both compilers + wasm
  build green.
- B: WebScene renders in Firefox (either upstream fixed or fallback landed),
  Chromium unchanged.
- C: audio resumes on gesture (user smoke); AS script runs in browser.

---

## State (appended 2026-08-03; original content above is unchanged)

**PARTIAL.** Base web is shipped (fetch-based player, AngelScript-on-web,
frame-perf); Net/Input/Script climbed to web (62f5a508). OPEN: (A) WebSocketTransport
for web networking - not built; (B) Firefox compat; (C) the small shipped-path gaps
+ P4 export polish. This is the main remaining web work.

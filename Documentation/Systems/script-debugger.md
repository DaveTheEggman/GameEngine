# Script debugger + profiler + remote transport (design)

Status: DESIGN, for review. The neutral seams already exist (committed in the
capabilities slice): `IScriptDebugger`, `IScriptProfiler`, `IScriptBlob` +
`ScriptCapabilities::{Debugger,Profiler}` (declared absent) + wire-symmetric snapshot
types (`ScriptStackFrame`/`ScriptVariable`/`ScriptValueObject`) with round-trip tests, and
skip-when-absent battery skeletons. This doc is the plan to fill them in. Reference:
Traktor's `code/Script` debug stack (studied) — we adapt, not copy.

## 1. Why

Behaviors are print-debugged today (`Log.info`). A real debugger — breakpoints, stepping,
call stack, locals, lazy object expansion — is the biggest scripting DX gap, and the one
place Traktor is clearly ahead. It fits our capability model (a backend declares
`Debugger`/`Profiler`; the battery certifies it). And the **remote** angle is
forward-looking: web/WASM and Android targets can't attach a local debugger — you debug
them over a connection. Designing for remote from the start is the point.

## 2. The load-bearing constraint (and the design it forces)

**The editor runs the embedded game (PIE) on the main thread** — the same thread as the
editor UI. So a classic *blocking-halt* debugger (breakpoint → block the game thread →
inspect from the UI, Traktor's model) is impossible in-process: blocking the game thread
freezes the editor too. Traktor sidesteps this by debugging a **separate process** (the
player) over a socket; the editor never blocks because the game is elsewhere.

We have a cleaner option that fits our single-threaded model AND machinery we already own:
**suspension-based breakpoints via AngelScript context suspension** — the exact mechanism
our AngelScript coroutine scheduler already uses (`ctx->Suspend()` / re-`Execute()`).

- A breakpoint is a line-callback that, when a breakpoint line is hit, calls
  `ctx->Suspend()`. Execution returns to *our* caller (the behavior subsystem / editor
  loop) with `asEXECUTION_SUSPENDED` — **non-blocking**, no separate thread.
- While suspended, the context is fully inspectable: `GetCallstackSize` / `GetFunction` /
  `GetLineNumber` for the stack, `GetVarCount` / `GetAddressOfVar` / `GetVarDeclaration`
  for locals. The editor pumps normally and reads this state.
- `Continue` re-`Execute()`s the suspended context; `StepOver`/`StepInto` re-arm the line
  callback to suspend on the next appropriate line.

This gives **in-process PIE debugging with no blocking, no worker thread, and no transport
required** — and the *same* `IScriptDebugger` is driven over a transport for remote
targets later. AngelScript is therefore the first backend; **Wren is deferred** (no
official debug API — it would need VM-internal hooks; revisit if there's demand).

## 3. Architecture — four layers, one contract

The contract is `IScriptDebugger`/`IScriptProfiler` + the serializable snapshot types. The
principle (Traktor's, and the reason the snapshots already serialize): **the editor UI is
written once against the contract, and works identically whether the debugger is in-process
or across a socket.**

1. **Backend debugger** (`draconic.script.angelscript`): implements `IScriptDebugger` over
   context suspension + AS introspection. `AngelScriptManager::CreateDebugger()` returns it;
   the manager declares `ScriptCapabilities::Debugger`. Breakpoints live on the manager
   (all contexts share the engine); the line callback is installed on the contexts the
   subsystem executes.
2. **Backend profiler** (`draconic.script.angelscript`): implements `IScriptProfiler` via
   line/call hooks + a timer, computing inclusive/exclusive per-function time (a stack of
   `{enter, childTime}` — Traktor's method). `CreateProfiler()` + `Capabilities::Profiler`.
3. **Transport** (new, `draconic.script.debug.transport` or a small `draconic.net`): a
   bidirectional serialized-message channel. Two implementations behind one interface:
   **loopback** (in-process, editor ↔ embedded runtime — trivial, no sockets) and
   **socket** (editor ↔ a deployed/remote player). A `RemoteScriptDebugger` *implements*
   `IScriptDebugger` on the editor side and forwards every call as a message to the game's
   real debugger; a `DebugServer` on the game side services them. The message set mirrors
   Traktor's `Remote/*`: `SetBreakpoint{file,line,add}`, `Control{Break|Continue|StepInto|
   StepOver}`, `StackFrames{Array<ScriptStackFrame>}`, `Locals{depth, Array<ScriptVariable>}`,
   `ObjectMembers{ref, Array<ScriptVariable>}`, `StateChanged{ScriptDebuggerState}`,
   `CallMeasured{ScriptCallMeasurement}` — all built from the snapshot types that already
   serialize.
4. **Editor UI** (`draconic.editor.script.debug`): a debugger panel (breakpoint gutter in
   ScriptPage, call-stack list, locals tree with lazy `CaptureObject` expansion, break/
   continue/step toolbar) and a profiler panel (per-function inclusive/exclusive/count
   grid). Both consume the contract, so they don't know or care if they're driving a
   loopback or a socket debugger.

## 4. The uniform-transport question (a real decision)

Two ways to wire layer 3:
- **(A) In-process direct, transport added later.** The editor drives the in-process
  `IScriptDebugger` directly for PIE; the transport + `RemoteScriptDebugger` come in a later
  phase for remote targets. Fastest to first value; the editor UI is already contract-based
  so adding the remote driver later is not a refactor of the UI, only a new
  `IScriptDebugger` impl.
- **(B) Loopback-always.** Even in-process goes through a loopback transport, so there is
  exactly one code path (editor always talks to a `RemoteScriptDebugger`, backed by loopback
  or socket). Traktor-uniform; more upfront plumbing.

**Recommendation: (A).** The snapshots already serialize and the UI is contract-based, so
(A) does not paint us into a corner — the remote phase adds a transport + a forwarding
`IScriptDebugger`, not a rewrite. (B)'s uniformity is nice but front-loads the transport for
no in-process benefit. We get PIE debugging sooner and build the transport when we build the
thing that needs it (remote/device).

## 5. Phasing

- **P1 — AngelScript in-process debugger + editor UI.** Suspension breakpoints,
  Break/Continue/StepInto/StepOver, `CaptureStackFrames`/`CaptureLocals`/`CaptureObject`,
  the async state listener. Breakpoint gutter in ScriptPage; call-stack + locals panels;
  step toolbar. `Capabilities::Debugger` on AngelScript; **the battery's skeleton debugger
  section becomes a live certification** (set a breakpoint, hit it, capture a known local,
  step, continue). Definition of done includes: driving a scripted PIE scene, hitting a
  behavior breakpoint, reading a harvested-property local, stepping, continuing.
- **P2 — AngelScript profiler + editor UI.** Inclusive/exclusive per-function timing;
  profiler grid; `Capabilities::Profiler`; battery profiler section live.
- **P3 — remote transport.** The message protocol + loopback + socket transports +
  `RemoteScriptDebugger`/`DebugServer`; the editor debugs a *player* process, then a
  deployed target. This is the phase that serves web/Android. (Needs a minimal
  `draconic.net` socket layer — we have none today; scope it to the transport's needs, not a
  general networking stack.)
- **P4 — breadth.** Multi-session breakpoint multiplexing (one editor, N targets — Traktor's
  `ScriptDebuggerSessions`); Wren debugger if we invest in VM hooks; step-out; conditional
  breakpoints; watch expressions.

## 6. Capability gating + conformance

`Debugger` and `Profiler` stay per-backend capability flags. AngelScript flips them on in
P1/P2 and must pass the battery's (currently skeleton) debugger/profiler sections — a
backend only advertises what it certifies, same rule as everything else. Wren stays absent
and the sections stay skipped for it, cleanly.

## 7. Open questions / decisions for review

1. **First backend = AngelScript, Wren deferred** — agree? (AS has real debug support + the
   suspend mechanism; Wren has no debug API.)
2. **Suspension-based in-process debugging** (§2) rather than blocking-halt or a PIE worker
   thread — agree? It's the one that fits our single-threaded editor without a threading
   overhaul.
3. **Transport wiring (A) vs (B)** (§4) — recommend (A): in-process first, transport as the
   P3 phase that needs it.
4. **P3 socket layer**: build a minimal `draconic.net` scoped to the debug transport, or is
   there an intended general networking layer this should align with? (We have none today.)
5. Scope of P1's locals: scalars + one level of lazy object expansion is the MVP; how deep
   do we want value rendering (e.g. our reflected value types like Float3 shown field-wise)?

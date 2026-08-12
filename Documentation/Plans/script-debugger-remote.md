# Script debugger - profiler + remote transport (plan)

> Status: DRAFT
> Track: [[script-debugger-track]]

Approved design for the NOT-YET-BUILT phases of the script debugger. The shipped in-process
debugger (AngelScript + Luau) is `Documentation/Systems/script-debugger.md`; this is the plan
for the profiler and the remote-transport layer that serves web/WASM + Android (targets that
cannot attach a local debugger). Adapts Traktor's `code/Script` debug stack.

## Profiler (P2)

`IScriptProfiler` via line/call hooks + a timer, computing inclusive/exclusive per-function
time (a stack of `{enter, childTime}` - Traktor's method). `CreateProfiler()` gated by
`ScriptCapabilities::Profiler`; the battery's profiler section becomes a live certification.
Editor: a per-function inclusive/exclusive/count grid. The snapshot type
`ScriptCallMeasurement` already serializes.

## Remote transport (P3)

A bidirectional serialized-message channel, two implementations behind one interface:
**loopback** (in-process, editor <-> embedded runtime - no sockets) and **socket** (editor <->
a deployed/remote player). A `RemoteScriptDebugger` IMPLEMENTS `IScriptDebugger` on the editor
side and forwards each call as a message to the game's real debugger; a `DebugServer` on the
game side services them. The message set mirrors Traktor's `Remote/*`: `SetBreakpoint`,
`Control{Break|Continue|StepInto|StepOver}`, `StackFrames`, `Locals`, `ObjectMembers`,
`StateChanged`, `CallMeasured` - all built from the snapshot types that already serialize.
Needs a minimal net/socket layer scoped to the transport (no general networking stack).

## The uniform-transport decision (resolved)

Two ways to wire it: **(A)** editor drives the in-process `IScriptDebugger` directly for PIE,
transport + `RemoteScriptDebugger` added later for remote targets; **(B)** loopback-always, one
code path (editor always talks to a `RemoteScriptDebugger`, backed by loopback or socket).
**Chosen: (A).** The snapshots serialize and the editor UI is contract-based, so the remote
phase ADDS a transport + a forwarding `IScriptDebugger`, not a UI rewrite; (B) front-loads the
transport for no in-process benefit. This is why the shipped debugger drives the in-process
contract directly and the UI is remote-ready without being remote yet.

## Breadth (P4)

Multi-session breakpoint multiplexing (one editor, N targets - Traktor's
`ScriptDebuggerSessions`); a Wren debugger if VM hooks are ever invested in; step-out;
conditional breakpoints; watch expressions.

## Open questions (for when this is built)

- The P3 socket layer: a minimal net layer scoped to the debug transport, or align with a
  future general networking layer (the replication net stack exists now - revisit whether it
  hosts this).
- Locals depth beyond scalars + one-level object expansion (already shipped): how deep to render
  reflected value types.

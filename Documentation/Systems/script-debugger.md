# Script debugger

> Status: CURRENT
> Verified: 2026-08-11 @ 9c9046f8
> Track: [[script-debugger-track]] / [[luau-backend-track]]

Source-level step debuggers for gameplay scripts: breakpoints, step into/over, call stack,
locals, and lazy object expansion, driven from the editor with the game paused. Shipped +
battery-certified for **AngelScript** and **Luau**. Wren has none (no VM debug API). Part of
the [[Systems/scripting]] subsystem.

## The constraint (and the mechanism it forces)

The editor runs the embedded game (PIE) on the MAIN thread - the same thread as the editor
UI. A classic blocking-halt debugger (break -> block the game thread -> inspect) would freeze
the editor too. Instead the debugger is **suspension-based and non-blocking**: hitting a
breakpoint SUSPENDS the running script (unwinding back to the editor loop), the run host
raises a pause flag, and the editor pumps normally while inspecting the held execution.
Continue resumes it. No worker thread, no transport needed in-process.

## The neutral contract (`foundation.script`)

- `IScriptDebugger`: `SetBreakpoint(file,line)` / `RemoveBreakpoint`, `Break` / `Continue` /
  `StepInto` / `StepOver`, `CaptureStackFrames()` / `CaptureLocals(depth)` /
  `CaptureObject(ref)`, `SetListener`. `IScriptManager::CreateDebugger()` returns one when the
  backend declares `ScriptCapabilities::Debugger`.
- `IScriptDebuggerListener::OnDebuggerStateChanged(ScriptDebuggerState)` -
  `Running` / `Breakpoint` / `Stepped` / `Terminated`.
- Snapshot types `ScriptStackFrame{file,line,function}` and `ScriptVariable{name,typeName,
  value,objectRef}` are wire-symmetric (they serialize) - so the editor UI is written once
  against the contract and would drive a remote debugger unchanged.
- The **conformance battery** (`Script.Tests/BackendConformance.h`) has a live debugger
  section: set a breakpoint, drive execution, assert it stops (state Breakpoint), capture the
  stack + a known local, step (Stepped), continue to completion (Terminated). Both AngelScript
  and Luau pass it - a backend only advertises `Debugger` if it certifies.

## Run-host integration (`engine.script`)

`ScriptRunHost::RequestDebugger(configurator)` creates the backend debugger (backend-neutral -
resolved from the run's language) and installs a `DebugPauseTracker` as its listener.
`IsDebugPaused()` gates the tick: on a break the game FREEZES (the world holds still, behaviors
stop advancing) and the break is NOT a fault; Continue clears the pause and ticking resumes.
One gameplay context per run (the [[Systems/scripting]] rule).

## Per-backend

- **AngelScript** (`AngelScriptDebugger`): a line callback calls `ctx->Suspend()` on a
  breakpoint/step line; execution unwinds as `asEXECUTION_SUSPENDED`; the debugger ADOPTS the
  suspended context (distinguished structurally from a coroutine suspend - coroutines suspend
  their own contexts, only the pooled executor is the debugger's), and Continue re-`Execute`s
  it. Introspection via `GetCallstack`/`GetVar*` + reflected-property expand.
- **Luau** (`LuauDebugger`, Fable P6): handlers run on POOLED resumable lua threads (always
  `lua_resume`, never `lua_pcall`); `lua_singlestep` is armed only while a debugger is
  attached; a debugstep callback `lua_break`s the thread on a (short_src,line) breakpoint or
  step; the run host holds the broken thread and re-resumes on Continue. One shared resume
  router keeps the coroutine scheduler and the debugger's Continue in agreement. Two nuances:
  the pause lands one bytecode instruction early (luau_callhook advances savedpc, so a local
  bound on the line JUST above a break may not be live yet), and a break cannot cross a C-call
  boundary (deferred to the next safe line).
- **Wren**: no debugger (Wren exposes no debug API).

**Bytecode-loaded classes stay debuggable**: the cook keeps debug info + the sourceName
section, so breakpoints line up whether a class runs from source or from consumed bytecode.

## Capture

Stack frames key on (file, line) - the section IS the source file (per-class chunks/sections),
so an editor breakpoint keyed on the file matches. Locals report name + display value; reflected
value types (e.g. Float3) render field-wise; a reflected object exposes an `objectRef` the UI
expands lazily via `CaptureObject`.

## Editor UI

- **Breakpoint gutter** in the script page (`EditorContext` owns the breakpoint store).
- **DebuggerPanel** (call-stack list + locals tree + break/continue/step toolbar), consuming
  the contract only (remote-ready).
- **Execution line**: the current break line is marked + scrolled to (`ScriptExecutionPoint`
  plumbed EditorContext <- GamePage -> ScriptPage).
- **Hover values**: hovering a local while paused shows "value : Type"
  (`HoverValueProvider`, resolved via `CaptureLocals(0)`).
- The game page freezes simulation on break and clears it on Continue.

## Deferred

Profiler (inclusive/exclusive per-function), remote transport (debug a player/device over a
connection - serves web + Android; needs a minimal net layer), and breadth (multi-session
multiplexing, step-out, conditional breakpoints, watch expressions, a Wren debugger) are NOT
built. The approved design for the profiler + remote transport is in
`Documentation/Plans/script-debugger-remote.md`; the deferred list is in
`Documentation/Backlog/scripting-followups.md`.

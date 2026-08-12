# Scripting - deferred follow-ups

> Status: CURRENT
> Track: [[script-behaviors-p1]] / [[luau-backend-track]] / [[script-debugger-track]]

Deferred-but-durable items for the scripting subsystem (split out of the old scripting design
doc). Reference: `Documentation/Systems/scripting.md`. None are blocking.

- **Behaviors P3.2 - live-state-preserving reload** (Godot placeholder model): preserve a
  behavior's declared-property LIVE values across hot reload. BLOCKED on a **getter
  convention** - behaviors declare setters only, so the runtime cannot read a live value back.
  Needs a read-side convention first.
- **Behaviors P3.3 - script-defined editor tooling hooks**: scripts declaring gizmos /
  inspector customization. Tied to the roadmap's edit-time-Configure questions.
- **Debugger track** (see [[script-debugger-track]] / `Systems/script-debugger.md`): P2
  profiler (inclusive/exclusive per-fn + editor grid); P3 REMOTE TRANSPORT (snapshots already
  serialize; loopback + socket; needs a minimal net layer; serves web/Android); P4
  multi-session multiplexing, Wren debugger (needs VM hooks - deferred), step-out, conditional
  breakpoints, watch expressions. (Luau debugger P6 SHIPPED - see the track memory.)
- **luau-analyze (P5b)** - external `luau-analyze` binary for `script_validate` + optional cook
  typecheck. Luau's Analysis lib uses exceptions (engine is -fno-exceptions), so it runs as a
  vendored CLI subprocess (the DXC/naga pattern), fed the P5a `.d.luau` declarations. DEFERRED.
- ~~ScriptClassesView (API-browser / autocomplete)~~ **DONE** - built as `ScriptApiBrowserView` +
  `ScriptApiSurface` (the one bound-API source driving both the browser and completion, off
  `DescribeBoundApi()`) + `ScriptCompletionImpl`, all wired into `ScriptPage` (own impl files,
  click-to-insert). Kept here only to correct the old "UI unbuilt" claim.
- **Delegate richer AngelScript signatures** - currently one general `double(double)` funcdef;
  per-signature funcdefs are an additive extension.
- **rayHitEntity / Entity placement** - `physics.subsystem` depends on `ScriptFacades` only for
  the `Entity` return type of `rayHitEntity`. Move `Entity` into the script CONTRACT lib so no
  producer subsystem depends on the facades lib.
- **AngelScript coroutine global naming** - only `waitUntil` is namespaced (`Coroutine::`);
  namespace `wait`/`startCoroutine` too if collisions matter (minor).
- **P7 - Wren retirement** - gated on parity + the user's visual pass (DONE 2026-08-11) but
  DEFERRED by the user. The backend gating ([[script-backend-gating]]) makes it a clean delete
  of the `#ifdef OPTION_HAS_WREN` blocks.
- KNOWN_ISSUES.md tracks the AngelScript vendored UBSan/GC-warning items.

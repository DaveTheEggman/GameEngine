# Handoff - Luau debugger P6 COMPLETE; next = bytecode consumption, visual pass, Wren retirement

Branch `game-ready-scripting`. Workflow: Opus BUILDS from `docs/specs/luau-backend.md`
(UNTRACKED - never `git add` docs/); Fable REVIEWS/RULES (the user drives that).

## P6 (Luau step debugger) - DONE + green, all pushed
Commits: `8d7794ef` P6.1, `2b6f123c` P6.2, `d35e2dcf` P6.3a, `9efe5ccc` P6.3b,
`6c1016a6` P6.4. All six Fable rulings (Q1-Q6) implemented in
`Code/Foundation/Script.Luau/LuauScript.cppm`. Status detail + the two positioning
gotchas are in `docs/specs/luau-backend.md` under "STATUS (Opus, 2026-08-11)" and in
the [[script-debugger-track]] memory.

Counts (clang + gcc; ASAN clean, no leaks; angelscript UBSan noise is pre-existing):
16/16 Script.Luau, 85/85 Engine.Script, 14/14 GameInstance.

Two Luau gotchas to remember before touching the debugger again:
- The pause lands ONE bytecode instruction early (luau_callhook advances savedpc ->
  ar->currentline is the NEXT line). Frame-0 line + m_stepFromLine use the stored
  m_brokenLine; a local bound on the line JUST above a break may not be live. Do NOT
  re-introduce the "defer one step" overshoot (it ran the breakpoint line).
- The resume-skip must survive descending into a call (`x = f()` returns to the same
  line for the store) - clear it only on a move to a DIFFERENT line at the resume
  depth or shallower, never on going deeper.

## NEXT (the user's ordered post-P6 plan)
1. BYTECODE CONSUMPTION at runtime, Luau AND AngelScript. Today the cook produces +
   stores bytecode (P3, in ScriptClassSource.bytecode) but the run host still
   LoadBehaviorModules from SOURCE. Make the player prefer the stored bytecode
   (CreateBlob + Serialize(read) + LoadBlob) when present. The per-class chunk path
   (P6.2/Q4) already aligns Luau for per-class blobs; AngelScript has SaveByteCode/
   LoadByteCode already. Land with tests; verify clang+gcc+ASAN.
2. VISUAL PASS (user-driven) on a Luau-scripted scene + the AngelScript debugger
   end-to-end (docs/smoke-checklist.md).
3. RETIRE WREN (P7) - gated on parity + the user's visual pass. One sweep: delete
   Foundation/Script.Wren + ThirdParty/wren + Wren starters/cook/tests, the
   ENABLE_WREN plumbing, the Wren prelude/emitter, every .wren demo (rewritten to
   .luau). AngelScript keeps the second seat.

## VERIFY discipline (every change) - non-negotiable
DEBUG on BOTH clang AND gcc, plus ASAN:
- `cmake --build build/clang --target <targets> -j4` (cap -j4; higher OOM-kills),
  run each binary from `Bin/Debug/Linux64-Clang/`. Same on `build/gcc`
  (`Bin/Debug/Linux64-GCC/`).
- ASAN `build/asan` (`Bin/Debug/Linux64-Clang-ASAN/`): grep EXPLICITLY for
  "detected memory leaks" (0 required); doctest "Status: SUCCESS" prints BEFORE LSan,
  so it alone is not proof. Filter out the pre-existing ThirdParty/angelscript UBSan.
Commit each sub-phase; omit the Co-Authored-By trailer; no em/en-dashes; stage
explicit `Code/` paths only (never `git add -A`; docs/ stays untracked).

## Context
- The AngelScript debugger (`Code/Foundation/Script.AngelScript/AngelScriptScriptImpl.cpp`,
  `class AngelScriptDebugger`) is the reference model + parity bar.
- Run host: `Code/Engine/Engine.Script/ScriptSubsystem.cppm` - RequestDebugger /
  Debugger() / IsDebugPaused / DebugPauseTracker; backend-neutral (resolves the
  backend by ScriptClass.language, one language per run).
- Shared conformance battery: `Code/Foundation/Script.Tests/BackendConformance.h`
  (its Debugger section now runs for Luau via the debug dialect fields).

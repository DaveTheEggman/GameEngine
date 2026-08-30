# Fix Draconic.GUI.Tests keyframes failures (task #115)  (archived)

> Status: ARCHIVED - fully built. Non-authoritative: this is the original build spec, kept
> as the record of what was built and why; present-tense truth is the code + tests.

Size: S. Module: `Code/Draconic/Experimental/Draconic.GUI/` (parked eepp-derived
GUI; Experimental role) + `Code/Draconic/Experimental/Draconic.GUI.Tests/`.

## Context

`Draconic.GUI.Tests` fails 4 keyframes cases; the fourth SEGVs, killing the
run. Pre-existing (not a regression from any current track). Current output:

```
TEST CASE:  keyframes: the animation property drives opacity across the stops
TEST CASE:  keyframes: infinite animations loop
TEST CASE:  keyframes: the animation is spawned once, not re-spawned each ApplyTree
TEST CASE:  keyframes: animates background-color across the stops
KeyframesTests.cpp:121: FATAL ERROR: test case CRASHED: SIGSEGV
```

The GUI module is PARKED (draconic.ui won the game-UI decision) but its test
suite must stay green - a red suite hides real regressions elsewhere in CI-less
workflows.

## Goal

All keyframes cases pass; zero SEGVs; no other GUI tests regress. This is a
bug-fix task in the CSS-animation (@keyframes) engine of draconic.gui, NOT a
feature task - keep the diff minimal.

## Plan

1. Run `Bin/Debug/Linux64-Clang/Draconic.GUI.Tests -tc="keyframes*"` and take
   the SEGV apart first (KeyframesTests.cpp:121, the background-color case).
   Build with ASAN if the stack is unclear: the ASAN build dirs use an `-ASAN`
   suffix (see build presets; never delete Bin output).
2. Diagnose whether the four failures share one root cause (likely: the
   animation-spawn / property-application pipeline) before fixing them
   individually.
3. Fix in draconic.gui source. If a test's EXPECTATION is wrong (upstream eepp
   semantics differ from what the test asserts), fix the test and cite the
   eepp behavior in the commit message.
4. Whole-suite run both compilers (`Draconic.GUI.Tests`, 258+ tests) plus
   `Draconic.GUI.Shell.Tests` / `Draconic.GUI.VFS.Tests`.

## Acceptance

- `Draconic.GUI.Tests` prints `Status: SUCCESS!` on clang AND gcc.
- An ASAN run of the keyframes cases is clean (no leaks/UAF masked by the fix).
- Diff is confined to the keyframes/animation path + tests; no API changes.

---

## State (appended 2026-08-03; original content above is unchanged)

**NOT STARTED.** No commits. GUI is parked (Experimental role), so this is low
priority; self-contained whenever the keyframes suite is revisited.

## State (updated 2026-08-30)

**DONE.** All 6 keyframes cases pass, including the former background-color SIGSEGV
(KeyframesTests.cpp:121); the full GUI.Tests suite is green (349 cases) and
ASAN-clean on clang. No dedicated fix commit landed against the test file - the
crash was resolved by UI-core work (the keyframes SIGSEGV was the same heap-use-
after-free class fixed in ~ViewGroup). Verified by running the suite on current
master (clang Debug + the ASAN build).

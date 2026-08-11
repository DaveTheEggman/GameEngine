# Handoff: export reachability pruning — Phase 1 (closure-pruned dist)

Implements Phase 1 of `docs/design/export-reachability.md`: an **opt-in** closure-pruned
export so a dist carries only what the game reaches from its entry points, plus a pruning
report. Reuses the cook's existing dependency-closure machinery — adds roots, not a
subsystem. C++23 modules, CMake+Ninja, dual clang+gcc Debug. Isolated worktree; commit on
the branch; the main session reviews + merges.

> **Base:** branch from CURRENT master (after the create/build-templates work has merged —
> it also touches `ExportPreset`/`Export.cppm`, so this must build on top of it, not race it).

## Read first
- `docs/design/export-reachability.md` — authoritative. Phase 1 = the "Phased plan" item 1 +
  "Closure computation & pruning in export" + "Reporting & validation". `docs/` is untracked,
  read-only, NEVER edit/commit.
- `docs/design/export.md` "Current status & handoff" block (top) — what export ships today.

## What exists (reuse, don't rebuild)
- `Code/Draconic/Editor/Cook/CookDriver.cppm`: **`CookPlan PlanFor(Span<const Guid> roots, bool
  force)`** already returns "the roots plus their dependency **closure**" (read-dep chaining;
  chases scene → prefab → asset). This IS the reachable-set computation — use it.
- `Code/Draconic/Editor/Core/Export.cppm` (`:export_pipeline`): `ExportContent` today stages
  EVERY scene (`CollectScenes(RootGroup())`) and `PackTree`s the WHOLE cooked dir. `ExportOne`/
  `ExportAll` return `ExportResult`. `ExportPreset` (in `ExportPreset.cppm`) is the per-preset
  config.
- `ProjectSettings::defaultSceneId` (GUID) — the seed root.
- The project/manifest may name a startup/game script (e.g. `game.wren`) — check `ProjectSettings`.

## Implement (Phase 1 only)
1. **`ExportPreset` gains `bool pruneToReachable = false;`** (serialized, back-compat: absent ⇒
   false = today's "export everything", the escape hatch). Pruning is strictly opt-in per preset.
2. **Seed roots** (when `pruneToReachable`): `defaultSceneId` ∪ (the startup script's *own asset
   GUID* if the project has one — the script FILE ships; per the doc's open question, the assets
   it loads follow the normal contract, so do NOT try to chase script contents in Phase 1).
   `ExportRoots` (the "Always Export" flag) is **Phase 2 — not this task**; leave a clear seam
   (a function that returns the root set) so Phase 2 just adds to it.
3. **Closure** = `CookDriver::PlanFor(roots, force)` → the reachable GUID set. Cook AND pack use
   the SAME set (cook only what ships).
4. **Stage + pack only the reachable set**: replace `CollectScenes(RootGroup())` with "reachable
   scenes only", and the whole-dir `PackTree` with "pack only cooked products whose SOURCE GUID
   is in the reachable set". Prefabs: the closure already chases prefab instances + overrides via
   the read-dep graph (`PlanFor` handles it) — verify with a test.
5. **Pruning report**: what was KEPT and *why each root was kept* (default-scene / startup /
   [later: flag/group/script-literal]), plus a count + list of what was DROPPED. Put it on
   `ExportResult` (a structured field) so the CLI prints it and the editor Console shows it, and
   ALSO write it beside the dist (e.g. `export-report.txt`). Must be loud + auditable — pruning
   can silently break a shipped game, so the report is the safety surface.
6. **Non-pruned path unchanged**: with `pruneToReachable=false` (default), behavior is byte-for-byte
   today's "pack everything". Don't regress it.
7. **CLI**: surface the report in `DraconicExport` output. If a quick per-run override is easy, a
   `--prune` / `--no-prune` flag is a nice-to-have, but the preset field is the source of truth.

Do NOT implement `ExportRoots`/"Always Export" (Phase 2), `AssetRef<T>` (Phase 3), or the
script-load lint (Phase 4 — a **stretch** goal per the doc, and language-neutral when built).

## Definition of done
- Both compilers clean (`cmake -S . -B build/clang -G Ninja -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=/usr/bin/cc`; g++→build/gcc). Foreground, sync.
- ALL ctest green BOTH compilers (baseline = whatever master is after templates merges). Extend
  `Code/Draconic/Editor/Core/Tests/ExportTests.cpp`: a project with a REFERENCED asset and an
  UNREFERENCED one → a pruned export's pak/staged set contains only the reachable closure (the
  unreferenced asset is dropped) and the report lists it; a scene→prefab→asset chain is fully
  kept; `pruneToReachable=false` still packs everything (no regression); the report names each
  kept root's reason.
- Smokes: editor `--exit-after 8` → 0; player `--exit-after 5` → `game.wren: exit after`. If
  feasible, a CLI sanity run of `DraconicExport … --preset <pruning preset>` producing a smaller
  pak + a report (write to the scratchpad, never the repo).
- Coherent commits ("Export: ..." subjects). Leave the branch; do NOT merge.

## Repo rules
PascalCase, full names, UTF-8 char8_t, core containers, no std:: in public APIs, **no
`std::filesystem`** (core file ops). Wire read/write symmetry for `pruneToReachable` (+ back-compat
test). Stage files EXPLICITLY. NO Co-Authored-By/Claude-Session trailers. Never touch docs/, Bin/,
ThirdParty/, scratch (test output → scratchpad, not the repo). `cd` to the worktree root each shell
call. `StringView::SubStr(offset,count)`; String `Format()`; HashMap Find+InsertOrAssign; unused
lambda captures are -Werror; a Python-edit assert before the write loses all edits.

## Final report
The `pruneToReachable` field + back-compat; how roots are seeded (and the seam left for Phase-2
`ExportRoots`); how `PlanFor`'s closure drives BOTH cook and pack; the pruning report shape (on
`ExportResult` + on disk) and where it surfaces; the prefab-chain test; ctest totals both compilers
+ smokes; anything deviated with reasons.

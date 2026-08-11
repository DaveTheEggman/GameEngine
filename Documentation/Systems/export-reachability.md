# Export Reachability — pruning the dist to what the game actually needs

Status: **Phases 1 + 2 SHIPPED** (2026-07-20). Related:
[export.md](export.md), [asset-pipeline.md](asset-pipeline.md).

## Status (2026-07-20)

- **Phase 1 (mechanics) SHIPPED** — `ExportPreset::pruneToReachable` (opt-in, default off =
  pack everything); roots seeded from `defaultSceneId` + the startup script's own asset
  (`CollectExportRoots`, which is the Phase-2 seam); a `SceneReferenceScanner` bridges the
  scene→asset edges `CookDriver::PlanFor` can't see; the closure drives BOTH cook and pack;
  an `export-report.txt` (kept roots + reasons, dropped list) beside the dist. Proven:
  a real project pruned **139 MB vs 719 MB**, 179 unreferenced instances dropped.
- **Phase 2 ("Always Export" flag) SHIPPED** — a project-level `ExportRootsSet`
  (`:export_roots` partition): flagged instance GUIDs (rename/move-proof) + flagged group
  PATHS (subtree-as-root, dynamic membership). Persisted as committable `export_roots.xml`
  beside the project (mirrors `export_presets.xml`), loaded by `EditorProject::Open`, CLI-
  loadable. `CollectExportRoots` now appends `Flag` (instances) + `Group` (every instance under
  a flagged group subtree via `CollectGroupInstances`) roots, deduped by guid (first/highest-
  priority reason wins). Editor UX: Asset Browser right-click **"Always Export"** (asset) /
  **"Always export contents"** (group) in the row menu AND the group-tree menu, saved
  immediately + notice + list refresh; a **Sedulous-style corner-dot badge** (`AssetCell`
  overlays a green dot top-right in list + grid) marks flagged items. Headless doctest cover:
  membership toggle + XML round-trip, `CollectGroupInstances` subtree enumeration,
  `CollectExportRoots` seeding + dedup + reopen-persistence. 61/61 editor-core clang+gcc;
  editor smoke exit 0. **Deferred follow-ups:** undoable toggle command (immediate-save mirrors
  the favorites pattern), an Export-Roots audit panel (the pruning report + badges already give
  auditability), left content-tree group badge.
- **`DraconicExport` (CLI) is the fully-wired pruning surface** — it supplies the scanner
  (`MakeSceneScanner`: loads a scene over the manager set, `CollectUnresolved`, reads parked
  prefab instances).
- **Editor pruning SHIPPED — main-thread pre-scan (2026-07-20).** The editor now prunes too.
  WHY it was hard: the scanner must LOAD scenes to enumerate their refs, but the editor runs
  export on the **`EditorJobService` background thread**, and scene loading (SceneSubsystem /
  ResourceManager / component managers) is not safe off the main thread. THE FIX (as designed):
  a `SceneRefScanner` hook on `EditorContext` (set by the scene editor plugin, loads a scene
  through the SceneSubsystem's full manager set), which `EditorApplication::SubmitExportJob`
  drives on the MAIN thread — when a preset in the run prunes, it computes `CollectExportRoots`
  + `ExpandReachableRoots` up front and passes the resulting **precomputed reachable-root guid
  set** into the background job via the new `ExportOne`/`ExportAll` `precomputedReachableRoots`
  param. The job then only cooks + stages + packs that set (pure I/O, no scene loading), so
  editor export stays non-blocking AND correctly pruned. Without a scanner it still falls back
  to pack-everything (safe). (Running the whole export on the main thread would freeze the UI —
  rejected.) Same pruned dist whichever surface (CLI or editor) triggers it.
- Phases 3-5 (`AssetRef<T>`, the script-load lint [stretch, language-neutral], runtime-capture
  validator) unbuilt. Phase 2 (`ExportRoots` "Always Export" flag) SHIPPED; editor pruning via
  the main-thread pre-scan SHIPPED — see above. Both CLI and editor now prune (Phases 1 + 2).

## Problem

Export today packs **everything**: `ExportContent` stages every scene in the source DB
(`CollectScenes(RootGroup())`) and `PackTree`s the entire cooked directory into `Content.pak`
(see [Export.cppm](../../Code/Draconic/Editor/Core/Export.cppm)). Two consequences:

1. **Bloat** — the dist carries assets no shipped scene references.
2. **Leakage** — unreferenced WIP / debug / "secret" assets ship to players.

We want a dist that contains only what the game needs. The dependency graph the cook already
maintains (`CookDriver`, recipe-hash with read-dependency chaining) can compute *reachable-from-a-root*
sets — `CookDriver::PlanFor(Span<const Guid> roots, …)` returns "the requested roots plus their
dependency **closure**". So static asset→asset references (scene → mesh → material → texture) are a
solved problem: seed the roots, take the closure, prune the rest.

The hard part is references that exist **only in code**. A Wren script can do
`Load("weapons/" + name)`; no static analysis can decide that in general (it's undecidable). So
reachability for code-referenced assets is not a pure graph-walk — it is a **contract**: the
reference must be either *visible to the graph* or *explicitly declared*, and tooling must enforce
the contract rather than silently ship a broken build.

## Goals / non-goals

**Goals**
- A predictable one-line mental model for what ends up in a dist.
- Automatic capture of references that *can* be captured; a first-class way to declare the rest.
- Never silently drop an asset the game loads at runtime — make violations visible (warn/fail), and
  make the pruning auditable.
- Reuse the existing roots+closure machinery; add roots, not a new subsystem.

**Non-goals**
- Solving dynamic code analysis in general (impossible). We constrain and declare instead.
- Per-asset compression / format optimization (separate concern).
- Changing the cook's incremental model.

## Mental model

> **Dist = closure( default-scene + startup-script + everything flagged "Always Export" )**

Everything outside that closure is pruned. Flagging an *entry point* (a mesh, a scene, a data table)
automatically pulls in its dependency closure — users flag entry points, not leaves.

## The three tiers

Each reference falls into exactly one tier, with a clear owner:

1. **Automatic — typed `AssetRef<T>`.**
   A reflected, GUID-carrying reference type used by component fields and script-exported properties
   instead of bare path strings. The same reflection walk that already enumerates asset→asset deps
   sees these, so a `WeaponSpawner { AssetRef<Mesh> sword; }` or a data-table asset a script reads is
   captured with **no flag needed**. This is where the *majority* of script references should live:
   script *logic* chooses which declared ref to use; the *set of possible refs* is reflected data.

2. **Explicit — the "Always Export" flag** (this doc's focus).
   For genuinely code-driven loads that are not reflected fields, the user marks an instance or group
   as an export root via the editor. See below.

3. **Linted — the script-load scan.**
   A pass resolves literal `Load("literal")` / GUID args into the closure and **warns on non-literal
   load args** in a shipping export: *"dynamic asset load not statically resolvable at foo.wren:42 —
   use an AssetRef field or mark the target Always Export."* The flag is the sanctioned answer to the
   lint; in shipping mode the warning can be escalated to a hard error.

   > **Revisit (2026-07-20):** this "Wren scan" predates the broader scripting track — we now have
   > **two backends (Wren + AngelScript)**, so the lint must be **language-neutral**, not Wren-specific
   > (each backend's cook already parses its source; the lint belongs behind a per-language cook hook,
   > like the neutral `IScriptLanguageCook`). And two newer facilities shrink its job: **`AssetRef<T>`**
   > (tier 1) is the reflected home for "the set of possible refs," and **`DescribeBoundApi()`** /
   > reflected asset properties mean most script asset references are already graph-visible. So tier 3
   > becomes a thin, backend-agnostic *safety lint* over whatever genuinely-dynamic `Load` calls remain
   > after tiers 1–2 — not a Wren-parser feature. Re-scope when this phase is built.
   >
   > **Priority (user, 2026-07-20): the lint is a STRETCH goal** — do it only if it can be done
   > cleanly (language-neutral, behind the cook hook). Otherwise it is fine to ship without it:
   > **tiers 1–2 (`AssetRef<T>` + the explicit "Always Export" flag) are the sufficient contract** for a
   > correct pruned dist. Explicit export is the answer; the lint is a convenience that catches misses,
   > never a prerequisite.

Automatic where it can be, explicit where it must be, loud where it's ambiguous.

## The "Always Export" flag

### Storage: central roots set, edited per-instance

Two options, and we take a hybrid:

- *On-instance bool* — moves with the asset, but scatters export policy across the whole DB and is
  invisible unless you open each item.
- *Central roots set* — a single auditable list; "show me every root" is one query.

**Decision: store centrally, edit per-instance.** A project-level `ExportRoots` set holds GUIDs (for
instances) and group references (for subtrees). The right-click toggle adds/removes membership; the UI
**badges** flagged items in the Hierarchy and Asset Browser. This gives the per-item ergonomics *and*
a single place to audit — which matters because the #1 failure mode of hidden flags is that people
forget them.

`ExportRoots` is **committable project data** (source-controlled, shared by the team), keyed by
**GUID** so it is rename/move-proof — the same reason `ProjectSettings::defaultSceneId` is a GUID, not
a path. Serialized as a versioned payload alongside the project (e.g. `export_roots.xml` or a section
of the project settings; TBD — see open questions).

### Group semantics: subtree-as-root (dynamic membership)

Flagging a group means **"this subtree is an export root"**, *not* "stamp each current child". Assets
dropped into the folder later are auto-included — the `Resources/`-folder pattern. The menu label must
say so explicitly ("Always export contents of this group") because dynamic membership is both the
powerful case and the main **over-inclusion** risk (WIP/junk that later lands in the folder ships
silently). Mitigations under *Reporting* below.

### Editor UX

- **Hierarchy** (scene instances) and **Asset Browser** (asset instances + groups): right-click →
  "Always Export" (checkable) / "Always export contents of this group".
- A **badge/icon** on flagged items so the set is discoverable at a glance.
- An **"Export Roots" audit panel** listing every root (instances + group subtrees) with jump-to.
- The toggle is an **undoable `EditorContext` command** (fits the hierarchy's existing full command
  coverage) that mutates `ExportRoots` and marks the project dirty.

### Use cases it covers

- A weapon mesh a script spawns by name → flag the mesh.
- An additive level a script loads at runtime → flag the (non-default) scene.
- A whole `runtime-loaded/` folder of data tables → flag the group.

## Closure computation & pruning in export

The export driver changes from "pack everything" to "pack the closure":

1. **Seed roots** = `defaultSceneId` ∪ (startup-script's referenced assets, if any) ∪ `ExportRoots`
   instance GUIDs ∪ every instance under an `ExportRoots` group subtree ∪ literal-resolved script refs
   (tier 3).
2. **Closure** = `CookDriver::PlanFor(roots, …)` already yields roots + dependency closure; export
   reuses the same reachability walk to get the full **reachable GUID set** (cook *and* pack use the
   same set, so we cook only what we ship).
3. **Stage & pack only the reachable set** — `ExportContent` stages only reachable scenes and packs
   only the cooked products whose source GUID is in the set (instead of `CollectScenes(RootGroup())` +
   whole-dir `PackTree`).
4. **Prefabs** — the closure must chase through prefab instances and their overrides (a scene → prefab
   → asset chain), which the read-dep graph already models.

Pruning is **opt-in per preset**: the current "export everything" behavior stays as the default and as
an escape hatch for teams not yet managing reachability. A preset flag (e.g. `pruneToReachable`) turns
it on.

## Reporting & validation

Because pruning can break a shipped game silently, it must be loud and auditable:

- **Pruning report** in the export output: what was kept and *why each root was kept* (default-scene /
  flag / group / script-literal), and a count/list of what was dropped. Surfaced in the editor Console
  and written beside the dist.
- **Runtime-capture validator** (optional mode): during play/smoke, record every asset actually loaded
  and diff against the computed closure. Flags:
  - *loaded but not in closure* → would have been **missing** in a pruned dist (a real bug to fix by
    flagging or converting to an `AssetRef`).
  - *in closure but never loaded* → dead-weight candidates to un-flag.
  This is a safety net and size optimizer — never a substitute for tiers 1–2 (it only covers exercised
  paths).
- **Shipping lint escalation**: tier-3's dynamic-load warning can be promoted warn→error so a build
  with unresolved dynamic loads fails until each is declared.

## Failure modes & mitigations

| Failure | Mitigation |
|---|---|
| Hidden flag forgotten | Badges + Export-Roots audit panel |
| Group root over-includes junk | Pruning report + "in closure but never loaded" diff |
| Script loads an unflagged asset dynamically | Tier-3 lint (warn→error in shipping) + runtime-capture "loaded but not in closure" |
| Rename/move breaks a root | Roots keyed by GUID, not path |
| Accidental toggle | Undoable command |

## Per-preset vs project-global

Start **project-global** `ExportRoots` (simplest, matches the "always export" intuition). Platform- or
preset-specific content (e.g. high-res desktop-only textures) argues for **per-preset include/exclude
overrides** later; defer until there is a concrete need. A future "Never Export" flag (exclude even
though referenced — e.g. a debug asset referenced by a debug-only component) is possible but adds
policy complexity; out of scope for v1.

## Phased plan

1. **Closure-pruned export (mechanics)** — add `pruneToReachable` to `ExportPreset`; seed roots from
   `defaultSceneId` (+ startup) and stage/pack only the reachable set via the existing `PlanFor`
   closure. Ship with a pruning report. *(Immediately useful even before any UI: correctness + size.)*
2. **`ExportRoots` + right-click flag** — central GUID/group set, undoable toggle command, Hierarchy +
   Asset Browser context-menu entries, badges, audit panel.
3. **Typed `AssetRef<T>`** — reflected GUID reference; migrate component/script-property references off
   bare strings so they are captured automatically (biggest structural win; larger change).
4. **Wren scan + lint** — literal resolution into the closure; dynamic-load diagnostics; shipping
   escalation.
5. **Runtime-capture validator** — record-and-diff mode for CI/smoke.

Phases 1–2 deliver the predictable, auditable core with mechanisms we already have; 3 raises the
"automatic" ceiling; 4–5 close the honesty gap.

## Open questions

- Where exactly does `ExportRoots` live — its own `export_roots.xml`, or a section of the project
  settings? (Leaning standalone, to keep it diff-friendly and independently loadable by the CLI.)
- Does the startup script contribute roots automatically, or must its assets be `AssetRef`/flagged like
  any other code load? (Leaning: the script *file* ships; the assets it loads follow the normal
  contract.)
- Group-root representation: store the group's GUID, or its path? (GUID if groups carry stable IDs;
  otherwise a path with the understood rename caveat.)

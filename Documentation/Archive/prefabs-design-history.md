# Prefabs - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/prefabs.md
> Track: [[prefabs-plan]]

NON-AUTHORITATIVE. The survey + design proposal behind the 2026-07 prefab track. Present-tense truth is
`Systems/prefabs.md`; the full original doc (the complete Sedulous walk-through, all pitfalls, the
clipboard-machinery reuse notes) is in git at the P0 commit 3b92560d. Kept for the "why".

## Why then

The editor's spawn workflow (drag asset -> scene) had no unit of composition bigger than one entity;
model imports spawned loose entity trees; and everything instantiation needs already existed in the
editor clipboard machinery (fresh-guid + relink paste path).

## Reference: Sedulous's prefab V2

Sedulous ships a production-grade sparse-delta prefab system (`Sedulous.Engine.Core` Prefab*.bf, tested,
used by TowerDefense):
- **Asset**: a serialized entity subgraph in the SAME record format as scenes (`PrefabSerializer` reuses
  `SceneSerializer`'s records). OpenDDL text.
- **Spawn** (`PrefabSpawner`): every entity gets a fresh guid; a `guidMap[sourceId] -> entity` relinks
  parents; roots re-parent to the drop target; each spawned entity is tagged with a runtime-only
  `PrefabInstanceTag { PrefabId, SourceEntityId, InstanceRoot, PrefabPath }` (empty serialization id =>
  never saved).
- **Overrides**: `LocalModifications` (scene-owned) stores WHICH property paths are overridden (a
  HashSet<PropertyPath> per entity + added/removed-child sets), NOT the values; the live component is
  the source of truth. `DiffComponentSerializer` writes only modified properties; `TrackingComponentSerializer`
  records which fields were read on load to rebuild the set.

## Key design calls (what shipped)

- **Overrides DERIVED, not tracked.** Instead of Sedulous's `LocalModifications`, our save-time path
  reflection-compares live components against the (cached) template baselines keyed by `sourceEntity`
  and writes only differing properties through the versioned Serialize path. No tracked-set analogue =>
  undo/redo/revert can never desync override state. Sedulous tracks because Beef reflection is weak;
  ours is not. (Shipped as `PrefabComponentBaseline`.)
- **Engine-side spawner** (scene.resource level, not editor-only): gameplay needs spawn-from-prefab
  (the TowerDefense pattern) and scene load needs it for instances. (Shipped as `SpawnPrefab` /
  `SpawnPrefabInstance`.)
- **Scene file**: a versioned `PrefabInstances` section (ref + transform diff + sparse overrides);
  instance entities excluded from the main records; `SceneSnapshot` inherits via `SerializeScene`.
- **Nesting** was written up as DEFERRED (P4) - everyone defers it; the duplicate-instance remap is the
  hard part - but it SHIPPED: nested-instance records keyed by `prefabId`, resolved by a prefabProvider,
  with the Referenced-v3 / Expanded-v2 wire encodings and the symmetry + count-guard rule (see
  [[snapshot-prefab-wire-asymmetry]]).

## Phasing (as planned; all delivered)

- P1a: PrefabAsset + create-from-selection + drag-spawn + instance tracking + ref-only scene instances;
  template edit = destroy+respawn.
- P1b: `PrefabInstances` ref+delta save/load via save-time reflection diff.
- P2: override indicators, per-property revert, apply-to-prefab, unlink.
- P3: model -> prefab import (cook a prefab product instead of loose entities).
- P4: nesting (planned deferred; shipped).

## Open questions (as resolved)

1. Cooked/runtime scenes keep ref+deltas vs bake flat -> keep refs (smaller scenes, template edits
   propagate, the runtime spawner exists for gameplay regardless).
2. Prefab editing reuses the scene page for `.prefab` (near-free) vs a dedicated isolation page (later).
3. P1a/P1b split vs one go -> split.

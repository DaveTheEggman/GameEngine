# Prefabs

> Status: CURRENT
> Verified: 2026-08-12 @ 84e710a7
> Track: [[prefabs-plan]]

Prefabs are reusable entity subgraphs in the scene serialization format, with derived (not tracked)
per-instance overrides and nesting. Shipped end to end (P1-P4) and user-confirmed. The spawner lives at
the scene level (`foundation.scene` / `foundation.scene.resource`), NOT editor-only, because gameplay
spawns from prefabs and scene load reconstructs instances.

## Asset + payload

A prefab is a `PrefabDocument` - a serialized entity subgraph in the SAME record format as scenes
(one serializer to maintain, shared with the editor clipboard subtree). It is a content-DB asset
(source + cooked product, like scenes). A scene can itself be authored as a prefab payload via
`ScenePrefabMode`.

## Spawn + instance tracking

`SpawnPrefab` (returns an `EntityHandle`) / `SpawnPrefabInstance` (returns a `Guid`) instantiate a
prefab: every entity gets a FRESH guid, parents relink via the existing paste path, and roots re-parent
to the drop target. Each spawn registers a per-scene `PrefabInstanceState` (tracked by root entity -
`FindPrefabInstanceByRoot` / `ForEachPrefabInstance`), holding the instance's `prefabId`, the
`sourceEntity` map, and the captured template baselines (`PrefabComponentBaseline` +
`baselineTransforms`, parallel to source ids).

## Overrides - derived, not tracked

There is NO stored "which paths are overridden" set. On scene save, for each instance the template
baselines are reflection-compared against the live components (keyed by `sourceEntity`), and only
DIFFERING properties are written, through the versioned component `Serialize` path. Because the diff is
derived on demand, undo/redo/revert can never desynchronize override state (the failure mode Sedulous's
tracked `LocalModifications` guards against - we do not need it, our reflection is strong enough). A
root-transform baseline flag records whether the instance ever moved.

Scene file: a versioned `PrefabInstances` section (prefab ref + transform diff + sparse component
overrides); instance entities are excluded from the main entity records. Play-in-editor `SceneSnapshot`
inherits this automatically (it uses the same `SerializeScene`).

## Nesting + wire

Prefabs nest. A nested instance is a record carrying its `prefabId`, resolved at read time by a
`prefabProvider` / resolver (the primary `GameInstance` / `DefaultApplication` installs one that reads
the nested-prefab payload by guid). Two wire encodings gate on the section mode:
`kPrefabWireReferenced3` (v3 - the nested payload stays referenced by id) and `kPrefabWireExpanded2`
(v2 - expanded inline). The read/write paths must stay SYMMETRIC and count-guarded (the fix for the
Simulate-stop Expanded write/read asymmetry - see [[snapshot-prefab-wire-asymmetry]]); a nested record
whose payload cannot be resolved is skipped with a warning, not a crash.

## Editor + propagation

- Create-from-selection (hierarchy context menu), drag-from-browser spawn, override indicators (bold
  label) + per-property revert (revert = set the property back to the template value, an ordinary
  undoable command), apply-to-prefab / unlink.
- **Model -> prefab import** (`Editor.Scene/ModelPrefab.cppm`) cooks a prefab product from an imported
  model instead of spawning loose entity trees.
- **Propagation / hot reload**: capture the reflection deltas, respawn from the new template, re-apply
  the deltas (reconciled by `sourceEntity`); entities added to the template appear, removed ones vanish
  (warned if they carried overrides).

---

The reference survey (Sedulous's production sparse-delta prefab V2 system) and the original design
proposal / phasing / open questions are in `Documentation/Archive/prefabs-design-history.md`.

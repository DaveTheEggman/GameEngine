# Prefabs — survey + design

Status: **surveyed + design proposed, NOT started** (2026-07-14). Editor-track phase 7
(docs/design/editor.md §5b decided: backfill prefabs at the spawn milestone). This doc is the
survey record and the recommended design; no code exists yet.

## 1. Why now

- The editor's spawn workflow (drag asset → scene) has no unit of composition bigger than one
  entity; model imports spawn loose entity trees.
- Model→prefab import is the designated owner of the per-submesh material-refs serialization
  TODO (`Render/Subsystem/Components.cppm` — `submeshMaterials` is runtime-only "until prefabs").
- Everything prefab instantiation needs already exists in the editor clipboard machinery (§4).

## 2. Survey: SedulousEngine (primary reference — has a FULL "V2" system)

Sedulous ships a production-grade sparse-delta prefab system (engine
`Sedulous.Engine.Core/src/Prefab*.bf`, `Resources/Prefab*.bf`; tested; used by TowerDefense).

- **Asset**: a serialized entity subgraph in the *same record format as scenes*
  (`PrefabSerializer` reuses `SceneSerializer`'s entity/component records). OpenDDL text.
- **Spawn** (`PrefabSpawner`): every entity gets a **fresh guid**; a `guidMap[sourceId] → entity`
  relinks parents; roots re-parent to the drop target. Each spawned entity is tagged with a
  **runtime-only** `PrefabInstanceTag { PrefabId, SourceEntityId, InstanceRoot, PrefabPath }`
  (serialization id empty ⇒ never saved).
- **Overrides**: `LocalModifications` (scene-owned) stores *which* property paths are overridden
  (a HashSet<PropertyPath> per entity + added/removed-child sets) — **not the values**; the live
  component is the source of truth. `DiffComponentSerializer` writes only modified properties on
  save; `TrackingComponentSerializer` records which fields were read on load to rebuild the set.
  The editor marks overrides in property-editor `OnEditEnd` hooks and gizmo drags.
- **Scene file**: instances are **excluded from the main Entities array** and written to a
  separate `PrefabInstances` section = prefab ref + transform diff + per-entity/component sparse
  overrides. Load re-spawns from the template and applies the diffs ⇒ template edits flow into
  every scene automatically. Old scene files without the section load fine.
- **Hot reload**: `PrefabRebuilder` = cache overrides → destroy instance → respawn from new
  template → re-apply overrides + `LocalModifications`.
- **Never finished upstream** (their roadmap "Remaining"): create-from-selection, apply-to-prefab,
  revert (property/all), override indicators, hierarchy badges, viewport drop, **nesting**, and
  undo/redo integration for prefab ops.

## 3. Survey: cross-engine (Flax / Godot / ez / Lumix / Spartan)

(traktor's "Prefab" is a build-time geometry merger — not an entity prefab; excluded.)

| Engine | Asset | Identity/remap | Overrides | Scene stores | Nesting |
|---|---|---|---|---|---|
| **Flax** | JSON flat object list, stable PrefabObjectID per object, cached default instance | `IdsMapping` guid→fresh guid at spawn; live link = (PrefabID, PrefabObjectID) per object | serialize-time **diff vs default instance**, keyed by PrefabObjectID; `SynchronizePrefabInstances` reconciles add/remove on re-save | ref + per-object deltas | first-class (forced re-sync gives duplicate nested instances distinct remaps) |
| **Godot** | PackedScene/SceneState node table | NodePath/positional — no guids | per-property diff at `pack()` (only values differing from base) | ref + deltas | unlimited (states stack, deepest wins) |
| **ez** | editor DDL graph; runtime binary WorldReader blob | per-instance **seed guid** deterministically remaps all UUIDs | **three-way merge** (base vs template vs instance diffs) | ref + seed + instance diff | recursive, merges compose |
| **Lumix** | serialized **mini-World blob** + content hash | flat `EntityMap` index→new EntityRef | **none** — template edit destroys + re-instantiates every instance (edits lost) | **baked expansion** + side table (entity→prefabHandle, roots) | copy-based only |
| **Spartan** | full XML tree | fresh IDs, no map, no link | none | baked | none |

**Pitfalls the successful systems design against**
1. **Never key overrides positionally** — key by a stable per-object id (Flax PrefabObjectID /
   Sedulous SourceEntityId). Positional keys shift when the template gains/loses an entity.
2. **Duplicate nested instances need independent remap tables** (Flax forces a sync pass for
   this) — one shared map silently aliases entities.
3. **Scenes must store ref+deltas** (Flax/Godot/ez/Sedulous), or template edits don't propagate
   to saved scenes (Lumix/Spartan bake and pay with respawn-and-lose-edits).
4. **Tracked override state drifts** under undo/redo/revert unless it is *derived* — or unless
   every mutation path updates it (Sedulous hooks OnEditEnd everywhere; fragile).
5. **Name-keyed property deltas are brittle under renames** — route deltas through the same
   versioned component `Serialize` path as everything else, not a parallel raw format.
6. **Template add/remove must be reconciled** into live instances (Flax
   `SynchronizeNewPrefabInstances`) unless the model is destroy-and-respawn.

## 4. What Draconic already has

- `EditContext` **SubtreeRecord** machinery (`Editor/Scene/EditContext.cppm`): pre-order entity
  records (original guid for parent relink + name/transform/active + serializable components),
  captured to a self-contained binary blob, pasted with **fresh guids** — repeatably, across
  scenes/pages. This *is* prefab capture + instantiation; Sedulous's `PrefabSerializer` records
  are structurally identical.
- `SerializeScene` with envelope versioning (the settings section was added exactly this way) —
  a `PrefabInstances` section is the same kind of versioned addition.
- Guid-routed undo commands for every scene mutation (spawn-as-command is free).
- Full runtime reflection incl. per-property address escape hatch (enables save-time diffing).
- Content DB source/product pipeline + hot-reload listeners (material/texture reload pattern).
- Non-serializable component managers (`IsSerializable()`) — the instance tag can piggyback.

## 5. Recommended design

**End state = Sedulous's scene-file schema + Flax's stable-id-keyed deltas, with one deliberate
deviation: no tracked override set — diffs are computed at save time by reflection.**

- **PrefabAsset** (content DB, source+product like scenes). Payload = the SubtreeRecord format
  (shared with the clipboard; one serializer to maintain). Cooked product = same blob.
- **PrefabInstanceTag** — runtime-only component `{ prefabId, sourceEntityId, instanceRoot }`.
- **Spawn** — engine-side spawner (scene.resource level, NOT editor-only): gameplay needs
  spawn-from-prefab anyway (Sedulous TowerDefense pattern), and scene load needs it for
  instances. Fresh guids + relink via the existing paste path; tag each entity.
- **Overrides — derived, not tracked.** On scene save, for each instance: instantiate (or cache)
  the template records, reflection-compare live components against them keyed by
  `sourceEntityId`, and write only differing properties **through the versioned component
  Serialize path** (pitfall 5). No LocalModifications analogue ⇒ undo/redo/revert can never
  desynchronize override state (pitfall 4). Sedulous tracks because Beef reflection is weak;
  ours isn't.
- **Scene file** — versioned `PrefabInstances` section (ref + transform diff + sparse component
  overrides); instance entities excluded from the main records. SceneSnapshot (play-in-editor)
  inherits this automatically since it uses SerializeScene.
- **Propagation / hot reload** — capture deltas (same reflection diff) → destroy → respawn from
  new template → re-apply deltas. Entities added to the template appear; removed ones vanish
  (warn if they carried overrides). Reconciliation is keyed by sourceEntityId (pitfall 1).
- **Inspector integration** — override indicators (bold label) + per-property revert computed
  on demand for the selected entity only (the per-frame Refresh already exists); revert =
  SetComponentProperty back to the template value (ordinary undoable command).
- **Nesting** — deferred (everyone defers it; the duplicate-instance remap is the hard part).
  The tag's `sourceEntityId` chain leaves the door open.

## 6. Phasing

- **P1a — Lumix-level milestone (shippable alone):** PrefabAsset + create-from-selection
  (hierarchy context menu) + drag-from-browser spawn + instance tag + scene stores ref-only
  instances; template edit = destroy+respawn (no overrides yet).
- **P1b — overrides:** `PrefabInstances` ref+delta save/load via save-time reflection diff;
  rebuild preserves overrides.
- **P2 — editor polish:** override indicators, per-property revert, apply-to-prefab (write the
  live instance back into the template, rebuild other instances), unlink/break.
- **P3 — model→prefab import:** importer cooks a prefab product instead of spawning loose
  entities; serialize per-submesh material refs (closes the MeshComponent TODO).
- **P4 — nesting** (deferred).

## 7. Open questions (for the user)

1. **Cooked/runtime scenes: keep ref+deltas or bake flat at cook?** Recommendation: keep refs —
   smaller scenes, template edits propagate, and the runtime spawner must exist for gameplay
   regardless. Baking is simpler only if the runtime should never know prefabs exist.
2. Where prefab editing happens: reuse the scene page for `.prefab` (Sedulous) vs a dedicated
   isolation page (Flax). Reusing the scene page is near-free; isolation can come later.
3. P1a/P1b split vs doing P1 in one go.

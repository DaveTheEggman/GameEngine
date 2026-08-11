# Draconic Asset Pipeline (editor phase 6)

**Status:** LOCKED 2026-07-11; 6a-6d BUILT+COMMITTED 2026-07-12 (dbae667..ecbd738) after full on-screen verification. Remaining: UX pass, ref sweep, picker dialog, thumbnails (see editor-track memory follow-up list).
**Directives:** NOT a Sedulous port ("we need much better") — synthesis of Traktor's
deterministic pipeline and Lumix's fast-iteration model on Draconic's typed content DB. ALL file
access through the VFS APIs, never the raw filesystem (user 2026-07-11); this phase finally
implements the watchable seam for the native VFS backend.

## 1. What exists (build on)

- **`draconic.editor` cook base** (`Code/Draconic/Editor/Asset.cppm`): `Asset` (source object:
  external `fileName` + import settings) / `IAssetBuilder` (`AssetType()` + one-shot
  `Build(asset, ctx)`) / `AssetBuildContext` (assetRoot + output Instance + db for cross-refs).
  No registry, no dependencies, no incrementality, no driver — builders are invoked by hand.
- **Eight concrete asset+builder pairs**: Texture, Image, Mesh, Animation, Material, Shader,
  ParticleEffect (+ Scene editor module). `ModelImporter/Cook.cppm` cooks whole models
  (textures + materials + meshes + manifest) through the stack; samples call it directly.
- **Content DBs**: `EditorProject` opens BOTH — `SourceDb()` (Content/, XML `.xasset`, typed
  primary object + named binary streams, GUID identity, group tree) and `CookedDb()` (Cooked/,
  binary `.rasset`). `.cache/` is reserved for "thumbnails + incremental-cook hash db".
- **Core primitives**: `JobSystem` (worker threads), `HashBytes` (FNV-1a 64).
- **VFS** (`draconic.vfs`): `IFileSystem` (Open/Exists) + capability queries `AsEnumerable()` /
  `AsWritable()` / `AsWatchable()`; the content DBs already read/write through mounts.
  `IWatchableFileSystem` -> `IChangeSource` (Track/Untrack/Poll) is a DECLARED, UNIMPLEMENTED
  seam. MISSING: any stat surface (size/mtime), watchable implementation.
- **Editor shell**: Assets panel is a placeholder label. Scenes open via the page-factory +
  creator registry.
- **Runtime gap**: components hold `RefPtr<StaticMesh>` etc. directly — no serializable GUID
  resource-reference layer yet (scenes with meshes can't round-trip). IN SCOPE for phase 6
  (user 2026-07-11); see §8.

## 2. Reference synthesis

**From Lumix** (validated fast-iteration model): background compile queue on the job system +
main-thread drain; reverse-dependency map (dep → dependents) so one change re-queues exactly the
affected set; per-job generation counter (superseded compiles dropped); hot reload rides the
live resource refcount/state graph; lazy compile-on-load hook. What we reject: mtime-only
staleness (fragile across VCS/clocks), single global compiler version (`_version.bin` — change
one importer, nuke the world), path-hash cooked identity (moves are lossy; we have GUIDs).

**From Traktor** (deterministic heavyweight; survey 2026-07-11): per-pipeline version baked
into the dependency hash; the buildDependencies/buildOutput split; persisted pipeline DB with
per-product records + lastWriteTime file-hash memos (exactly our fast-path); the PdfUse vs
PdfResource edge distinction (build-READ deps chain hashes; runtime REFERENCES only need the
product to exist); build-on-save setting + file-watcher thread + output-DB-event-driven hot
reload (ResourceManager::reload(guid)); "List Dependents" as a browser action. What we FIX
(Traktor's own defects, confirmed in code): its build loop is SERIAL (parallelism smuggled
inside pipelines) -> we schedule the DAG on the JobSystem; four additive commutative u32 hashes
(collision-prone) -> one ordered 64-bit folded digest; NO orphan collection (deleted sources
leave stale cooked products forever) -> Plan sweeps orphans; whole-file pipeline DB rewritten
per flush -> fine at our scale but kept atomic + corruption-safe; three overlapping nesting
models (buildProduct/buildOutput/ad-hoc, one path even fatal-errors) -> ONE model (importer
fan-out to real source instances). What we defer: content-addressed shared build cache
(team/CI concern; the recipe hash is already the key when it's wanted).

**From Sedulous** (baseline to beat; survey 2026-07-11): it has NO cook stage — import bakes
the final engine format once into the project tree; no incrementality, no content hashing, no
dependency graph, no background builds, and import settings are DISCARDED after the dialog (no
deterministic reimport). Our source DB already beats that structurally: import settings persist
AS the typed Asset instance. What we KEEP from Sedulous: the `ResourceRef {Guid, Path}` dual
with GUID-first resolution surviving renames (adopt for the §8 ref layer); the polled
`IChangeSource` hot-reload pattern (our VFS watchable seam is literally its descendant — now
implemented); the two-level mtime-validated thumbnail cache + throttled async GPU thumbnail
renderer (MaxRequestsPerFrame-style budget); the importer-chooser dialog when extensions
collide; the browser's "registered but missing" broken-ref badge.

## 3. Identity + staleness model (the core improvement over both)

Every source Instance that has a builder gets a **recipe hash**:

```
recipeHash = H( assetObjectBytes            // the serialized Asset (import settings)
             , H(each source file content)  // fileName + any extra files the builder declares
             , builderVersion               // per-builder u32, bump on logic change
             , recipeHash(each asset dep) ) // other instances this build READS (sorted by Guid)
```

Folded ORDERED (field-tagged, deps sorted by Guid) into one u64 - never commutative sums
(Traktor's four additive u32 fields collide on reordered/offsetting children).

**Two dependency kinds** (Traktor's PdfUse/PdfResource, the one distinction worth keeping):
- **read** (`assets` above): the builder READS that instance's content during Build (material
  bakes a texture) -> chains into the recipe hash, induces build order.
- **reference**: the product stores the other instance's Guid for RUNTIME resolution (scene
  references a mesh) -> needs the product to EXIST (cook-order hint) but does NOT chain hashes;
  editing the mesh never re-cooks the scene.

- **Content hashes, not mtimes**, decide staleness (deterministic across checkouts/machines).
  mtime+size is only a FAST-PATH memo: a file-hash cache in the pipeline DB keyed by
  (path, mtime, size) skips re-hashing untouched large files (FBX/PNG).
- Per-builder `Version()` means one importer change rebuilds exactly its products.
- Asset-dep recipe hashes chain, so a texture edit dirties the materials that bake it in, etc.
- Products carry the recipeHash that built them. **Dirty ⇔ product missing or hash mismatch.**
  This subsumes "cook on demand", "incremental build", and "farm build" with one rule.

**Pipeline DB** (`.cache/cook.db`, binary, one file, atomic tmp+rename): per source Guid →
{ recipeHash, product Guids, file deps (path, mtime, size, contentHash), asset deps (Guids) },
plus the reverse-dependency index rebuilt on load. Corruption/missing = full rescan (never
wrong output, only wasted work).

## 3b. VFS integration (binding constraint)

The pipeline never touches the raw filesystem — everything goes through mounts:

- **Mounts**: the project already mounts Content/ and Cooked/ for the DBs; the driver adds a
  **Sources/ mount** (native backend). `AssetBuildContext.assetRoot` (a raw path) is REPLACED by
  `vfs::IFileSystem* sources` + an `OpenSource(fileName)` helper; builders read source bytes as
  VFS streams. (Existing builders route `LoadImage(path)`-style calls through stream loads; the
  8 builders migrate as part of 6a.) The CLI mounts the same three dirs the editor does.
- **Stat capability (new, small)**: the file-hash memo needs (size, mtime). Add
  `struct FileStat { u64 size; u64 modifiedTime; }` and an `IStatFileSystem` capability
  (`AsStat()`), implemented by the native backend; Pak returns null (pak content is immutable —
  memo unnecessary). Where stat is unavailable the driver just hashes content every plan (still
  correct, only slower).
- **Watchable native backend (finally implemented)**: `NativeFileSystem` implements
  `IWatchableFileSystem`/`IChangeSource` — v1 as a THROTTLED STAT SWEEP inside Poll() (portable,
  no platform event plumbing, fits the poll-based contract; the editor polls ~1-2s). Tracked
  locators are directories (recursive). inotify/ReadDirectoryChangesW become drop-in upgrades
  behind the same Poll() later if sweeps get slow on huge trees. The cook driver consumes
  changes: changed source files -> file-hash memo invalidation -> dependents re-queued via the
  reverse map.

## 4. Builder API v2 (`draconic.editor`, backward compatible)

```cpp
struct AssetDependencies {
    Array<String> files;      // source files read (beyond Asset::fileName, which is implicit)
    Array<Guid>   reads;      // instances whose CONTENT this build consumes (hash-chained)
    Array<Guid>   references; // instances the product refers to at runtime (existence only)
};
class IAssetBuilder {
    virtual const TypeInfo* AssetType() const = 0;
    virtual u32 Version() const { return 1; }                    // bump on cook-logic change
    virtual void ScanDependencies(const Asset&, AssetBuildContext&, AssetDependencies&) {}
    virtual Status Build(const Asset&, AssetBuildContext&) = 0;  // unchanged
};
class BuilderRegistry {                    // type -> builder; modules self-describe
    void Register(UniquePtr<IAssetBuilder>);
    IAssetBuilder* Find(const TypeInfo* assetType) const;
};
```

Existing 8 builders keep working (defaults: version 1, deps = fileName only). One product per
source instance keeps the v1 model (multi-product = the ModelImporter pattern: the importer
FANS OUT to many source instances at import time — better than hidden synthesized outputs, the
DB shows the real graph).

## 5. Cook driver (`draconic.editor` — new `CookDriver`)

- **Plan**: walk source DB → instances whose primary type has a builder → compute recipe hashes
  (file-hash memo) → dirty set. Asset-deps induce a topological order; independent assets cook
  in parallel on the JobSystem (Lumix-style queue + generation counter so a re-edit mid-cook
  supersedes the stale job).
- **Products**: written to CookedDb under a mirrored path, product Guid = source Guid
  (deterministic; the sprites-track collision lesson says derived products use
  HashGuid(sourceGuid, productName)). RecipeHash recorded in the pipeline DB (not in the
  product envelope — cooked output stays pure runtime data).
- **Errors**: a failed build keeps the last good product + marks the record failed (listed in
  Console; rebuild retries). Never half-written products (write to temp instance, swap).
- **Orphan sweep** (Traktor's missing piece): Plan diffs the pipeline DB against the source DB -
  products whose source instance is gone (or whose type lost its builder) are deleted from
  CookedDb + the pipeline DB. Cooked/ never accumulates garbage.
- **Version discipline**: builder Version() is manual (code can't hash itself portably) - the
  known failure mode is forgetting the bump (Traktor suffers this too). Mitigations: the rule
  lives in the IAssetBuilder doc comment, and Build > Rebuild All is always a one-click big
  hammer.
- **API**: `CookDriver::Plan(scope) -> CookPlan` (dirty list + order, inspectable for UI),
  `Execute(plan, progress callback)`, `CookAll`, `CookInstance(guid)` conveniences.

## 6. Surfaces

- **CLI** (`Code/Tools/Cook`, `DraconicCook <projectDir> [--all] [--asset <path>] [--dry-run]`):
  same driver, console progress, exit code = failures. Runs headless (no shell/GPU) — builders
  must stay GPU-free (they already are: cooked data is raw pixels/verts).
- **In-editor**: background cook on the JobSystem. v1 triggers: project open (dirty scan), Build
  menu (Cook All / Rebuild All), asset save/import (cook that instance + dependents via the
  reverse map). File WATCHER (external edits to Sources/) is 6c — the shell gains a watcher and
  the driver just gets "these paths changed" events feeding the same reverse map.
- **Status**: StatusBar shows "Cooking N/M..." + Console lines per failure.

## 7. Asset browser panel (replaces the placeholder)

Source-DB-backed (the typed DB is the truth, like Traktor's DatabaseView; Lumix's dir-scan model
doesn't fit our DB): left = group tree (reuses toolkit TreeView), right = instance grid/list of
the selected group. v1 deliberately ships without GPU thumbnails:

- Rows: type icon (per asset type) + name + cook badge (fresh / dirty / failed / no-builder).
- Header: search filter (name substring, all groups), view toggle list/grid.
- Context: New > (creator registry — Scene today, Material/Texture/etc. as creators get
  registered), Import File... (routes by extension to the registered importer: image → 
  TextureAsset instance; model → ModelImporter fan-out), Cook, Rebuild, Rename, Delete,
  New Group.
- Double-click: instance with a page factory (SceneDocument) opens as a page; others open a
  properties popup v1 (full asset-editor pages later).
- Drag source: instance Guid + type payload — consumed by the future resource pickers (§8) and
  scene drop-to-spawn.
- Import collisions: several importers claiming one extension -> chooser dialog (Sedulous).
- Cook badge doubles as the broken-state surface: "failed" and "missing product" both visible
  in the grid (Sedulous IsMissing).
- Thumbnails (6c): offscreen render -> .cache/thumbs/<guid>.png validated against the
  recipeHash (not mtime - we have the better key), lazy + budgeted per frame (Sedulous
  throttle), async GPU readback.

## 8. Resource-reference layer (IN SCOPE — phase 6b)

The bridge from cooked products to live scenes; without it, scenes referencing meshes/materials
cannot round-trip. Shape (Sedulous's proven pattern + our typed DB):

- **`ResourceRef<T>`** (core-level, serializable): { Guid id } + a non-serialized resolved
  `RefPtr<T>`. Serializes the Guid (+ a path HINT string for human-readable diffs; resolution is
  GUID-FIRST and survives renames — the path is never authoritative).
- **`ResourceManager`** (runtime, engine-agnostic core in draconic.content or a thin
  draconic.resource module): Guid -> loaded-object cache + a typed loader registry
  (resource type name -> ILoader reading a cooked Instance into a RefPtr<Object>). Subsystems
  register their loaders in OnInit (the reflection-registration pattern): texture (the existing
  model-A GPU factory), mesh (MeshResource), material. Rename = cache re-key, never eviction.
- **Scene integration**: components swap raw `RefPtr<StaticMesh>` fields for
  `ResourceRef<StaticMesh>`; component Serialize writes/reads the ref; a POST-LOAD RESOLVE pass
  (SceneSubsystem, after deserialize) resolves every ref through the ResourceManager - no
  serializer-context coupling, load order stays trivial.
- **Editor**: the inspector gains a resource-ref property editor (picker popup filtered by
  resource type, backed by the source DB) and accepts drag-drop from the Assets panel.
- **Hot reload seam**: the manager owns the Guid -> live-object map, so a recooked product
  reloads in place and Generation-bumps (Sedulous), notifying dependents. Wired in 6d.

## 8b. Explicitly deferred

- File watcher-driven recook + live hot reload (6d - the ref layer above provides the
  propagation path).
- GPU thumbnails (6d). Cooked-format platform variants (one CookedDb per target) - the recipe
  hash already accommodates a platform salt; not built until a second platform ships.

## 9. Build order

- **6a — cooker core**: DONE (committed 04953f3..fdccf05, 2026-07-11). VFS stat capability +
  native impl; Builder API v2 (Version/ScanDependencies/ProductType/registry) + all ten builders
  on VFS streams; content DeleteInstance/CreateInstanceWithId/OpenEnvelope; CookDb + CookDriver
  (plan/execute/level-parallel/orphan sweep); DraconicCook CLI. Five driver suites cover the
  staleness model end to end. Note: generation-counter job superseding + per-item background
  scheduling land with 6c (editor background cook) - the CLI path is plan-then-execute.
- **6b — resource-reference layer** (§8): DONE (committed a4ca4f1..0843d31, 2026-07-11).
  KEY DISCOVERY: the proxy-handle system already existed (draconic.resource: ResourceHandle
  replaceable slot + Proxy<T> + IResourceFactory + ResourceManager w/ auto dependency edges +
  transitive Reload) - 6b added the missing bridge: resource::Ref<T> (Guid-serialized, ADL
  Serialize, Bind() attaches the proxy, direct-RefPtr override for procedural code keeps every
  call site source-compatible), the ComponentManagerBase::ResolveResources ADL hook +
  ResolveSceneResources post-load pass, and the MeshComponent migration (serializable manager,
  type id "mesh"). Per-submesh material refs deferred to phase 7 (prefabs own model->entity).
  Editor wiring (a ResourceManager over the project CookedDb + resolve on scene open) lands
  with 6c alongside the pickers that create refs.
- **6c — editor integration**: BUILT (uncommitted, awaiting on-screen verification 2026-07-11).
  EditorCookService (background-thread cook, one at a time, progress -> status bar + Console,
  badges Cooked/Missing/Failed/NoBuilder, Revision for UI refresh; gcc-modules note: use core's
  exported Atomic<T> alias, a second module including <atomic> corrupts the CMI); AssetsView
  (group tree + filtered instance list + badges + context menus + double-click-open); Build menu
  (Cook All / Rebuild All); app-owned ResourceManager over CookedDb (exe registers mesh/skinned/
  material factories) + ResolveSceneResources on scene open; inspector resource-ref picker rows
  (exact Ref<T> TypeInfo match -> asset menu, undoable SetResourceRefCommand that re-derives the
  component address per apply - pools move); IMPORT FLOW: IShell::DrainDroppedFiles (SDL3
  SDL_EVENT_DROP_FILE queue), IFileImporter/ImporterRegistry in editor.core (+CopyIntoSources +
  path helpers), TextureFileImporter (png/jpg/jpeg/tga/bmp/hdr -> Sources/ copy + TextureAsset
  w/ 3D preset), app routes drops into the Assets panel's selected group. Still open in 6c
  scope: drag-from-Assets to pickers/viewport spawn, importer-chooser on extension conflicts,
  model importer (fan-out).
- **6d — polish**: watcher + hot reload BUILT (uncommitted, same verification batch as 6c).
  NativeFileSystem implements IWatchableFileSystem/IChangeSource - a stat-sweep diff against a
  (size,mtime) snapshot per tracked directory tree: Track baselines silently, Poll emits
  added/changed/removed locators (caller throttles; platform event backends can swap in behind
  the same Poll). The cook service tracks Sources/ and polls every 2s - any external file edit
  queues an incremental cook. CookStats now carries the rebuilt product guids; the app reloads
  each through the ResourceManager after OnCookFinished, so live scenes hot-swap products behind
  the proxy handles (dependents reload transitively via the manager's recorded edges).
  Remaining 6d: thumbnails.

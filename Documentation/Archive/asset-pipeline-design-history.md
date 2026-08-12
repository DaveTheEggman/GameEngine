# Asset pipeline - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/asset-pipeline.md
> Track: [[editor-track]]

NON-AUTHORITATIVE. The reference synthesis + build-order record from the 2026-07 "editor phase 6"
pipeline design. Present-tense truth is `Systems/asset-pipeline.md`; the full original design doc
is in git at the P0 commit 3b92560d. Kept for the "why".

## Reference synthesis (survey 2026-07-11)

**From Lumix** (fast-iteration model, adopted): background compile queue on the job system +
main-thread drain; reverse-dependency map so one change re-queues exactly the affected set;
per-job generation counter (superseded compiles dropped); hot reload rides the live resource
refcount/state graph. REJECTED: mtime-only staleness (fragile across VCS/clocks); a single global
compiler version (change one importer -> nuke the world); path-hash cooked identity (moves are
lossy - we have GUIDs).

**From Traktor** (deterministic heavyweight): per-pipeline version baked into the dependency hash;
the buildDependencies/buildOutput split; a persisted pipeline DB with per-product records +
lastWriteTime file-hash memos; the PdfUse vs PdfResource edge distinction (build-READ deps chain
hashes; runtime REFERENCES only need the product to exist); build-on-save + file-watcher + hot
reload; "List Dependents" as a browser action. FIXED (Traktor's own defects): its serial build
loop -> we schedule the DAG on the JobSystem; four additive commutative u32 hashes (collision-prone)
-> one ordered 64-bit fold; NO orphan collection -> Plan sweeps orphans; whole-file DB rewrite per
flush -> kept atomic + corruption-safe. DEFERRED: a content-addressed shared build cache (the
recipe hash is already the key when it is wanted).

**From Sedulous** (baseline to beat): NO cook stage - import bakes the final format once, no
incrementality/hashing/dependency graph, and import settings are DISCARDED after the dialog. Our
source DB beats that structurally (settings persist as the typed Asset). KEPT from Sedulous: the
`{Guid, Path}` ref with GUID-first resolution surviving renames; the polled `IChangeSource`
hot-reload pattern; the mtime-validated + throttled thumbnail cache; the importer-chooser dialog on
extension collisions; the "registered but missing" broken-ref badge.

## Build-order record (what shipped, 2026-07-11..12)

- **6a - cooker core** (04953f3..fdccf05): VFS stat capability + native impl; Builder API v2
  (Version/ScanDependencies/registry) + all builders on VFS streams; content
  DeleteInstance/CreateInstanceWithId; cooked DB + CookDriver (plan/execute/level-parallel/orphan
  sweep); the cook CLI. Five driver suites cover the staleness model end to end.
- **6b - resource-reference layer** (a4ca4f1..0843d31): the proxy-handle system already existed
  (ResourceHandle replaceable slot + Proxy<T> + IResourceFactory + ResourceManager w/ auto
  dependency edges + transitive Reload); 6b added the bridge - `resource::Ref<T>` (Guid-serialized,
  ADL Serialize, Bind() attaches the proxy, direct-RefPtr override), the
  `ComponentManagerBase::ResolveResources` hook + `ResolveSceneResources` post-load pass, and the
  MeshComponent migration.
- **6c - editor integration** (dbae667..ecbd738): EditorCookService (background cook, badges,
  Revision refresh; gcc-modules note - use core's exported `Atomic<T>` alias, a second module
  including `<atomic>` corrupts the CMI); AssetsView; Build menu; app ResourceManager over the
  cooked DB + resolve on scene open; inspector resource-ref picker rows; the drop-file import flow
  (`IShell::DrainDroppedFiles`, `ImporterRegistry`, `TextureFileImporter`).
- **6d - polish**: watcher + hot reload - `NativeFileSystem` implements
  `IWatchableFileSystem`/`IChangeSource` as a stat-sweep diff; the cook service tracks `Sources/`
  and hot-swaps rebuilt products through the proxy handles. Remaining: thumbnails.

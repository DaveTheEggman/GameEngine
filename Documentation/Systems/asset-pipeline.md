# Asset pipeline

> Status: CURRENT
> Verified: 2026-08-12 @ 9c9046f8
> Track: [[editor-track]]

Deterministic, incremental asset cooking on a typed content DB. Source assets (import settings
as typed objects) cook to runtime products only when their inputs actually change, ordered by a
real dependency graph, in the background in the editor and headless from a CLI. All file access
goes through the VFS - never the raw filesystem.

## Content databases (`foundation.content`)

A project opens two `ContentDatabase`s over VFS mounts:
- **Source DB** (`Content/`, XML `.xasset`): a typed primary `Asset` object (external `fileName`
  + import settings) + named binary streams, GUID identity, a `Group` tree of `Instance`s.
- **Cooked DB** (`Cooked/`, binary `.rasset`): the cooked runtime products.
- `.cache/` holds the pipeline DB + thumbnails.

`Instance` = one typed object in a `Group`; import settings persist AS the typed Asset (so a
reimport is deterministic).

## Identity + staleness (the deterministic core)

Every source `Instance` whose primary type has a builder gets a **recipe hash**:

    recipeHash = fold( serialized Asset bytes,
                       content hash of each source file,
                       builder Version(),          // per-builder u32, bump on cook-logic change
                       recipeHash of each READ dependency )   // sorted by Guid

Folded ORDERED into one u64 (field-tagged, deps sorted) - never commutative sums (which collide
on reordered children). Staleness is by CONTENT HASH, not mtime (deterministic across
checkouts/machines); an (path, mtime, size) -> contentHash memo in the pipeline DB is only a
fast path to skip re-hashing untouched large files.

**Two dependency kinds** (the one Traktor distinction kept):
- **read**: the build consumes that instance's content (a material bakes a texture) -> chains
  into the recipe hash and induces build order.
- **reference**: the product stores another instance's Guid for RUNTIME resolution (a scene
  references a mesh) -> the product only needs to EXIST (a cook-order hint); editing the mesh
  never re-cooks the scene.

**Dirty <=> product missing or recipe-hash mismatch.** One rule subsumes cook-on-demand,
incremental build, and farm build. The **pipeline DB** (`.cache/cook.db`, one binary file,
atomic tmp+rename) stores per source Guid `{recipeHash, product Guids, file deps
(path,mtime,size,contentHash), read-dep Guids}` + a reverse-dependency index rebuilt on load;
corruption/missing = full rescan (never wrong output, only wasted work).

## Builders (`pipeline.core`)

    class IAssetBuilder {
        virtual const TypeInfo* AssetType() const = 0;
        virtual u32 Version() const;                                  // bump on cook-logic change
        virtual void ScanDependencies(const Asset&, AssetBuildContext&, AssetDependencies&);
        virtual Status Build(const Asset&, AssetBuildContext&) = 0;
    };
    // AssetDependencies { files; reads; references; }
    class BuilderRegistry { Register(...); Find(assetType); };  // type -> builder, modules self-describe

One product per source instance. Multi-product is the importer FAN-OUT pattern (a model importer
creates many real source instances at import - the DB shows the true graph, no hidden synthesized
outputs). Builders are GPU-free (cooked data is raw pixels/verts) so the headless CLI can run
them. The builder set + all type/product/importer registration is assembled at ONE composition
root, [[pipeline-registration]].

## Cook driver (`pipeline.cook`)

`CookDriver`: `Plan(scope) -> CookPlan` (dirty list + order, inspectable for UI), `Execute(plan,
progress)`, plus `CookAll` / `CookInstance(guid)`. Read-deps induce a topological order;
independent assets cook in PARALLEL on the JobSystem (a generation counter drops a job a re-edit
superseded). A failed build keeps the last good product + marks the record failed (listed in the
Console; rebuild retries); products are written to a temp instance and swapped (never
half-written). An **orphan sweep** in Plan deletes cooked products whose source is gone or whose
type lost its builder, so `Cooked/` never accumulates garbage. Builder `Version()` is manual (the
known failure mode; Build > Rebuild All is the one-click hammer).

## VFS integration (`foundation.vfs`)

Everything routes through mounts (`Content/`, `Cooked/`, `Sources/`) - the pipeline never touches
the raw filesystem. `IStatFileSystem` (`AsStat()`, native backend) provides the (size, mtime) for
the memo (Pak returns none - immutable content). `NativeFileSystem` implements
`IWatchableFileSystem` / `IChangeSource`: v1 is a THROTTLED STAT SWEEP inside `Poll()` (portable,
no platform event plumbing; inotify/ReadDirectoryChangesW are drop-in upgrades behind the same
Poll). The cook service tracks `Sources/` and polls (~2s); a changed file invalidates the memo
and re-queues dependents via the reverse map.

## Resource-reference layer (`foundation.resource`)

The bridge from cooked products to live scenes:
- **`resource::Ref<T>`** (serializable): a Guid + a Bind()-attached proxy handle; GUID-first
  resolution survives renames (a path string is a human-readable hint only). A direct-`RefPtr`
  override keeps procedural call sites source-compatible.
- **`ResourceManager`** (`ResourceHandle` replaceable slots + `IResourceFactory` registry): Guid
  -> loaded-object cache with auto dependency edges + transitive `Reload`. Subsystems register
  their factories (mesh/skinned-mesh/material/texture); rename is a cache re-key, not eviction.
- **`ResolveSceneResources`** is a POST-LOAD pass (after deserialize, via the
  `ComponentManagerBase::ResolveResources` ADL hook) - no serializer-context coupling.
- **Hot reload**: a recooked product reloads in place behind its proxy handle and Generation-bumps;
  dependents reload transitively via the manager's recorded edges.

## Surfaces

- **CLI** (`Code/Tools/Cook`): the same `CookDriver`, headless (no shell/GPU), console progress,
  exit code = failures.
- **Editor** (`EditorCookService`, `editor.core`): background-thread cook (one at a time,
  progress -> status bar + Console), per-instance cook badges (Cooked / Missing / Failed /
  NoBuilder), a Build menu (Cook All / Rebuild All), an app-owned `ResourceManager` over the
  cooked DB + `ResolveSceneResources` on scene open, and inspector resource-ref picker rows
  (exact `Ref<T>` TypeInfo match, undoable). Import: `IShell::DrainDroppedFiles` (SDL3 drop
  events) -> `ImporterRegistry` (by extension) -> copy into `Sources/` + a typed Asset (e.g.
  `TextureFileImporter` for png/jpg/tga/bmp/hdr/dds).
- **What a texture cook costs** (2026-09-23, measured through the MCP `asset_cook` tool on
  the RTHomes1 4k JPEGs): decode ~0.2 s, mips ~0.1 s, BC7 ~0.8 s, write ~0 - about 1.2 s for
  one 4k texture alone, 7 s for six together on 16 cores; it was 84 s and 506 s. Four things
  made the difference and each is a rule now: the block encoder fans its block rows out over
  the cook's job system (`AssetBuildContext::jobs`, `EncodeBlockCompressed(..., jobs)`,
  byte-identical to the inline loop) and so does the mip filter; the vendored encoders
  (bc7enc, astcenc) and stb build at -O2 in EVERY configuration (a Debug editor cooks too);
  Default compression is bc7enc's uber 0 (its own default; the mid level cost ~4x for a
  fraction of a dB) and Quality is uber 4; and `Array::Resize` grows geometrically with a
  block relocation for trivially copyable elements (the 85 MB mip chain used to reallocate
  byte by byte per level - 6 s at -O0). The builder logs each texture's phase split and the
  driver logs each build's time, so a slow phase names itself in the console.
- **DDS sources** (`foundation.image.dds`, 2026-09-22): a DDS is a GPU-ready container
  (BC1-BC7 / BC6H blocks, a mip chain, legacy FourCC or DX10 header). The module reads it as-is
  (`LoadDds` keeps the payload bytes), decodes any level through bcdec (`DecodeLevel`: RGBA8,
  or RGBA32F for the float formats; BC5 / RG8 get a reconstructed Z so a two-channel normal
  reads whole), and writes the DX10 form. Every generic image loader sniffs the magic and
  hands back level 0, so thumbnails, pages and model loaders read DDS without knowing it.
  The texture builder PASSES THE LEVELS THROUGH untouched when the target reads BC, the
  authored compression is not None, the block format fits the asset's usage by the policy
  table's own rules (Colour = BC1/BC2/BC3/BC7; Normal = BC7 only - never BC5, until the shaders
  reconstruct Z; Mask = BC4/BC7; HDR = BC6H) and the file carries the mip chain the asset asks
  for; otherwise level 0 decodes and cooks like any image (the ASTC route, the None escape hatch,
  a BC5 normal, a chain-less file wanting mips). The sRGB / linear twin of a block format is the
  asset's colour space (same bytes, another GPU view); a DX10 header settles it at import for a
  colour map, and BC5 / BC4 / float headers pick the normal / mask / HDR presets. A model that
  references DDS files (Bistro) keeps them on disk: the loaders leave a DDS undecoded
  (`ModelTexture::sourceFile`), the model importer copies it into Sources/ and creates a
  FILE-BACKED TextureAsset with the material slot's usage, and the cook passes it through.
  The glTF loader prefers the `MSFT_texture_dds` image over a texture's PNG `source` and maps
  `KHR_materials_pbrSpecularGlossiness` (diffuse -> base colour, 1 - glossiness -> roughness).
  Cubemap / array / volume DDS: not yet (the cook refuses with NotSupported).
- **Asset browser** (`AssetsView`): source-DB-backed - a `Group` tree + a filtered instance
  grid/list with cook badges and a context menu (New / Import File / Cook / Rebuild / Rename /
  Delete / New Group); double-click opens a page (scene) or a properties popup; instances carry a
  Guid+type drag payload for the pickers and scene drop-to-spawn.

## Deferred

GPU thumbnails (offscreen render -> `.cache/thumbs/<guid>.png` keyed by recipe hash, budgeted async
readback) and cooked per-target platform variants (one cooked DB per target - the recipe hash
already has a platform-salt seam) are not built. See the week-2026-09-05 absorbed backlog.

---

Design lineage (the Lumix / Traktor / Sedulous synthesis and what was rejected/fixed) is in
`Documentation/Archive/asset-pipeline-design-history.md`.

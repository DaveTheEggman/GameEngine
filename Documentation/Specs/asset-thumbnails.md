# Asset thumbnails (generation + display)

> STATUS: P2 (mesh + material) BUILT 2026-08-30 (Fable) - see the P2 status block below.
> P1 BUILT 2026-08-16 (Fable). Service + texture generator + browser-grid and
> picker-slot display + tests shipped. ARCHITECTURE RULING (user): generators live in
> the DOMAIN editor libs beside their asset's editor surface (Editor.Texture carries
> the texture generator + its Pipeline::Texture knowledge) and register through the
> domain's Register<X>Editor call in the Tools.Editor composition root, with a
> GeneratorCount tripwire there - Editor.App owns only the SERVICE + lifecycle and
> links no pipeline libs for thumbnails (the Pipeline.Registration pattern applied).
> Cross-engine research (Sedulous/Lumix/Traktor, recorded in the P2 notes below)
> validated the content-hash filename key and added the in-flight budget + the
> checkerboard-behind-alpha texel treatment. The DISPLAY
> seams already exist: `AssetPickerSlot::SetPreviewThumbnail` (thumbnail wins, type
> icon is the fallback - built with asset-picker-slot.md P1) and the asset browser's
> tile `DrawableView` (its comment has said "future-thumbnail" since the tile
> shipped). This spec is the generation + cache + service side, plus wiring those
> two display points.

## Goal

Real per-asset thumbnails wherever an asset is shown: the browser's grid tiles, the
inspector's picker slots, and (later) the picker dialog - generated on demand, cached
per project, invalidated when the asset changes, and always falling back to the
asset-type icon until a thumbnail exists (the user-locked display rule).

## Principles

- **Icon-first, thumbnail-when-ready.** Every display point renders the TYPE ICON
  immediately; the thumbnail swaps in when generation completes. No blocking, no
  blank squares. (The slot already implements exactly this two-layer model.)
- **Generation is editor-only.** Nothing in the runtime or the cook knows about
  thumbnails; the service lives in the editor tier and its cache is disposable.
- **The cache is files, keyed by content** - no manifest, no database (the
  no-JSON-for-engine-data rule stays untouched; there is no engine data here at
  all). Deleting the cache directory is always safe.

## Architecture

### EditorThumbnailService (editor.core or editor.app; one per open project)

```
DrawablePtr Get(const Guid& id);          // resolved thumbnail or EMPTY (caller keeps icon)
Function<void(const Guid&)> OnThumbnailReady;  // swap-in signal (UI thread)
void Invalidate(const Guid& id);          // drop cache entry; next Get regenerates
```

- `Get` checks RAM (LRU of decoded drawables), then disk, then schedules
  GENERATION on the existing `EditorJobService` (the "pages submit light work
  (preview bakes) here" precedent) and returns empty.
- Completed jobs write the disk cache, decode into the RAM cache, and fire
  `OnThumbnailReady` from the UI thread (the job service's completion path).
  Consumers re-query and swap.
- RAM cache: LRU over decoded thumbnails (a few hundred entries; a browser page
  shows dozens). Disk cache: unbounded, prunable.

### Disk cache - content-keyed filenames, no manifest

`<project>/.thumbnails/<guid>-<contentHash>.png` (gitignored; sibling of the cook
cache). The contentHash is the SAME source-content hash the cook already computes
for the instance (envelope + streams), so:

- a stale thumbnail is DETECTED by filename mismatch (guid matches, hash does not)
  and regenerated; the stale file is deleted at that moment (lazy cleanup);
- reimport/cook invalidation costs nothing: the next `Get` after a content change
  simply misses.

PNG via the existing `foundation.image` IO; 128x128 (the grid tile is 40 logical
today - headroom for the picker dialog and DPI without regeneration).

### Generators - per-asset-type providers, one registry

`IThumbnailGenerator { assetTypeNames; Generate(instance, pixels 128x128) }`,
registered by the editor (same first-wins idiom as the panel-provider registry).
Phased coverage:

- **Texture/Image** (P1): decode the source image, box-downscale on the CPU job
  thread. No GPU involvement.
- **Material / Mesh / Prefab / Scene / ParticleEffect** (P2): offscreen render
  through the EXISTING preview machinery (the editor already renders meshes in the
  mesh-viewer page and scenes in the camera preview - the generator drives that rig
  headless at 128x128 into a readback). GPU work runs on the render thread via the
  established preview-bake path, NOT on job threads.
- **Font** (P3): glyph sample ("Ag") via the fonts stack. **AudioClip** (P3):
  min/max waveform strip rendered on the CPU.
- Everything else: no generator - the type icon simply remains (that is the
  designed fallback, not an error).

## Display wiring

- **AssetPickerSlot**: the inspector's ref rows resolve
  `thumbnails.Get(guid)` when binding/refreshing and call `SetPreviewThumbnail`
  (already built); `OnThumbnailReady` re-resolves visible rows. The slot needs no
  changes.
- **Browser tiles**: `AssetsView`'s adapters bind
  `iconView->Drawable = thumbnail ? thumbnail : RowIcon(position)`;
  `OnThumbnailReady` invalidates the affected row so the virtualized bind re-runs.
  List rows keep the small type icon (16px - a thumbnail reads as noise there);
  GRID tiles get thumbnails.
- **Picker dialog** (P4): same pattern as the grid.
- Damage: `OnThumbnailReady` marks the hosting context for redraw (a swap-in is a
  damage producer; idle browsers stay idle).

## Invalidation triggers

- Cook/import events the editor already has (`OnCookFinished`, the import
  listeners): call `Invalidate(id)` for rebuilt instances - cheap, and the
  content-hash filenames make even a MISSED invalidation self-healing.
- Delete/rename: nothing to do (cache is guid-keyed; orphans are pruned lazily
  when encountered, or by a "clear thumbnail cache" maintenance action).

## Phasing

- **P1** - service + disk/RAM cache + the Texture/Image generator + browser grid
  tiles + picker-slot wiring. Tests: content-hash keying (stale file replaced),
  LRU eviction, icon-fallback-until-ready (stub generator), ready-signal rebind.
- **P2** - offscreen-render generators (material sphere, mesh turntable frame,
  prefab/scene view) on the preview-bake path. Pixel-probe tests on real devices
  per the GPU-testing convention.
- **P3** - font + audio generators.
- **P4** - picker dialog thumbnails; a settings knob for thumbnail size; cache
  maintenance action (clear/regenerate all).

## P2 status (2026-08-30)

Built: the GPU lane end to end, with MESH + MATERIAL generators.

- `ISceneThumbnailGenerator` (editor.core): the type-erased GPU seam - a generator populates
  the shared stage scene (Stage per frame until Ready/Failed, Unstage removes what it added)
  and reports a framing radius; it never touches the GPU. The service grew the one-in-flight
  job queue (`TakeSceneJob`/`AcceptSceneResult`), and its disk cache is now generator-kind
  agnostic: a cached PNG for a GPU-generated type loads on the light lane exactly like a CPU
  one, with the corrupt-file self-heal falling through to a fresh GPU job.
- `ThumbnailStage` (editor.preview, PIMPL like PreviewViewport): a persistent hidden scene in
  its own SceneManager, an ortho camera along (1,1,1) with half-extent = radius * 1.1, a 4x
  supersampled RGBA16Float 512 target (the viewport format - the post passes never see a
  second format) box-downscaled to 128 with a half-float decode (the tonemap output is
  already sRGB-encoded; quantize directly), and a readback whose copy is recorded one frame
  AFTER the scene view (the graph executes at EndRendering, behind the frame encoder's own
  commands - a same-frame copy captures the previous job) and retires by frame-ring
  round-trip - no fence, no stall. Driven by Editor.App: Update in OnUpdate, Render inside
  the scene-render bracket; destroyed on project close before the service resets.
- Generators (editor.scene, registered from RegisterSceneEditor; composition-root tripwire =
  2): meshes (cooked product, bounds-centered at the origin, neutral PBR, radius = bounds
  half-diagonal; covers StaticMeshAsset + SkinnedMeshAsset) and materials (cooked material on
  a unit sphere). Each owns PERSISTENT stage entities toggled active per job (the
  Sedulous/Lumix persistent-world pattern) with a private no-shadow sun.
- Tests: the GPU-lane queue mechanics are pinned headlessly in Editor.Core.Tests (dedupe,
  one-in-flight, publish + ready signal + light-lane PNG persist, negative on failure, disk
  round-trip without re-queueing, Reset drops a taken job's late result).

KNOWN GAP (the honest one): the stage itself has NO on-device pixel probe yet - the spec's
P2 calls for one, and it needs an editor-host harness (RenderSubsystem + SceneSubsystem +
resource manager + a cooked product) that no test target stands up today. That probe is the
immediate P2 follow-up, together with the remaining P2 generators (prefab / scene /
particle-effect - they ride the existing stage; prefab/scene can also use the
dependency-delegate trick below). In-editor visual verification PASSED (2026-08-30, user):
PaperKid mesh grid generates clean - no validation errors, correct tiles.

## P2 notes from the cross-engine research (Sedulous / Lumix / Traktor)

For the offscreen-render generators: keep exactly ONE GPU preview job in flight with
a decline-and-retry protocol (all three engines converge on this); render at 2-4x the
tile size and box-downscale for free AA (Lumix's 384->96 + compute reduce); use an
ORTHO camera with size = boundingRadius * 1.1 along (1,1,1) for rotation-invariant
framing (Lumix); a persistent hidden preview scene with pre-created entities beats
per-job scene setup (Sedulous's __Thumbnails__ world, itself modeled on Lumix);
materials can preview as a lit sphere OR cheaply as their base texture tinted
(Lumix); composite assets (prefab/scene) can delegate to the first dependency any
generator handles (Traktor's dependency-walk trick); fence-poll the readback and
convert on the CPU (both GPU engines). Pitfalls seen: size-less cache keys (Sedulous
serves any size from one file), unsynchronized readback flags (Lumix), orphan cache
files forever (Traktor - our lazy same-guid cleanup + safe-to-delete directory
answer this).

## Non-goals (for now)

Atlasing (individual textures are fine at these counts; revisit if the RAM LRU
thrashes), thumbnail EXPORT (thumbnails are editor-cache, never shipped),
animated previews, and background pre-generation sweeps (on-demand keeps the
cache proportional to what the user actually looks at).

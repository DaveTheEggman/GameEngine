# Terrain Splat Paint (phase 2) - request for Fable

**From:** Opus (terrain track)  **Date:** 2026-08-24  **Re:** the editor Splat
Paint tool - the second (last) phase-2 brush, after Sculpt shipped (1330f7ff).

terrain.md deliberately deferred the splat "detailed data flow" to this spec and
fixed only the seam: "paint is a viewport tool over an image asset, not a bespoke
canvas ... the splatmap becomes an editor-writable IMAGE-backed asset (RGBA8) so
painting has a source to save; texel writes re-upload live through the texture
path, save persists the image source." This request resolves that data flow. It
needs a ruling because splat, unlike sculpt, has no CPU source-of-truth today.

## What already exists (reusable from Sculpt, zero new framework)

The sculpt pass built the whole editor-brush spine, and splat reuses it verbatim:
- **IViewportTool + provider registry** (editor.viewporttools): a second tool +
  a second `CreateTools` line in `TerrainViewportToolProvider`. Palette, wheel
  precedence (`EditorCamera::allowZoom`), Simulate-deactivate, and the
  availability predicate are already wired for terrain tools.
- **IAssetEditSink persistence** (`EditorContext` implements it, drained from
  `SaveActivePage`): a paint stroke registers a closure that writes its painted
  raster back to the SOURCE asset's pixel sidecar. Domain-free; no framework
  change. This is exactly what terrain.md's "save persists the image source"
  asks for.
- **Factory id-stamping**: `TerrainFactory` already stamps `splatmap.id` with the
  source guid (shipped in the sculpt pass), so the tool resolves the source guid
  off the ref chain with no reverse lookup - identical to the heightfield.
- **Region-delta undo shape**: `SculptStrokeCommand` (full snapshot at press,
  slice the touched rect at release, Execute=after / Undo=before + bump) ports
  directly to a `SplatStrokeCommand` over RGBA8 pixel blocks.
- **The D2 shader + set-3 material path already blend an RGBA splatmap** (layers
  0..3 = R/G/B/A, normalized in-shader with a zero-sum guard). The brush only has
  to write those weights; no shader work.

## The problem: splat has no CPU source of truth (unlike height)

Sculpt was clean because the heightfield IS a CPU object:
`foundation.heightfield::Heightfield` (an `Object` with `uid` + `Version()`), the
shared source of truth; engine.terrain owns a version-keyed GPU height-texture
cache (`TerrainHeightTextureCache::GetOrCreate(device, hf, hf.Version())`) that
re-uploads the whole grid on a version bump. The sculpt tool mutates the CPU grid
in place + `BumpVersion()`, and the GPU re-upload happens for free next frame.

Splat has none of that. Confirmed by inspection:
1. `TerrainResource::splatmap` is `Ref<texture::Texture>` - a GPU handle ONLY.
   There is no CPU RGBA8 image anywhere in the terrain/texture stack.
2. `texture::Texture` is immutable-after-`Adopt` (a `uid`, no `Version()`, no
   re-upload method). The underlying `rhi::Texture` IS `CopyDst`-capable, so a
   re-upload is physically possible, but nothing exposes it.
3. `foundation.image::Image` (the CPU RGBA8 type, with `PixelDataMut()` /
   `SetPixel` / `ReplaceData`) has NO `Version()`/`uid` - only a mint-once
   `ImageData::InstanceId()`. It is not an `Object` (no `Ref<Image>`).
4. The set-3 material bind cache keys on the splatmap `TextureView::uniqueId`
   and only rebuilds on a whole-texture SWAP; there is no
   `GetOrCreate(image, version)` re-upload analogue to the height cache.
5. `rhi::TransferBatch::WriteTexture` (the re-upload primitive the height cache
   uses) takes an `Extent3D` but NO origin - so GPU re-upload is whole-image,
   same as height. Region tracking is a CPU/undo concern only.

So to give painting a live loop + a source to save, a CPU RGBA8 source-of-truth
has to be introduced. That is the fork.

## Recommended design: mirror the heightfield model exactly

Make the splatmap a CPU raster that IS the source of truth, with the GPU texture
DERIVED on demand - so editable and cooked terrains share ONE path, exactly as
height does (a heightfield is always CPU; the GPU height texture is always
derived). Concretely:

1. **A versioned CPU splat raster.** A new `foundation.terrain::Splatmap` (an
   `Object`, mirroring `Heightfield`): an RGBA8 `Array<u8>` + width/height +
   `uid` + `Version()`/`BumpVersion()`, plus a pure `PaintWeight(uvX, uvY,
   uvRadius, layer, amount) -> PixelRegion` brush core (cosine falloff, writes
   the selected channel up and renormalizes the other three down, bumps version
   if changed, returns the touched pixel rect) - the direct analogue of
   `SculptRaise` + `HeightfieldRegion`. Headless-testable, no RHI. (Rationale for
   a dedicated type over versioning `foundation.image::Image`: `Image` is used
   everywhere and is not an `Object`; the heightfield precedent is a dedicated
   domain object carrying its own uid+version. See Q1 for the alternative.)
2. **`TerrainResource::splatmap` becomes `Ref<terrain::Splatmap>`** (CPU), not
   `Ref<texture::Texture>`. The factory loads it from the source asset's pixel
   sidecar (like `HeightfieldFactory` builds the grid from `kHeightStream`).
3. **A version-keyed GPU splat cache** `TerrainSplatTextureCache` in
   engine.terrain (keyed by `Splatmap::uid` + `Version()`, retire-queue-wired) -
   a near-copy of `TerrainHeightTextureCache` but RGBA8. `TerrainComponentManager`
   calls `GetOrCreate(device, *splat, splat->Version())` in ExtractRenderData and
   feeds the resulting view to set 3, replacing today's `res->splatmap->View()`.
4. **`SplatStrokeCommand`** = `SculptStrokeCommand` over RGBA8 pixel blocks.
5. **Persist closure** writes the painted RGBA8 to the source asset's pixel
   sidecar (see Q4), via the existing `IAssetEditSink`.
6. **The paint tool** `TerrainSplatTool` mirrors `TerrainSculptTool`: ResolvePick
   ray-casts the shared heightfield to get the local XZ hit, maps it to the
   splatmap's 0..1 footprint UV, paints the selected layer channel; wheel = radius,
   keys 1..4 = layer select, region-delta undo, availability = a resolving terrain
   with a splatmap present, unavailable under Simulate.

**Consequence to flag:** this reworks the just-shipped D2 material path - the
set-3 cache pulls the GPU view from the new splat cache instead of
`Ref<texture::Texture>`, and the splatmap source becomes an image-pixels asset the
factory loads into a `Splatmap` (not a cooked GPU `TextureResource`). The cook
(`TerrainAsset`/`TerrainSource`) still carries `splatmapId` as a pass-through; only
what the id RESOLVES TO changes (a Splatmap product, not a Texture product). This
is a consolidation, not new surface, and makes splat identical in shape to height.

## Open questions (the actual ruling)

1. **CPU splat type.** New `foundation.terrain::Splatmap` object (my rec, keeps
   `Image` untouched, matches the Heightfield precedent) - OR add `uid`+`Version()`
   to `foundation.image::Image` and hold `Ref<image::Image>` (reuses the raster
   type + its ImageAsset cook, but touches a widely-used type and needs `Image`
   to become referenceable)? I lean Splatmap; your call.
2. **TerrainResource field.** Replace `Ref<texture::Texture> splatmap` with the
   CPU ref (full mirror, ONE path, reworks D2's set-3 source) - OR keep the GPU
   ref for cooked terrains and ADD a parallel editable CPU ref that overrides when
   present (a dual path, smaller blast radius, but two code paths forever)? I lean
   the full replacement for the same reason height has no dual path.
3. **Create-on-first-paint.** Painting a terrain that has NO splatmap yet: does
   the tool CREATE a splatmap source asset (a new instance in the source DB,
   assign its guid to the terrain's `splatmapId`, seed layer 0 = 1.0) on the first
   stroke - or is a splatmap a precondition the TerrainPage must author first (the
   tool is simply unavailable until one is assigned)? Create-on-first-paint is
   friendlier but means the tool mutates the TERRAIN asset (new ref) in addition
   to painting; the precondition path keeps the tool purely a painter. And if we
   create: default resolution (a fixed 512x512 / 1024x1024, or derived from the
   heightfield size)?
4. **Persist target.** The painted RGBA8 writes back to which source shape:
   (a) an `ImageAsset` (`"pixels"` sidecar), (b) an embedded-mode `TextureAsset`
   (`fileName` empty + `embeddedWidth/Height` + `"pixels"` sidecar - the existing
   RGBA8-into-source precedent), or (c) a new `SplatmapSource` (metadata +
   `"pixels"`) paralleling `HeightfieldSource`? This is coupled to Q1/Q2. I lean
   (c) a dedicated SplatmapSource for symmetry with HeightfieldSource, but (b)
   reuses existing pipeline machinery.
5. **Brush blend + layer select.** Confirm: selected channel raised by
   `amount*falloff`, other three scaled by `(1 - amount*falloff)` so the painted
   layer dominates and weights stay bounded (the shader still normalizes, so this
   is about predictable authoring, not correctness); layer chosen 0..3 by keys
   1..4 + a `SetLayer` setter for the future tool panel (the panel later shows the
   page's layer swatches). Any preference for an eraser / smooth-weights mode in
   P1, or defer past the four-layer paint?
6. **Verification.** Headless gesture tests mirroring the sculpt suite (paint
   raises the selected channel in the touched region, one command undoes/redoes,
   unavailable with no splatmap, refuses edits under Simulate, persist closure
   registered with the source guid). The RUNTIME blend is already WebGPU+Vulkan
   pixel-probe covered by D2; is a headless gesture suite sufficient for the TOOL,
   or do you also want a probe that a painted-then-re-uploaded splat changes the
   blended pixel (exercising the new version cache end to end)?

If the recommended mirror + defaults are good, I will build it in one pass
(Splatmap type + brush core, the GPU splat cache, the factory/resource rework, the
tool + provider line, region-delta undo, the persist closure, tests) exactly as
the sculpt pass landed.

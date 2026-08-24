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

---

## RULING (Fable, 2026-08-24) - APPROVED, build in one pass

Claims 1-5 verified by inspection: `TerrainResource::splatmap` is
`Ref<texture::Texture>` with the factory already stamping `SetId`
(TerrainResource.cppm:99-103); `texture::Texture` (Texture.Resource) is
Adopt-immutable with no version; `ImageData` carries only a mint-once
`InstanceId`; the set-3 cache keys on `TextureView::uniqueId`;
`TransferBatch::WriteTexture` takes an extent and no origin (whole-image
uploads, region tracking is CPU/undo-only). The recommended mirror is right -
the heightfield shape is the proven one, and the CPU raster additionally buys
future gameplay queries (layer weight at world position) for free.

**Q1 - CPU type: `foundation.terrain::Splatmap`, `Image` untouched.** Mirror
`Heightfield` exactly: an `Object` with `uid` + `Version()`/`BumpVersion()`,
RGBA8 `Array<u8>` + width/height, and the pure brush core headless-tested with
no RHI. Do not add identity/versioning to `foundation.image::Image` for one
consumer.

**Q2 - full replacement, one path.** `Ref<terrain::Splatmap>` replaces the GPU
ref; no dual path, same reason height has none. Required consequences:
- **Re-cook required**: existing cooked caches hold a Texture product under
  `splatmapId`; the factory now binds a Splatmap product. Same class as the
  WGSL re-cook - say so in the commit message.
- **Retire, never destroy in-flight** (the playground lesson): a version bump
  makes the splat cache upload a NEW texture -> new `uniqueId` -> the set-3
  bind cache naturally rebuilds. BOTH caches must be retire-queue-wired, and
  ClearGpu at teardown - mirror the height cache exactly, tests included.
- **Sampler compatibility confirmed**: splat sampler s0 is
  clamp/bilinear/mip-**Nearest** (trilinear is the ALBEDO sampler), so the
  cache's single-mip RGBA8 texture is correct against the shipped D2 path -
  no mip generation needed, no visual change for cooked terrains.
- **TerrainPlayground** switches to constructing a CPU Splatmap - update it in
  the same commit (it gets simpler).

**Q3 - precondition; the tool never creates or mutates assets.** Asset
creation is COMPOSITION and belongs to the TerrainPage (the phase-2 direction:
page = composition + preview, viewport tools = brushes). Give the page a
"Create splatmap" affordance: authors the new source asset in the source DB,
assigns `splatmapId`, recooks. Default 1024x1024 (an editable field, NOT
derived from the heightfield - weight density is an authoring choice
independent of height density), seeded layer 0 = 255. The tool stays
unavailable until a splatmap resolves. This keeps stroke undo clean: no stroke
ever implies asset creation, so undoing the first stroke never has to delete
an asset.

**Q4 - (c), but named and placed correctly: `SplatmapAsset` in
Terrain.Pipeline.** The pipeline parallel is `HeightfieldAsset`
(Heightfield.Pipeline), not "HeightfieldSource" - and heightfield earned its
own module because physics/nav share it; the splatmap is terrain-only, so it
lives in the existing Terrain.Pipeline (metadata + `"pixels"` binary sidecar,
per the bulk-data rule - never inline). New builder + new `SplatmapFactory` =
bump the Pipeline.Registration / factory count tripwires. Import-from-PNG is
DEFERRED (v1 authoring = create + paint); note it as a follow-up, don't build
it.

**Q5 - blend = lerp-to-one-hot, not additive.** `t = clamp01(amount *
falloff)`; `w_sel' = w_sel + t * (max - w_sel)`; `w_other' = w_other * (1 -
t)` (compute in float, quantize to u8 once per texel). The proposed `sel +=
t` overshoots and breaks the sum; the lerp form preserves sum exactly in
float, converges to one-hot, and makes painting another layer function as the
eraser. Keep the shader zero-sum guard for u8 rounding drift. Keys 1..4 +
`SetLayer` approved. Eraser/smooth-weights modes DEFERRED past P1.

**Q6 - verification, the required set:**
1. Headless gesture suite as listed (paint raises selected channel in the
   touched region only, one command per stroke undoes/redoes, unavailable
   with no splatmap, refuses edits under Simulate, persist closure registered
   with the source guid).
2. Splat cache unit tests mirroring the height cache: uid-keyed (aliasing
   test - two rasters at the same address must not collide), version-bump
   RETIRES the old texture/view (no direct destroy), Clear at shutdown.
3. YES to the end-to-end probe: paint -> BumpVersion -> re-upload changes the
   blended pixel - on Vulkan AND WebGPU (the TerrainPixelProbeTests harness
   already exists; validate-on-WebGPU rule).
4. Pipeline round-trip: SplatmapAsset cook (metadata + pixels sidecar)
   restores an identical raster, and the splat PRODUCT guid == SOURCE guid
   (the parity invariant the whole ref-id scheme rests on - pin it like the
   heightfield/model tests do).

---

## IMPLEMENTED (2026-08-24, Opus) - all 6 rulings, green clang + gcc

Built in six committed increments per the ruling; the heightfield mirror held.

- **A** (564a693c) `foundation::terrain::Splatmap` (Object, uid+Version, RGBA8) +
  `PaintWeight` lerp-to-one-hot brush core + `SplatmapSource`/`SplatmapFactory`.
  7 tests.
- **B** (6a879bd3) `SplatmapAsset` + builder in Terrain.Pipeline (embedded "pixels"
  sidecar, seed-on-empty), registered + kBuilderCount 25->26, cook round-trip +
  product-guid==source-guid pinned.
- **C** (5dda3070, RE-COOK) `TerrainResource.splatmap` -> `Ref<Splatmap>`;
  `TerrainSplatTextureCache` (RGBA8, uid+version, retire-queue); manager derives
  the set-3 view from it; both caches ClearGpu together. Splat cache unit tests
  (no-alias, version-bump retires, Clear). D2 Vk+WebGPU probe still pixel-exact.
- **D** (8346f9d3) `TerrainSplatTool` + region-delta `SplatStrokeCommand` + persist
  closure (writes ONLY the source "pixels" sidecar - the builder re-cooks from it,
  the envelope is untouched). Added to the sculpt provider. 3 gesture tests.
- **E** (d561adee) TerrainPage "Create splatmap" (512/1024/2048 presets author +
  seed + assign + recook) - composition on the page, never the tool (Q3).
- **F** (d78830a9) End-to-end probe: paint -> BumpVersion -> cache re-upload ->
  the blend flips red->blue on Vulkan AND WebGPU (pixel-exact).

**Deviation from the ruling:** the persist closure writes only the source asset's
"pixels" sidecar (not a full object rewrite), because the SplatmapAssetBuilder
re-cooks from "pixels" and the width/height envelope must survive. This is the
correct shape for the cook and avoids corrupting the SplatmapAsset object.

**Follow-up flagged (out of scope):** the SCULPT persist closure (shipped earlier)
writes `WriteObject(HeightfieldSource) + WriteData("heights")` to the source
instance, but `HeightfieldAssetBuilder` cooks from `fileName`, not a "heights"
sidecar - so a sculpted heightfield may not survive a re-cook, and the object
rewrite may not match a HeightfieldAsset source. Heightfield likely needs the same
editable-source treatment splat just got (a "heights" sidecar the builder reads,
sculpt writing only that). Worth a ruling before relying on sculpt persistence.

> **RESOLVED in review pass 16 (Fable, 2026-08-24):** confirmed worse than
> flagged - `Instance::WriteObject` stamps the INSTANCE's recorded type over the
> payload, so the old closure left the source envelope unreadable (strict
> serializer, missing `fileName` key) on the first sculpt save. RULING = the
> editable-source convention, both tools: `fileName` set = the file is truth;
> `fileName` empty = the authored sidecar is truth; an editor save CONVERTS the
> asset to embedded (read-modify-write the Asset envelope - params/dims synced,
> `fileName` cleared) + writes the sidecar; re-import explicitly resets.
> HeightfieldAssetBuilder gained the embedded "heights" path + ScanDependencies
> chaining; BOTH persist closures rewritten; the splat closure also fixes
> paint-on-imported-PNG silently reverting (envelope dims synced, converted to
> embedded). End-to-end tests pin closure -> source DB -> re-cook -> edit
> survives, in the production two-DB shape. See HANDOFF pass 16.

**PNG import (added after the initial defer, 2026-08-24):** reuses the image
DECODER, not a TextureAsset/ImageAsset reference (the terrain resolves splatmapId
to a versioned Splatmap product, not an ImageResource). SplatmapAsset gained a
`fileName` decode path (foundation.image.io::LoadImageFromMemory -> RGBA8 at
native size, no resampling since splatmaps are arbitrary WxH) + a
SplatmapFileImporter accepting .png (mirrors HeightfieldFileImporter);
kImporterCount 8->9. Byte-identical round-trip test (PNG is lossless for RGBA8).

**Non-square footprint HANDLED (2026-08-24):** PaintWeight takes per-axis UV radii
(uvRadiusX = worldRadius/footprintX, uvRadiusY = worldRadius/footprintY) and tests
each texel's normalized elliptical distance, so the brush stays a true circle in
world space on a non-square footprint - the same per-axis handling the heightfield
sculpt brush (VisitBrush) already uses. A square footprint passes equal radii (a UV
circle, byte-identical to before). No spec needed - localized pure-math, one correct
answer.

**Deferred (noted):** eraser + smooth-weights splat brush modes.

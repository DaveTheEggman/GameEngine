# Terrain Splat: Unlimited Layers via Top-K Blending (+ explicit Base layer)

Status: APPROVED (Fable, 2026-08-25) with required amendments R1-R8 (see RULING at the bottom);
amendments folded into the body below. Building P0. Supersedes the P1 "one RGBA8 splatmap, 4
layers" model documented in terrain-splat-d2.md / terrain-splat-phase2.md.

## Motivation

The current splat system is hard-capped at 4 layers because it encodes layer weights in a single
RGBA8 splatmap (4 channels) and the renderer binds exactly 4 albedo slots (`TerrainRenderData::
kMaxLayers = 4`). Two problems surfaced:

1. The terrain asset page's `AddLayer()` does NOT enforce the cap, so authors can add a 5th+ layer
   that is silently dead: it is stored but can never be painted (no channel) or rendered (renderer
   stops at 4). The asset UI promises something the runtime does not honor.
2. Layer 0 pulls double duty. A fresh splatmap is `SeedLayer0()`-ed to `(255,0,0,0)` (layer 0 at
   full weight everywhere), so layer 0 is really the "what shows when nothing is painted" BASE, but
   it appears in the paint palette as if it were a normal paint slot. Assigning it a texture fills
   the whole terrain with no painting, which reads as a bug.

The 4-layer number is an implementation choice, not a graphics necessity. This spec replaces it with
the standard scalable model: an explicit base layer plus an unlimited paint palette, blended per
pixel by keeping only the top-K contributing layers at each texel (fixed per-pixel cost regardless
of palette size).

## Goals

- Unlimited paint layers per terrain (palette size bounded only by the index encoding: 256 with an
  8-bit index, extensible to 16-bit later).
- Constant per-pixel blend cost: exactly K albedo samples per pixel (K = 4), independent of palette
  size.
- Explicit, separate BASE layer that fills wherever painted weights do not sum to 1. The base is not
  in the paint palette and is not painted directly; erasing reveals it.
- Remove the "layer 0 is secretly the base" confusion end to end (data, tool, editor UI).
- Lossless migration of existing 4-layer terrains.

## Non-goals (deferred)

- More than K=4 layers blended AT A SINGLE texel (K is fixed; the palette is unlimited, the per-texel
  blend is 4). Raising K is a one-constant change later if needed.
- 16-bit palette indices (>256 layers). 8-bit is ample for now; note the extension point.
- Triplanar / height-blend / normal + roughness per layer. This spec keeps albedo + tileScale per
  layer, matching today's material; PBR-per-layer is a separate track.
- Bindless albedo binding. We use a Texture2DArray (portable); bindless is a possible future swap.

## Locked decisions (confirmed with the user)

- K = 4 (layers blended per pixel).
- Base layer is NOT paintable; you erase paint to reveal it.
- 8-bit palette index (up to 256 paint layers); extension to 16-bit is noted, not built.

## Design overview

Per texel, the terrain stores the top K = 4 (paintLayerIndex, weight) pairs, weights in [0,1] with
`sum(weights) <= 1`. The BASE layer implicitly owns the remainder `baseW = 1 - sum(weights)`. An
unpainted texel has all four weights 0 -> pure base.

Final shaded albedo per pixel:

    albedo = base.Sample(uv * baseTile) * baseW
           + Σ over k in 0..3:  paletteArray.Sample(uv * tile[idx[k]], idx[k]) * w[k]

`sum(weights) + baseW == 1` always, so the blend is a convex combination (no over/under-darkening).

## Data model

### foundation.terrain (Terrain.Resource)

Replace the single RGBA8 `Splatmap` with a top-K weight map. Two options; this spec picks (A):

- (A, chosen) `SplatWeights`: two rasters of equal WxH:
  - `index`  (**RGBA8Uint** - integer, NON-filterable): the 4 palette indices at this texel
    (0..255). Slot is "unused" iff its weight is 0 (index value is then don't-care; writers set it
    to 0). MUST be `Load`-only in the shader - see R1: a filtered sample would interpolate indices
    into garbage layers at every boundary. RGBA8Uint is non-filterable on WebGPU anyway, so Load-only
    is also the portable choice.
  - `weight` (RGBA8Unorm): the 4 quantized weights (0..255 -> 0..1). `baseW` is derived in-shader as
    `1 - dot(weight, 1)`.
  - Rationale: two fixed rasters are trivially GPU-samplable, headless-testable, and paint edits are
    a small per-texel array update. Memory is 2x the old splatmap (still tiny).
- (B, rejected for now) A packed R32/RG32 texture (index+weight interleaved). Denser but needs
  manual bit unpacking in the shader and the paint code; not worth it at K=4.

`SplatWeights` owns the paint math (the `PaintWeight` precedent), headless, no RHI:
- `Splatmap` -> `SplatWeights` (rename or replace; keep the WxH + versioned-pixels shape).
- `PaintTopK(SplatWeights&, uvX, uvY, uvRadiusX, uvRadiusY, paletteIndex, strength) -> SplatRegion`
  (see Paint algorithm below). Returns the dirtied texel rect for the region-delta undo, exactly as
  `PaintWeight` does today.
- `EraseTopK(SplatWeights&, uv..., strength)` fades all weights toward 0 (reveals base).
- No `SeedLayer0`: an all-zero `SplatWeights` is a valid "all base" surface by construction (the
  base is separate, so nothing needs seeding).

### TerrainResource / TerrainSource

`TerrainResource` today: `Array<Layer{ Ref<Texture> albedo; f32 tileScale; }> layers` + `splatmapId`.

New shape:
- `Layer base;` -- the base ground layer (albedo + tileScale). Always present (nil albedo -> the
  1x1 white dummy, same as today's absent-slot behavior).
- `Array<Layer> palette;` -- the paint layers, unbounded. `PaletteCount()` replaces `LayerCount()`.
- `Guid weightsId;` -- the `SplatWeights` product (was `splatmapId`).

`TerrainSource` mirrors it: `baseAlbedoId` + `baseTileScale`, `paletteAlbedoIds[]` +
`paletteTileScales[]`, `weightsId`. Bump `DataVersion` and gate deserialization (serializer-strict-
versioning rule); write a v(old)->v(new) upgrade (see Migration).

### Renderer (Engine.Terrain, TerrainRenderData + TerrainRenderer)

- `TerrainRenderData`: drop the fixed `albedoViews[4]` / `tileScales[4]`. Add:
  - `rhi::TextureView* baseAlbedoView;`
  - `rhi::TextureView* paletteArrayView;`  (a Texture2DArray of all palette albedos)
  - `rhi::TextureView* indexView; rhi::TextureView* weightView;` (the two SplatWeights rasters)
  - `rhi::Buffer* tileScaleBuffer;` (base tileScale + per-palette-layer tileScale; a small storage
    buffer indexed by palette index; base at a fixed slot).
  - `u32 paletteCount;`
- Bind group (set 3) becomes: indexMap (t0) + weightMap (t1) + baseAlbedo (t2) + paletteArray (t3) +
  tileScale buffer (t4/b) + the two samplers. Cache key = uniqueIds of all views + the buffer
  generation (bind-group-cache-versioning rule: never raw pointers).
- Texture2DArray requires uniform slice size/format. The cook resizes every palette albedo to a
  common authored size (default 1024x1024, mip-chained) and packs them into the array product. Base
  is a normal Texture2D (any size). Note the quality/memory implication: palette albedos are
  resampled to the common size at cook time. (Alternatives if this bites: a texture atlas, or
  bindless later; both out of scope here.)

### Terrain PS (splat blend)

Replace the current 4-fixed-slot blend with the top-K blend. Per R1 the index raster is integer and
Load-only, and the splat bilinear is done MANUALLY over the 2x2 texel neighborhood (never let a
sampler interpolate indices). HLSL sketch (stays inline per the leave-shader-source-inline rule):

    // Blend the base + top-K palette layers for ONE texel's stored slots.
    float3 BlendTexel(uint2 texel, float2 uv) {
        uint4 idx = indexMap.Load(int3(texel, 0));        // integer indices, no filtering
        float4 w  = weightMap.Load(int3(texel, 0)) / 255.0;
        float baseW = saturate(1.0 - dot(w, float4(1,1,1,1)));
        float3 c = baseAlbedo.Sample(albedoSampler, uv * baseTile).rgb * baseW;
        [unroll] for (int k = 0; k < 4; ++k) {
            if (w[k] <= 0) continue;                       // unused slot (typical texel: 1-2 used)
            c += paletteArray.Sample(albedoSampler,
                     float3(uv * tileScales[idx[k]], idx[k])).rgb * w[k];
        }
        return c;
    }
    // Manual bilinear over the 4 covering texels (weights/indices are per-texel, albedo is tiled).
    float2 t = uv * splatSize - 0.5;
    uint2  b = (uint2)floor(t);
    float2 f = frac(t);
    float3 albedo = lerp(lerp(BlendTexel(b + uint2(0,0), uv), BlendTexel(b + uint2(1,0), uv), f.x),
                         lerp(BlendTexel(b + uint2(0,1), uv), BlendTexel(b + uint2(1,1), uv), f.x), f.y);

Worst case is 4 * (K + 1) albedo taps, but the zero-slot skip makes the typical texel a few taps;
terrain has the headroom. If profiling ever bites, a point-index + bilinear-weight quality fallback
is the escape hatch, but v1 ships the correct blend. `paletteCount == 0` -> baseW clamps to 1 ->
pure base (covers "no weights authored yet"); keep the height-ramp fallback only when there is no
base albedo either. WebGPU: RGBA8Uint Loads + the array sampling are exactly where the naga/WGSL
path can diverge - validate the cook early (R1, R5).

## Paint tool (Editor.Terrain, TerrainSplatTool)

The tool selects a PALETTE index (0..PaletteCount-1), or the ERASER. Per dab, for each covered
texel, apply the top-K update with the falloff-scaled strength `t`:

Paint(paletteIndex L, strength t):
  1. If L is already one of the 4 slots at this texel: `slotOf(L)`.
     Else if a slot is free (weight 0): use it, set its index = L.
     Else (all 4 slots used by OTHER layers): unconditionally evict the MINIMUM-weight slot - set its
        index = L, weight = 0 - then proceed with the raise below (R8, no thrash guard in v1). The
        evicted weight falls to base; the error is bounded by the smallest weight, the least visible
        by definition. Painting is falloff-shaped, so the brush center converges to L immediately and
        rim churn is sub-quantum.
  2. Fade the OTHER three slots: `w[j] *= (1 - t)` for j != slotOf(L). (This also fades baseW, since
     baseW = 1 - sum; painting toward L reduces base.)
  3. Raise L: `w[slotOf(L)] = w[slotOf(L)] + t * (1 - w[slotOf(L)])`.
  4. Result stays valid: each `w in [0,1]`, `sum <= 1`, base = remainder. Fully painting L (t -> 1
     repeatedly) drives that texel to `w[L] = 1`, others 0, base 0 -> pure L.

Erase(strength t): `w[k] *= (1 - t)` for all k. Base = 1 - sum rises toward 1. When a slot's weight
crosses ~0 it is freed (index cleared) so the top-K stays meaningful.

Undo/redo: unchanged model - one region-delta command per stroke over BOTH rasters (index + weight)
snapshotting the touched rectangle before/after (mirrors today's single-raster stroke command). The
stroke keeps the `SplatWeights` alive and re-uploads the dirty rect to the two GPU textures.

Hotkeys: number keys select palette slots 0..8 (was 0..3); an eraser key (e.g. `0` or a modifier).
The brush-ring tint uses the selected palette layer's average albedo or a per-index color.

## Editor asset page (Editor.Terrain, TerrainEditorPage)

- Split the layer list into TWO sections:
  - "Base layer": one albedo picker + tile-scale field. Cannot be removed or reordered.
  - "Paint layers": the unbounded palette list (albedo + tileScale per row) with Add / Remove.
    `AddLayer()` no longer needs a cap. Arbitrary reorder stays FORBIDDEN in v1 (R6). REMOVE remaps
    the index raster (decrement indices above the removed one; zero any slot referencing it - its
    freed weight falls to base by construction) and edits the asset as ONE undo entry: a full-raster
    snapshot bundled with the asset edit (remove is rare, so no region-delta - R6).
- Editing rewrites the source, pushes the existing merge-keyed undo, rebinds + rebuilds.

## Splat picker (Editor.Terrain, ToolPanelsImpl - the FloatingPanel content)

- Show the BASE layer separately and clearly (a labeled "Base" swatch, not selectable for painting).
- Show the PALETTE as the paint choices: thumbnails per palette layer (reuse the LayerSwatch +
  ThumbnailService already built), plus an ERASER button. Selecting a swatch sets the tool's palette
  index; the eraser sets erase mode.
- The picker is unbounded now: a horizontal wrap/scroll row of swatches (the FloatingPanel already
  clips + has a min width; a wrapping FlexLayout or a scroll row handles many layers).
- Tooltip: the layer's albedo asset name (now that the EditorContext / source DB is threaded in).

## Migration (existing 4-layer terrains)

Old on-disk terrain: RGBA8 splatmap `(w0,w1,w2,w3)` per texel, `layers[0..3]`. The OLD shader
NORMALIZED the four channels in-shader, so a texel whose stored sum drifted from 255 still rendered
exact ratios. The new model derives base from the deficit, so migrating raw `(w1,w2,w3)` would shift
visuals wherever `sum != 255`. Convert at cook (guarded by the DataVersion bump), renormalizing by
the old sum (R2):
- `base` = old `layers[0]`; `palette` = old `layers[1..3]` (in order) -> palette indices 0,1,2.
- Per texel, `s = w0+w1+w2+w3`. If `s > 0`: new palette weights `wi' = wi / s` for i in 1..3 (so the
  derived `baseW = 1 - (w1'+w2'+w3') = w0/s` equals the old layer-0 SHARE); quantize to 0..255.
  `index = (0, 1, 2, 0)`, unused 4th slot weight 0. If `s == 0`: all-base (empty slots). Lossless
  and visually exact vs the old normalized blend (all old data has <= 3 non-base layers, fits K=4).
- A terrain with no splatmap -> empty `SplatWeights` (all base). An old `SeedLayer0` raster
  (`(255,0,0,0)`) -> all-base after conversion, correct.

Provide the converter in the cook builder; no runtime migration path needed (assets re-cook). Pin
VISUAL PARITY in the converter test: old-normalized blend == new blend per texel within quantum.

## Phasing

- P0 - Data + headless paint math: `SplatWeights` (index+weight rasters) + `PaintTopK`/`EraseTopK`
  + region-delta, all headless-tested. TerrainResource/Source base+palette fields + DataVersion +
  the renormalizing migration converter (R2). The editable-source TWO-sidecar convention (R3:
  "indices" alongside "pixels", asset DataVersion bump, ScanDependencies chains both, paint persist
  writes both) + the pass-16 persist tests extended to two rasters. No rendering yet. Green +
  reviewed before P1.
- P1 - Renderer: Texture2DArray palette (cook resize/pack to a common size), the new set-3 bind
  group + tileScale buffer, the top-K PS blend with R1's integer-index Load + manual 2x2 bilinear
  (fold R1 into the design BEFORE any shader work), base+palette bind + the R4 cache/retire for the
  three GPU objects. Verify a hand-authored multi-layer terrain renders.
- P2 - Paint tool: palette-index selection + eraser, two-raster stroke undo, GPU dirty-rect
  re-upload of both textures, hotkeys, ring tint.
- P3 - Editor UX: asset page base/paint split (uncapped palette + index remap on remove), splat
  picker base-vs-palette + eraser + unbounded swatch row + name tooltips.
- P4 - Polish + migration verification on a real >4-layer terrain; docs (this file -> IMPLEMENTED),
  retire the `kMaxLayers = 4` references.

## Testing

- Headless (foundation.terrain): `PaintTopK` convexity (`sum(weights) + baseW == 1` within quantum),
  eviction picks the min slot, full-paint drives to one-hot, erase drives to all-base, dirty-rect
  bounds. Migration converter round-trips a synthetic old splatmap to the expected index/weight.
- Editor.Terrain.Tests: the splat tool paints the SELECTED palette index into the correct slot;
  eraser reveals base; one command per stroke undoes/redoes both rasters. PanelProvider builds the
  base+palette picker; the asset page adds/removes palette layers past 4 and remaps indices.
- Renderer (R5, the established pixel-probe bar - not a golden/non-fallback assertion): a >4-layer
  terrain probe on Vulkan AND WebGPU with pixel-exact parity (extends TerrainPixelProbeTests -
  proves the cap is gone AND the WGSL path agrees on RGBA8Uint Loads + array sampling); the existing
  paint->re-upload probe updated to the two-raster + palette-index model; the type-confusion
  regression's FillTerrainRenderData extended to the new fields (must keep compiling); plus the
  splat-cache aliasing/retire/Clear suite for the three GPU objects (R4).

## Settled by the ruling (were open questions)

- Palette reorder: FORBIDDEN in v1 (add/remove only); remove remaps indices as one full-snapshot
  undo entry (R6). Reorder can come later if artists ask.
- Common palette-albedo size: a terrain-asset authoring setting, default 1024 (ruling notes). The
  cook resamples every palette albedo to it and chains their PIXELS (reads) so an albedo edit
  re-cooks the array.
- Eraser: a separate eraser button in the picker + mode toggle on the tool (not a palette entry).
- tileScale storage (R7): a 256-entry std140 UBO (16B stride -> 4KB, fine) or a read-only fragment
  storage buffer - builder's choice; its GENERATION is part of the set-3 bind cache key either way.
- K=4 + 8-bit index == WebGPU minimum `maxTextureArrayLayers` (256) EXACTLY; do not widen the index
  without checking that limit (ruling notes).

## Touch list (for the build)

- foundation.terrain: `SplatWeights` (was Splatmap) + `PaintTopK`/`EraseTopK`; TerrainResource
  base/palette; TerrainSource fields + DataVersion + migration.
- Terrain.Pipeline: SplatmapAsset KEEPS its type but gains a second sidecar stream "indices"
  alongside "pixels"(weights) + an asset DataVersion bump (R3); the renormalizing migration
  converter (R2); palette-array pack/resize to the common size, chaining every palette albedo's
  PIXELS (reads, so an albedo edit re-cooks the array); builder ProductType unchanged conventions
  (builder = source type, factory = runtime).
- Engine.Terrain: TerrainRenderData fields; TerrainRenderer set-3 bind group + tileScale buffer +
  Texture2DArray; the PS top-K blend; drop kMaxLayers.
- Editor.Terrain: TerrainSplatTool palette index + eraser + two-raster stroke; TerrainEditorPage
  base/paint split + uncapped palette + index remap; ToolPanelsImpl base/palette picker + eraser.
- Tests across foundation.terrain + Editor.Terrain + a renderer check.

Related: [terrain.md], [terrain-splat-d2.md], [terrain-splat-phase2.md].

---

## RULING (Fable, 2026-08-25) - APPROVED with required amendments

The shape is right: explicit base + unbounded palette + fixed-K per-texel blend is the standard
scalable model, the paint math is verified correct (step 2+3 give `sum' = lerp(sum, 1, t)` - the
convex invariant holds and converges), the motivation is real (the uncapped `AddLayer()` promise
and the layer-0 double duty are genuine defects), and the migration is the right direction. The
phasing (headless P0 first, reviewed before rendering) matches the house pattern. Amendments:

**R1 - THE index-map filtering flaw (must fix before P1; the sketch as written is wrong).**
`indexMap.Sample(splatSampler, ...)` with the existing BILINEAR splat sampler interpolates
PALETTE INDICES: halfway between index 3 and index 7 samples as index 5 - a garbage layer at
every boundary between texels that reference different layers, which after P2 painting is
everywhere. Required shape:
- The index raster is **RGBA8Uint** (integer format, `Load` only - no sampler, no rounding
  round-trip through unorm). Weights stay RGBA8Unorm.
- The blend does **manual bilinear over the 2x2 texel neighborhood**: `Load` index+weight at the
  four corners, evaluate the base+top-K blend PER CORNER (skip zero-weight slots - the typical
  texel uses 1-2), and lerp the four results with the bilinear fractions. Worst case is 4x(K+1)
  albedo taps but the zero-slot skip makes the typical cost a few taps; terrain has headroom.
  If profiling ever bites, a point-sampled-index + bilinear-weight QUALITY fallback is the
  escape hatch - but v1 ships the correct blend.
- Consequence: the bilinear `SplatSampler` may disappear from set 3 entirely (both rasters are
  Load-based); the albedo sampler is unchanged. Note WebGPU: RGBA8Uint is non-filterable, which
  is exactly why Load-only is also the portable choice; validate the WGSL cook (naga) early.

**R2 - Migration must renormalize by the old sum.** The OLD shader normalized the four weights
in-shader (the zero-sum guard aside), so a texel whose stored sum drifted from 255 still
rendered exact ratios. The new model derives base from the DEFICIT - migrating raw
`(w1,w2,w3)` changes visuals wherever sum != 255. Convert as: if `sum > 0`, write
`weight_i' = round(255 * wi / sum * (1 - w0/sum))`... plainly: new palette weights =
`wi/sum` renormalized so the new baseW equals the old layer-0 SHARE `w0/sum`. Pin visual
parity in the converter test (old-normalized blend == new blend per texel within quantum).

**R3 - The editable-source convention now covers TWO sidecars.** Keep the `SplatmapAsset` TYPE
(no new asset type, no envelope migration): it gains a second stream - `"indices"` alongside
`"pixels"`(weights) - with an asset DataVersion bump. Restate the whole convention: fileName
set = file is truth (PNG import maps to... an imported RGBA8 image becomes single-layer
weights? Simplest: import = palette-0 weights from luminance, or drop import support for
weights v1 - builder's choice, document it); fileName empty = BOTH sidecars are truth;
ScanDependencies chains BOTH streams in embedded mode; the paint persist closure read-modify-
writes the envelope (dims synced, fileName cleared) + writes BOTH sidecars; re-import resets.
The pass-16 end-to-end persist tests extend to the two-raster shape.

**R4 - Cache + retire rules restated for the new GPU set.** The weights cache becomes THREE
GPU objects per terrain (index tex, weight tex, palette Texture2DArray) + the tileScale
buffer: ALL keyed by uid+version/generation (never pointers), ALL retire-queued on rebuild
(paint bumps re-upload both rasters; palette add/remove rebuilds the ARRAY - retire the old
one, in-flight frames still sample it), Clear at teardown, aliasing + retire + Clear unit
tests mirroring the existing splat-cache suite. The set-3 bind cache keys on every view's
uniqueId + the buffer generation, as specced - good.

**R5 - Verification bar: the established pixel-probe standard, not "a golden or non-fallback
assertion".** Required: a >4-layer terrain probe on Vulkan AND WebGPU, pixel-exact parity
(extends TerrainPixelProbeTests; proves the cap is gone AND the WGSL path agrees - RGBA8Uint
Loads and the array sampling are exactly the kind of thing the naga path diverges on); the
existing paint->re-upload probe updated to the two-raster + palette-index model; plus the
headless suite as specced. Also extend the type-confusion regression's FillTerrainRenderData
to the new fields (it must keep compiling as the layout changes).

**R6 - Palette remove = ONE undo entry covering everything it touches.** Removal remaps the
index raster (decrement indices above the removed layer, zero slots referencing it - the freed
weight falls to base by construction) and edits the asset. Rare operation: a FULL-raster
snapshot command bundled with the asset edit in one undo entry is fine; do not build a
region-delta for it. Arbitrary reorder stays forbidden (as the spec leans).

**R7 - tileScale storage: either works; mind std140.** A 256-entry UBO is the simplest
portable choice but std140 pads scalar arrays to 16B stride (4KB - still fine); a read-only
storage buffer in the fragment stage is also WebGPU-valid. Builder's choice; whichever is
picked, the WebGPU probe covers it. The buffer's GENERATION (bumped on any tileScale/palette
edit) is part of the bind cache key.

**R8 - Eviction stated plainly (the spec's step 1 parenthetical contradicts itself):** when
all four slots hold OTHER layers, unconditionally evict the minimum-weight slot (set index=L,
weight=0, then proceed with the normal raise). The evicted weight falls to base; the error is
bounded by the smallest weight, which is the least visible by definition. No thrash guard in
v1 - painting is falloff-shaped, so the center converges to L immediately and the rim churn is
sub-quantum. Headless test pins min-slot eviction.

**Notes, no change required:** K=4 + 8-bit indices matches WebGPU's minimum
maxTextureArrayLayers (256) exactly - do not raise the index width without checking that
limit. The cook's palette-array product makes the terrain recipe depend on every palette
albedo's PIXELS - chain them (reads, not references) so an albedo edit re-cooks the array.
Number-key hotkeys are per-tool (tools are modal) - no conflict with sculpt's 1-4 modes.
Palette-albedo common size as an authoring setting: yes, default 1024, on the terrain asset.

Build order approved as phased (P0 -> P4), with R1 folded into P1's design before any shader
work and R2/R3 folded into P0.

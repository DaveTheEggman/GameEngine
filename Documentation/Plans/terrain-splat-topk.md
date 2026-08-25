# Terrain Splat: Unlimited Layers via Top-K Blending (+ explicit Base layer)

Status: SPEC / proposed (awaiting review). Supersedes the P1 "one RGBA8 splatmap, 4 layers"
model documented in terrain-splat-d2.md / terrain-splat-phase2.md.

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

- (A, chosen) `SplatWeights`: two RGBA8 rasters of equal WxH:
  - `index`  (RGBA8): the 4 palette indices at this texel (0..255). Slot is "unused" iff its weight
    is 0 (index value is then don't-care; writers set it to 0).
  - `weight` (RGBA8): the 4 quantized weights (0..255 -> 0..1). `baseW` is derived in-shader as
    `1 - dot(weight, 1)`.
  - Rationale: two fixed RGBA8 textures are trivially GPU-samplable, headless-testable, and paint
    edits are a small per-texel array update. Memory is 2x the old splatmap (still tiny).
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

Replace the current 4-fixed-slot blend with the top-K blend (HLSL sketch; stays inline per the
leave-shader-source-inline rule):

    float4 idxN = indexMap.Sample(splatSampler, uv);      // 0..1 per channel
    float4 w    = weightMap.Sample(splatSampler, uv);     // 0..1 per channel
    float  baseW = saturate(1.0 - dot(w, float4(1,1,1,1)));

    float3 albedo = baseAlbedo.Sample(albedoSampler, uv * baseTile).rgb * baseW;
    [unroll] for (int k = 0; k < 4; ++k) {
        if (w[k] <= 0) continue;                          // unused slot
        uint layer = (uint)round(idxN[k] * 255.0);
        float tile = tileScales[layer];
        albedo += paletteArray.Sample(albedoSampler, float3(uv * tile, layer)).rgb * w[k];
    }

`layerCount == 0` (no palette) -> baseW clamps to 1 -> pure base, which also covers the "no weights
authored yet" case. Keep the existing height-ramp fallback only when there is no base albedo either.

## Paint tool (Editor.Terrain, TerrainSplatTool)

The tool selects a PALETTE index (0..PaletteCount-1), or the ERASER. Per dab, for each covered
texel, apply the top-K update with the falloff-scaled strength `t`:

Paint(paletteIndex L, strength t):
  1. If L is already one of the 4 slots at this texel: `slotOf(L)`.
     Else if a slot is free (weight 0): use it, set its index = L.
     Else (all 4 slots used by other layers): find the slot with the SMALLEST weight `m`. Only evict
        it if L would end up stronger than `m` after this dab (avoid thrashing); i.e. proceed if
        `t*(1 - 0) > m` is not required - simpler: evict the min slot, set its index = L, weight 0.
        (Eviction error is bounded by the smallest weight, which is by definition the least visible.)
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
  - "Paint layers": the unbounded palette list (albedo + tileScale per row) with Add / Remove /
    reorder. `AddLayer()` no longer needs a cap. Reordering a palette layer must remap the weight
    map's stored indices (or forbid reorder and only allow add/remove-at-end to keep indices stable;
    simplest: remove-swaps-with-last is disallowed - removal renumbers, so the tool remaps the
    index raster on remove; spec this precisely in the build).
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

Old on-disk terrain: RGBA8 splatmap `(w0,w1,w2,w3)` per texel, `layers[0..3]`, `sum == 1` by the old
seed/paint invariant. Convert at cook (guarded by the DataVersion bump):
- `base` = old `layers[0]`.
- `palette` = old `layers[1..3]` (in order) -> palette indices 0,1,2.
- weight map per texel: `weight = (w1, w2, w3, 0)`, `index = (0, 1, 2, 0)`; base weight is then
  `1 - (w1+w2+w3) = w0`. Lossless (all old data has <= 3 non-base layers, fits K=4).
- A terrain with no splatmap -> empty `SplatWeights` (all base). A terrain with an old `SeedLayer0`
  raster -> all-base after conversion (w1=w2=w3=0), correct.

Provide the converter in the cook builder; no runtime migration path needed (assets re-cook).

## Phasing

- P0 - Data + headless paint math: `SplatWeights` (index+weight rasters) + `PaintTopK`/`EraseTopK`
  + region-delta, all headless-tested. TerrainResource/Source base+palette fields + DataVersion +
  migration converter. No rendering yet (tests only). Green + reviewed before P1.
- P1 - Renderer: Texture2DArray palette (cook resize/pack), the new set-3 bind group + tileScale
  buffer, the top-K PS blend, base+palette bind. Verify a hand-authored multi-layer terrain renders.
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
- Renderer: a small golden or a "renders non-fallback" assertion for a 6-layer terrain (the whole
  point - proves the cap is gone).

## Open questions / to settle in the build

- Palette reorder vs index stability: recommend forbidding arbitrary reorder in P3 (add/remove only),
  and on REMOVE, remap the weight raster indices (decrement indices above the removed one, zero any
  slot that referenced it). Reorder can come later if artists ask.
- Common palette-albedo size for the array (default 1024; expose as a terrain authoring setting?).
- Eraser as a palette entry vs a separate mode toggle in the picker (spec leans: separate eraser
  button + the existing mode-selection idiom).

## Touch list (for the build)

- foundation.terrain: `SplatWeights` (was Splatmap) + `PaintTopK`/`EraseTopK`; TerrainResource
  base/palette; TerrainSource fields + DataVersion + migration.
- Terrain.Pipeline: SplatmapAsset -> weights product (two rasters); palette-array pack/resize;
  builder ProductType unchanged conventions (builder = source type, factory = runtime).
- Engine.Terrain: TerrainRenderData fields; TerrainRenderer set-3 bind group + tileScale buffer +
  Texture2DArray; the PS top-K blend; drop kMaxLayers.
- Editor.Terrain: TerrainSplatTool palette index + eraser + two-raster stroke; TerrainEditorPage
  base/paint split + uncapped palette + index remap; ToolPanelsImpl base/palette picker + eraser.
- Tests across foundation.terrain + Editor.Terrain + a renderer check.

Related: [terrain.md], [terrain-splat-d2.md], [terrain-splat-phase2.md].

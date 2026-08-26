# Terrain Layers: Per-Layer Coverage / Opacity Mask

Status: DRAFT (awaiting Fable review). Follow-up to terrain-height-blend.md - SEQUENCED AFTER it (this
spec assumes height-blend has landed: source/asset DataVersion 4, builder Version 6, set-3 at 12
entries t0..t10, ShadowParams.zw carrying the height params). Extends terrain-layer-pbr.md (per-layer
normal + ORM) and terrain-splat-topk.md (base + unbounded palette + top-K weights). Pure
material/render extension - NO change to the paint tool, the weight rasters, or the paint data model.

## Motivation

Many authored layer sets are SPARSE: the texture is not a solid sheet but scattered coverage over
transparent gaps - sparse grass tufts, gravel with soil showing between stones, moss patches. Poly
Haven ships these with a single-channel `_mask_` (verified: `sparse_grass_mask_4k.png` is a 16-bit
grayscale stencil, mostly dark = sparse), whose whole job is compositing: it says WHERE the layer's
material actually is, so the surface beneath shows through the holes.

Our top-K splat has no way to express this today. Painting a sparse-grass layer at weight 1 tiles it
as a solid opaque sheet - the gaps are gone, the grass reads as a lawn. A per-layer coverage mask
restores the gaps: it multiplies into the layer's blend weight so the layer contributes only where the
mask is opaque, and the freed weight reveals whatever is beneath (the base layer, i.e. dirt).

This is distinct from height-blend. Displacement/height interlocks the boundary between TWO
full-coverage layers (gravel-in-the-low-spots). A coverage mask cuts holes in ONE sparse layer to
reveal what is under it. They compose: the mask cuts coverage, then height-blend interlocks whatever
still competes.

## The blend math

The mask is a HARD coverage multiplier applied to each PALETTE layer's weight BEFORE the base-weight
computation and BEFORE height-blend:

    // Per splat texel corner, per palette slot k:
    //   m_k        = MaskArray slice idx[k] sampled at layer k's tiling UV (red channel, [0,1])
    //   w'[k]      = w[k] * m_k                          (effective coverage weight)
    // then:
    //   baseW      = saturate(1 - sum(w'[k]))            (gaps reveal base - what is beneath)
    //   ... feed w'[k] + baseW into the existing linear OR height-blend weighting ...

- The mask applies to PALETTE layers ONLY. The BASE layer is "what shows where nothing else covers" -
  there is nothing beneath it, so masking base is meaningless; base has NO mask and its weight simply
  grows to fill the cut coverage. (Layer.mask exists on the struct for array-build symmetry but the
  base layer's is never bound.)
- Because `baseW` is recomputed from the MASKED weights, a fully-masked texel (all `m_k = 0`) falls
  entirely to base - exactly sparse-grass-over-dirt. A fully-opaque mask (`m_k = 1`) leaves the blend
  identical to today.
- Composition order per corner: Load idx/weight -> multiply each `w[k]` by its mask (if maskBound) ->
  recompute baseW -> run the existing linear or height-blend weighting on the masked weights -> the
  2x2 bilinear across corner results still anti-aliases the seam. Mask can only REDUCE a weight, so
  the "skip zero-weight slots" fast path still holds (a fully-masked slot drops out of the loop).

Limitation (state it up front): a flat top-K blend has no layer ORDERING, so the gaps reveal the base
layer (and any other still-weighted palette layers, proportionally) - NOT a specifically chosen
"layer directly beneath this one." For the common sparse-over-ground case (one sparse palette layer +
base) this is exactly right; stacking two sparse layers with a chosen reveal order is out of scope.

OFF path (no palette layer authored a mask): the shader keeps the pre-track weighting unchanged - the
mask samples and the multiply are skipped entirely, byte-identical output. Strictly opt-in per terrain.

## Data model (foundation.terrain.resource)

Mirror the height addition from terrain-height-blend.md, but PALETTE-ONLY (no base map):

- `TerrainResource::Layer` gains `Ref<texture::Texture> mask;` (base layer's stays nil, never bound).
- `TerrainSource` gains `Array<Guid> paletteMaskIds;` (NO `baseMaskId`). Serialized under a NEW version
  gate (`ar.Version() >= 5`); `RTTI_DEFINE_OBJECT_VERSIONED(..., 5)` (was 4 after height-blend).
- `TerrainPaletteData` gains `Array<u8> maskTexels;` + `HasMask()`.
- New sidecar stream `kPaletteMaskStream = u8"palette.mask"`.
- `TerrainFactory::Create`: the `bind` lambda binds palette mask via `ReadArrayStream` on demand
  (absent -> no array); no base-mask binding.

Storage: RGBA8 slice read via `.r` (reuse all existing cook/BuildArray machinery, same as height). The
nil-layer default slice is `{255,255,255,255}` = mask 1.0 = FULLY OPAQUE. This default is the critical
difference from height's 0.5: a palette layer without a mask, sitting inside a present mask array, must
be UNAFFECTED (contribute at full painted weight), so its default coverage is 1.

## Cook (Terrain.Pipeline)

Mirror height P0:

- `TerrainAsset` gains `paletteMaskIds` under the `ar.Version() >= 5` gate; builder `Version()` bumped
  (6 -> 7); `builder.DataVersion(5)` (was 4).
- `ScanDependencies`: the `chain` lambda extends over `paletteMaskIds`.
- `CookPaletteArray`: build the mask array ON DEMAND via `AnyNonNil(paletteMaskIds)`, default
  `kOpaqueMask{255,255,255,255}`; `writeArray` writes the `palette.mask` stream. Mask mips = plain box
  filter, linear (a coverage fraction; do NOT sRGB it).

## Renderer (engine.terrain)

- `SplatTexture.cppm` (palette cache): PaletteGpu + Entry gain a mask array tex+view; `Build()` builds
  it via `BuildArray(RGBA8Unorm, ...)` when `HasMask()`; Destroy / RetireOrDestroy / MakeGpu handle it.
  (No base-mask texture - palette array only, so ONE new GPU object, not two.)
- `TerrainRenderData.cppm`: add `maskArrayView` (no base view).
- `TerrainComponents.cppm`: fill `maskArrayView` from PaletteGpu.
- `TerrainRenderer.cppm`: set-3 matEntries 12 -> 13 (add `t11 MaskArray` Texture2DArray);
  `MaterialBindGroup` ids[10] -> ids[11] (one new uniqueId). Add a 1x1 dummy
  `m_opaqueMaskArrayTex/View` (upload `{255,255,255,255}`), bound when the view is null.
  `EnsureMaterialBindGroup` wires maskArr (dummy when null).
- View UBO params: the two ShadowParams spare lanes are taken by height-blend, so add a NEW
  `float4 SplatParams2` to the terrain view UBO: `SplatParams2.x = mask maps bound` (>= 0.5), y/z/w
  spare. This GROWS the view UBO by 16 bytes (bump its size + fill site; a VS touch is not needed - PS
  only). `maskBound` is computed at bind time (any palette mask view is real, not a dummy). [Fable: the
  height-blend track deliberately avoided growing the UBO by reusing spare lanes; this track has no
  spare lanes left, so it grows the struct once. Confirm you would rather grow than, e.g., steal a bit
  of an existing lane.]

## Shader (Data/Shaders/terrain.ps.hlsl)

- Add `Texture2DArray MaskArray : register(t11, space3);`.
- Read `maskBound = SplatParams2.x >= 0.5`.
- In the manual-bilinear corner loop, per palette slot: when `maskBound`, sample
  `MaskArray.SampleGrad(AlbedoSampler, uvk, gx, gy).r` (grads already hoisted) and multiply it into
  `wk` before the `wk > 0` test and before accumulating. Recompute `baseW` from the masked weights.
  Then the existing linear / height-blend weighting runs on the masked weights unchanged.
- `maskBound` is UNIFORM (cbuffer), so the branch is uniform control flow (safe under the WGSL
  derivative rules) and no-mask terrains skip the extra samples.
- Tangent frame, CSM, and GBuffer writes are UNCHANGED - the mask only reweights coverage.

## Editor (Editor.Terrain, TerrainEditorPage)

Mirror the height/normal/ORM pickers, palette rows ONLY:

- Each palette row: add a "Layer N mask" picker (AssetPickerDialog filtered to TextureAsset), routed
  through the `SetPaletteMap` path so `paletteMaskIds` grows lazily to the albedo count; `AddLayer` /
  `RemoveLayer` keep it parallel. NO base-mask picker, NO scalar param (mask has no contrast knob).

## Phasing

- P0 - Data + cook: `Layer.mask` + `TerrainSource` / `TerrainAsset` `paletteMaskIds` + both
  DataVersion bumps (5) + v-gated reads; `TerrainPaletteData.maskTexels` + the sidecar stream; the cook
  builds the mask array ON DEMAND with the `{255,255,255,255}` OPAQUE default; ScanDependencies chains
  it. Tests: on-demand mask array + nil-layer OPAQUE-default fill + no-mask compat + source-id
  round-trip (Terrain.Pipeline.Tests).
- P1 - Renderer + shader: palette cache builds the mask array; render data + component fill + the
  maskBound param into SplatParams2.x; set-3 t11 + the 1x1 opaque dummy + the cache key; the PS
  multiplies coverage under the uniform `maskBound` branch and recomputes baseW. WGSL: all variants
  translate (naga check BEFORE any probe). Verified on Vulkan AND WebGPU (below).
- P2 - Editor: per-layer mask pickers on the terrain page; recook wiring. Tests: the v5 page snapshot
  round-trips the mask ids through the versioned undo payload; a no-mask snapshot stays empty.
- P3 - Polish + docs -> IMPLEMENTED.

## Verification (mirroring height-blend / layer-pbr R5)

Extend TerrainPixelProbeTests with a coverage case: a 2-contributor terrain - one PALETTE layer
(distinct colour, painted weight 1 everywhere) over a distinct-colour BASE - with a mask that is
OPAQUE on one half of the footprint and ZERO on the other:

1. OFF (no mask authored): the blended pixel is byte-identical to the pre-track blend, WebGPU ==
   Vulkan (existing no-maps invariant holds).
2. ON: the opaque half renders the palette layer's colour; the zero half renders the BASE colour
   (the gap reveals what is beneath) - assert both halves at a known texel, and that flipping the mask
   flips which colour shows.
3. Backend parity: the ON result matches across Vulkan and WebGPU within the established epsilon.

Plus: naga translates every terrain variant before the probe (R7-style gate); the height-blend +
layer-pbr + top-K probes stay green (no regression to the linear path).

## Import notes (this asset class)

Poly Haven sparse sets (the driving example) ship: `_diff_` (albedo), `_nor_gl_` (OpenGL/green-up
normal - flip G at import if the terrain shader expects DX handedness), `_rough_` + optional AO (pack
into ORM as G=roughness / R=AO, metallic 0 for organics), `_disp_` (the height-blend map), `_mask_`
(THIS coverage mask). Normal + roughness are often EXR (linear float) - the texture importer may need
EXR decode; mask + disp are 16-bit PNG (decode to R). None of that is this track's job (it consumes
whatever TextureAsset the picker resolves), but it is the reason this asset class motivates the track.

## Touch list

- foundation.terrain.resource: `Layer.mask`; `TerrainSource.paletteMaskIds` + v5 gate;
  `TerrainPaletteData.maskTexels` / `HasMask()`; `kPaletteMaskStream`; `TerrainFactory` bind +
  ReadArrayStream (palette only).
- Terrain.Pipeline: `TerrainAsset.paletteMaskIds` + v5 gate; builder Version + DataVersion bumps;
  `ScanDependencies` chain; `CookPaletteArray` on-demand mask array + `palette.mask` write.
- Terrain.Pipeline.Tests: on-demand + opaque-default + compat + round-trip cases; RemoveTree adds
  `terrain.palette.mask.bin`.
- engine.terrain: `SplatTexture` mask array build/destroy/retire; `TerrainRenderData` maskArrayView;
  `TerrainComponents` fill; `TerrainRenderer` set-3 t11 + dummy + cache key + SplatParams2 fill (UBO
  grows one float4).
- Data/Shaders/terrain.ps.hlsl: MaskArray binding + the coverage multiply + baseW recompute.
- Engine.Terrain.Backend.Tests: the coverage-mask pixel probe (Vk + WebGPU).
- Editor.Terrain: TerrainEditorPage per-layer mask pickers + recook.
- Editor.Terrain.Tests: v5 snapshot round-trip (mask ids) + no-mask-empty.

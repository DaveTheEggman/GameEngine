# Terrain Layers: Height-Blended Splatting (per-layer displacement as a blend mask)

Status: IMPLEMENTED (2026-08-26). Approved by Fable with amendments R1-R6 (RULING at the bottom), all
folded; P0-P3 shipped (commits in the phase list). Extends terrain-layer-pbr.md (per-layer normal +
ORM on the top-K blend) and terrain-splat-topk.md (base + unbounded palette + top-K weight rasters).
Pure material/render extension - NO change to the paint tool, the weight rasters, or the paint data
model. Next optional track: per-layer coverage mask (terrain-coverage-mask.md, drafted).

## Motivation

The top-K splat blends layers by a LINEAR weight lerp: at a boundary between grass (weight 0.6) and
gravel (weight 0.4) the shader mixes 60/40 uniformly across the seam, so the transition reads as a
soft dissolve. Real ground does not dissolve - the gravel shows first in the low spots (mortar lines,
divots) while grass holds the high tufts. That interlocking is what a boundary looks like in nature,
and its absence is a large part of why splatting still reads as "painted-on" even after normal + ORM
maps are in.

Height-blended splatting fixes this WITHOUT touching geometry: each layer carries a small tiling
HEIGHT (displacement) map, and at each texel the blend is biased toward the layer whose local height
is greatest, with a controllable soft skirt. This is the classic "advanced terrain texture splatting"
technique (Mishkinis) - a per-texel reweighting of the SAME top-K weights we already compute, so the
blend stays a convex combination and albedo / normal / ORM all keep flowing through it unchanged.

Scope note (answers the "what is a displacement map used for" question directly): this track uses the
displacement map as a BLEND MASK only. It does NOT move vertices (that is the heightfield's job) and
it does NOT do parallax occlusion mapping or tessellation - those are the "real micro-displacement"
options, deferred below, and tessellation is off the table anyway (the terrain renderer is one path
incl. WebGPU, no tessellation stage).

## The blend math

Today, per splat texel corner (the manual 2x2 bilinear from R1), the shader forms a LINEAR convex
combination: base gets `baseW = 1 - sum(w)`, palette slot k gets `w[k]`, and albedo / normal / ORM are
weighted-summed by those. Height-blend replaces the WEIGHTS (only) with height-biased ones:

    // Per corner, per contributor i in {base} u {palette slots with w[k] > 0}:
    //   score_i = weight_i + height_i          (weight and height both ~[0,1])
    //   sMax    = max score over contributors
    //   b_i     = max(score_i - (sMax - depth), 0)     // depth = contrast, the soft-skirt width
    //   final weight_i = b_i / sum(b)                  // renormalize -> convex again

- `height_base` = BaseHeight sampled at the base tiling UV; `height_k` = HeightArray slice `idx[k]` at
  that layer's tiling UV. Value is the map's red channel in [0,1].
- The base contributor is included ONLY when `baseW > 0` (else its mid-height would bleed phantom base
  where the palette already sums to 1).
- `depth` (contrast) is a per-terrain scalar > 0. Small depth = sharp, near-binary transition (the
  tallest layer wins hard); large depth = a wider soft skirt (more contributors survive). NOTE (R2):
  growing depth washes the SCORE differences out toward an EQUAL mix of the participating
  contributors - it does NOT recover the linear paint-weight blend (that is exclusively the OFF path).
  Clamped to a safe floor so `sum(b) >= depth > 0` (the max contributor always survives with
  `b = depth`) - no divide-by-zero, no need for a zero-sum guard.
- The reweighting is per CORNER; the existing 2x2 bilinear across the four corner RESULTS still does
  the spatial smoothing, so seams stay anti-aliased.

This preserves the sum-to-one property (renormalized), so the downstream albedo / normal / ORM
weighted sums remain valid convex blends - the whole PBR stack rides height-blend for free.

OFF path (no layer authored a height map): the shader keeps TODAY's exact linear weighting - the
height samples and the height-blend math are skipped entirely, byte-identical output. Height-blend is
strictly opt-in per terrain.

## Data model (foundation.terrain.resource)

Mirror the normal / ORM addition from terrain-layer-pbr.md EXACTLY (that is the established pattern):

- `TerrainResource::Layer` gains `Ref<texture::Texture> height;` (one more optional map alongside
  albedo / normal / orm).
- `TerrainSource` gains `Guid baseHeightId;` + `Array<Guid> paletteHeightIds;`, plus a per-terrain
  `f32 heightBlendContrast = 0.25f;`. Serialized under a NEW version gate (`ar.Version() >= 4`);
  `RTTI_DEFINE_OBJECT_VERSIONED(..., 4)` (was 3).
- `TerrainPaletteData` gains `Array<u8> heightTexels;` + `HasHeight()`.
- New sidecar stream `kPaletteHeightStream = u8"palette.height"` (parallel to `palette.normal` /
  `palette.orm`).
- `TerrainFactory::Create`: the existing `bind` lambda binds base/palette height the same way it binds
  normal/orm; `ReadArrayStream` reads the height stream on demand (absent -> no array; base height nil
  -> runtime dummy, never a cook product, per layer-pbr R4). The factory also copies
  `heightBlendContrast` from source -> resource.

Storage decision: height is stored as an RGBA8 slice and read via `.r`. This reuses ALL existing cook
+ array machinery (DecodeTextureRgba8, CookArray, BuildArray, the RGBA8 palette format) with ZERO new
single-channel format plumbing; the 3x memory for a scalar is negligible for small tiling maps. The
nil-layer default slice is `{128,128,128,255}` (mid-height 0.5). [Fable: R8Unorm would halve the
array memory at the cost of a new format path through cook + BuildArray + the dummy - I judge the
reuse worth the memory; flag if you disagree.]

## Cook (Terrain.Pipeline)

Mirror layer-pbr P0 exactly:

- `TerrainAsset` gains `baseHeightId` + `paletteHeightIds` + `heightBlendContrast` under the same
  `ar.Version() >= 4` gate; builder `Version()` bumped (5 -> 6); `builder.DataVersion(4)` (was 3).
- `ScanDependencies`: the `chain` lambda extends over `paletteHeightIds` too; `baseHeightId` chained.
- `CookPaletteArray`: build the height array ON DEMAND via `AnyNonNil(paletteHeightIds)`, default
  `kMidHeight{128,128,128,255}`; `writeArray` writes the `palette.height` stream.
- Base height is NOT a cook product - it stays a runtime `Ref` bound by the factory to a dummy when
  nil (layer-pbr R4 base-map rule).

## Renderer (engine.terrain)

- `SplatTexture.cppm` (palette cache): PaletteGpu + Entry gain height tex+view; `Build()` builds the
  height array via the existing `BuildArray(RGBA8Unorm, ...)` when `HasHeight()`; Destroy /
  RetireOrDestroy / MakeGpu handle it.
- `TerrainRenderData.cppm`: add `baseHeightView`, `heightArrayView`.
- `TerrainComponents.cppm`: fill `baseHeightView` from `res->base.height->View()`; `heightArrayView`
  from PaletteGpu. Also plumb `heightBlendContrast` from the resource into the render data (a scalar
  the renderer copies into the view UBO).
- `TerrainRenderer.cppm`: set-3 matEntries 10 -> 12 (add `t9 BaseHeight`, `t10 HeightArray`
  Texture2DArray); `MaterialBindGroup` ids[8] -> ids[10] (two new uniqueIds in the cache key). Add
  1x1 dummies `m_midHeightTex/View` + `m_midHeightArrayTex/View` (upload `{128,128,128,255}`), bound
  when the views are null. `EnsureMaterialBindGroup` wires baseHeight / heightArr (dummies when null).
- View UBO params: reuse the two SPARE lanes of `ShadowParams` (its comment already reads
  `zw spare`) - `ShadowParams.z = heightBlendContrast`, `ShadowParams.w = height maps bound` (>= 0.5).
  This needs NO UBO size change and no VS touch. The `heightBound` flag is computed at bind time the
  same way the cook uses `AnyNonNil` (any base-or-palette height view is real, not a dummy). [Fable:
  alternative is a dedicated `SplatParams2` float4 - I recommend the spare lanes for zero UBO churn +
  no re-version; flag if you would rather grow the struct.]

## Shader (Data/Shaders/terrain.ps.hlsl)

- Add `Texture2D BaseHeight : register(t9, space3);` +
  `Texture2DArray HeightArray : register(t10, space3);`.
- Read `heightBound = ShadowParams.w >= 0.5` and `contrast = max(ShadowParams.z, 1e-3)`.
- In the manual-bilinear corner loop, when `heightBound`: sample base height (once, at baseUV) and
  each participating slot's height via `HeightArray.SampleGrad(..., uvk, gx, gy).r` (grads already
  hoisted for the albedo/normal/orm taps - reuse them), run the height-blend formula above to get the
  renormalized per-contributor weights, and weight the albedo / normal / ORM sums by THOSE. When
  `!heightBound`: the existing linear weighting, unchanged.
- `heightBound` is UNIFORM (from the cbuffer), so branching on it is uniform control flow - safe under
  the WGSL derivative rules, and terrains without height maps skip the extra ~17 SampleGrads/pixel.
- The tangent frame (R2 chunk-frame analytic tangent), the CSM bias on the geometric normal n, and the
  GBuffer writes are all UNCHANGED - height-blend only reweights the material blend.

## Editor (Editor.Terrain, TerrainEditorPage)

Mirror the layer-pbr P2 pickers I just shipped:

- Base layer: add a "Base height" picker (AssetPickerDialog filtered to TextureAsset), editing
  `baseHeightId`.
- Each palette row: add a "Layer N height" picker, routed through the existing `SetPaletteMap` path
  (generalize it to a third target array, or add a height-specific branch) so `paletteHeightIds` grows
  lazily to the albedo count; `AddLayer` / `RemoveLayer` keep the fourth array parallel too.
- Add a "Height blend" FloatEditor in the property grid (0..1, default 0.25) editing
  `heightBlendContrast` - merge-keyed CommitEdit + recook, same as `baseTileScale`.
- Recook wiring identical to the albedo / normal / ORM pickers (CommitEdit -> rebind -> RequestCook).

## Phasing

- P0 - DONE (data + cook): `Layer.height` + `TerrainSource` / `TerrainAsset` height ids +
  `heightBlendContrast` + both DataVersion bumps (4, RTTI 4) + builder Version 6 -> 7 (R1) + v-gated
  reads; `TerrainPaletteData.heightTexels` / `HasHeight()` + the `palette.height` sidecar stream (factory
  ReadArrayStream cross-checks geometry vs the albedo header); the cook builds the height array ON
  DEMAND (`AnyNonNil`) with the `{128,128,128,255}` mid default; `ScanDependencies` chains the height
  ids; factory copies contrast source -> resource. Tests: on-demand height array + nil-layer mid-default
  slice + no-height compat (`HasHeight()` false) + base/palette id + contrast round-trip
  (Terrain.Pipeline.Tests 12 pass, clang + gcc).
- P1 - DONE (renderer + shader): the palette cache builds the height Texture2DArray on demand
  (`HasHeight()`) + retire/destroy/MakeGpu wired; TerrainRenderData baseHeight/heightArray views +
  contrast; component fill; ShadowParams.z = contrast / .w = heightBound (R5 - both comments renamed
  off "spare"); set-3 extended to t9/t10 + 1x1 mid-height 2D/array dummies + the material cache key
  gains the two view uniqueIds; the PS keeps the LINEAR path verbatim when `!heightBound` (byte-hold)
  and runs the Mishkinis reweight under the uniform branch otherwise (samples each slot once, scores =
  weight + height, cull below `sMax - contrast`, renormalize). WGSL: naga translates every variant
  (the 8 existing probes stayed green -> cook succeeded + OFF byte-hold). Verified Vulkan AND WebGPU: a
  NEW probe proves the tall layer wins a 50/50 tie (red R=14.4M vs B=65K), swapping the tall slice
  flips it, OFF holds the linear ~50/50 mix (R=7.27M/B=7.67M), and R6 - equal heights -> the greater
  WEIGHT wins - all matching across backends (9 cases / 617 asserts, clang + gcc).
- P2 - DONE (editor): the terrain page grows a Base height picker under the base layer, a Layer N
  height picker on each palette row (routed through `SetPaletteMap`, now a 3-way Normal/Orm/Height enum
  that lazily grows the target array), and a "Height blend" contrast FloatEditor (0..1, default 0.25)
  in the property grid; `AddLayer` / `RemoveLayer` keep the fourth array parallel. All recook-wired
  (`PickReference` -> commit -> rebind -> RequestCook). Tests: the v4 page snapshot round-trips base +
  ragged per-layer height ids + contrast through the versioned undo payload, and a no-height snapshot
  stays empty with the default contrast preserved (Editor.Terrain.Tests 21 pass, clang + gcc).
- P3 - DONE (docs): this spec -> IMPLEMENTED; Guides/terrain-authoring.md gains the height-blend note
  (R3 - crisp interlocked seams replace soft dissolves the moment a terrain opts in; raise the contrast
  to widen the skirt). Next optional tracks: POM / tessellated micro-displacement, per-layer height
  amplitude, and the per-layer coverage mask (terrain-coverage-mask.md, already drafted).

## Verification (the required bar, mirroring layer-pbr R5)

Extend TerrainPixelProbeTests with a height-blend case on a 2-layer terrain whose two height maps
interlock (one high where the other is low), at a texel where the two weights are ~equal (0.5/0.5):

1. OFF (no height maps): the blended pixel is byte-identical to the pre-track linear blend, and
   WebGPU == Vulkan pixel-exact (the existing no-maps invariant holds).
2. ON (height maps + a low contrast): at that equal-weight texel the result shifts measurably toward
   the layer whose local height is greater there (assert the dominant channel crosses > 50% vs the
   linear ~50/50), and swapping which map is tall at that texel flips which layer wins - proving the
   blend follows height, not a fixed order.
3. Backend parity: the ON result matches across Vulkan and WebGPU within the established epsilon.

Plus: the naga WGSL cook translates every terrain shader variant before the probe runs (R7 from
layer-pbr), and the layer-pbr + top-K probes stay green (no regression to the linear path).

## Deferred

- REAL micro-displacement: parallax occlusion mapping (ray-march the height map in tangent space to
  shift UVs - gives parallax + self-occlusion without geometry, WebGPU-friendly) and/or tessellated
  displacement (off the table while the renderer stays tessellation-free). Height-blend authors the
  SAME maps POM would use, so this is a natural follow-up.
- Per-layer height AMPLITUDE (how tall each layer reads) - v1 folds amplitude into the map authoring
  (a flatter map pokes through less) + a single global contrast. A per-layer amplitude buffer could
  ride the TileScales StructuredBuffer pattern later.
- Triplanar projection for steep slopes (orthogonal to this track; the tiling UV is still local XZ).
- Per-layer COVERAGE / OPACITY mask (SEPARATE follow-up track, user-decided 2026-08-26): a
  single-channel stencil that multiplies into a layer's top-K weight before renormalize, so a sparse
  layer (e.g. Poly Haven sparse_grass `_mask_`) shows the layer(s) beneath through its gaps. Reuses
  this same on-demand-array + editor-picker plumbing (a 4th optional map), but is its own track. Note
  for that track: Poly Haven ships `_nor_gl_` (OpenGL/green-up normals - flip G at import if the shader
  wants DX) and EXR normal/roughness (importer may need EXR handling); mask + disp are 16-bit PNG.

## Touch list

- foundation.terrain.resource: `Layer.height`; `TerrainSource` height ids + `heightBlendContrast` +
  v4 gate; `TerrainPaletteData.heightTexels` / `HasHeight()`; `kPaletteHeightStream`; `TerrainFactory`
  bind + ReadArrayStream + contrast copy.
- Terrain.Pipeline: `TerrainAsset` height ids + contrast + v4 gate; builder Version + DataVersion
  bumps; `ScanDependencies` chain; `CookPaletteArray` on-demand height array + `palette.height` write.
- Terrain.Pipeline.Tests: on-demand + default-fill + compat + round-trip cases; RemoveTree adds
  `terrain.palette.height.bin`.
- engine.terrain: `SplatTexture` height array build/destroy/retire; `TerrainRenderData`
  baseHeight/heightArray views + contrast; `TerrainComponents` fill; `TerrainRenderer` set-3 t9/t10 +
  dummies + cache key + ShadowParams.zw fill.
- Data/Shaders/terrain.ps.hlsl: BaseHeight/HeightArray bindings + the height-blend reweighting.
- Engine.Terrain.Backend.Tests: the height-blend pixel probe (Vk + WebGPU).
- Editor.Terrain: TerrainEditorPage base + per-layer height pickers + contrast slider + recook.
- Editor.Terrain.Tests: v4 snapshot round-trip (height ids + contrast) + no-height-empty.

---

## RULING (Fable, 2026-08-26) - APPROVED with amendments

The shape is exactly right: the Mishkinis reweight on the EXISTING top-K contributors, per corner,
renormalized convex, with the whole PBR stack riding it unchanged, opt-in per terrain, and the
established layer-pbr pattern mirrored at every level (on-demand array, headered sidecar, dummies,
cache key, uniform branch). Both flagged decisions are confirmed; two corrections and one probe
addition below.

### R1 - Stale version numbers

TerrainAssetBuilder::Version() is ALREADY 6 (the layer-pbr review pass bumped it for the
linear-space albedo mips, cd5ad8dd, after this spec was drafted). This track ships 6 -> 7. The
DataVersion story is correct as written (TerrainAsset + TerrainSource both 3 -> 4,
RTTI_DEFINE_OBJECT_VERSIONED 4).

### R2 - Correct the contrast description (math)

"Large depth ... approaching the linear blend" is wrong. With b_i = score_i - sMax + depth, growing
depth washes the SCORE DIFFERENCES out: the blend converges to an EQUAL mix of the participating
contributors - the paint weights DILUTE, they are not recovered. The linear weight blend is
exclusively the OFF path. Keep the 0..1 slider + 0.25 default (scores span [0,2], so 1.0 is already
very soft); fix the prose in the blend-math section when building.

### R3 - The technique sharpens ALL painted transitions - document it for authors

Even where every height is equal (all-default mid slices), score = weight + 0.5 means any
contributor more than `depth` below the max is CULLED: a soft-painted 90/10 texel renders
near-binary at the default contrast. That is the intended interlock look (crossovers still land
where the weights tie), but it visibly changes existing soft gradients the moment a terrain opts
in. P3's docs step updates Guides/terrain-authoring.md (it exists now): "height-blend trades soft
dissolves for crisp interlocked seams; raise Height blend (contrast) to widen the skirt."

### R4 - RGBA8 storage CONFIRMED

The author's call stands: the sidecar/array contract is single-format end to end (SliceBytes is
4Bpp, ReadArrayStream cross-checks geometry against the albedo header, the palette cache shares one
generation) - an R8 path would fork all of it for a scalar. Revisit R8 only if palette memory is
MEASURED to matter.

### R5 - ShadowParams spare lanes CONFIRMED, with naming discipline

Verified genuinely spare in both terrain.ps.hlsl ("zw spare") and the renderer's ViewUBO struct.
Required: BOTH comments change to name the lanes (z = heightBlendContrast, w = height maps bound)
at the declaration AND the fill site. If a future shadow feature wants its lanes back, it grows the
struct and relocates these - never squeezes in alongside. Contrast floor clamp in the PS as
specced; when hasWeights is false (pure base) skip the reweight entirely - one contributor needs no
blend.

### R6 - One probe addition: pin that the WEIGHTS still steer

Probe #2 proves the blend follows height; add its complement: with BOTH height maps present but
EQUAL at the probed texel, the winner must be the layer with the greater WEIGHT (the crossover
sits at the weight tie). This catches an implementation that drops the weight term from the score -
which passes every height-varies test. Everything else in the verification section stands,
including naga-before-probes and the OFF-path byte-hold via the existing suites.

Build order stands (P0 -> P3, green + reviewed between phases). Deferred list (POM, per-layer
amplitude, triplanar) confirmed out.

# Terrain Layers: Per-Layer Coverage / Opacity Mask

Status: IMPLEMENTED (2026-08-26). Approved by Fable with amendments R1-R5 (RULING at the bottom), all
folded; P0-P3 shipped (commits in the phase list). Follow-up to terrain-height-blend.md. Extends
terrain-layer-pbr.md (per-layer normal + ORM) and terrain-splat-topk.md (base + unbounded palette +
top-K weights). Pure material/render extension - NO change to the paint tool, the weight rasters, or
the paint data model.

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

The mask is a coverage multiplier applied to each PALETTE layer's weight, and the REMOVED coverage is
handed to the OTHER PAINTED layers (revised 2026-08-26 after the first user test - see the note below):

    // Per splat texel corner, per palette slot k:
    //   m_k        = MaskArray slice idx[k] sampled at layer k's tiling UV (red channel, [0,1])
    //   freed     += w[k] * (1 - m_k)                    (coverage removed from this slot)
    //   w'[k]      = w[k] * m_k                          (cut weight)
    //   receiverW  = sum of w'[k] over UNCUT slots (m_k ~= 1) - the other painted layers
    // then:
    //   if receiverW > 0:  w''[k] = w'[k] + freed * (w'[k] / receiverW)  for each UNCUT slot
    //                      (palette sum restored -> baseW unchanged: gaps reveal PAINTED layers)
    //   else:              baseW = saturate(1 - sum(w'[k]))  (no other painted layer -> reveal base)

- The mask applies to PALETTE layers ONLY. The BASE layer is "what shows where nothing else covers";
  base has NO mask. (Layer.mask exists on the struct for array-build symmetry but base's is never bound.)
- REVEAL-PAINTED, not reveal-base: the freed coverage flows to the layers you actually PAINTED under
  the sparse one (by continuous receptivity, smoothstep(0.75, 0.95, mask)) - grass gaps show the ground layer you painted, not
  the base canvas. Only when NO other painted layer has weight (a sparse layer painted directly over
  base) does the freed coverage fall to base. A fully-opaque mask (`m_k = 1`) leaves the blend
  identical to today.
- Composition order per corner: Load idx/weight -> for each slot multiply `w[k]` by its mask + tally
  `freed` and a continuous receiver pool `recvSum` (post-cut weight x smoothstep receptivity) ->
  redistribute `freed * saturate(recvSum / 0.05)` into the receivers (or let baseW absorb
  it) -> run the existing linear or height-blend weighting on the result -> the 2x2 bilinear across
  corners anti-aliases the seam.

Limitation (state it up front): a flat top-K blend has NO layer ORDERING, and painting a layer to full
strength EVICTS the others (their weight -> 0). So "grass fully covering intact ground, grass has
holes" cannot be expressed - once the ground weight is gone there is nothing to reveal but base. The
mask reveals a painted layer only where that layer still has weight (paint the sparse layer at partial
strength, or rely on feathered edges). True ordered/stacked layers are a separate, larger feature; for
a photo texture whose diffuse already bakes the substrate into its gaps (e.g. Poly Haven sparse_grass),
skip the mask entirely - the diffuse already reads as grass-on-dirt.

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
  7 -> 8 (R1 - height-blend shipped 7); `builder.DataVersion(5)` (was 4).
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
  `MaskArray.SampleGrad(AlbedoSampler, uvk, gx, gy).r` (grads already hoisted), tally the removed
  coverage (`freed`) and a continuous receiver pool (`recvSum` = post-cut weight x
  smoothstep(0.75, 0.95, mask) receptivity), then redistribute `freed * saturate(recvSum / 0.05)`
  into the receivers (reveal painted layers) and let the `baseW = 1 - sum` recompute absorb the
  remainder (reveal
  base when no other painted layer). Then the existing linear / height-blend weighting runs unchanged.
- `maskBound` is UNIFORM (cbuffer), so the branch is uniform control flow (safe under the WGSL
  derivative rules) and no-mask terrains skip the extra samples.
- Tangent frame, CSM, and GBuffer writes are UNCHANGED - the mask only reweights coverage.

## Editor (Editor.Terrain, TerrainEditorPage)

Mirror the height/normal/ORM pickers, palette rows ONLY:

- Each palette row: add a "Layer N mask" picker (AssetPickerDialog filtered to TextureAsset), routed
  through the `SetPaletteMap` path so `paletteMaskIds` grows lazily to the albedo count; `AddLayer` /
  `RemoveLayer` keep it parallel. NO base-mask picker, NO scalar param (mask has no contrast knob).

## Phasing

- P0 - DONE (data + cook, 30d92c3b): `Layer.mask` (palette-only) + `TerrainSource` / `TerrainAsset`
  `paletteMaskIds` + both DataVersion bumps (5, RTTI 5) + builder Version 7 -> 8 (R1) + v-gated reads;
  `TerrainPaletteData.maskTexels` / `HasMask()` + the `palette.mask` sidecar; the cook builds the mask
  array ON DEMAND (`AnyNonNil`) with the `{255,255,255,255}` OPAQUE default; ScanDependencies chains
  it; factory reads the stream. Tests: on-demand + nil OPAQUE-default slice + no-mask compat + id
  round-trip (Terrain.Pipeline.Tests 13, clang + gcc).
- P1 - DONE (renderer + shader, 82771b65): palette cache builds the mask array on demand + retire
  wired; TerrainRenderData maskArrayView + component fill; the renderer grows the view UBO by a
  `SplatParams2` float4 (x = maskBound, R2 - documented at both HLSL + C++ fill site) since the
  ShadowParams spares were spent; set-3 t11 + the 1x1 OPAQUE dummy + the cache-key id; the PS
  multiplies each slot's weight by its mask under the uniform `maskBound` branch BEFORE the baseW
  recompute. WGSL: naga translates every variant (9 existing probes green, OFF byte-hold). Verified
  Vulkan AND WebGPU: a coverage probe splits the footprint blue|red at tileScale=worldSize (R3),
  flipping swaps the halves, an all-zero mask reveals base everywhere; PLUS the R5 green-sign probe
  (a +green normal leans -Z -> GL green-up is correct, no author flip) (11 cases / 802 asserts).
- P2 - DONE (editor): per-layer mask pickers on each palette row (SetPaletteMap extended to a 4-way
  enum, lazy-grows) + AddLayer/RemoveLayer keep the array parallel; recook-wired. Tests: v5 page
  snapshot round-trips ragged mask ids, no-mask snapshot stays empty (Editor.Terrain.Tests 23).
- P3 - DONE (docs): this spec -> IMPLEMENTED; terrain-authoring.md gains the mask slot + the R5
  green-up note (Poly Haven `_nor_gl_` import as-is) + the EXR-source note (PNG-in). R4 mip softening
  recorded as intended (coverage-preserving mips deferred).
- POST-SHIP REVISION (2026-08-26, first user test): the freed coverage now flows to the OTHER PAINTED
  layers, not the base canvas (reveal-painted, not reveal-base). Shader redistributes `freed` into the
  uncut slots proportionally; base absorbs it only when no other painted layer has weight. New Vk +
  WebGPU probe: two painted layers (ground red + fully-masked grass) over a blue base -> the ground
  (red) wins, not the base (blue) (13 cases / 862 asserts). See the revised blend-math + limitation.

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
normal - imports AS-IS, NO flip: our terrain frame is glTF/GL green-up, pinned by the R5 green-sign
probe), `_rough_` + optional AO (pack into ORM as G=roughness / R=AO, metallic 0 for organics),
`_disp_` (the height-blend map), `_mask_` (THIS coverage mask). Normal + roughness are often EXR
(linear float) which our stb importer does NOT decode - grab the PNG variants or convert offline
(terrain layer maps cook to RGBA8 regardless, so 8-bit PNG loses nothing). Mask + disp are 16-bit PNG.
None of the decode is this track's job (it consumes whatever TextureAsset the picker resolves), but it
is the reason this asset class motivates the track.

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

---

## RULING (Fable, 2026-08-26) - APPROVED with amendments

The shape is right and the reasoning is sound where it matters: mask multiplies PALETTE weights
before the baseW recompute and before height-blend (so the two tracks compose in the right order),
palette-only with no base mask (there is nothing beneath the base), the OPAQUE default slice
(the critical inversion of height's mid default, correctly argued), the flat-blend no-ordering
limitation stated honestly, and the established array/sidecar/dummy/cache pattern reused wholesale.
The flagged decision is confirmed and there are four corrections/additions:

### R1 - Stale version numbers (again - drafts race the review passes)

Height-blend SHIPPED builder Version 7 (its own R1 absorbed the review-pass mip bump). This track
ships 7 -> 8, not 6 -> 7. The DataVersion story is correct as written (both envelopes 4 -> 5,
RTTI_DEFINE_OBJECT_VERSIONED 5). Rule of thumb for future drafts: read the CURRENT
Version()/DataVersion out of the code at build time, not out of the previous spec.

### R2 - UBO growth CONFIRMED (SplatParams2)

The spare lanes are spent; grow the view UBO by one float4 APPENDED at the end. The VS and the
depth-VS declarations stay untouched - a cbuffer declaration is a prefix view of the bound range,
and the buffer only grows (the PS declares the full struct; Vulkan and WebGPU validate against the
largest declared size, which the grown buffer satisfies). Document x = maskBound, yzw spare at
both the HLSL declaration and the C++ fill site - the height-blend R5 discipline, learned twice
now.

### R3 - The probe fixture's tile scale must land the mask split on screen

The mask samples at the LAYER's tiling UV. The existing probe fixtures use tileScale 1000 to get a
solid colour - at that scale the whole footprint samples a single texel of a 4x4 mask and the
"half opaque / half zero" fixture never produces a screen-space split. The coverage probe must set
the layer's tileScale = the terrain's world size (exactly ONE mask repeat spans the footprint) so
the opaque/zero halves land in the left/right probe bands, then assert the split + the flip as
specced.

### R4 - Mip behavior of stencil masks: accepted softening, deferred fix

Box-filtered mips of a near-binary mask gray toward the average, so sparse coverage fades to a
uniform partial blend at distance rather than keeping contrast. For a weight MULTIPLIER (not an
alpha test) that is acceptable - distant sparse grass reading as a soft grass/dirt mix is
visually plausible. State it as intended; coverage-preserving mip scaling (alpha-coverage-style)
is DEFERRED with POM and per-layer amplitude.

### R5 - Import-note correction + pin the green sign while you are in there

The import note hedges "flip G if the terrain shader expects DX handedness" - it does not. The
house convention is glTF (mesh path = glTF tangent handedness, GL green-up), and the terrain
frame's B = cross(n, T) points toward -Z, which under the top-left UV origin (v grows with +Z)
IS the GL green-up behavior: Poly Haven `_nor_gl_` maps import AS-IS, no flip. However layer-pbr's
probe pinned the U axis only (its test normal had green = 0), so the green sign is argued, not
measured. This track's probe work adds the cheap missing case: a +green-tilted normal map
(e.g. encoded (128, 204, 229)) on the identity terrain must brighten under a -Z sun and darken
under +Z. If the probe disagrees, fix B's sign in the SHADER - never ask authors to flip maps.
(EXR decode for Poly Haven normal/rough sources is noted and stays out of scope.)

Everything else stands as written: phasing, the verification bar (plus R3's fixture correction and
R5's green case), the palette-only data model, and the touch list (with R1's 7 -> 8).

---

## POST-IMPLEMENTATION REVIEW (Fable, 2026-08-26) - PASS, no findings

Every amendment held: R1 builder 7 -> 8 + both envelopes at DataVersion 5
(RTTI_DEFINE_OBJECT_VERSIONED 5); R2 SplatParams2 appended with the lane named at BOTH the HLSL
declaration and the C++ ViewData fill (the discipline finally arrived pre-review); R3 the mask
probe's tileScale = the footprint (one repeat), and the half-split provably lands on screen AND
mirrors exactly when the mask flips; R4's mip-softening note shipped in the authoring guide; R5
the import note now says `_nor_gl_` imports AS-IS and the green-sign probe PINS it empirically -
a +green base normal brightens 4x under a -Z sun and darkens under +Z against a symmetric flat
control, closing the green-axis gap layer-pbr left argued-but-unmeasured. The shader ordering is
exactly as ruled (mask multiply -> baseW recompute -> linear or height-blend weighting, uniform
branch, SampleGrad with hoisted grads). WGSL cook clean; full clang + gcc batteries green; the
mask probe shows opaque = pure layer, zero = pure revealed base, WebGPU parity on the split.

## Pass-17 review amendment (2026-08-29, Fable)

The reveal-painted redistribution originally gated receivers on a HARD `mask >= 0.999` test. Two
defects (pass-17 review):
- masks are byte-quantized and filtered, so only exactly-255 texels qualified - a photo mask's
  opaque pixels (250..254) could never receive, resurfacing the reveal-base symptom one case over,
  with hard rings along the 0.999 iso-contour of any soft mask edge;
- the `receiverW > 1e-4` guard amplified a vanishing receiver to ~full coverage right at the
  threshold (unbounded factor).

Fixed: receptivity is now continuous - `recv = postCutWeight * smoothstep(0.75, 0.95, mask)`, and
the redistributed share fades with the pool (`freed * saturate(recvSum / 0.05)`), the remainder
falling to base. Convexity unchanged (share <= freed). The probe gained an unmasked A/B baseline
(green-layer removal + red boost asserted, both backends).

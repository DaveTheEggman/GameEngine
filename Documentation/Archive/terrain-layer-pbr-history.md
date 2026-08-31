# Terrain Layers: Per-Layer Normal + ORM Maps (top-K blended)

Status: IMPLEMENTED (2026-08-26). Approved by Fable with required amendments R1-R7 (RULING at the
bottom), all folded into the body; P0-P2 shipped (commits below), P3 = this closeout. Extends
terrain-splat-topk.md (the base + unbounded palette + top-K weight model). Pure material/render
extension - NO change to the paint tool, weight rasters, or paint data model. Next optional track:
height-blend / triplanar (see the Deferred note below).

## Motivation

The terrain splat looks flat because the material is ALBEDO-ONLY with a CONSTANT surface response.
In Data/Shaders/terrain.ps.hlsl today:

    o.normal   = OctEncode(normalize(mul(float4(n, 0.0), View).xyz));  // n = geometric heightfield normal only
    o.material = float2(1.0, 0.0);                                     // roughness=1, metallic=0 - hardcoded

Every layer is lit by the coarse heightfield normal (no fine surface detail) and is uniformly
fully-rough / non-metallic. A rock texture shades identically to grass: a flat photo pasted on the
slope, with no bumps and no specular variation. `TerrainResource::Layer` carries only
`{ Ref<Texture> albedo; f32 tileScale; }`. This was an explicit deferral in terrain-splat-topk.md
("normal + roughness per layer ... PBR-per-layer is a separate track"). This spec closes it.

## Goals

- Per-layer NORMAL maps: fine surface detail (perturb the shading normal per texel), so painted
  layers respond to light differently and read as 3D, not painted-on.
- Per-layer ORM maps (Occlusion / Roughness / Metallic): per-layer specular response feeding both
  the lit color and the SSR material target (SV_Target3 is literally "for SSR").
- Blended by the SAME top-K weights as albedo - one shared blend, no new paint data.
- Backward-compatible: a terrain with no normal/ORM maps renders BYTE-IDENTICAL to today (flat
  normal, fully-rough, non-metallic).

## Non-goals (deferred)

- Height-map / height-based ("height-blend") layer transitions and triplanar projection on steep
  slopes. Both are natural follow-ons that reuse this plumbing; kept out to bound the spec.
- Detail/normal-detail secondary maps, per-layer tint/HSV, per-layer displacement/POM.
- Changing the paint tool, the weight rasters, or the base/palette model (terrain-splat-topk.md
  stands unchanged).

## Locked shape (PINNED by the ruling from the codebase, R1)

- Three textures per layer MAX: `albedo`, `normal`, `orm`. Each of `normal`/`orm` is OPTIONAL per
  layer (missing -> renderer dummy if NO layer has it, else a cook default slice - R4).
- ORM packing: one texture per layer, `R = AO`, `G = roughness`, `B = metallic` - CONFIRMED
  channel-identical to the mesh material (MetallicRoughness `.gb`, Occlusion `.r`) and glTF's common
  packed image. SV_Target3 = (roughness, metallic) = `float2(orm.g, orm.b)` (terrain.ps.hlsl:52).
- Normal maps: tangent-space, full RGB `* 2 - 1` decode (forward.ps.hlsl:332), flat default
  (128,128,255). NO RG-reconstruct, NO handedness (terrain UVs never mirror), NO NormalScale in v1.
  Tangent frame is ANALYTIC in the CHUNK frame (no per-vertex tangents; R2).
- AO is AMBIENT-ONLY (forward.ps.hlsl:433 does the same); a later GTAO combine is orthogonal.

## Design overview

The palette is already a `Texture2DArray` of albedo slices (one per palette layer) with the base as
a standalone `Texture2D`. Add two more parallel arrays (normal, ORM) + two standalone base textures,
built and bound exactly like the albedo array. The PS blends the top-K NORMALS and ORMs per texel
with the identical 2x2-corner manual bilinear it already does for albedo, then:

- builds an analytic tangent frame from the geometric normal + the world-XZ tiling axes,
- perturbs the shading normal with the blended tangent-space normal,
- writes the perturbed VIEW-space normal to SV_Target1 and the blended roughness/metallic to
  SV_Target3, and modulates ambient by the blended AO.

No new weight data, no paint change: normals/ORM ride the SAME (index, weight) top-K as albedo.

## Data model

### foundation.terrain (TerrainResource / TerrainSource)

`Layer` gains two optional refs:

    struct Layer {
        Ref<texture::Texture> albedo;   // existing
        Ref<texture::Texture> normal;   // NEW - tangent-space normal map (nil = flat)
        Ref<texture::Texture> orm;      // NEW - R=AO G=roughness B=metallic (nil = 1,1,0 default)
        f32 tileScale = 1.0f;           // existing; shared by all three maps of this layer
    };

`base` (the base Layer) and every `palette[i]` gain `normal` + `orm`. BOTH the runtime `TerrainSource`
AND the pipeline envelope `TerrainAsset` mirror it (R5): `baseNormalId` / `baseOrmId`,
`paletteNormalIds[]` / `paletteOrmIds[]` (parallel to `paletteAlbedoIds[]`; nil entries allowed).
Bump `TerrainSource::DataVersion` AND `TerrainAssetBuilder::Version()` (RE-COOK note in the landing
commit), gate both reads (serializer-strict-versioning); the v(old)->v(new) upgrade defaults all new
ids nil -> flat/default, so old terrains are visually unchanged.

Sidecar layout (R5): THREE separate streams on the terrain's cooked instance - the existing
"palette" (albedo) + new "palette.normal" / "palette.orm" - each with the same
{sliceSize, mipCount, sliceCount} header. An ABSENT stream = no array (pairs with the R4 on-demand
rule). `TerrainPaletteData` carries the three texel arrays + per-array presence under ONE uid (all
present arrays rebuild together, share the generation). `ScanDependencies` chains every normal/ORM
source's PIXELS as reads.

### Cooked palette data (Terrain.Pipeline, TerrainPaletteData)

Today the cook packs every palette albedo, resized to one common slice size, into a single array
product. Extend to THREE arrays at the same common size / slice count / mip chain, but built ONLY
ON DEMAND (R4 - no default-array bloat; a real albedo array is ~11-22MB, do not triple it for
terrains that never assign these maps):
- `albedoArray` (existing) - but switch its format to RGBA8UnormSrgb (R3, below).
- `normalArray` (NEW, RGBA8Unorm linear): built ONLY IF at least one palette layer has a normal map;
  then nil layers inside it get the DEFAULT flat slice `(128,128,255)`. If NO layer has a normal
  map, the array is NOT written at all and the renderer binds a 1x1 flat-normal dummy.
- `ormArray` (NEW, RGBA8Unorm linear): same rule; default slice `(255,255,0)` = AO 1 / roughness 1 /
  metallic 0 (= today's constant material exactly); absent -> 1x1 default-ORM dummy.
BASE normal/ORM are NOT cook products (R4): `base.normal`/`base.orm` are plain runtime `Ref`s bound
by the factory like base albedo; a nil base ref binds the renderer's 1x1 dummy. So "cook-injected
default slice" applies ONLY to nil layers INSIDE a present array.

R3 (close an EXISTING sRGB gap while here): today the albedo palette array is RGBA8Unorm (raw sRGB
bytes, no decode) while BASE albedo binds the texture product's sRGB-aware view - the SAME texture
shades brighter as base than as a palette layer. Fix it: albedoArray becomes RGBA8UnormSrgb; its
mips average in LINEAR space (reuse the texture cook's recipe, TextureAsset.cppm:513). The normal +
ORM arrays are linear with a plain box filter; normal-map mips are NOT renormalized in v1 (standard,
accepted).

The recipe CHAINS every source albedo/normal/ORM's PIXELS (reads) so editing any of them re-cooks
the affected array (mirrors the topk albedo-pixel chaining).

## Renderer (Engine.Terrain)

### TerrainRenderData

Add, parallel to the albedo views:
- `rhi::TextureView* baseNormalView; rhi::TextureView* baseOrmView;`
- `rhi::TextureView* normalArrayView; rhi::TextureView* ormArrayView;`
Any of these that has no cook product / nil base ref binds a 1x1 DUMMY (R4): a flat-normal
(128,128,255) dummy texture + a default-ORM (255,255,0) dummy + a 1x1 flat-normal array + a 1x1
default-ORM array, created once alongside the existing white dummy in TerrainRenderer. Present arrays
are uid/generation-keyed, retire-queued on rebuild, Cleared at teardown, exactly like the albedo
array (the splat-cache suite extends to cover them; the palette-array cache builds all present arrays
together so they share a generation).

### Set-3 bind group

Append to the existing set-3 layout (space3): normal + ORM base textures + the two arrays. New
registers (extend, do not renumber existing t0..t4/s0):
- `t5 BaseNormal`, `t6 NormalArray` (Texture2DArray), `t7 BaseOrm`, `t8 OrmArray` (Texture2DArray).
The albedo sampler (s0) is reused (repeat/trilinear) for all three - same tiling UV. Cache key gains
the four new view uniqueIds (bind-group-cache-versioning: never raw pointers).

### Terrain PS (top-K normal + ORM blend + tangent frame)

Mirror the albedo blend for normals and ORM. Per corner texel, blend base + top-K (weights already
loaded), using `SampleGrad` with the gradients HOISTED out of the data-dependent branches (the topk
R1 / WGSL non-uniform-control-flow rule the albedo taps already follow). Then bilinear-lerp the four
corner results, exactly like albedo. Sketch (extends the existing `BlendTexel`):

    // Per corner, alongside the existing albedo accumulation:
    float3 nTS = (BaseNormal.SampleGrad(s, uvBase, gx, gy).rgb * 2 - 1) * baseW;   // tangent space
    float3 orm = float3(1,1,0) * baseW;                                            // AO,rough,metal
    orm = BaseOrm.SampleGrad(s, uvBase, gx, gy).rgb * baseW; // (base default handled by cook slice)
    [unroll] for (k) {
        if (w[k] <= 0) continue;
        float3 uvk = float3(uv * tileScales[idx[k]], idx[k]);
        nTS += (NormalArray.SampleGrad(s, uvk, gxk, gyk).rgb * 2 - 1) * w[k];
        orm +=  OrmArray.SampleGrad(s, uvk, gxk, gyk).rgb            * w[k];
    }
    // corner returns: albedoColor, nTS (tangent-space, weight-blended), orm

    // After the 2x2 bilinear lerp of the corners (color, nTS, orm):
    float3 gN = normalize(i.normal);                      // geometric world normal
    // Analytic tangent frame in the CHUNK frame (R2): the tiling UV is terrain-LOCAL XZ, so the map's
    // U axis is local +X ROTATED to world by ChunkToWorld (world +X shears on a rotated terrain).
    float3 axisU = normalize(mul(float4(1,0,0,0), ChunkToWorld).xyz); // the map's U in world
    float3 T = normalize(axisU - gN * dot(axisU, gN));    // Gram-Schmidt onto the tangent plane
    float3 B = cross(gN, T);                              // world image of local +Z; sign PINNED by probe #1
    float3 N = normalize(nTS.x * T + nTS.y * B + nTS.z * gN);  // perturbed world normal
    // ... lighting uses N (was gN); AO (R1: ambient-ONLY, matches the mesh material):
    float3 lit = base * (ambient * orm.r + ndl_N * shadow);
    o.normal   = OctEncode(normalize(mul(float4(N, 0.0), View).xyz));
    o.material = float2(orm.g, orm.b);                    // roughness, metallic

Notes:
- Blending tangent-space normal VECTORS weighted then normalizing is the standard, adequate for
  terrain (RNM/UDN partial-derivative blends are overkill here); the base's flat `(0,0,1)` default
  contributes `baseW` toward "no perturbation," which is exactly right.
- Conventions PINNED from the mesh material (R1): normal decode is full RGB `* 2 - 1`
  (forward.ps.hlsl:332), flat default (128,128,255), NO RG-reconstruct, NO handedness (terrain UVs
  never mirror - skip the tangentWS.w equivalent), NO NormalScale in v1.
- The V-axis sign (B) is PINNED by probe #1 run on an identity-transform terrain AND a 90-degree-
  rotated one (the rotation is exactly what R2 guards): a known-direction normal map under a sun
  flip must move the brightness the predicted way on both.
- `ndl` recomputed with the perturbed `N`. Shadow bias still uses a stable normal (use gN for the
  CSM normal-offset to avoid self-shadow acne from high-frequency perturbation; keep sampling with
  gN in SampleCascade).
- No-splat / height-ramp fallback path unchanged (still flat, still fine).

## Editor (Editor.Terrain, TerrainEditorPage)

Each layer row (base + every palette row) gains a NORMAL albedo-picker and an ORM picker next to the
existing albedo picker + tileScale. Reuse the `app::AssetPickerDialog` (texture types) already used
for albedo. Splat PICKER (ToolPanelsImpl) is UNCHANGED - thumbnails stay albedo (that is the paint
identity). Edits rewrite the source (new ids), merge-keyed undo, rebind + recook (adds/refreshes the
normal/ORM arrays).

## Migration / compatibility

No behavioral migration: new ids default nil, so no normal/ORM array is built (R4), the renderer
binds the flat-normal + default-ORM dummies, and the shader math with `(0,0,1)` normals + `(1,1,0)`
ORM reduces to today's output - byte-identical until an author assigns a map. The `TerrainSource`
AND `TerrainAsset` DataVersion bumps cover the deserialize (RE-COOK note in the landing commit);
compat is PINNED by probe #3 (pre/post in the same run).

## Phasing

- P0 - DONE (data + cook): `TerrainResource::Layer.normal/orm` + `TerrainSource` / `TerrainAsset` ids
  + both DataVersion bumps (3) + v-gated reads; TerrainPaletteData carries optional normalTexels /
  ormTexels + the two sidecar streams; the cook builds the normal/ORM arrays ON DEMAND (absent when
  no layer uses that map) with the exact default slices (128,128,255) / (255,255,0) for nil layers,
  ScanDependencies chains their pixels. Tests: on-demand arrays + nil-layer default fill + no-maps
  compat + source-id round-trip (Terrain.Pipeline.Tests 10 pass). The R3 sRGB albedo fix rides P1
  (it needs the renderer format change too). No render change yet.
- P1 - DONE (renderer + shader): the palette cache builds the normal/ORM Texture2DArrays on demand
  (linear) + the albedo array flipped to RGBA8UnormSrgb (R3 sRGB fix); TerrainRenderData views +
  component fill; the 1x1 flat-normal / default-ORM dummies (2D + array, R4); set-3 layout extended
  to t5..t8 + the material bind cache key gains the four view uniqueIds; the PS blends the top-K
  tangent-space normals + ORM (SampleGrad, hoisted grads), builds the CHUNK-frame analytic tangent
  frame (R2), perturbs the normal, and writes the perturbed view-space normal + roughness/metallic +
  AO-modulated ambient. WGSL: ShaderPack translates all 84 variants (naga). Verified on Vulkan AND
  WebGPU: the 6 existing probes stay green (no-maps byte-identical, WebGPU==Vulkan) + a NEW
  normal-map probe proves a tilted base normal brightens under an aligned sun / darkens under an
  opposed one while the flat control stays symmetric (R2 direction correct), matching across
  backends. commits 0abd5fa4 (impl) + cd7991e9 (probe).
- P2 - DONE (editor): the terrain page grows a Base normal + Base ORM picker under the base layer,
  and a Layer N normal + Layer N ORM picker on each palette row, mirroring the albedo pickers
  (`PickReference` -> `AssetPickerDialog` filtered to TextureAsset -> `CommitEdit` + rebind + recook).
  Each per-row setter routes through `SetPaletteMap`, which lazily grows the optional
  `paletteNormalIds` / `paletteOrmIds` to the albedo count so an older terrain (empty map arrays)
  edits cleanly; `AddLayer` / `RemoveLayer` keep the three arrays parallel. Tests: v3 snapshot
  round-trips the base + ragged per-layer normal/ORM ids through the page's versioned undo payload,
  and a no-maps snapshot stays empty (Editor.Terrain.Tests 19 pass on clang + gcc).
- P3 - Polish + docs -> IMPLEMENTED; note height-blend/triplanar as the next optional track.

## Verification

- Headless (Terrain.Pipeline): three arrays built at the common size, correct slice for each layer,
  DEFAULT slices for nil normal/ORM (exact `(128,128,255)` / `(255,255,0)`), source-id round-trip.
- ORDER (R7): run `Tools.ShaderPack Data/Shaders <out> wgsl` the MOMENT the PS changes, BEFORE any
  GPU probe - SampleGrad-in-branch on the two new arrays is exactly where naga rejected us last time
  (webgpu-stricter-than-vulkan). Probe #3 (compat) renders the pre-change baseline and the post-change
  frame in the SAME run/driver (old-path fixture expectations, not stored goldens).
- Renderer pixel probe (the established bar - Vulkan AND WebGPU, pixel-exact parity):
  1. A terrain with a NORMAL map lit from a fixed sun shows the expected bump shading (differs from
     the flat-normal baseline in the predicted direction) - proves the tangent frame + convention.
  2. An ORM roughness/metallic change moves SV_Target3 (and the lit specular) as expected.
  3. A terrain with NO normal/ORM renders byte-identical to the pre-change baseline (compat pin).
  4. Multi-layer: two layers with different normals blend across the top-K boundary without seams.
  Extends TerrainPixelProbeTests; the WGSL path (SampleGrad on arrays + the tangent math) is exactly
  where naga can diverge - validate the cook early.
- Extend the type-confusion FillTerrainRenderData to the new fields (keeps compiling as the layout
  grows); the splat-cache retire/Clear suite covers the two new arrays.

## Settled by the ruling (were open questions)

- ORM packing: PACKED, R=AO/G=roughness/B=metallic, channel-identical to the mesh material + glTF
  (R1). Not separate textures.
- Normal decode: full RGB `* 2 - 1`, no RG-reconstruct, no handedness, no NormalScale (R1); green/V
  sign PINNED by probe #1 on identity + 90-deg-rotated terrain (R2).
- AO: ambient-only (R1).
- sRGB: albedo array becomes RGBA8UnormSrgb (fixes the base-vs-palette brightness gap), normal/ORM
  linear (R3).

## Touch list

- foundation.terrain: `Layer.normal/orm` + `TerrainSource` normal/ORM ids + DataVersion + upgrade.
- Terrain.Pipeline: `TerrainAsset` envelope ids + `Version()` bump (R5); TerrainPaletteData builds
  normal + ORM arrays ON DEMAND (R4: absent when no layer uses them; default slices only for nil
  layers inside a present array), albedo array -> RGBA8UnormSrgb with linear-space mips (R3),
  normal/ORM linear; three sidecar streams + PIXEL chaining; base normal/ORM are runtime refs, NOT
  cook products (R4); round-trip tests.
- Engine.Terrain: TerrainRenderData views + the 1x1 flat-normal / default-ORM dummies + dummy arrays
  (R4); TerrainRenderer set-3 bind (t5..t8) + cache/retire; the PS top-K normal+ORM blend + the
  CHUNK-frame analytic tangent frame (R2) + GBUFFER normal/material writes. Depth pipeline unchanged
  (R6).
- Editor.Terrain: TerrainEditorPage per-layer normal + ORM pickers + recook.
- Tests: Terrain.Pipeline cook (on-demand arrays, sRGB, defaults, round-trip), the extended
  Vulkan+WebGPU pixel probe (WGSL pack FIRST - R7), FillTerrainRenderData, the splat-cache suite.

Related: [terrain.md], [terrain-splat-topk.md].

---

## RULING (Fable, 2026-08-26) - APPROVED with required amendments

The shape is right: three parallel arrays riding the existing top-K blend, defaults that reduce to
today's output, no paint-model change. The open questions are all SETTLED below from the codebase
(not preference), plus two corrections and one size trap.

### R1 - Conventions PINNED from the mesh material (they were "open questions"; they are not open)

- Normal decode: full RGB `* 2 - 1` (forward.ps.hlsl:332), flat default (128,128,255). NOT
  RG-reconstruct. No handedness machinery: terrain UVs never mirror, so skip the tangentWS.w
  equivalent entirely; no NormalScale in v1.
- ORM: PACKED, `R = AO, G = roughness, B = metallic` - CONFIRMED. This is exactly the glTF
  shared-image layout: the mesh material reads MetallicRoughnessMap`.gb` (G=rough, B=metal) and
  OcclusionMap`.r`, and glTF authoring commonly packs all three into ONE image already. A packed
  terrain ORM is channel-identical to what mesh authors export; nothing new to learn.
- AO scope: AMBIENT-ONLY - CONFIRMED; that is what the mesh does (forward.ps.hlsl:433 applies `ao`
  to the ambient/IBL sum only, never the direct term). A later GTAO combine is orthogonal.
- SV_Target3 = (roughness, metallic) in RG - the sketch's `float2(orm.g, orm.b)` is correct
  against terrain.ps.hlsl:52.

### R2 - Tangent frame in the CHUNK frame, not world axes (sketch correction)

The tiling UV is terrain-LOCAL XZ (pre-ChunkToWorld; terrain.vs.hlsl:46 outputs `localXZ`
explicitly "for albedo tiling", and the normal is rotated to world by ChunkToWorld). The sketch
builds T from world `+X` - on a rotated terrain the tangent no longer aligns with the map's U axis
and every normal map SHEARS. Build the frame from the ChunkToWorld-ROTATED local axes:

    float3 axisU = normalize(mul(float4(1,0,0,0), ChunkToWorld).xyz); // the map's U in world
    float3 T = normalize(axisU - gN * dot(axisU, gN));
    float3 B = cross(gN, T); // sign = the world image of local +Z; PIN with the probe

The V/green sign is pinned by probe #1 (a known-direction normal map under a sun flip must move
the brightness the predicted way), run on an identity-transform terrain AND a 90-degree-rotated
one (the rotation is what R2 exists for).

### R3 - Close the EXISTING sRGB gap while adding the arrays

Today the cooked albedo palette array is RGBA8Unorm (raw sRGB bytes, no decode) while the BASE
albedo binds the texture product's view, which the texture cook builds sRGB-aware
(TextureAsset.colorSpace, Srgb default). The SAME texture assigned as base vs as a palette layer
shades at different brightness today. This track fixes it: the albedo array becomes
RGBA8UnormSrgb; the normal + ORM arrays are RGBA8Unorm (linear) as specced. Mips for the sRGB
albedo array should average in LINEAR space (the texture cook already does exactly this -
TextureAsset.cppm:513 - reuse the recipe); the linear arrays keep the plain box filter.
Normal-map mips are NOT renormalized in v1 (accepted, standard).

### R4 - No default-array bloat; base defaults are RENDERER dummies, not cook products

The albedo array for a real terrain is ~11-22MB cooked. Do NOT triple that for terrains that never
assign a normal/ORM map: when NO palette layer has a normal (resp. ORM) map, the cook writes NO
normal (ORM) array at all, and the renderer binds a 1x1 dummy array (flat normal / default ORM -
the existing dummy-texture pattern in TerrainRenderer). Only when at least one layer has a map is
the full array built, with the exact default slices (128,128,255)/(255,255,0) filling the nil
layers. Likewise BASE normal/ORM are plain runtime refs bound by the factory (like base albedo) -
the cook produces NO standalone base products; nil base refs bind the renderer's 1x1 dummies. The
spec's "cook-injected default" language applies ONLY to nil layers inside a present array.

### R5 - Sidecar layout + BOTH envelopes version-gated

Three SEPARATE sidecar streams on the terrain's cooked instance: the existing "palette" (albedo) +
new "palette.normal" / "palette.orm", each with the same {sliceSize, mipCount, sliceCount} header;
an ABSENT stream = no array (pairs with R4). TerrainPaletteData carries the three texel arrays +
per-array presence, ONE uid - all three rebuild together and share the generation (as specced).
ScanDependencies chains every normal/ORM source's PIXELS as reads. The spec names TerrainSource
only - the PIPELINE envelope (TerrainAsset) equally gains baseNormalId/baseOrmId +
paletteNormalIds/paletteOrmIds behind ITS version gate, and TerrainAssetBuilder::Version() bumps
(RE-COOK note in the landing commit). Serializer-strict-versioning on both reads.

### R6 - Bind-count headroom checked; depth pipeline untouched

Set 3 grows to 9 sampled textures (idx, wgt, baseAlbedo, albedoArray, baseNormal, normalArray,
baseOrm, ormArray + none spare) + set-0's CSM array = comfortably under WebGPU's default
16-sampled-textures-per-stage limit; verified, no action. The DEPTH-ONLY pipeline layout stays
3-set (no material) exactly as today. The set-3 cache key gains the four new view uniqueIds (the
spec already says so; bind-group-cache-versioning stands).

### R7 - Verification order + the compat pin

Run `Tools.ShaderPack Data/Shaders <out> wgsl` the moment the PS changes, BEFORE any GPU probe -
SampleGrad-in-branch on the two new arrays is precisely where naga rejected us last time (the
webgpu-stricter-than-vulkan uniformity lesson). Probe #3 (no-maps terrain byte-identical) captures
the pre-change baseline and the post-change frame in the SAME run/driver (render old-path fixture
expectations, not stored goldens). Probes #1/#2/#4 as specced, Vulkan + WebGPU parity; extend
FillTerrainRenderData and the palette-cache retire/Clear suite to the new views/arrays (specced).

Build order stands as phased (P0 data/cook -> P1 renderer -> P2 editor -> P3 docs), green +
reviewed between phases. Height-blend + triplanar stay out, as specced.

---

## POST-IMPLEMENTATION REVIEW (Fable, 2026-08-26) - PASS with fixes applied in-pass

Opus's build honors the amendments: R1 conventions exact (RGB*2-1 decode, packed ORM channels,
AO ambient-only, (rough, metal) target, shadow bias on the geometric normal), R2 tangent frame
built from the ChunkToWorld-rotated local +X, R4 on-demand arrays + renderer dummies + base as
runtime refs, R5 separate headered sidecars with geometry cross-checks + both envelopes gated
(TerrainAsset v3 / TerrainSource v3 / builder v5), R6 ten-entry set-3 with the 8-id cache key,
depth path untouched. The WGSL cook is clean (checked in-pass). Findings, all FIXED in-pass:

1. R3's mip half was deferred with a stale "rides P1" note: the albedo array uploaded as
   RGBA8UnormSrgb but its mips still averaged the ENCODED bytes (dark mips). Added
   BoxHalveRgba8SrgbAware (decode -> average -> encode, alpha linear) used for the albedo array
   only; pinned by the checker test (black/white averages to sRGB ~188, not 128).
2. Probe gaps: the shipped normal probe exercised only the BASE (Texture2D) path. Added the
   array-PBR probe: one-hot painted layer with an ARRAY normal shades directionally (4x
   asymmetry measured), an ORM array with AO=0 removes exactly the ambient share, the R2
   ROTATION pin (90-degree terrain: the asymmetry leaves the world-X sun pair and moves fully to
   world-Z - the frame follows the chunk, not the world), and WebGPU parity on the array taps.

ADVISORY (authoring, not a code defect): BASE normal/ORM maps bind texture PRODUCTS, so a
hand-imported TextureAsset left at the default colorSpace=Srgb will decode a normal/ORM map
through the sRGB curve (warped values). Set colorSpace=Linear on those assets (model-importer-
produced data maps already are). The PALETTE arrays decode raw source pixels and are immune.

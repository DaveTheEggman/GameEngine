# Terrain Layers: Per-Layer Normal + ORM Maps (top-K blended)

Status: SPEC / proposed (awaiting Fable review). Extends terrain-splat-topk.md (the base + unbounded
palette + top-K weight model). Pure material/render extension - NO change to the paint tool, weight
rasters, or paint data model.

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

## Locked shape (confirm in review)

- Three textures per layer MAX: `albedo`, `normal`, `orm`. Each of `normal`/`orm` is OPTIONAL per
  layer (missing -> a cook-injected default slice; see Defaults).
- ORM packing: one texture per layer, `R = AO`, `G = roughness`, `B = metallic` (glTF metallic-
  roughness adjacent; AO folded into R). Single texture keeps it to three parallel arrays.
- Normal maps: tangent-space, engine's standard normal convention (match the mesh material's decode:
  `nTS = tex.rgb * 2 - 1`, or RG-reconstruct if that is the house convention - use whatever
  mesh/forward uses so authoring is uniform). Tangent frame is ANALYTIC (no per-vertex tangents).

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

`base` (the base Layer) and every `palette[i]` gain `normal` + `orm`. `TerrainSource` mirrors:
`baseNormalId` / `baseOrmId`, `paletteNormalIds[]` / `paletteOrmIds[]` (parallel to
`paletteAlbedoIds[]`; nil entries allowed). Bump `TerrainSource::DataVersion` and gate the read
(serializer-strict-versioning); write the v(old)->v(new) upgrade (all new ids default nil ->
flat/default, so old terrains are visually unchanged).

### Cooked palette data (Terrain.Pipeline, TerrainPaletteData)

Today the cook packs every palette albedo, resized to one common slice size, into a single array
product. Extend it to THREE arrays built the same way, all at the same common size / slice count /
mip chain:
- `albedoArray`  (existing)
- `normalArray`  (NEW): each palette layer's normal map; a layer with no normal gets a DEFAULT flat
  slice `(128,128,255)` (tangent-space +Z).
- `ormArray`     (NEW): each palette layer's ORM; no-ORM layer gets DEFAULT `(255,255,0)` = AO 1,
  roughness 1, metallic 0 (matches today's constant material exactly).
Base normal/ORM are standalone `Texture2D` products (like base albedo), with the same defaults when
nil. The recipe CHAINS every source normal/ORM's PIXELS (reads) so editing an albedo/normal/ORM
re-cooks the array (mirrors the albedo-pixel chaining from topk R-notes). Normal-map slices are NOT
sRGB (linear); albedo stays sRGB; ORM is linear.

## Renderer (Engine.Terrain)

### TerrainRenderData

Add, parallel to the albedo views:
- `rhi::TextureView* baseNormalView; rhi::TextureView* baseOrmView;`
- `rhi::TextureView* normalArrayView; rhi::TextureView* ormArrayView;`
All uid/generation-keyed, retire-queued on rebuild, Cleared at teardown, exactly like the albedo
array (topk R4 - the splat-cache suite extends to cover them; the palette-array cache builds all
three arrays together so they share a generation).

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
    // Analytic tangent frame for world-XZ-tiled maps (no per-vertex tangents):
    float3 T = normalize(float3(1,0,0) - gN * gN.x);      // world +X projected to the tangent plane
    float3 B = normalize(cross(gN, T));                   // world +Z-ish (fix V sign to the map convention)
    float3 N = normalize(nTS.x * T + nTS.y * B + nTS.z * gN);  // perturbed world normal
    // ... lighting uses N (was gN); AO from orm.r modulates ambient:
    float3 lit = base * (ambient * orm.r + ndl_N * shadow);
    o.normal   = OctEncode(normalize(mul(float4(N, 0.0), View).xyz));
    o.material = float2(orm.g, orm.b);                    // roughness, metallic

Notes:
- Blending tangent-space normal VECTORS weighted then normalizing is the standard, adequate for
  terrain (RNM/UDN partial-derivative blends are overkill here); the base's flat `(0,0,1)` default
  contributes `baseW` toward "no perturbation," which is exactly right.
- The V-axis sign (B) and the normal-map green convention (OpenGL vs DirectX) must line up with
  `splatUV`/`localXZ` orientation - finalize + PIN with the probe (a known-direction normal map lit
  from a known sun must brighten the correct facing).
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

No behavioral migration: new source ids default nil, the cook injects the flat-normal + default-ORM
slices, and the shader math with `(0,0,1)` normals + `(1,1,0)` ORM reduces to today's output. So
existing terrains render byte-identical until an author assigns a normal/ORM map. DataVersion bump
covers the deserialize; a re-cook produces the (default-filled) arrays.

## Phasing

- P0 - Data + cook: `Layer.normal/orm` + source ids + DataVersion + upgrade; TerrainPaletteData
  builds the three parallel arrays (default flat-normal / default-ORM slices for nil), linear vs
  sRGB per array, pixel-chaining. Headless: the cook packs three arrays of matching slice
  count/size; nil layers get the exact default slices; round-trip of the new source ids. No render
  change yet (the arrays exist, the shader still ignores them). Green + reviewed before P1.
- P1 - Renderer + shader: TerrainRenderData views, set-3 bind extension + cache, the PS normal+ORM
  blend + analytic tangent frame + GBUFFER writes. Verify a normal-mapped layer shades bumpy and an
  ORM roughness change alters the specular/SSR.
- P2 - Editor: per-layer normal + ORM pickers on the terrain page; recook wiring.
- P3 - Polish + docs -> IMPLEMENTED; note height-blend/triplanar as the next optional track.

## Verification

- Headless (Terrain.Pipeline): three arrays built at the common size, correct slice for each layer,
  DEFAULT slices for nil normal/ORM (exact `(128,128,255)` / `(255,255,0)`), source-id round-trip.
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

## Open questions / to settle in review

- ORM as one packed texture (R=AO,G=rough,B=metal) vs separate roughness/metallic (+ AO) textures.
  Spec leans packed (one texture, one array, glTF-adjacent). Confirm the channel convention against
  the mesh material so authoring is uniform.
- Normal decode convention (full RGB vs RG-reconstruct; OpenGL vs DirectX green). Match the mesh/
  forward material exactly; PIN with the probe.
- Whether AO should also attenuate direct light (spec: ambient-only, the conservative choice) or
  feed a later GTAO combine.

## Touch list

- foundation.terrain: `Layer.normal/orm` + `TerrainSource` normal/ORM ids + DataVersion + upgrade.
- Terrain.Pipeline: TerrainPaletteData builds normal + ORM arrays (defaults for nil, linear/sRGB,
  pixel-chained) + base normal/ORM standalone products; round-trip tests.
- Engine.Terrain: TerrainRenderData + TerrainRenderer set-3 bind (t5..t8) + cache/retire; the PS
  top-K normal+ORM blend + analytic tangent frame + GBUFFER normal/material writes.
- Editor.Terrain: TerrainEditorPage per-layer normal + ORM pickers + recook.
- Tests: Terrain.Pipeline cook, the extended Vulkan+WebGPU pixel probe, FillTerrainRenderData, the
  splat-cache suite.

Related: [terrain.md], [terrain-splat-topk.md].

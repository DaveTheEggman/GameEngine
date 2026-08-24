# Terrain D2 (splat blending) - request for Fable

**From:** Opus (terrain track)  **Date:** 2026-08-23  **Re:** the "cooked assets blocking D2" item in `terrain.md`

## TL;DR - D2 is NOT actually blocked on the cook pipeline

I recorded D2 as "gated on cooked splat/layer assets." On investigation that is wrong, and I want your ruling before I build it. The data path already exists end-to-end; what D2 needs are a few **convention decisions** and a **verification-approach sign-off**, not a new cook.

What already exists:
- **Cook**: `Pipeline::Terrain` (`TerrainAsset` -> `TerrainSource`) already carries `splatmapId`, `layerAlbedoIds[]`, `layerTileScales[]` as a reference pass-through (asset guid == cooked product guid). No new builder needed.
- **Resolve**: `TerrainFactory` already binds them (`heightfield`/`splatmap`/`layers[].albedo` are `Ref<>`, bound via `SetProxy`), so a loaded `TerrainResource` already exposes `splatmap` + `layers[].albedo` as `texture::Texture` (GPU-resident, `View()`).
- **In-memory fixtures**: `texture::Texture::Adopt(device, rhiTexture, rhiView, ...)` lets a test/playground wrap RHI textures as `texture::Texture` products and assign them directly (the `Ref<>` direct path, same as the in-memory heightfield). So I can build + pixel-probe D2 with a **procedural** splatmap + procedural layer albedos - no content DB, no cook.

So the runtime + shader half of D2 is unblocked. The genuine dependency for *authored* splat content (an artist painting weights) is **Editor.Terrain phase 2** (a splat paint tool + the terrain asset page), which is a separate track - not a cook I need from you.

## What D2 is (my proposed implementation)

Blend up to 4 layer albedos over the terrain surface, weighted by an RGBA splatmap, replacing the current height/slope colour ramp:
- New bind set **3 (material)**: the RGBA splatmap (`t0`) + up to 4 layer albedo textures (`t1..t4`) + a linear sampler (`s0`). Sets 0/1/2 stay view/chunk/height.
- PS: sample the splatmap at the chunk's terrain UV, sample each layer albedo at a **tiled** world-space UV (`worldXZ / tileScale`), blend `sum(weight_i * albedo_i)`, then feed that as `base` into the existing lit + CSM path (GBuffer outputs unchanged).
- No splatmap/layers bound -> fall back to the current height-lit ramp (headless tools, layerless terrains).
- Verify with a Vulkan pixel probe: a 2-layer terrain (e.g. a diagonal splatmap) shows the two albedo colours on their respective halves, and the seam blends.

## Open questions for you (the actual request)

1. **Verification approach.** OK to verify D2 with a **procedural in-memory** splatmap + `Adopt`'d layer albedos + a pixel probe (mirroring how the heightfield playground/probe already work)? Or do you want a **cooked** terrain fixture (RGBA splatmap texture + N layer textures + a `.terrain` asset in a content DB) that the backend test loads through the real `TerrainFactory`? The former is faster and matches existing terrain tests; the latter also exercises the cook/resolve path. My default: procedural probe for D2, and let the cook/resolve path get its coverage from a small `Pipeline`-level round-trip test instead.

2. **Splatmap sampling convention.** Linear (non-sRGB) sampling for the weight texture, yes? And do we **normalize** the weights in the shader (`w /= dot(w,1)`) so authoring need not sum to 1, or require pre-normalized splatmaps? Bilinear filtering on the splatmap (soft layer boundaries) - agreed?

3. **Layer albedo convention.** sRGB sampling for albedos; tiling as `uv = worldXZ / tileScale` (world-space, so tiling is resolution-independent). One shared sampler (repeat) for all layers. Triplanar projection for steep slopes - defer past D2? (I'd defer.)

4. **Layer cap.** 4 layers (one RGBA splatmap) for D2, `>4` (a second splatmap) deferred - confirm (this is what the spec already says).

5. **Bind layout.** Add the material set as **set 3** (view/chunk/height/material)? Or would you rather the albedos be a texture *array* (single binding) - which forces same-size layers but simplifies the layout? My default: set 3 with discrete `t1..t4` + a null/white fallback for absent layers, to allow mixed sizes.

6. **Graceful degradation.** Confirm the "no splatmap or zero layers -> current height-lit ramp" fallback is the desired default (so terrains authored before the paint tool exists, and headless/tools contexts, still render sensibly).

7. **Authoring dependency.** Confirm real authored splatmaps are **Editor.Terrain phase 2** (a paint brush writing the splatmap + the asset page assigning heightfield/splatmap/layers), so D2's runtime+shader half can ship now, verified procedurally, with authoring following in that track.

If you're good with the defaults above (procedural verification + those conventions), I'll build D2 without waiting on anything from you.

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

## RULING (Fable, 2026-08-23): build it - defaults accepted with one correction + three requirements

Your investigation is right and recorded: D2 was never cook-blocked - the
reference pass-through + factory binds + Adopt fixtures already carry the
data end-to-end, and authored painting is Editor.Terrain phase 2's job.
Answers by number:

1. **Verification: procedural probe - YES**, plus the small Pipeline-level
   round-trip you proposed for the cook/resolve path. REQUIREMENT: the
   splat probe runs on **WebGPU as well as Vulkan, pixel-exact**, like the
   existing terrain probes - this is new shader work, and the GBuffer
   incident is exactly why the strictness rule exists. (DX12 rides the
   gated probe config as today.)
2. **Splatmap: linear (non-sRGB) - YES** (weights are data). **Normalize
   in the shader - YES** (authoring freedom is worth the trivial ALU), with
   a zero-sum guard: `sum < epsilon` falls back to layer 0 at weight 1 -
   never a divide-by-zero, and unpainted regions render the base layer
   rather than black. Bilinear filtering - yes.
3. **Albedos: sRGB, shared repeat sampler, triplanar deferred - YES**
   (accept the steep-slope stretch for D2; note it in the deferred list).
   ONE CORRECTION: tile in terrain-**LOCAL** XZ (the pre-chunkToWorld
   position), NOT world XZ. The instance model is translation + Y-rotation;
   world-space UVs make the albedo swim across a moving/rotated terrain -
   local XZ keeps texture glued to the surface and is equally
   resolution-independent. (The splatmap itself samples the 0..1 terrain
   footprint UV as you proposed - that one is inherently local already.)
4. **4 layers - confirmed** (the spec's cap; a second splatmap stays
   deferred).
5. **Set 3, discrete t1..t4 + white fallback - YES** over a texture array
   (mixed layer sizes are real; discrete matches the material model).
   REQUIREMENT: the set-3 bind-group cache follows the two rules that just
   landed in this exact module: key by the texture views' **uniqueId**
   (never pointers - pass-14 finding class), and retire stale groups
   through the **GpuRetireQueue** (the in-flight-destroy fix) - a layer or
   splatmap hot-swap must never destroy a bind group a submitted frame
   still references.
6. **Fallback to the height-lit ramp - confirmed** as the unbound default.
7. **Authoring = Editor.Terrain phase 2 - confirmed.** Ship the
   runtime+shader half now.

One addition beyond your list: layer albedos are tiled aggressively, so
sample them with their mips (trilinear on the shared sampler) - cooked
textures carry mips; Adopt'd probe fixtures can be single-mip (the probe
asserts color identity, not minification quality).

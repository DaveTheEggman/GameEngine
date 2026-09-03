# Texture Compression: BC6H for HDR + the Normal-Map Format Fix

Status: READY for build (authored by Fable, 2026-08-26). SHIPS AFTER texture-page-ux.md (the
page's "Cooks to:" row will surface these policy changes automatically). Extends the
asset-variants P1 block-compression work (Texture.Compression: bc7enc = BC7, rgbcx = BC1/3/4/5).

## Motivation

Two gaps in the P1 policy table:

1. **HDR cooks uncompressed.** The policy's HDR row is an explicit escape hatch ("BC6H not
   vendored in P1"). The cost is real: ImportTest's BlueSky sky is a 134 MB RGBA32F sidecar.
   BC6H is 1 byte/texel, GPU-native, and part of the SAME `texture-compression-bc` feature we
   already require on WebGPU desktop - that sky cooks to ~9-11 MB with mips, a ~12x cut, with no
   runtime decode.
2. **Normal-map compression is a latent correctness bug.** The policy routes Usage = Normal to
   BC5, which stores RG ONLY - but both `forward.ps.hlsl` and `terrain.ps.hlsl` decode normals as
   full `rgb * 2 - 1`. A BC5-compressed normal map samples B = 0, decoding to nTS.z = -1:
   visibly broken shading the first time anyone combines Usage = Normal with Default compression.
   Nobody has hit it because usage defaults to Color - the page-UX track (usage inference,
   Normal Map profile) is about to make Usage = Normal common, so this fix lands FIRST in the
   build order below.

## Part 1 - Normal maps: BC7-linear now, BC5 + reconstruct deferred

Change the policy's Normal row from BC5 to **BC7 (linear, non-sRGB)**.

- Same memory (both are 16 bytes per 4x4 block = 1 byte/texel), no shader change, quality
  slightly below BC5's two independent planes but far above "z = -1".
- BC5 + shader Z-reconstruction (`nTS.z = sqrt(saturate(1 - dot(nTS.xy, nTS.xy)))`, behind a
  format-driven material flag in BOTH forward and terrain) is the later quality win - DEFERRED,
  noted at the bottom. Do not ship BC5 normals until the shaders reconstruct.
- Mask row (BC4, single-channel R) is CORRECT as-is - both consumers (terrain height/mask reads
  `.r`; ORM is Usage = Mask only if authored single-channel... see the note) - AUDIT NOTE: a
  packed ORM texture carries THREE meaningful channels (R=AO G=rough B=metal); if authored as
  Usage = Mask it would cook BC4 and lose G/B. Rule: the policy's Mask row keeps BC4, and the
  page-UX inference maps `_orm` / `_arm` to Mask - so ADD a guard: Mask + a source with more than
  one distinct channel cooks BC7-linear instead (channel-content sniff at cook, cheap on the
  already-decoded pixels). Single-channel masks (rough/AO/height/coverage) stay BC4.

## Part 2 - BC6H for HDR

### Encoder

Vendor the BC6H encoder from the same family as our existing `bc7enc`/`rgbcx` (richgel999's
bc6h_enc from the bc7enc_rdo repository - MIT, single .cpp/.h pair, matches the vendoring style).
ThirdParty/bc7enc grows the pair; the Texture.Compression impl unit remains the ONLY includer
(GCC module hygiene).

New entry point alongside `EncodeBlockCompressed` (which is RGBA8-only):

    // Encode one mip of tightly-packed RGBA32F pixels to BC6H (unsigned half float).
    [[nodiscard]] Array<byte> EncodeBlockCompressedHdr(const f32* rgba, u32 width, u32 height,
                                                       u8 quality);

Unsigned (BC6HRGBUfloat): our HDR sources are radiance maps, non-negative by construction; the
signed variant stays unplumbed. Alpha is dropped (BC6H is RGB) - sky/IBL sources carry none.

### Policy + cook

- `ResolveCompressedFormat`: the HDR row returns `BC6HRGBUfloat` when `profile.bc` (still
  uncompressed when the target has no BC - the mobile ASTC-HDR variant is a follow-up, below).
  The small-texture and authored-None escapes apply as today.
- The texture cook's HDR path feeds the RGBA32F mip chain to the HDR encoder (float mips are
  averaged in linear space already - HDR data IS linear; no sRGB question here).
- `BlockCompressedSize` covers BC6H (same 16-byte 4x4 arithmetic as BC7) - the exact-size cook
  assertion holds.
- TextureAssetBuilder Version() BUMPS (read the current value from code at build time - the
  height-blend lesson) -> HDR assets re-cook on next open (BlueSky drops ~12x); LDR recipes are
  policy-identical EXCEPT Usage = Normal/Mask-multichannel assets, which also re-cook into their
  corrected formats - one bump covers all of Part 1 + Part 2.

### Runtime

- Format conversions: Vulkan `VK_FORMAT_BC6H_UFLOAT_BLOCK`, WebGPU `bc6h-rgb-ufloat` (both under
  the BC feature we already enable; WebGPU treats it filterable-float - no sample-type surprises).
  The generic block-compressed upload path (bytesPerRow = blocks-per-row * 16) already carries
  BC7; BC6H rides it unchanged.
- The IBL/sky consumers sample through existing float paths - no shader change.

## Verification

- Unit (Texture.Compression tests): BC6H encode of a known 4x4 solid + gradient block decodes
  within tolerance (decode via the vendored reference or a hand-decoded expectation); exact
  `BlockCompressedSize` assertions; the multichannel-Mask guard picks BC7 for a synthetic ORM and
  BC4 for a single-channel mask.
- Cook (Texture.Pipeline tests): an HDR source cooks to BC6H with the full mip chain and the
  expected byte size; Compression = None still escapes; a Normal-usage asset cooks BC7-linear
  (never BC5).
- GPU (Backend probe, the established Vulkan + WebGPU bar): render a BC6H sky (or a fullscreen
  sample of the cooked texture) and assert the readback matches the uncompressed cook of the same
  source within a compression epsilon, Vulkan == WebGPU within the standard epsilon. R7 habit:
  the WGSL/naga cook check runs before any probe (no shader changes expected - run it anyway).
- The page's "Cooks to:" row (texture-page-ux.md) shows `BC6H` / `BC7 (linear)` for the affected
  assets with zero extra UI work - eyeball check in the editor.

## Phasing

- P0 - Part 1 (the correctness fix): Normal -> BC7-linear + the multichannel-Mask guard + tests.
  Small, ships alone; unblocks the page-UX usage inference safely.
  IMPLEMENTED 2026-09-03 (user hit the predicted bug on the chess set's nor_gl + Normal
  preset - white shading). ResolveCompressedFormat gained a `multiChannel` param (the
  cook's channel sniff, tolerance 8 for JPEG chroma noise on gray); builder Version 3 -> 4
  re-cooks existing products. Policy tests + cook tests cover BC7-linear normals, the
  ORM-shaped Mask -> BC7 guard, and gray Mask -> BC4.
- P1 - Encoder: vendor bc6h_enc + EncodeBlockCompressedHdr + unit tests.
- P2 - Policy + cook + runtime conversions + builder bump + cook tests + the GPU parity probe.
- P3 - Docs (authoring guide: "HDR skies cook to BC6H automatically") -> IMPLEMENTED.

## Deferred

- BC5 normals + shader Z-reconstruction (forward + terrain, format-driven flag) - the quality
  step after BC7-linear.
- ASTC HDR profile for the mobile-web variant (astcenc is already vendored and supports it) -
  belongs to the asset-variants ASTC track (P3), not here.
- BC6H signed variant; RDO/rate-distortion encoding modes.

## Touch list

- ThirdParty/bc7enc: + bc6h encoder pair.
- Pipeline/Texture.Compression: policy rows (Normal -> BC7-linear; HDR -> BC6H; Mask multichannel
  guard), EncodeBlockCompressedHdr, BlockCompressedSize, tests.
- Pipeline/Texture.Pipeline: HDR cook path -> HDR encoder; builder Version bump; cook tests.
- Foundation/RHI.Vulkan + RHI.WebGPU: BC6HRGBUfloat conversions.
- Engine backend tests: the BC6H render-parity probe.
- Documentation/Guides/terrain-authoring.md + the texture sections: one-line notes at P3.

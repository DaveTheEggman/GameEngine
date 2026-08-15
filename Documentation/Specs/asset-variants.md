# Per-platform asset variants + texture compression

> STATUS: APPROVED shape (user + Fable, 2026-08-15), ready to build after
> property animation (or interleaved at review gates - coordinate build windows).
> Encoder dependencies (bc7enc/rgbcx + astcenc, vendored) and meshoptimizer
> (for the sibling mesh-lod spec) are USER-APPROVED 2026-08-15.
> Supersedes the sketch in Documentation/Backlog/issues-triage.md I6.

## Why this is one spec, not two

Texture compression is the first REAL producer of platform-divergent cooked
data: desktop wants BCn, future mobile wants ASTC, and the same source asset
must cook differently per export target. Rather than bolt a texture-only
switch into the cook, this spec builds the general PER-TARGET VARIANT AXIS
the pipeline already reserved ("cooked per-target platform variants (one
cooked DB per target - the recipe hash already has a platform-salt seam)" -
Documentation/Systems/asset-pipeline.md, Deferred). Texture compression is
V1's consumer; shader packs, audio codecs, and mesh quality tiers ride the
same axis later without new architecture.

## Decision 1 - variant axis: one cooked DB per target (lazy), NOT
## per-asset variant streams

Weighed 2026-08-15:

- Per-asset variant streams in ONE DB store platform-agnostic products once,
  but change the cooked-DB schema, touch every reader (editor, player,
  export, MCP), split cache invalidation per-variant, and buy that
  complexity for what is today ONE producer (textures).
- Per-target DBs keep the schema untouched and the mental model flat: a
  cooked DB IS a platform's view of the project. The cost is duplication of
  platform-invariant products - addressed below.

RESOLUTION: per-target cooked DBs, with the two sharpenings that remove the
size objection:

1. **Lazy materialization.** The HOST platform DB is the only always-warm
   DB - the editor dev loop is unchanged. A target DB is created/refreshed
   only when an export preset for that target runs (or an explicit
   `Tools.Cook --target <platform>`). No target export = no second DB.
2. **Platform-invariant copy-forward.** Builders declare variance (see
   Decision 3). An INVARIANT product whose recipe hash (minus the platform
   salt) matches the host DB's is copied forward into the target DB, never
   recooked. Scenes, scripts, prefabs, audio, input maps, fonts, and (until
   mesh tiers exist) meshes all copy forward; only genuinely variant bytes
   (textures) cook twice. Disk duplication is bounded by export staging,
   which already copies everything it ships.

## Decision 2 - encoders: vendor bc7enc/rgbcx AND astcenc now

User picked both-now (2026-08-15). BCn (bc7enc/rgbcx: BC1/3/4/5/7 + BC6H
via bc7enc's companion or rgbcx HDR path - confirm exact repo coverage at
vendor time) serves every current target. astcenc (ARM's reference encoder)
lands now so the mobile path exists from day one, even though no shipped
target consumes it yet (see Decision 4 for exactly when web-mobile does).
Both vendored under ThirdParty/ per the SDL precedent, MSVC-native, no
toolchain additions (NO ispc).

## Decision 3 - where things live (library shape)

- `ThirdParty/bc7enc`, `ThirdParty/astcenc` - vendored, untouched layout,
  distinct archive names (the imgui case-collision lesson, 3bb34756).
- `Code/Pipeline/Texture.Compression` (module `texture.compression`) - the
  ONLY code that includes encoder headers. Exposes:
  - `EncodeBlockCompressed(image, TextureCompressionFormat, quality) -> bytes`
    per mip level (caller iterates the existing mip chain);
  - `ResolveCompressedFormat(usage, colorSpace, alpha, TargetPlatform) ->
    rhi::TextureFormat` - THE POLICY TABLE (Decision 5) in one place.
  Pipeline-side and UI-free (the Pipeline rule); heavy third-party headers
  stay in implementation units (GCC module hygiene rule).
- `Code/Pipeline/Pipeline.Core` - `AssetBuilder` gains
  `Variance() -> PlatformInvariant | PlatformVariant` (default INVARIANT so
  every existing builder keeps its behavior; TextureAssetBuilder overrides).
  The recipe hash gains the platform salt ONLY for variant builders - so
  turning on the axis does not dirty every invariant product.
- `Code/Pipeline/Texture.Pipeline` - the consumer: after the existing mip
  generation, encode each level via Texture.Compression when the resolved
  policy says compress. Builder `Version()` 2 -> 3 IN THE SAME COMMIT as
  the product change (the bump IS the migration).
- Runtime: NO new code paths beyond upload. `TextureResource` already
  carries `rhi::TextureFormat`; RHI already enumerates BC1-BC7 + ASTC and
  the WebGPU adapter already reports `textureCompressionBC`/`ASTC`.
  Verify block-compressed upload row-pitch handling per backend (WebGPU
  bytesPerRow is per BLOCK row, 256-byte aligned - the strict path, per
  the WebGPU-stricter rule; validate on the WGSL path too).
- Editor: import dialog + texture page gain the authored compression choice
  (`Editor.Texture`); asset field `compression = Default | None | Quality`
  (Default = policy table; None = today's raw path, the escape hatch;
  Quality = force BC7/ASTC-4x4). Authored data lives on the ASSET (never
  the runtime struct - the no-editor-data-in-runtime rule).

## Decision 4 - the web answer (user asked 2026-08-15)

WebGPU guarantees exactly one compressed-texture family per device:
`texture-compression-bc` on DESKTOP browsers (Windows/macOS/Linux),
ASTC and/or ETC2 on MOBILE browsers. One exported web bundle may be opened
by either - the cook cannot know the client at cook time.

RESOLUTION, in order:

- NOW: the web target cooks **BC** - our web track's reality is desktop
  browsers (Chrome/Dawn verified, Firefox pending). Desktop-web and native
  desktop share the BC policy row.
- WHEN MOBILE-WEB MATTERS: the web export gains a SECOND variant pak (ASTC)
  produced by the same axis, and the wasm boot selects the pak by the
  adapter feature flag we already detect. Variant-selection-at-load, NOT
  runtime transcoding - no Basis Universal dependency, no wasm transcoder
  cost, and it reuses this spec's machinery verbatim.
- ETC2: only if an ASTC-less mobile budget device ever matters; noted as
  the escape hatch (etc2comp-class encoder), deliberately NOT vendored.

## Decision 5 - the format policy table (Default mapping)

| Source | Desktop / desktop-web | Mobile / mobile-web (dormant) |
|---|---|---|
| sRGB color, no alpha | BC1 sRGB (fast) or BC7 sRGB (Quality) | ASTC 6x6 / 4x4 |
| sRGB color + alpha | BC7 sRGB (BC3 only under a size knob) | ASTC 4x4 |
| Normal map (Linear RG) | BC5 | ASTC 4x4 (LA swizzle) |
| Single channel (mask/height) | BC4 | ASTC 4x4 |
| HDR (RGBE/half) | BC6H | ASTC HDR profile |
| UI/pixel-art/small (<= 64px) or None | uncompressed (unchanged) | uncompressed |

Mip composition: generate the existing mip chain FIRST (sRGB-correct
averaging already shipped), then encode every level. Levels smaller than
the 4x4 block are padded per the BC/ASTC spec (encoder handles it); NPOT
textures are fine (block-ceil per level). The 5,592,404-byte full-chain
verification case becomes ~1/4 (BC7) or ~1/8 (BC1) of that - assert the
exact expected size in the cook test.

## Phasing

- **P1 - desktop BCn end-to-end (this week's build item).** Vendor both
  encoders (astcenc builds but is only exercised by tests). Texture.
  Compression lib + policy table; TextureAssetBuilder encodes (v3);
  authored knob + import dialog; upload verified on Vulkan AND WebGPU
  (desktop-web BC). Acceptance: per-format PSNR-floor round-trip tests;
  cooked-product format + exact-size assertions; on-screen probe (a
  BC-cooked texture renders identically-enough on both backends); a real
  project's cooked DB size drop recorded in the commit message.
- **P2 - the variant axis.** Variance() + platform-salted recipe hash for
  variant builders; lazy per-target DB materialization driven by export
  presets (`Tools.Cook --target` for headless); invariant copy-forward;
  export staging reads the target DB. Acceptance: web export produces a
  web-target DB whose textures are BC and whose invariant products were
  copied not recooked (cook-stats assertion); desktop export byte-identical
  to pre-P2; editor dev loop untouched (host DB only).
- **P3 - ASTC dormant path.** Policy-table mobile column live behind a
  target that no preset ships yet; PSNR + format assertions keep it honest.
  No boot-time pak selection until a mobile/web-mobile target exists.
- **Deferred (do NOT build now):** boot-time variant-pak selection for
  mixed web audiences, ETC2, RDO/rate-distortion knobs, GPU-compute
  encoding, per-asset per-platform overrides in the UI.

## Test notes

Doctest in Texture.Pipeline.Tests + Pipeline.Cook.Tests (variance/copy-
forward), both compilers, no UI deps in any Pipeline test target. The
count-tripwire pattern applies if a registration list grows. Re-import is
NOT required for existing projects (compression is cook-side; `Default`
policy applies on next cook after the version bump).

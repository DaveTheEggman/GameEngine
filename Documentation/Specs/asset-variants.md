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
vendor time) serves desktop and desktop-web. astcenc (ARM's reference
encoder) has a REAL consumer from day one: mobile-web, in scope now
(Decision 4) - the web export's ASTC variant pak.
Both vendored under ThirdParty/ per the SDL precedent, MSVC-native, no
toolchain additions (NO ispc).

## Decision 3 - where things live (library shape)

- `ThirdParty/bc7enc`, `ThirdParty/astcenc` - vendored, untouched layout,
  distinct archive names (the imgui case-collision lesson, 3bb34756).
- `Code/Pipeline/Texture.Compression` (module `texture.compression`) - the
  ONLY code that includes encoder headers. Exposes:
  - `EncodeBlockCompressed(image, TextureCompressionFormat, quality) -> bytes`
    per mip level (caller iterates the existing mip chain);
  - `ResolveCompressedFormat(usage, colorSpace, alpha, TargetProfile) ->
    rhi::TextureFormat` - THE POLICY TABLE (Decision 5) in one place.
    `TargetProfile` is a CAPABILITY struct ({bc, astc, etc2} + quality
    default), derived from the export target - the resolver asks "what
    format families does this target support", never "which platform is
    this" (adopted from the 2026-08-15 external review). Consoles/new
    targets later = a new profile, not new resolver branches.
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
  (`Editor.Texture`). TWO asset fields (adopted from the external review -
  inference alone cannot classify a standalone PNG):
  - `usage = Color | Normal | Mask | HDR` - the SEMANTIC hint the policy
    table keys on. Model import sets it automatically from the material
    slot (normal/mask slots known); standalone imports default to Color
    with a dropdown.
  - `compression = Default | None | Quality` (Default = policy table;
    None = today's raw path, the escape hatch; Quality = force
    BC7/ASTC-4x4).
  Authored data lives on the ASSET (never the runtime struct - the
  no-editor-data-in-runtime rule).

## Decision 4 - the web answer (user asked 2026-08-15)

WebGPU guarantees exactly one compressed-texture family per device:
`texture-compression-bc` on DESKTOP browsers (Windows/macOS/Linux),
ASTC and/or ETC2 on MOBILE browsers. One exported web bundle may be opened
by either - the cook cannot know the client at cook time.

RESOLUTION (mobile-web is IN SCOPE NOW - user directive 2026-08-15; we
already ship web, so the web export must serve whatever browser opens it):

- The web export produces TWO variant paks from the same axis: **BC**
  (desktop browsers) and **ASTC** (mobile browsers). The wasm boot selects
  the pak by the adapter feature flag we already detect
  (`textureCompressionBC` / `ASTC` in WebGpuAdapter). Variant-selection-
  at-load, NOT runtime transcoding - no Basis Universal dependency, no
  wasm transcoder cost.
- PRECURSOR (before any compression work makes this urgent): smoke-test
  the CURRENT web build on a mobile browser (Chrome Android; iOS Safari
  where WebGPU is enabled). Today's textures are uncompressed RGBA, which
  every WebGPU device accepts - so if the current build fails on mobile,
  the failure is input/surface/memory, not formats, and it blocks this
  spec's web acceptance. Added to the UAT web session.
- ETC2: only if an ASTC-less mobile budget device ever matters; noted as
  the escape hatch (etc2comp-class encoder), deliberately NOT vendored.

## Decision 5 - the format policy table (Default mapping)

| Source (by authored `usage`) | BC-capable profile | ASTC-capable profile |
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
  STATUS 2026-08-15: the CORE is DONE + tested (a-e). IAssetBuilder::Variance
  (TextureAssetBuilder = Variant), CookTarget{id,bc,astc,etc2} +
  AssetBuildContext::target, recipe platform-salt for variant builders (folds
  through read-deps), ContentDatabase::CopyContentForward, CookDriver
  SetTarget/SetCopyForwardSource + TryCopyForward (CookStats.copiedForward),
  CookForTarget + CookTargetFor helpers, `Tools.Cook --target <id>`
  materializing Cooked/<id>/ + .cache/<id>/. Cook tests: a web-astc target
  recooks the one variant product + copies the invariants forward; an
  invariant product reading variant content recooks per target. REMAINING:
  the export + dist + web-boot integration (pack the target DB; two-pak web;
  boot selection) - out to Fable for a design decision (chicken-and-egg: the
  content pak is fetched before the WebGPU adapter's bc/astc support is known).
- **P3 - mobile-web: ASTC variant + boot-time pak selection.** The web
  export preset produces the BC pak AND the ASTC pak; the wasm boot picks
  by adapter capability before resource binding starts. Acceptance: PSNR +
  format assertions for ASTC; desktop browser loads the BC pak, a mobile
  browser (or a forced-capability test hook) loads the ASTC pak and
  renders the probe scene. Precondition: the mobile-web smoke of the
  CURRENT build (Decision 4) has run, so format work isn't debugging
  input/surface issues at the same time.
- **Deferred (do NOT build now):** ETC2, RDO/rate-distortion knobs,
  GPU-compute encoding, per-asset per-platform overrides in the UI.

### P1 status (2026-08-15) - COMPLETE

The whole desktop-BCn AND mobile-web-ASTC encode/format path is landed and
proven. Web support was DEVELOPED, not deferred (user directive): the ASTC
encoder + format plumbing are real, not test-only. What still sequences after
P2 is only the two-pak PRODUCTION + boot selection, which needs the variant
axis - not the encoder.

- bc7enc/rgbcx + astcenc vendored (`ThirdParty::Bc7enc` / `::Astcenc`);
  Texture.Compression = the ONLY encoder-header includer. Policy table
  (Decision 5) + EncodeBlockCompressed: BC1/3/4/5/7 (block loop) AND ASTC4x4
  (astcenc whole-image, scalar/portable). PSNR round-trip tests for both.
- Authored `usage` + `compression` knobs on TextureAsset (editor-data,
  reflected + a "Compression" group in the texture page). Normal-map import
  heuristic (_normal/... -> Normal + linear). Builder encodes the RGBA8 mip
  chain (2D file + embedded), Version 2 -> 3.
- RHI block helpers (BlockWidth/Height/Bytes, CompressedRowPitch/LevelBytes);
  block-aware per-level upload in the TextureResource factory; Vulkan
  compressed copy fixed (tightly-packed 0/0). ASTC added to the WebGPU format
  map + GetFormatSupport + requested at device creation (Vulkan already had
  it); the profile prefers BC when a target supports both (desktop-first).
- Cook test: format policy + exact per-level block-byte sizes + real size
  drop (128x128 sRGB color = 10936 B BC1 vs 87380 B raw, 8x).
- Real-GPU probe (Integration.TextureCompression): BC1/BC7/BC5 upload +
  sample correct hue on Vulkan AND WebGPU (covers the 256-align path); the
  ASTC-blue leg is capability-gated (self-skips on desktop, runs on mobile HW).

Next: P2 - the per-target variant axis (Variance() + lazy per-target DBs +
invariant copy-forward). That is the prerequisite for the web export to
PRODUCE the BC + ASTC paks and for boot-time selection (Decision 4).

## Open design questions for Fable - P2f + P3 (export + dist + web boot)

The variant-axis CORE (P2 a-e) is built + tested. The remaining work is the
EXPORT + DIST + WEB-BOOT integration, and it has real design forks I want a
ruling on before building. Fable: please add notes inline under each question.

Relevant facts (verified in code):
- Export: `Code/Editor/Editor.Core/Export.cppm` + `ExportImpl.cpp`.
  `ExportContent()` packs `Content.pak` from the project's hardcoded `Cooked/`
  dir (ExportImpl.cpp ~425) and reads `project.CookedDb()` for reachability
  pruning (~394). `ExportProject()` cooks (CookDriver over `project.CookedDb()`)
  then calls `ExportContent()`.
- Dist layout: `<out>/Content.pak` (products + scenes + script, one binary DB),
  `<out>/player.xml` (dist manifest, ProjectSettings shape), `<out>/shaders.dpak`.
- Export preset carries a platform STRING ("Win64"/"Linux64"/"Web") - no enum.
- Web boot: `Code/Engine/Engine.Player/WebMain.cpp` - `MakeOptions()` calls
  `FetchDistFile("player.xml"/"Content.pak"/"shaders.dpak")` synchronously
  BEFORE the app boots, so the content pak is fetched before the WebGPU device
  exists. The adapter's `textureCompressionBC`/`ASTC` flags are only known
  after device creation (inside Initialize).
- `Tools.Cook --target <id>` roots the per-target DB at `Cooked/<id>/` with
  `.cache/<id>/`. NOTE (my own concern): the desktop pack walks `Cooked/`
  RECURSIVELY (`PackTree(cookedMount, "")`), so a `Cooked/web-astc/` subtree
  would be swept into the DESKTOP `Content.pak` unless the per-target DBs live
  OUTSIDE `Cooked/` (e.g. `Cooked.web-astc/` sibling) or the pack walk is
  scoped. This affects "desktop byte-identical". Flagging for the ruling in Q2.

My proposed shape (react / redirect):
1. Desktop target (Win64/Linux64): unchanged - cook host, pack the single
   `Content.pak` from the host DB. Byte-identical to pre-P2.
2. Web target: cook TWO variant targets (web-bc, web-astc) via `CookForTarget`
   (invariant products copy-forward from host, only textures recook per
   target). Pack TWO paks (`Content-bc.pak`, `Content-astc.pak`). `player.xml`
   gains a small content-variants list `[{key:"bc",pak:...},{key:"astc",pak:...}]`;
   desktop dists keep the single pak + empty list.
3. Web boot: resolve the fetch-before-adapter order by fetching only
   `player.xml` (+ `shaders.dpak`) up front, creating the WebGPU device early
   in Initialize, reading the adapter bc/astc flags, then fetching + mounting
   the matching variant pak before the content loader runs. Fallback to
   `Content.pak` when the manifest has no variants (desktop / old bundle).

Questions:
- **Q1 (boot ordering).** Is moving the content-pak fetch to AFTER device-init
  in the web boot sound, or does the PlayerApplication / MEMFS / project-loader
  lifecycle forbid it? If risky, is a pre-Initialize adapter PROBE
  (`requestAdapter` only, read features, then fetch) safer despite
  `requestAdapter` being async while `FetchDistFile` is sync `emscripten_wget`?
- **Q2 (pak layout + dir roots).** Two separate paks vs one pak with both
  variant sub-trees selected by path prefix? And where do per-target cooked DBs
  live so the desktop pack stays byte-identical - `Cooked/<id>/` (needs a
  scoped pack walk) or a sibling `Cooked.<id>/` outside the desktop tree?
- **Q3 (manifest schema).** Extend `player.xml` (ProjectSettings shape) with the
  variants list, or a sidecar? Keep it minimal.
- **Q4 (correctness traps).** Anything in the copy-forward-per-target + two-pak
  flow I'm missing - scenes/scripts staged once vs per-pak, reachability
  pruning per target, the shader pack (already platform-keyed via
  `StageShaderPack(preset.platform)`)?

Constraints to hold: "variant-selection-at-load, no transcoder" (Decision 4);
"desktop byte-identical"; "editor dev loop untouched (host DB only)".

## Test notes

Doctest in Texture.Pipeline.Tests + Pipeline.Cook.Tests (variance/copy-
forward), both compilers, no UI deps in any Pipeline test target. The
count-tripwire pattern applies if a registration list grows. Re-import is
NOT required for existing projects (compression is cook-side; `Default`
policy applies on next cook after the version bump).

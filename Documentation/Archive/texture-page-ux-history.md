# Texture Page UX: Usage-First Authoring

Status: BUILT (Fable, 2026-09-01) - all five design points landed as specced: usage-first
Content block with derived color space (one undo entry), the mismatch lint row (live
SetRowVisible, warning + the softer inverse hint), profile BUTTONS replacing the sentinel
enum (7 profiles incl. the new Normal Map / Data Mask; SetupFor* now set usage+colorSpace so
the importer helpers and the page share one definition), importer token inference
(InferTextureUsage, exported + tested incl. the boundary rule), and the read-only "Cooks to:"
row (desktop + mobile when they differ). Tests: profile pairs + inference in
Texture.Pipeline.Tests; policy-row anchors + the derivation/lint contract in
Editor.Texture.Tests. USER VISUAL PASS OWED (absorbed into week-2026-09-05). The compression
track (texture-compression-hdr-and-normals.md) is now UNBLOCKED.

Original status: READY for build (authored by Fable, 2026-08-26). SHIPS FIRST - the compression track
(texture-compression-hdr-and-normals.md) follows and its resolved-format preview row lands here.

## Problem

The texture page asks "what is this texture?" three times in three vocabularies and lets the
answers disagree:

- **Color Space** (Srgb / Linear) - how the GPU interprets the bytes at sample time.
- **Usage** (Color / Normal / Mask / HDR) - what the texture IS; drives the compression policy's
  BC family.
- **Preset** (UI / Sprite / 3D / Equirect Skybox / Cubemap Skybox) - Sedulous-derived
  sampler/shape setups.

They overlap almost completely: Usage semantically IMPLIES Color Space (Normal / Mask / HDR are
ALWAYS Linear; only Color has a real choice, and even there sRGB is right ~95% of the time), and
the presets predate the usage/compression axis entirely - none of them set `usage`, and only the
Equirect preset touches `colorSpace`. Nothing stops an author picking Usage = Normal +
Color Space = Srgb, which silently warps the data - the exact trap from the terrain PBR work
(hand-imported normal map left at the Srgb default).

The Preset row is also the wrong SHAPE of control: an EnumEditor whose index 0 is an
"(apply preset)" sentinel. It is an ACTION disguised as a property - its displayed value is
whatever was clicked last, not a fact about the asset, and two identically-configured assets can
show different "Preset" values. Useful idea (one click configures the sampler coherently), wrong
presentation, stale content.

## Design: one primary question, everything else derived or grouped

### 1. Usage is the primary field; Color Space derives from it

- Selecting Usage AUTO-SETS Color Space in the same undo entry: Color -> Srgb;
  Normal / Mask / HDR -> Linear.
- Color Space stays visible but demoted (an "advanced" row under the Content block), for the one
  legitimate override (linear color data, e.g. a LUT authored as an image). Setting it manually is
  allowed - see the mismatch lint below.
- Relabel the Usage items for authors: "Color", "Normal map", "Data mask (roughness / AO / height
  / coverage)", "HDR".

### 2. The mismatch lint

A warning row appears (theme warning color, small text) when the stored combination is
semantically wrong: `usage != Color && colorSpace == Srgb` -> "Normal/Mask/HDR maps are data -
Srgb will warp the values. Set Linear." (and the inverse hint for Color + Linear, phrased softer -
that one is occasionally intentional). The lint is presentation only - no data change, no
migration; it fires on existing assets that carry the bad combination today, which is the point.

### 3. Presets become profile BUTTONS, and they set the whole story

Replace the sentinel-enum with a labeled row of BUTTONS ("Apply profile:") - the honest shape for
an action. Each applies sampler + shape + usage + colorSpace coherently as ONE undo entry
(ApplyEdit, the page's existing whole-asset snapshot model):

| Profile          | usage  | colorSpace | sampler (existing SetupFor* body)          |
|------------------|--------|------------|--------------------------------------------|
| UI               | Color  | Srgb       | linear, clamp, no mips, aniso 1            |
| Sprite (pixel)   | Color  | Srgb       | nearest, clamp, no mips, aniso 1           |
| 3D Surface       | Color  | Srgb       | trilinear, repeat, mips, aniso 16          |
| Normal Map       | Normal | Linear     | trilinear, repeat, mips, aniso 16          |
| Data Mask        | Mask   | Linear     | trilinear, repeat, mips, aniso 16          |
| Equirect Sky     | HDR    | Linear     | linear, clamp, no mips (existing preset)   |
| Cubemap Sky      | HDR    | Linear     | cubemap shape + existing preset            |

The three NEW profiles (Normal Map / Data Mask, plus HDR on the sky rows) are where the old
presets were stale. `SetupFor*` in TextureAsset gains the usage/colorSpace assignments (Equirect/
Cubemap set usage = HDR; new SetupForNormalMap / SetupForDataMask wrap SetupFor3D + the
usage/colorSpace pair) so the importer helpers and the page share one definition.

### 4. Import-time usage inference (filename suffixes)

`TextureFileImporter` seeds usage + colorSpace from the universal texture-pack suffixes, checked
case-insensitively against the stem: `_nor` / `_normal` / `_nrm` -> Normal map;
`_disp` / `_height` / `_mask` / `_rough` / `_ao` / `_orm` / `_arm` / `_metal` -> Data mask;
`.hdr` / `.exr` extension -> HDR (+ the equirect sampler setup, today's behavior for .hdr).
Everything else keeps the Color default. The result is just the stored fields - the page shows
what was inferred and the author corrects it like any edit. No magic beyond the seed.

### 5. Page layout regrouped

- **Preview column** (unchanged): image + source facts.
- **Content** block: Usage, then the derived Color Space (advanced), then Compression choice,
  then a read-only "Cooks to:" row - `ResolveCompressedFormat(...)` evaluated for the desktop
  profile (and the mobile profile when it differs), so "Default" stops being opaque. This row is
  where the compression track's changes become visible with zero extra UI.
- **Sampler** block: shape, filters, wraps, mips, anisotropy (the existing rows).
- **Profiles** row: the buttons from (3), placed above Content.

## Non-goals

- No TextureAsset serialization changes beyond the SetupFor* helper bodies (usage/colorSpace are
  existing fields; no DataVersion bump, no re-cook).
- No policy/encoder changes (the compression track owns those; the "Cooks to:" row simply renders
  today's policy).
- No stored "profile" field - profiles are actions, not state (deriving a display value back from
  nine fields is guess-work; the honest answer is the fields themselves).

## Verification

Editor.Texture.Tests (headless page tests exist - extend):
- Selecting Usage = Normal auto-sets colorSpace = Linear (one undo entry; undo restores both).
- The mismatch lint row exists exactly when usage != Color && colorSpace == Srgb.
- Each profile button applies its full row from the table (spot-check Normal Map + Equirect) as
  ONE undo entry.
- The "Cooks to:" row matches ResolveCompressedFormat for a known asset (Color/alpha -> BC7 srgb;
  Compression = None -> uncompressed).
- Importer inference: `foo_nor_gl_4k.png` -> Normal/Linear; `bar_disp_2k.png` -> Mask/Linear;
  `sky.hdr` -> HDR/Linear; `grass_diff.jpg` -> Color/Srgb.

## Touch list

- Pipeline/Texture.Pipeline: SetupFor* gain usage/colorSpace; new SetupForNormalMap /
  SetupForDataMask; TextureFileImporter suffix inference.
- Editor/Editor.Texture: page regroup (Content / Sampler blocks), usage->colorSpace derivation,
  mismatch lint row, profile buttons replacing the sentinel enum, "Cooks to:" row.
- Editor.Texture.Tests + Texture.Pipeline tests per Verification.

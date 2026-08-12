# Export Templates

> Status: CURRENT
> Verified: 2026-08-12 @ b5d0418b
> Track: [[settings-and-export-track]]

An export template is a portable, prebuilt RUNTIME bundle for one `(platform, config)`: a player
executable, that build's runtime sidecars, optional symbols, and a `template.xml` manifest. A project's
preset references a template and adds the game half (cooked content + extras + naming). Template
(runtime) + preset (game) -> dist. Shipped. Overview: `Documentation/Systems/export.md`.

The line held: a template is the RUNTIME you ship against - never game content, never editor code (the
player links zero editor), never a toolchain (the bundle is prebuilt, so using it needs no compiler -
the whole point vs an SDK-toolset model).

## The config axis

A template is `(platform, config)`: Debug / Release / RelWithDebInfo are SEPARATE templates, not
profiles inside one. Rationale: it is what the build already produces (each `Bin/<Config>/<Platform>-
<Compiler>` dir is a self-contained runtime with its own `<player>.runtime-libs`); sidecars are
config-specific (debug vs release link different native runtimes); and size (tripling a player+deps for
three configs is wasted disk when most projects ship Release only). Product stance: config is
first-class so "release template" and "debug template" are both expressible, but the product defaults to
and optimizes for RELEASE - Debug/RelWithDebInfo are opt-in dev/power-user variants.

The host implicit template inherits the running tool's config (develop the editor in Debug -> the
zero-setup host template is a Debug player), so shipping requires an explicit Release template; the
config axis makes this a non-special-case (the host template carries whatever config built it).

## Identity + resolution

Identity = `platform x config x engineVersion` (e.g. `draconic-win64-release-0.1.0`); `compiler` is
METADATA for traceability, not a selector; `arch` folds into the platform tag. A preset selects
`(platform, config)` (config defaulting to Release), resolved by `TemplateRegistry::FindBy(platform,
config)` with an explicit `templateId` winning and a platform-only fallback (nearest config, preferring
Release) so old presets resolve. `EffectiveConfig()` treats an unstamped (v1) template as Release.

## Payload + manifest

`template.xml` (shipped): `id`, `name`, `platform`, `config`, `compiler`, `engineVersion`,
`playerBinary`, categorized runtime files - `sidecars` (REQUIRED, always staged, read from the build's
`runtime-libs` manifest) + `symbols` (optional PDB/DWARF, staged only when the preset opts in) - and
`notes` (plus runtime-resolved, non-serialized `directory` / `isHost`). Symbols do not justify a
separate template; they are an optional group on Debug/RelWithDebInfo templates, and export defaults to
NOT staging them into the shipped dist (ship stripped, retain symbols beside the template for
symbolication). If the player runtime-compiles shaders, its compiler (e.g. `dxcompiler`) is just another
required runtime-lib.

## Create / Import / Resolve

- **Create** - `CreateTemplate(configDir, destRoot, mode)` synthesizes the descriptor from a build dir
  (`SynthesizeHostTemplate` reads platform + `runtime-libs`), stamps config/compiler/engineVersion,
  copies the player + sidecars (`FileCopyPreserving`, keeps +x), writes `template.xml`. Two modes:
  install into the templates root (usable immediately) or export to a folder (for zip + distribution).
  A Web build synthesizes a "Web" template (the player is the `.html`, sidecars from the manifest, and
  export stages page + sidecars + `Content.pak` + `player.xml` + the WGSL `shaders.dpak`).
- **Import** - `ImportTemplate` (validate `template.xml` -> recursive copy under `<id>`).
- **Resolve** - as above.

## Deferred

- **Baseline engine content** (open question). Does the player need default assets (fonts, fallback
  textures, later shaders) to boot? Today ~none: the core render-pass shaders are inline HLSL baked into
  the player binary, so passes need no external shader pak. If shaders leave C++ (the shaders-out-of-C++
  sub-track), a baseline shader pak would ride in the template as runtime content. The biggest
  structural open item.
- **`capabilities`** manifest field (compiled-in RHI backends so a preset can require one) - forward-
  looking, not built.
- **Downloadable templates** (fetch a versioned archive; same install path as import) - future.

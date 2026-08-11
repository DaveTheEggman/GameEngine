# Export Templates — what a template is, and the config axis

Status: design (extends the shipped template/preset system). Related: [export.md](export.md),
[export-reachability.md](export-reachability.md).

## What a template is

An **export template** is a portable, prebuilt **runtime bundle** for one platform *and* one build
config: a player executable, that build's runtime sidecars, and a `template.xml` manifest. A project's
**preset** references a template and adds the game-specific half (cooked content + extra files +
naming). Template (runtime) + preset (game) → dist. The template exists so a project can be packaged
into a runnable dist **without a compiler or SDK** — you distribute/import a prebuilt bundle
(Godot-export-templates style), and the host build synthesizes an implicit template for zero-setup dev
export.

The line to hold: a template is the **runtime you ship against**, never game content and never editor
code (the player links zero editor).

## The config axis (decision)

A template is **`(platform, config)`** — Debug / Release / RelWithDebInfo are **separate templates**,
not profiles nested inside one. "Windows Release", "Windows Debug", "Linux RelWithDebInfo" are each their
own bundle. Rationale:

- **It's what the build already produces.** Each `Bin/<Config>/<Platform>-<Compiler>` directory is a
  complete, self-contained runtime with *its own* `<player>.runtime-libs`. `CreateTemplate` just
  packages one such directory — no multi-config logic; point it at a config dir and it becomes that
  template.
- **Sidecars are config-specific.** Debug vs Release link different native runtimes (MSVC debug CRT /
  `SDL3d.dll` vs release / `SDL3.dll`). Keeping configs as separate templates keeps each sidecar list
  internally consistent; bundling all configs into one template would mix incompatible sidecar sets.
- **Size.** A template is a player + native deps; tripling that for three configs is wasted
  download/disk when most projects only ever ship Release.

**Product stance:** config is a first-class axis so "release template" and "debug template" are both
expressible, but the product defaults to and optimizes for **Release**. Debug / RelWithDebInfo are
opt-in dev/power-user variants (debug a *packaged* build outside the editor; profile a shipped build
with symbols). The common path stays trivial: one Release template per platform, which is all most
projects import.

## Identity & resolution

- **Identity = `platform × config × engineVersion`.** Example id: `draconic-win64-release-0.1.0`.
- **`compiler` is metadata**, recorded for traceability, *not* a selector — you ship one canonical
  compiler build per platform. (Promote to a selector only if a real need appears.)
- **`arch` folds into the platform tag** (`Win64` = x64; ARM would be a new tag, e.g. `WinArm64`).
- **A preset selects `(platform, config)`**, `config` defaulting to **Release**. Registry resolution
  becomes `FindBy(platform, config)` (today it is `FindByPlatform(platform)` — see Migration).

## Host template nuance

The implicit **host template inherits the running tool's config** — develop the editor in Debug and the
zero-setup host template is a *Debug* player. That is fine for iteration, but **shipping requires an
explicit Release template** (created/imported); you never ship your Debug editor's player. The config
axis makes this a non-special-case: the host template just carries whatever config built it.

## Template payload

1. **The player executable** — one, for this platform+config.
2. **Runtime sidecars for this config** — the per-config `runtime-libs` (native shared libs / DLLs). If
   the player runtime-compiles shaders, its compiler (e.g. `dxcompiler`) is just another runtime-lib
   here — already covered by the per-config list.
3. **Symbols** (optional) — PDB / DWARF, for RelWithDebInfo/Debug. Categorized, strip-on-ship (below).
4. **Baseline engine content** (open) — assets the player needs to *boot* that are runtime, not game.
   Today this is ~empty: the 13 core render-pass shaders (`Render/*Shaders.cppm`) are **inline HLSL
   baked into the player binary**, so the passes need no external shader pak. Revisit if/when shaders
   move out of C++ (the shaders-out-of-C++ sub-track), at which point a baseline shader pak would ride
   in the template as runtime content. See open questions.

## Manifest schema (`template.xml`)

Current fields (shipped): `id`, `name`, `platform`, `engineVersion`, `playerBinary`, `sidecars[]`,
`notes` (plus runtime-resolved, non-serialized `directory` / `isHost`). Proposed additions:

- **`config`** — `Debug` / `Release` / `RelWithDebInfo` (identity). Absent ⇒ `Release` (back-compat).
- **`compiler`** — metadata string (e.g. `MSVC`, `Clang`, `GCC`).
- **Categorized sidecars** — replace the single `sidecars[]` with groups:
  - `required` — must always stage (the runtime libs).
  - `symbols` — optional; stripped from the shipped dist by default (kept beside the template for
    symbolication).
  - `optional` — situational extras.
  (Or keep `sidecars[]` = required and add a parallel `symbols[]`; a nested/categorized shape reads
  better and is the recommendation.)
- **`capabilities`** (forward-looking, optional) — e.g. compiled-in RHI backends (`Vulkan`, `D3D12`), so
  a preset can require a backend.

## Symbols policy

Symbols do **not** justify a separate template. They are an **optional sidecar group** on the
RelWithDebInfo/Debug templates, and **export/preset decides** whether to stage them. Default: **do not**
stage symbols into the shipped dist (ship stripped; retain the symbol files beside the template for
later crash symbolication).

## Create / Import / Resolve flow

- **Create** — `CreateTemplate(configDir)` synthesizes the descriptor from a build dir
  (`SynthesizeHostTemplate` reads platform + `runtime-libs`), stamps `config` + `compiler` +
  `engineVersion`, copies the player + sidecars (`FileCopyPreserving`, keeps +x), writes `template.xml`.
  Two output modes: **install** into the templates root (`templatesRoot/<id>`, usable immediately) or
  **export to a folder** (for zip + distribution). Config comes from *which* `Bin/<Config>/…` dir you
  package.
- **Import** — existing `ImportTemplate` (validate `template.xml` → recursive copy under `<id>`).
- **Resolve** — preset `(platform, config)` → `TemplateRegistry::FindBy(platform, config)`; explicit
  `templateId` still wins. Imported bundle out-ranks the synthesized host (unchanged).

## What is NOT in a template

- **Game content** — cooked pak + game-specific extras come from the project/preset side.
- **Editor code** — the player links none.
- **Toolchain / compiler** — the bundle is prebuilt; using it needs no compiler (the whole point vs an
  SDK-toolset model).

## Migration from the shipped schema

- Add `config` (default `Release` when absent) — existing single-platform templates keep resolving.
- Add `compiler` metadata; split/extend sidecars into required/symbols(/optional) — a reader that only
  knows `sidecars[]` still works if `required` maps onto it.
- `TemplateRegistry`: `FindByPlatform` → `FindBy(platform, config)`, config-aware; keep a
  platform-only fallback (nearest config, preferring Release) so old presets without a config resolve.
- Host id convention `host-<platform>` → `host-<platform>-<config>`; created id
  `draconic-<platform>-<config>-<engineVersion>`.

## Open questions

1. **Baseline engine content** — does the player need default assets (fonts, fallback textures, and
   later shaders) to boot? Today ~none (shaders baked in). If yes, they live in the template as a
   baseline pak (runtime, not game). *Biggest structural item; revisit when shaders leave C++.*
2. **Config as an explicit preset field** vs. always-Release-with-override. (Lean: explicit `config`,
   default Release.)
3. **Symbols**: categorized sidecar with strip-on-ship (recommended) vs. never in templates.
4. **Do we expose Debug templates at all**, or restrict to Release + RelWithDebInfo (Debug = editor-only
   / run-from-Bin)? (Lean: allow, but de-emphasize.)
5. **Sidecar shape**: nested categories vs. flat `required` + parallel `symbols[]`.

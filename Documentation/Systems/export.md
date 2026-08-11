# Draconic — Export & Export Templates (design)

Plan of record for the project **export** system: turning an authored project into a shippable
dist, per-platform, driven uniformly from the CLI and the editor (the same way cook is uniform).

Status: **mostly SHIPPED** (updated 2026-07-20). The template/preset architecture (§2), data formats
(§3), templates-root resolution (§5), preset→template resolution (§6), the uniform driver (§7), and
both surfaces' core flows (§8) all landed, along with the precursors (§11). **Two items are queued and
now handed off to fable** — see *Current status & handoff*. Sections §4/§10/§11 are historical
(the work they describe is done); §2/§3/§6/§7 remain the accurate architecture reference.

Companion design docs (the queued work): **[export-templates.md](export-templates.md)** (the config
axis — a template is `platform × config`; the create/build-template command) and
**[export-reachability.md](export-reachability.md)** (pruning the dist to a reachable closure).

---

## Current status & handoff (2026-07-20)

**SHIPPED (in `draconic.editor.core`, the `:export_pipeline` + `:export_preset` + `:export_template`
partitions; CLI `DraconicExport`; editor `File > Export…` / `Import Template…` / `Preferences…`):**

- **ExportPreset** `{ name, platform, templateId, playerName, outputSubdir, additionalFiles[] }` —
  the §4 revision is done (`playerSource` dropped; `templateId` + `additionalFiles` added). XML
  round-trips (`export_presets.xml`).
- **ExportTemplate** `{ id, name, platform, engineVersion, playerBinary, sidecars[], notes }` +
  `template.xml` + **TemplateRegistry** (Refresh / FindById / FindByPlatform / Resolve) +
  **host implicit template** synthesized from the running tool's dir.
- **Config-driven sidecars** — `draconic_copy_runtime_deps` emits `<player>.runtime-libs` at build;
  the host template reads it. (`KnownSidecars` in §3 was **replaced** by this — the runtime-libs
  manifest is authoritative.)
- **Uniform driver** — `ExportContent` (cook-less pack/stage) factored out of `ExportProject`
  (cook + content); `ExportOne` / `ExportAll` with a `cook` flag, an `ExportProgress` sink, and
  `ExportResult { content, filesStaged, outputDir, engineVersionWarning }`.
- **Engine-version soft-match** — a resolved template whose `engineVersion` differs warns (CLI prints
  it; editor Console shows it) and still exports.
- **CLI** — `DraconicExport <project> [--out --preset|--all --rebuild]`, `--template list|import`; the
  `../../Player` walk is deleted; host-preset fallback when no `export_presets.xml`.
- **Editor** — `File > Export…` runs **non-blocking** (cook via `EditorCookService`, then pack/stage on
  `EditorJobService`), status-bar progress, a **sticky success toast with Open Folder** (native reveal
  via `core::OpenPathInFileManager` — Explorer / xdg-open); `File > Import Template…` via the native
  folder dialog.
- **Templates root** — configurable: `EditorExportSettings.templatesRoot` (Settings phase 2) →
  `ResolveTemplatesRoot(override → $DRACONIC_TEMPLATES_DIR → <user-data>/templates)`; edited in
  **`File > Preferences…`**.
- **Precursors (§11)** — both shipped: `draconic.settings` + Core `GetEnvironmentVariable` /
  `GetUserDataDirectory`; shell `IDialogService` (SDL3) file/folder dialogs + `OpenPath` reveal.
- **Under-the-hood (fable's rework)** — export lives in the `:export_pipeline` partition; file ops use
  native `core::FileCopyPreserving` / `CreateDirectories` (not `std::filesystem`); scene/prefab source
  streams are **pre-transcoded to binary** before packing (`CollectSceneStreams` /
  `TranscodeSceneStreamToBinary`); the editor **mutation lock covers background jobs** (closes the
  export-vs-UI DB race).

**QUEUED — handed off to fable (2026-07-20):**

1. **Create / build templates** — *nothing produces a template bundle on disk yet.* The host template
   is only ever synthesized in memory; there is no `--template create` / editor "Create Template…", so
   Import can't be exercised end-to-end. Design + the **config axis** (a template is `platform × config`;
   Debug/Release/RelWithDebInfo are separate templates; `config`/`compiler`/categorized-sidecar manifest
   additions; symbols policy; open questions incl. baseline engine content) live in
   **[export-templates.md](export-templates.md)**.
2. **Reachability pruning** — export currently packs **every** scene (`CollectScenes(RootGroup())`) and
   the **whole** cooked dir, so dists carry unreferenced assets. Design (closure of default-scene +
   startup + "Always Export" roots; typed `AssetRef`; Wren-scan lint; opt-in per preset) lives in
   **[export-reachability.md](export-reachability.md)**.

**Remaining editor UI (not queued to anyone yet):** a templates-manager list/remove panel (import is
currently fire-and-forget) and **preset authoring** (presets are hand-edited in `export_presets.xml`
today — no add/edit UI).

---

## 1. Problem & goals

- Export a project to a runnable dist: **content** (`Content.pak` + `player.xml` manifest — exists) plus
  a **player executable + its runtime files** (today: hard-coded `argv[0]/../../Player/DraconicPlayer`
  walk in the CLI, which the `Bin/<Config>/<Platform>-<Compiler>` layout change broke, and which the
  editor never did at all).
- **Per-platform** export and **multiple exports at once**.
- **Uniform** across CLI + editor (one driver, like cook).
- **Author export configs in the editor** — pick a player, add files, save, reuse.
- **Importable templates** (Godot-style): the prebuilt player bundles are shareable/downloadable
  artifacts, decoupled from any one machine's paths.

Non-goal (now): cross-compiling players at export time. We stage *prebuilt* player binaries.

---

## 2. Architecture — templates vs. presets

The one decision everything hangs on: **separate the portable prebuilt bundle from the project-local
config.**

### `ExportTemplate` — the portable, installable bundle (machine-local, NOT committed)
A per-platform prebuilt player + its runtime deps + a manifest. Lives in a **templates root** outside
any project, so it is shared across projects and referenced by id — never by absolute path.

Disk layout:
```
<templatesRoot>/<templateId>/
    template.xml        # manifest (see §3)
    DraconicPlayer[.exe]  # the player binary
    SDL3.dll            # runtime sidecars this template needs
    dxcompiler.dll
    ...
```

Sources of templates:
- **Imported** — copy/extract a template folder (later: archive) into the templates root.
- **Host/dev implicit template** — the running tool's own directory (`Bin/<Config>/<Platform>-<Compiler>/`)
  already holds the player + its sidecars. It is surfaced as a virtual template `host-<platform>` so a
  dev export works with **zero setup** (no import needed for the platform you're on).
- **Downloadable** (future) — fetch a versioned template archive; same install path as import.

### `ExportPreset` — the project-local config (committed with the project)
References a template and adds game-specific choices. No machine paths, so it is safe to commit and
share. Persisted to `<project>/export_presets.xml`.

Fields:
| field | meaning |
|---|---|
| `name` | "My Windows Build" |
| `platform` | `Win64` / `Linux64` — target tag (also selects a template when `templateId` is blank) |
| `templateId` | which template to use; blank = the installed template for `platform` (host template counts) |
| `playerName` | output exe name; blank = the template's player basename |
| `outputSubdir` | export-root-relative output dir; blank = sanitized `name` |
| `additionalFiles` | **game-specific** extra files to copy into the dist (icon, config, data) — beyond the template's own sidecars |
| *(later)* `productName`, `icon`, `version`, per-platform options | |

The template supplies the player + engine-runtime sidecars; the preset supplies game content + naming.
Clean split of "engine build" vs "game build."

---

## 3. Data formats

Both are versioned XML (the `BeginVersionedPayload` idiom, same as `ProjectSettings`/`player.xml`).

`template.xml` (in a template dir):
```
ExportTemplate v1:
    id            string   # stable id, e.g. "draconic-win64-0.1.0" (unique in the templates root)
    name          string   # display name, "Windows Desktop 0.1.0"
    platform      string   # Win64 / Linux64 / Mac64
    engineVersion string   # engine this was built against (soft-matched; warn on mismatch)
    playerBinary  string   # filename of the player exe within the template dir
    sidecars      [string] # runtime files (relative to the template dir) staged beside the player
    notes         string   # optional
```

`export_presets.xml` (in a project) — `ExportPresetSet { presets: [ExportPreset] }`, per §2.

`KnownSidecars(platform)` — **superseded (not built).** Instead of a hard-coded per-platform list, the
build emits a config-specific `<player>.runtime-libs` (`draconic_copy_runtime_deps`) that the host
template reads; the template's manifest stays authoritative at export time. See *Current status*.

> Note: the manifest schema above is the shipped v1. The **config axis** (adding `config` /
> `compiler` / categorized sidecars) is designed but not yet built — see
> [export-templates.md](export-templates.md).

---

## 4. Revision to the landed ExportPreset (piece 1)  — ✅ DONE (historical)

Piece 1 shipped `ExportPreset { name, platform, playerSource(dir), playerName, outputSubdir,
runtimeFiles }`. Revise to the §2 shape:
- `playerSource` (dir) → **drop**; the player comes from the referenced template.
- add `templateId` (blank = resolve by platform).
- `runtimeFiles` → `additionalFiles` (game-specific extras only; engine sidecars belong to the template).
- keep `name`, `platform`, `playerName`, `outputSubdir`.

Cheap — nothing consumes the piece-1 schema yet.

---

## 5. Templates root (where installed templates live)

Resolution order (first that exists / is set wins):
1. `DRACONIC_TEMPLATES_DIR` env override (CI, portable installs).
2. **Editor-configured root** — settable in the editor, persisted to **editor settings** (§11 precursor).
3. Engine/tool-relative `Templates/` (a checkout or packaged install can ship templates alongside).
4. User data dir — `$XDG_DATA_HOME/draconic/templates` (Linux), `%LOCALAPPDATA%\Draconic\templates`
   (Windows), `~/Library/Application Support/Draconic/templates` (macOS) — the default.

The **host implicit template** is always available regardless of the root — it is synthesized from the
running tool's own directory, so a fresh machine can still export for its own platform.

---

## 6. Resolution: preset → template

`TemplateRegistry` enumerates `<templatesRoot>/*/template.xml` + synthesizes the host template.
Resolve a preset:
1. `preset.templateId` non-blank → that exact template (error if missing/uninstalled).
2. else the installed template whose `platform == preset.platform` (prefer a real import over host).
3. else, if `preset.platform == host` → the host implicit template.
4. else → error: "no export template installed for `<platform>` — import one."

Engine-version mismatch between a template and the running engine → **warn**, don't block.

---

## 7. ExportDriver — the uniform entry point

`draconic.editor.core`, called identically by the CLI and the editor (the cook-uniformity pattern
extended to the whole dist). Player-staging moves **out of the CLI `Main.cpp`** into here.

```
ExportOne(project, preset, registry, templates, outRoot, rebuild, &stats):
    template = templates.Resolve(preset)              # §6, error if none
    outDir   = outRoot / (preset.outputSubdir | sanitized(preset.name))
    ExportContent(project, outDir, registry, rebuild) # existing: cook + stage scenes + pack + manifest
    Stage(template.playerBinary -> outDir / preset.ResolvedPlayerName(template))
    for f in template.sidecars:        Stage(template_dir/f -> outDir/f)
    for f in preset.additionalFiles:   Stage(resolve(f)     -> outDir/basename(f))

ExportAll(project, presets, ...) -> loop ExportOne, aggregate stats
```

`ExportContent` = today's `ExportProject` body (cook/stage/pack/manifest), factored out so the driver
composes content + runtime staging. Missing template / missing file → clear error, that preset fails,
others continue (report per-preset).

---

## 8. Surfaces

### CLI — `DraconicExport`
```
DraconicExport <projectDir> [--out <dir>] [--preset <name> | --all] [--rebuild]
DraconicExport --template list
DraconicExport --template import <dir>          # install a template into the templates root
```
- No `--preset`/`--all` and no `export_presets.xml` → synthesize a **host preset** on the fly (today's
  `DefaultExportPresets`, unsaved) so a quick dev export still works out of the box.
- Host tool dir (for the host implicit template) comes from `argv[0]` — replaces the `../../` walk.

### Editor
- **Templates manager** — list installed templates + the host template; **Import…** (folder now,
  archive later); remove. Shows engine-version match state.
- **Export presets panel** — list presets from `export_presets.xml`; **Add** (pick a template, set
  output name/subdir, add game files) / edit / duplicate / delete; **Export** per preset + **Export All**.
- Both call the shared `ExportDriver`. Editor export = greenfield (nothing exists today).

---

## 9. Decisions (recommended; confirm before/while building)

- **Templates ≠ presets** (§2) — the core split. LOCKED as the direction.
- **Templates root** = user data dir default (§5), **overridable in the editor** and saved to editor
  settings (§11). Not project-local (keeps binaries out of the repo).
- **Host implicit template** — always synthesized from the running tool's dir, so dev export needs no
  import. YES.
- **Preset references template by id/platform** — never an absolute path, so presets are committable.
- **Import format** — folder now; **archive (zip) later**, tied to the pak/compression roadmap item.
- **Engine-version match** — soft (warn), not enforced (for now).
- **Standalone-player relocatability** — the template bundles the player's sidecars, and export copies
  them beside the player, so the dist is self-contained **as long as the template lists them** (Linux
  DXC via rpath needs `$ORIGIN` + the .so in the dist; captured in the host template's sidecars).

---

## 10. Phased build plan  — ✅ phases 1–4 DONE (historical; see *Current status*)

1. **ExportPreset skeleton** — ✅ landed (`c13be2b`); revised by §4.
2. **Templates** — `ExportTemplate` + `template.xml` + `TemplateRegistry` (enumerate root + synthesize
   host template) + templates-root resolution (§5) + `KnownSidecars`. Tests: round-trip, host synthesis,
   resolution.
3. **ExportDriver + CLI** — factor `ExportContent`, add `ExportOne`/`ExportAll`, move player+sidecar+
   additionalFiles staging into the library, delete the `../../` walk; CLI `--preset`/`--all`/host
   fallback + `--template list|import`. Tests: fake template dir → dist has player + sidecars + content.
4. **Editor UI** — templates manager + export presets panel, both on the driver. **Needs the §11
   precursors** (settings for the configurable root; native file/folder dialogs for picking a player
   binary, additional files, a template folder to import, and the output dir).
5. **Future** — archive (zip) templates + download/import-from-URL + engine-version enforcement +
   product metadata (icon/name/version) + per-platform options.

Phases 2–3 (templates, driver, CLI) do **not** need the precursors — the CLI takes paths as args — so
they can land first and deliver working per-platform export while the precursors are built for Phase 4.

---

## 11. Precursors (needed before Phase 4, useful engine-wide)  — ✅ BOTH DONE (historical)

Two systems the editor export UI depends on. Both shipped: `draconic.settings` (+ Core
`GetEnvironmentVariable`/`GetUserDataDirectory`) and the shell `IDialogService` (SDL3 file/folder
dialogs + `OpenPath` reveal). Both are broadly useful beyond export.

### 11.1 Settings system (core)
Editor settings need a home (configurable template root, and much more later: recent projects, window
layout, theme, tool prefs). Model on **Traktor's `PropertyGroup`/`Settings`** — a serialization-integrated
property tree in core:
- A dynamic **property group** (nested named values: bool/int/float/string/string-list/child groups) so
  any subsystem contributes keys without a fixed schema — the editor's extensibility need.
- Serialized through the **existing stack** (`ISerializer` + versioned payloads; XML backend for
  human-readable settings files), same idiom as `ProjectSettings`.
- Storage: **user/global** editor settings in the user data dir; **per-project** settings alongside the
  project; layered (project overrides user overrides defaults).
- Lives in `draconic.core` (the primitive) + an editor-settings consumer on top.
- Open questions for its own design: dynamic property-bag vs reflected-struct payloads (lean bag, for
  extensibility, like Traktor), change notification, and the layering rules.

### 11.2 Shell file/folder dialogs
The export UI must pick a player binary (file), additional files (files), a template folder (folder),
and an output dir (folder). Add native dialogs to the **shell interface**:
- `IDialogService` (or on `IShell`): `ShowOpenFileDialog` / `ShowOpenFolderDialog` / `ShowSaveFileDialog`
  — async (callback with the chosen path[s]), with default location, extension filters, optional
  modal-parent window, and `allowMultiple` on open.
- Desktop impl: **SDL3 provides these natively** — `SDL_ShowOpenFileDialog`, `SDL_ShowSaveFileDialog`,
  `SDL_ShowOpenFolderDialog`, `SDL_DialogFileFilter` (confirmed in the vendored headers + system
  SDL 3.4.2). Web/Android shells implement platform equivalents later.
- **Reference: Sedulous** `Sedulous.Shell/IDialogService.bf` + `Sedulous.Shell.SDL3/SDL3DialogService.bf`
  — a clean, correct SDL3 wrapper worth porting the *shape* of. Adopt: the three-method interface, and
  the **async heap `CallbackContext`** freed in the SDL trampoline (dialogs return immediately, fire the
  callback later — the context owns the callback + filter/path strings until then). Improve on it:
  (a) a **typed filter struct** `{ StringView name; StringView pattern; }` instead of its
  `"Name|pattern"` string-parsing; (b) hand the callback **owned `Array<String>` paths** (copied out of
  SDL's transient `filelist`), not views valid only during the callback (Sedulous's is a use-after-free
  footgun if a path is stashed).
- Unblocks all editor filesystem interaction (asset import, project open, export), not just export.
- An **in-engine file-browser widget** (our own, on the UI toolkit) is explicitly deferred — native
  dialogs first.

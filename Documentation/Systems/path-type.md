# Source Path type

> Status: CURRENT
> Verified: 2026-08-12 @ 89516efc
> Track: [[path-type-plan]]

`SourcePath` is a mount-relative logical path value type for source references in asset data - normalized,
relative, forward-slash, wire-compatible with the `String` it replaced. The type (P1) and its adoption
as the single asset path reference (P2) shipped; the generic inspector picker (P3) and the check-assets
lint (P4) are not built.

## `SourcePath` (`foundation.vfs`)

`foundation.vfs`'s `SourcePath` (`VFS/SourcePath.cppm`; the vfs owns the mount concept). Not a general
OS path - no volumes, no macros, no absolute form. One guarantee: whatever you construct it from, the
stored form is forward-slash, relative, and dot-segment-free. Construction from a `StringView` normalizes
`\` -> `/`, collapses duplicate slashes and `./`, and REJECTS (empty + logged) absolute paths, `..`
escapes, and volume/scheme prefixes. Accessors: `View()`, `Extension()` (lowercased, no dot),
`FileName()`, `Stem()`, `Directory()`. `==`/`<` are case-SENSITIVE everywhere (one rule, all platforms).
Serialization writes/reads the stored string - the SAME wire shape as `String`, so existing
`.xasset`/`.rasset` data loads unchanged with no version bump, and Windows-authored backslash paths
silently heal on load (the bug this exists to kill).

## Adoption

`Asset::fileName` (the editor asset base, `Pipeline.Core/Asset.cppm`) IS a `SourcePath`, inherited by
every asset type (font/image/texture/mesh/animation/material/physics/audio/script/shader/UI/inputmap/
particle...). Builders read it through the VFS sources mount (mount-based resolution, not CWD-based).
Editor pages that pick a source file use `PathPickerDialog` (`Editor.App`) - the AssetPickerDialog's
sibling for SOURCE files, a modal picker over the sources mount with an extension filter, producing
mount-relative paths by construction (so the absolute-path hole a generic OS browse would have cannot
happen). The Font/Texture/Script/Audio pages wire it directly.

## Deliberate non-goals

No `$(MACRO)` expansion (VFS mounts/schemes already provide machine-independent roots; macros with
silent-erase semantics are a footgun); no volume handling (OS-absolute paths - font settings, project
registry, export dirs - stay `String` at the Win32/DXC edge per the UTF-8 policy); no case-insensitive
comparison anywhere.

## Deferred

- **P3 - generic inspector integration.** The reflected asset inspector does NOT yet recognize a
  `SourcePath`-typed field and auto-render a read-only value + Browse row; editor pages hand-wire
  `PathPickerDialog` today. This rides the reflection track (field attributes want reflection metadata,
  e.g. `extensions=.ttf,.otf`).
- **P4 - check-assets lint.** A "Check Assets" editor action / cook-time warning (every Asset-derived
  instance's `fileName` + dependency files present in the sources mount) is not built.

---

The Traktor `traktor::Path` survey (the eager string decomposition, the serializer-primitive + typed
UI-binding model, and the sharp edges NOT copied - inconsistent case, silent macro-erase, colon-as-volume,
generic-browse-stores-absolute) is in `Documentation/Archive/path-type-design-history.md`.

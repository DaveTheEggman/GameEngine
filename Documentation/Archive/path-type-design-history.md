# Source Path type - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/path-type.md
> Track: [[path-type-plan]]

NON-AUTHORITATIVE. The Traktor survey behind the `SourcePath` design. Present-tense truth is
`Systems/path-type.md`; the full original doc is in git at the P0 commit 3b92560d. Kept for the "why".

## What Traktor does

One type, `traktor::Path` (`code/Core/Io/Path.h`), is both the filesystem utility AND the serialized
path-reference type:
- Constructed eagerly from a string: decomposes into volume/dir/file/ext, normalizes `\` -> `/`,
  lowercases volume + extension, expands `$(ENV_VAR)` macros - while preserving the raw input in
  `m_original`, which is what serialization round-trips (macros survive in data files).
- A first-class serializer primitive next to Guid; every backend persists `getOriginal()`, so a path
  edit dirties the build hash.
- `editor::Asset::m_fileName` (a `Path`) is the single path reference in asset data; 13 asset types
  inherit it, relative to the `Pipeline.AssetPath` setting.
- UI binding is type-driven: `Member<Path>` -> `FilePropertyItem` (inline edit + browse -> FileDialog;
  `AttributeDirectory` switches to a directory dialog). `Member<Guid>` maps to a DIFFERENT editor
  (database browser) - two reference concepts, two pickers.
- The relative-path invariant is convention, enforced only by an opt-in lint (`CheckAssetsTool`).

## Sharp edges NOT copied

- Inconsistent case handling (`==` case-insensitive on Windows only, `<` always case-sensitive - a
  `map<Path>` disagrees with `==`).
- Unknown `$(MACRO)` silently erases instead of erroring.
- Any colon parses as a volume (URI-like strings mangle).
- The GENERIC property browse stores ABSOLUTE paths; only bespoke editors relativize (the lint exists
  because of this hole).

## The design calls that shipped

`SourcePath` took the good half (a typed, serialized, mount-relative path reference with a type-driven
picker) and rejected the sharp edges: one case rule everywhere (case-sensitive; a lint catches case
bugs instead of platform-dependent equality), no macros (VFS mounts/schemes cover machine-independent
roots), no volumes (OS paths stay `String`), and a picker that only produces mount-relative paths (so
the absolute-path hole cannot happen). Sequenced P1 (the type) + P2 (root adoption) as one change; P3
(generic inspector integration) parked on the reflection track; P4 (lint) as a cook-driver add-on.

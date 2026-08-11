# A typed Path for source references (Traktor-inspired)

Status: PLANNED (investigation done 2026-08-01; not yet implemented).
Reference: Traktor checkout at /home/robert/Dev/CPP/traktor.

## What Traktor does (survey summary)

One type, `traktor::Path` (`code/Core/Io/Path.h`), serves as both the filesystem
utility and THE serialized path-reference type:

- Constructed eagerly from a string: decomposes into volume/dir/file/ext, normalizes
  `\` to `/`, lowercases volume and extension, expands `$(ENV_VAR)` macros - while
  preserving the raw input in `m_original`, which is what serialization round-trips
  (macros survive in data files).
- It is a first-class serializer primitive: `ISerializer::operator>>(Member<Path>&)`
  sits next to Guid; every backend (Xml/Binary/DeepHash/Reflection) persists
  `getOriginal()`, so a path edit dirties the build hash.
- `editor::Asset::m_fileName` (a `Path`) is the single path reference in asset data;
  13 asset types inherit it. Convention: relative to the `Pipeline.AssetPath` setting.
  Pipelines resolve with `getAbsolutePath(Path(assetPath) + asset->getFileName())`.
- UI binding is type-driven: the property reflector maps `Member<Path>` to
  `FilePropertyItem` (inline edit + browse button -> FileDialog; `AttributeDirectory`
  switches to a directory dialog). `Member<Guid>` deliberately maps to a DIFFERENT
  editor (database browser) - two reference concepts, two pickers.
- The relative-path invariant is convention, enforced only by an opt-in lint
  (`CheckAssetsTool`: flags absolute or missing asset fileNames).

Sharp edges we should NOT copy:
- Inconsistent case handling (`==` case-insensitive on Windows only, `<` always
  case-sensitive - a `map<Path>` disagrees with `==`).
- Unknown `$(MACRO)` silently erases instead of erroring.
- Any colon parses as a volume (URI-like strings mangle).
- The GENERIC property browse stores absolute paths; only bespoke editors relativize.
  (The lint exists because of this hole.)

## Where Draconic stands today

We already have the structural half:

- `draconic::editor::Asset` (`Code/Draconic/Editor/Draconic.Editor/Asset.cppm`) has
  `String fileName; // source file, relative to the sources mount` - inherited by
  Font/Image/Texture/Mesh/Animation/Material/Physics/Audio/Script/Shader/UI/InputMap/
  ParticleEffect asset types. Builders read it through the VFS sources mount, so
  resolution is mount-based rather than CWD-based (better than Traktor's default).
- `AssetDependencies::files` carries extra mount-relative source paths.
- The FontEditorPage's PathPickerDialog (2026-08-01) picks mount-relative paths -
  the first browse UI; it produces relative paths by construction.

What we lack is the TYPE: `fileName` is a raw `String`, so nothing normalizes
separators (a Windows-authored `Fonts\Roboto.ttf` would break the VFS on every
platform), nothing offers extension/stem accessors (importer dispatch re-implements
extension parsing), and the generic inspector (AssetFormPage) cannot recognize "this
field is a path" to render a picker row - the font page had to hand-wire its Browse.

OS-absolute paths (EditorFontSettings.fontPath, project registry entries, export
output directories) are a DIFFERENT category and stay `String` - they are per-machine
settings, never cooked into content.

## The plan

Concept: `SourcePath` - a mount-relative logical path value type. Not a general OS
path (no volumes, no macros, no absolute form). One honest guarantee: whatever you
construct it from, the stored form is forward-slash, relative, dot-segment-free.

P1 - the type (draconic.vfs, `:source_path` partition; vfs owns the mount concept):
  - `class SourcePath { String m_value; }` with: construction from StringView
    normalizing `\`->`/`, collapsing duplicate slashes and `./`, REJECTING (empty +
    logged) absolute paths, `..` escapes, and volume/scheme prefixes; accessors
    `View()`, `Extension()` (lowercased, no dot), `FileName()`, `Stem()`,
    `Directory()`; `==`/`<` case-SENSITIVE everywhere (one rule, all platforms).
  - Serialization: writes/reads the stored string - SAME wire shape as String, so
    existing .xasset/.rasset data loads unchanged, no version bumps.
  - doctest suite: normalization table, rejection cases, accessor table, wire
    round-trip against a String-written stream (compat proof).

P2 - adopt at the root: `editor::Asset::fileName` becomes `SourcePath`.
  - Subclasses inherit the change (they call `Asset::Serialize`); compile fallout is
    the call sites that assign/read it as String - importers (`CopyIntoSources`
    results), builders (`ReadSourceBytes(ctx, fa.fileName.View())`), pages.
  - `AssetDependencies::files` -> `Array<SourcePath>` in the same pass (same
    consumers).
  - Windows-authored data with backslashes silently heals on load (normalization),
    which is the bug this exists to kill.

P3 - inspector integration (the Traktor lesson done right): the generic
  AssetFormPage/reflection path recognizes SourcePath-typed fields and renders the
  read-only value + Browse via PathPickerDialog (extension filter from an attribute
  on the field, e.g. reflection metadata "extensions=.ttf,.otf"). Because the picker
  only produces mount-relative paths, the absolute-path hole Traktor's generic
  browse has cannot happen here. FontEditorPage's hand-wired row migrates to this.

P4 - lint: a "Check Assets" editor action (and/or cook-time warning): every
  Asset-derived instance - fileName present in the sources mount, plus dependency
  files. Traktor's CheckAssetsTool equivalent, but ours can run inside the cook
  driver where the mount is already open.

Deliberate non-goals: no `$(MACRO)` expansion (VFS mounts/schemes already cover
"machine-independent roots"; macros with silent-erase semantics are a footgun); no
volume handling (OS paths stay String at the Win32/DXC edge per the UTF-8 policy);
no case-insensitive comparison anywhere (lint catches case bugs instead of
platform-dependent equality).

Sequencing: P1+P2 are one PR-sized change (type + root adoption + compile fallout).
P3 rides the reflection track (task #110) since field attributes want reflection
metadata. P4 is an afternoon on top of the cook driver.

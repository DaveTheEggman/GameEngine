# Binding conventions for all specs

These are the standing project rules. Every spec in this folder assumes them;
violating them fails review even if the feature works.

## Build and verification

- Develop and verify against DEBUG: `cmake --build build/clang -j4` and
  `cmake --build build/gcc -j4`. BOTH compilers must be green before a phase is
  called done. Binaries land in `Bin/Debug/Linux64-Clang/` and
  `Bin/Debug/Linux64-GCC/`.
- Never `rm -rf` anything under `Bin/` - the user's EditorProject lives inside.
  Inspect before any destructive delete.
- New test targets need a cmake re-configure (`cd build/clang && cmake .`) before
  they appear.
- The user does ALL visual verification. Do not claim something "looks right";
  hand over a build and list what to look at.

## Tests

- EVERY addition lands with adequate doctest tests in the module's `Tests/`
  target. Nothing is "done" without them. Tests write scratch files only under
  the gitignored `.test-scratch/` (via the shared `TestMain.h` helpers), using
  relative paths - never an absolute POSIX path like `/tmp`.
- For GPU-visible behavior, prefer semantic pixel probes on real devices
  (model: `Code/Foundation/VG.Backend.Tests/`) over stored image goldens.

## Code style

- Folder == target == module: a library's folder name, CMake target (dotted
  namespaced alias), and C++ module name agree; a module defines only its own
  target, siblings are declared at the root; test suites cluster in one root
  block. (The fallout convention of the retired reorg/role-grouping branch,
  2026-08-22 - the branch is dead; the naming discipline is standing.)
- Allman braces, per-module `.clang-format`, PascalCase for methods/functions
  (Raptor/Sedulous convention - do NOT camelCase).
- Full descriptive names, no abbreviations. Platform-specific logic goes in
  Core/System backend units, not `#if` branches inside feature modules.
- NO em-dashes or en-dashes anywhere (code, comments, docs, commit messages).
  ASCII hyphen only.
- String currency is UTF-8 (`char8_t` String/StringView). WideString (UTF-16)
  only at the Win32/DXGI/DXC edge.
- Heavy third-party headers and REFLECT_* bodies go in module IMPLEMENTATION
  units, never interface units (GCC gcm-cluster blowup).
- Core reflection partitions import RTTI/base partitions only - never
  serialization, never consumers. Capability flows INTO reflection through
  registration-time function pointers (ContainerInfo slots, factories passed
  as parameters), not imports.
- Missing math: port from Sedulous math first; only write from scratch if
  Sedulous lacks it too.
- Namespaces: one level per module (`foundation::core`, `engine::scene`); nest
  deeper only for a genuine sub-layer (`ui::toolkit`, `vg::renderer`). The one
  canonical alias for a module is its leaf name verbatim - no abbreviations or
  initials; reach sub-namespaces through the parent alias (`ui::toolkit::Button`).
  Never alias `std`. `using namespace` is banned in interface units and normal
  `.cpp`s, with one exception: `using namespace foundation::core` (the de-facto
  prelude). App entry points (`Tools/*/Main.cpp`, sample `Main.cpp`) may use it
  freely - they are leaves, not library surface.
- One primary type per file, named for it (`class NetworkManager` ->
  `NetworkManager.cppm`); generic names (`Manager`/`Types`/`Common`/`Impl`) are
  banned when one named type is the file's reason to exist. Pair a serialized
  descriptor with its runtime object as `XxxData`/`Xxx`, each in its own file.
  Each module has one `<Module>Module.cppm` aggregator re-exporting its
  partitions. Interface units carry declarations + trivial inline bodies only;
  non-trivial bodies (loops, branching, anything substantial) move to a `.cpp`
  implementation unit. Trivial `constexpr` math value types stay header-only.
- Includes/imports: global-module-fragment `#include`s are minimal (only what the
  interface needs). Order imports `foundation.core` first, then other engine
  modules alphabetically, then third-party; one per line. In a `.cpp` the matching
  own header/partition comes first. Project includes use full paths from `Code/`
  (`"Core/Prelude.h"`), never relative (`"../..."`).

## Comments and docs

- Every `.cppm`/`.cpp` opens with a brief `//` file-header describing its
  contents, directly below the two-line SPDX MIT header (see the license rule).
  Public types and non-obvious public methods carry a one-line `///` doc comment
  in the interface unit; member/enum-value trailing docs use `///<`. Trivial
  accessors need none.
- Comments describe what the code does NOW and why - the test is "is it true right
  now?". A comment stating a real current limitation is correct and stays. NO
  phased-work, track, or status narration in comments or commit messages (no "P2
  will...", "as part of the X track", "for now"); write plain, present-tense.
- Unfinished work stays visible as `// TODO(area): what is missing[, and why]` -
  area-tagged so it is greppable (`grep -rn "TODO(" Code/`). Normalize informal
  markers (`XXX`, `FIXME`, "not yet ported") to this form; never delete a TODO
  unless the referenced work is actually done.

## Accepted deviations (do NOT "fix" these)

- UI structs use PascalCase fields (`view.Bounds`, `.Parent`), not `m_`-camelCase
  members - user-accepted; new UI code follows it for local consistency, the rest
  of the codebase uses `m_`-camelCase.
- `using namespace foundation::core` is the single sanctioned whole-namespace
  using (see Code style).

## Architecture rules that recur

- Editor-only state lives on the editor Asset class, never in resource/runtime
  wire structs.
- Every Settings section type needs BOTH `GlobalTypeRegistry().Register(type)`
  AND `RegisterSerializable<T>()`; never discard a settings-load Status.
- Bind-group caches invalidate by version/generation, never raw pointer.
- UI actions that destroy views/controls defer via
  `UIContext::MutationQueueRef().QueueAction(...)`, never mid-event-dispatch.
- New `resource::Ref<T>` component fields need an explicit InspectorView
  dispatch entry or no picker renders.
- New components need displayName + category attributes (editor component menu).
- Wire formats: keep write/read symmetric; guard counts on read
  (snapshot-prefab lesson).
- ONE supported layout per serialized type: the CURRENT data version. The
  versioned-payload reader refuses any other stored version (no migration
  branches, no `ar.Version()` gates, no legacy readers or name remaps); a
  wire change bumps `dataVersion` and the data is re-saved / re-imported /
  re-cooked. Same for stream headers (scene stream, navmesh blob) and section
  mode tags: the current value or a failed read.
- sRGB decode lives in the SHADER (vg.vs), never CPU-side; `ToColor` is a plain
  /255. Never decode twice.

## Specs

- Before a spec commissions NEW UI or infrastructure, grep the tree for an
  existing implementation and CITE what was checked in the spec. Two trips
  in one week earned this rule: camera-preview sat "pending" for two weeks
  because its spec was never stamped BUILT, and property-animation
  commissioned a curve-editor widget while UI.Toolkit's CurveCanvas (1263
  lines, tested, in use by the particle page) already existed.
- When a spec's work ships, stamp the spec (STATUS header) in the same
  commit - an unstamped spec reads as unbuilt work forever.

## Git

- NO Co-Authored-By trailer on commits.
- Documentation/ IS tracked (policy reversed 2026-08-11): commit doc updates
  with the change; Systems docs update in the SAME commit as behavior. Only
  the user's root-level scratch files stay untracked.
- Commit messages: imperative summary line prefixed with the module/track
  (e.g. `VG: ...`, `Editor: ...`), body explains the why.
- Do not commit or push without the phase being green on both compilers and its
  tests passing.

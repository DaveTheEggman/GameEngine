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
  the gitignored `.test-scratch/` (via DraconicTestMain.h helpers).
- For GPU-visible behavior, prefer semantic pixel probes on real devices
  (model: `Code/Draconic/Foundation/Draconic.VG.Backend.Tests/`) over stored
  image goldens.

## Code style

- Allman braces, per-module `.clang-format`, PascalCase for methods/functions
  (Raptor/Sedulous convention - do NOT camelCase).
- Full descriptive names, no abbreviations. Platform-specific logic goes in
  Core/System backend units, not `#if` branches inside feature modules.
- NO em-dashes or en-dashes anywhere (code, comments, docs, commit messages).
  ASCII hyphen only.
- String currency is UTF-8 (`char8_t` String/StringView). WideString (UTF-16)
  only at the Win32/DXGI/DXC edge.
- Heavy third-party headers and DRACONIC_REFLECT_* bodies go in module
  IMPLEMENTATION units, never interface units (GCC gcm-cluster blowup).
- Core reflection partitions import RTTI/base partitions only - never
  serialization, never consumers. Capability flows INTO reflection through
  registration-time function pointers (ContainerInfo slots, factories passed
  as parameters), not imports.
- Missing math: port from Sedulous math first; only write from scratch if
  Sedulous lacks it too.

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

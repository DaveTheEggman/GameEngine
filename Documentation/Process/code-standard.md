# Draconic - Code Standard

Status: **DRAFT for review (2026-07-22).** The rulebook for the codebase-wide quality pass. Written to
be applied **module by module, starting with Core**. Adopted as the north-star for "clean and
consistent": the **Traktor** engine (`/home/robert/Dev/CPP/traktor`) - we port its *cleanliness
principles*, not its literal style (Traktor is `.h/.cpp` + tabs + Allman + camelCase methods + wide
strings; Draconic keeps its C++23 modules + PascalCase methods + UTF-8, and adopts Allman braces).

> This document describes the **target** state. Where the codebase disagrees with it today, the code is
> wrong, not the standard - the cleanup pass brings the code into line. Two deviations are *accepted*
> and called out explicitly in §11; do not "fix" those.

---

## 1. Principles (what "clean" means here)

1. **A file name tells you what is inside it.** `NetworkManager` lives in `NetworkManager.cppm`, never
   `Manager.cppm`. Opening a directory listing should read like a table of contents.
2. **One primary type per file.** Dumping many unrelated public types in one unit is a smell. Small,
   cohesive helper types may share a file with the primary type they serve.
3. **Interfaces declare; implementation units define.** Non-trivial logic lives in `.cpp`, not inlined
   into the `.cppm` interface. (Reinforces the existing GCC-module-hygiene rule.)
4. **No cryptic abbreviations, anywhere** - not in namespace aliases, type names, or member names. Full,
   descriptive names (this is already a project rule - see the naming memory).
5. **Comments describe the *actual current state*.** A comment must be true *now*. But **unfinished
   work is not hidden - it is marked with a visible `TODO`** (§5.4). The two are complementary: describe
   what exists, and flag what is missing with a `TODO`. Never dress up a half-finished thing as if it
   were complete.
6. **TODOs are never deleted unless the work is actually done.** A `TODO` is a tracked follow-up, not
   clutter. Any placeholder, stub, or unfinished path **must carry a `TODO`** so we can find it later.
   Removing a `TODO` asserts the work is finished - only do that when it is.
7. **Changes are made in place, surgically - never rewrites.** We are cleaning existing, working code,
   not reauthoring it. Preserve behavior exactly; move code verbatim rather than retyping it; keep diffs
   minimal and reviewable. Every module pass must leave tests green on both compilers (§9). Regressions
   are the one unacceptable outcome.
8. **Consistency beats individual preference.** Pick one convention and make everything match it. This
   document *is* that pick.

Traktor is the reference for structure and cleanliness. It has **zero namespace aliases**, **no `using namespace` in
library code**, strictly **one class per file named after the class**, implementation in `.cpp`, and a
uniform file-header block - across 44 modules.

---

## 2. Namespaces

### 2.1 Structure
- Root namespace is `draconic`. Every module nests exactly one level: `draconic::core`,
  `draconic::scene`, `draconic::runtime`, `draconic::render`, `draconic::ui`. Sub-areas nest one deeper
  only when they are a genuine sub-layer: `draconic::ui::toolkit`, `draconic::ui::runtime`,
  `draconic::ui::viewport`, `draconic::vg::renderer`. **Do not go deeper than needed.**
- Open with the **collapsed form, Allman brace on its own line** (§7):
  ```cpp
  export namespace draconic::scene
  {
  ```
- Contents are indented one level inside the namespace (4 spaces - see §9). Close with a bare `}` - **no
  `} // namespace` trailer comment** (`FixNamespaceComments: false`).

### 2.2 Aliases - the big cleanup
The codebase currently aliases the *same* target namespace many incompatible ways. `draconic::runtime`
alone appears as `rt`, `rtc`, `grt`, `runtime`, `rt2`, `irt`. This ends.

**Rule: the one canonical alias for a module is its own leaf namespace name, verbatim.** No prefixes, no
truncation, no cutesy initials.

| Target namespace          | Canonical alias | Banned variants seen today                     |
|---------------------------|-----------------|------------------------------------------------|
| `draconic::runtime`       | `runtime`       | `rt`, `rtc`, `grt`, `rt2`, `irt`               |
| `draconic::scene`         | `scene`         | `dscene`, `gscene`                             |
| `draconic::render`        | `render`        | `drender`, `grender`                           |
| `draconic::shell`         | `shell`         | `dshell`, `sh`, `platform`                     |
| `draconic::ui`            | `ui`            | `gui`, `dui`, `iui`                            |
| `draconic::net`           | `net`           | `dnet`                                         |
| `draconic::script`        | `script`        | `dscript`                                      |
| `draconic::input`         | `input`         | `din`, `dinput`                                |
| `draconic::physics`       | `physics`       | `dphysics`                                     |
| `draconic::audio`         | `audio`         | `gaudio`                                       |
| `draconic::geometry`      | `geometry`      | `geo`                                          |
| `draconic::resource`      | `resource`      | `res`                                          |
| `draconic::materials`     | `materials`     | `mats`                                         |
| `draconic::settings`      | `settings`      | `st`                                           |
| `draconic::modelimporter` | `modelimporter` | `mi`                                           |
| `draconic::particles`     | `particles`     | `px`                                           |
| `draconic::editor`        | `editor`        | `ed`, `edapp`                                  |
| `draconic::animation`     | `animation`     | `anim`, `danim`                                |

- **Sub-namespaces are never separately aliased.** Reach them *through* the parent alias:
  `ui::toolkit::Button`, `ui::runtime::UIHost`, `ui::viewport::ViewportView`, `vg::renderer::Canvas`.
  This is the user's explicit preference - write `ui::toolkit`, not `tk`; kill `tk`, `gtk`, `itk`,
  `uirt`, `guirt`, `iuirt`, `guivp`, `uivp`, `vgr`, `gvgr`.
- **Standard-library namespaces are not aliased.** Write `std::filesystem::path`, not `fs::path`.
- **Samples/framework**: `samples::framework`, not `sf`.
- Declare the alias once, at the top of the file, after the `import`s, before the `export namespace`.

### 2.3 `using namespace`
- **Banned in library headers/interface units and normal `.cpp`s**, exactly as Traktor. Qualify through
  the module alias instead (`scene::Scene`, `render::MeshRenderer`).
- **One sanctioned exception: `using namespace draconic::core`.** Core is the foundational vocabulary
  (`i32`, `String`, `Array`, `Float3`, `Function`, `RefPtr`) - the de-facto prelude, used this way 857
  times. Treating it like `std` for our purposes is pragmatic and keeps signatures readable. **DECIDED
  (2026-07-22): keep the exception** (the alternative, `core::` everywhere, is large mechanical churn
  with little readability gain).
- Application entry points (`Tools/*/Main.cpp`, sample `Main.cpp`) may use `using namespace` freely -
  they are leaves, not library surface. This matches Traktor.

---

## 3. Files & modules

### 3.1 One primary type per file, named for it
- `class NetworkManager` → `NetworkManager.cppm`. `struct Float3` → `Float3.cppm`. Core already does this
  well and is the model.
- **Generic file names are banned** when a specific one exists: no `Manager.cppm`, `Subsystem.cppm`,
  `Impl.cpp`, `Types.cppm`, `Common.cppm` when the file's reason for existing is one named type. Rename
  to the type. (Known offenders: `Net/Manager/Manager.cppm` → `NetworkManager.cppm`;
  `Net/Subsystem/Subsystem.cppm` → `NetworkSubsystem.cppm`. The redundant `Manager/Manager` nesting
  collapses too.)
- **Follow the `XxxData` / `Xxx` split** where a serialized descriptor pairs with a runtime object
  (Traktor's pervasive convention; we already do this in places) - each half in its own file.
- Small, tightly-coupled helpers (a private enum, a POD the primary type returns) may live in the
  primary type's file. The test is cohesion: would a reader expect to find it here from the file name?

### 3.2 Interface vs implementation split
- The `.cppm` **interface unit** carries: the file-header doc block, `module;` + the global-module
  fragment `#include`s, `export module`, `import`s, alias declarations, and the **declarations** -
  class/struct shape, method signatures, member variables with default initializers, and trivial inline
  bodies only (one-liners, `constexpr`, small accessors).
- **Non-trivial method bodies move to a `.cpp` implementation unit** (`FooImpl.cpp`, or a
  same-named `.cpp` module implementation partition). Loops, branching logic, anything substantial.
- **Hard rule (already in force, restated):** heavy third-party `#include`s and `DRACONIC_REFLECT_*`
  macro bodies **must** live in implementation units, never in interface units (GCC gcm-cluster
  blowup). The cleanup treats a violation here as a bug.
- Trivial math value types (`Float3`, `Quaternion`) are the deliberate exception - they are `constexpr`,
  header-only by nature; keep them inline.

### 3.3 Module aggregation & directory layout
- Each module has one aggregation interface named `<Module>Module.cppm` (e.g. `CoreModule.cppm`,
  `NetModule.cppm`) that re-exports the module's partitions. Keep this consistent across all modules.
- Tests live in a `Tests/` subdirectory of the area they cover, one `TestsMain.cpp` per test binary.
- Platform-specific code lives in backend files under the module (`System/Linux/…`, `System/Win32/…`),
  never in `#if` branches sprinkled through shared units (existing rule - the naming memory).

---

## 4. Naming

Draconic keeps **PascalCase methods/functions** (project rule - do *not* adopt Traktor's camelCase). The
rest mirrors Traktor's discipline.

| Kind                          | Convention                    | Example                         |
|-------------------------------|-------------------------------|---------------------------------|
| Class / struct / enum         | PascalCase                    | `NetworkManager`, `Float3`      |
| Interface                     | `I` + PascalCase              | `ISerializer`, `INetworkController` |
| Method / free function        | **PascalCase**                | `StartServer()`, `Normalized()` |
| Member variable               | `m_` + camelCase              | `m_refCount`, `m_projection`    |
| Local variable / parameter    | camelCase                     | `port`, `inValue`               |
| File-scope constant           | `k` + PascalCase              | `kRpcChannel`, `kNetScriptService` |
| Static (file-local) variable  | `s_` + camelCase              | `s_registry`                    |
| `enum class` value            | PascalCase, explicitly numbered | `Projection::Orthographic = 0` |
| Macro                         | `DRACONIC_` + UPPER_SNAKE     | `DRACONIC_REFLECT_VALUE`        |
| File                          | matches the primary type      | `NetworkManager.cppm`           |

- **Fixed-width integer types everywhere** (`i32`, `u8`, `u64`, `f32`) - never bare `int`/`long`.
- **Strings are UTF-8 `char8_t`/`String`** (project currency); `WideString` (UTF-16) only at the
  Win32/DXGI/DXC edge (existing memory rule).
- **No abbreviations in identifiers** - `sourceRectangle`, not `srcRect`; `descriptor`, not `desc`
  (unless the abbreviation is a domain term of art, e.g. `RGBA`, `PBR`, `UUID`).

---

## 5. Comments & documentation

### 5.1 File-header block
Every `.cppm` / `.cpp` opens with a brief header describing the file's contents. Codify the dominant
`//` line style (539 files use `//`, 223 use `///` - pick `//` for narrative headers):
```cpp
// Draconic Net - NetworkManager
//
// A networked endpoint owned per running game (a GameInstance): owns a live NetSession + RpcTable over
// an IDatagramSocket, driven each fixed step; installs the Net script service. See docs/design/networking.md sec 6.
```
- **Use ASCII hyphens `-`, never em-dashes (`-`) or en-dashes (`-`).** This is a hard rule for all code,
  comments, and identifiers - the existing codebase uses `-` and em-dashes are not to be introduced
  (they are also a common auto-generated-text tell). Separators like `Module - Type` use a plain hyphen.
- No license/copyright boilerplate today (Draconic has none; revisit if that changes - Traktor carries an
  MPL block per file, out of scope here).

### 5.2 Doc comments on declarations
- Public types and non-obvious public methods carry a one-line `///` doc comment in the interface unit
  (Doxygen-friendly; the `///` form is reserved for *declaration* docs, distinct from the `//` file
  header). Trivial accessors need none.
- Member/enum-value trailing docs use `///<`.

### 5.3 Implementation comments - describe reality
- Comments state **what the code does now and why**. A `.cpp` is lightly commented; comment only
  non-obvious logic.
- Comments that accurately describe a **real current limitation** are correct and stay ("one material per
  mesh - single-material models"). The test: is it true *right now*? Keep it.
- **Unfinished work stays visible as a `TODO` (§5.4) - it is never silently removed.** A narrative
  comment describing a gap ("point-device wiring not yet present") is fine *and* should be accompanied by
  a `TODO` on the stubbed code so the gap is greppable. Do not relocate real backlog out of the code and
  delete the marker; the code is where the follow-up must be found.

### 5.4 TODO markers - the follow-up trail
Unfinished functionality, placeholders, and stubs **must** carry a `TODO` so nothing is lost. This is a
hard rule, reinforced by principles 5-6.

- **Format:** `// TODO(area): what is missing and, if useful, why.` The `area` is a short tag
  (module or feature) to make TODOs greppable and groupable, e.g.
  `// TODO(rhi-dx12): blit pipeline needs D3DCompile from d3dcompiler.lib.`
- **Every placeholder/stub gets one.** If the cleanup pass *encounters* unmarked placeholder or
  not-yet-implemented behavior, it **adds** a `TODO` - the pass increases visibility, it never hides
  gaps. A function that returns a dummy value, a seam that is a no-op, a "handle later" branch - all get
  a `TODO`.
- **Never delete a `TODO` unless the referenced work is actually done.** Reformatting or renaming around
  a `TODO` must preserve it. Deleting one is a claim of completion.
- Existing informal markers ("not yet ported", "left as declarations to be resolved", `XXX`, `FIXME`)
  are **normalized to the `TODO(area):` form**, not removed - this makes the whole backlog uniformly
  greppable (`grep -rn "TODO(" Code/`). The DX12 RHI backend is the main concentration and becomes
  properly-tagged `TODO(rhi-dx12): …` markers.

---

## 6. Includes & imports

- **Global-module-fragment `#include`s** (in `module;` before `export module`) are minimal - only what
  the interface truly needs (`Core/Prelude.h`, `Core/Reflection/Reflect.h` for reflected types).
- **`import`s** are grouped and ordered: `draconic.core` first, then other Draconic modules
  alphabetically, then third-party. One `import` per line, each with a short trailing `//` note only when
  the reason is non-obvious.
- In `.cpp` implementation units, the matching own header/partition comes first, a blank line, then the
  rest alphabetically (Traktor's rule).
- Project includes use full paths from `Code/` (`"Core/Prelude.h"`), never relative (`"../…"`).

---

## 7. Formatting

Formatting is **enforced by `clang-format`, not by hand** (§8). This section records only the decisions
the config encodes.

- **Indentation: 4 spaces**, no hard tabs (Draconic's existing style; differs from Traktor's tabs).
- **Braces: Allman** - opening brace on its own line for namespaces, types, functions, and control
  blocks. **DECIDED (2026-07-22):** aligns with Traktor (the gold standard); a one-shot `clang-format`
  pass conforms the tree (which had drifted to a same-line majority under no enforcement).
- **Namespace close: bare `}`, no `} // namespace` trailer** (§2.1) - matches Traktor. The config sets
  `FixNamespaceComments: false` accordingly.
- **Namespace indentation: `All`** (contents indented one level) - this is what the code already does, so
  it keeps the format diff minimal (Traktor uses flush-left, but re-indenting every file body is need-
  less churn against the in-place principle).
- Always brace control-flow bodies, even single statements.
- `override`/`final` explicit on overrides. `explicit` on single-argument constructors. C++ casts
  (`static_cast`), never C casts. `[[nodiscard]]` / `noexcept` where they carry meaning.

---

## 8. Tooling - `clang-format` for the formatting layer

**clang-format handles the mechanical formatting subset; everything semantic is hand-done.** The tree
already has a tracked `.clang-format` (LLVM base, 4-space, 100-col, Allman) that was **never applied** -
sample files show 112-1185 violations each. clang-format 21.1.8 is installed and parses our `.cppm`
module units correctly.

**What clang-format does (and does not) cover:**
- **Does:** brace placement, indentation, spacing, line wrapping at the column limit, include *sorting*
  within a block, pointer/reference alignment. The entire §7 formatting layer + §6 include ordering.
- **Does NOT:** namespace-alias renaming, `using namespace` removal, file renames, one-type-per-file
  splits, inline→`.cpp` extraction, TODO/comment content, naming conventions. That is the semantic ~80%
  of §10 - all hand-done, per module, in place (principle 7).

**Recommended sequence (answers "format pass first?"):**
1. **Finalize `.clang-format`** to encode the §7 decisions once they're made (brace style,
   `NamespaceIndentation`, `.cppm`/`.cpp` handled via `--style=file`; set `ReflowComments: false` so it
   never rewraps our comments; keep `SortIncludes` but note it only sorts *within* a block, so it won't
   fight the module `import` ordering). Validate it on **Core** first - inspect the diff, build both
   compilers, run tests.
2. **Run a format-only baseline pass.** Two ways to stage it:
   - **Per-module (recommended):** step 0 of each module's §9 pass is `clang-format -i` on that module,
     committed *separately* from the semantic changes so each diff is pure and reviewable, then the
     semantic cleanup. Keeps commits small; matches the module-by-module cadence.
   - **Repo-wide once:** one mechanical format commit across `Code/` up front, then semantic passes on an
     already-consistent tree. Cleaner separation but a large (whitespace-only) diff.
   Either way it is **format-only and verified**: `clang-format` changes no semantics, and each pass ends
   with both-compiler builds + tests green (principle 7). Recommended: **per-module**, so we never carry
   a giant unreviewed diff.
3. clang-format is the enforcement mechanism going forward - new code stays conformant, so §7 never drifts
   again.

**Guardrails:** never let clang-format reflow comments (`ReflowComments: false`); it must not touch the
alias/`using` decisions (those are edits we make, then clang-format only re-spaces); after each run,
sanity-check that no `TODO` was disturbed (§5.4) and that module `import`/`export` lines are intact.

---

## 9. Per-module cleanup checklist

Applied **module by module, Core first**, then upward (Core → containers/consumers). Each module is one
reviewable pass / commit-set:

All work is **in place and surgical** - move code verbatim, never rewrite it (principle 7). Steps are
ordered so mechanical/low-risk changes land before structural ones.

0. **Format baseline** (§8): `clang-format -i` the module against the finalized config; commit
   *separately* as a pure format diff; build both compilers + run tests before moving on.
1. **Namespaces**: replace every abbreviated alias with the canonical one (§2.2); remove stray
   `using namespace` (keep only the sanctioned `draconic::core`, §2.3); make `} // namespace` trailers
   match the chosen §7 convention.
2. **File names**: rename generic files to their primary type (§3.1); collapse redundant nesting. Update
   every `import`/CMake reference to the renamed unit; this is a move, not a content change.
3. **File split**: move non-trivial inline bodies from `.cppm` to `.cpp` **verbatim**; verify no heavy
   includes / reflect bodies remain in interface units (§3.2). Split grab-bag files (§10.4).
4. **Naming**: fix abbreviated identifiers and any off-convention members/constants (§4).
5. **Comments & TODOs**: add/normalize the file-header block; **normalize informal in-progress markers to
   `TODO(area):` and add `TODO`s to any unmarked placeholder - never delete a TODO whose work is not done
   (§5.4)**; verify remaining comments describe current reality; add `///` docs to undocumented public
   surface.
6. **Build + test**: DEBUG on **both** clang and gcc green, module tests pass (`export TMPDIR=$PWD/build/.gcctmp`
   for gcc). Nothing is "done" without its tests staying green (project rule). No behavior change is the
   bar - a diff that alters runtime behavior is out of scope for a cleanup pass.
7. **Commit** the module's pass as focused commits (format separate from semantic; no `git add -A`; no
   Co-Authored-By trailer).

The concrete offender inventory (which files need renames, which `.cppm`s are inline-heavy, which files
dump too many types) is tracked separately in the cleanup backlog - see §10.

---

## 10. Cleanup backlog

From the structural audit (35 modules, ~1090 files). Ordered by confusion-to-effort ratio. Namespace
aliases (§2.2) are a cross-cutting pass applied to every module and are not re-listed here.

### 10.1 File renames - generic name → primary type *(highest ratio, mechanical)*
The anti-pattern is `Concept/GenericFile.cppm` (e.g. `Net/Manager/Manager.cppm`). Rename to the primary
type; collapse redundant `X/X` nesting.

| Current | → |
|---|---|
| `Net/Manager/Manager.cppm` (+`ManagerImpl.cpp`) | `NetworkManager.cppm` (+`NetworkManagerImpl.cpp`) |
| `Net/Subsystem/Subsystem.cppm` | `NetworkSubsystem.cppm` |
| `Input/Subsystem/Subsystem.cppm` | `InputSubsystem.cppm` |
| `Audio/Subsystem/Subsystem.cppm` | `AudioSubsystem.cppm` |
| `Physics/Subsystem/Subsystem.cppm` | `PhysicsSubsystem.cppm` |
| `Script/Subsystem/Subsystem.cppm` | `ScriptSubsystem.cppm` |
| `Animation/Subsystem/Subsystem.cppm` | `AnimationSubsystem.cppm` |
| `Particles/Subsystem/Subsystem.cppm` | `ParticleSubsystem.cppm` |
| `Runtime/Context.cppm` | `RuntimeContext.cppm` |
| `Scene/System.cppm` | `SceneSystem.cppm` |
| `Input/Model.cppm` | `InputMap.cppm` |
| `Input/Runtime.cppm` | `ActionRuntime.cppm` |
| `GUI/Model/Model.cppm` | `IModel.cppm` |
| `Fonts/TTF/Common.cppm` | `TrueTypeShared.cppm` |

`Runtime/Subsystem.cppm` is the generic base `Subsystem` class - the name is *correct* there, but it
name-clashes with all the above; renaming the derived ones resolves the clash. The 6 per-subsystem
`Components.cppm` files are generic but *consistent*; lower priority (leave or rename as a set later).

### 10.2 Reflection macro bodies inline in interface units *(rule violation, mechanical)*
Move `DRACONIC_REFLECT_*` bodies to `Impl.cpp` (Audio/Physics/Script already do this correctly):
- `Render/Subsystem/Components.cppm` - ~11 bodies inline (worst)
- `Core/Reflection/CoreReflection.cppm` - 17 bodies inline
- `Animation/Subsystem/Components.cppm` - 3 · `Particles/Subsystem/Components.cppm` - 1
- `Input/Subsystem/Subsystem.cppm` - `DRACONIC_REFLECT(Input, …)` inline

### 10.3 Heavy third-party headers in interface units *(rule violation)*
Move the include + dependent code to impl units:
- `Fonts/TTF/TrueTypeFont.cppm`, `TrueTypeFontAtlas.cppm` - `stb_truetype.h`
- `Model/GLTF/GltfLoader.cppm` - `cgltf.h` (+ 9 std headers)
- `Shaders/Compiler.cppm` - `<dlfcn.h>`
- Lighter (std headers into interface units): many `RHI/DX12/*.cppm`, `RHI/Log.cppm`, `Model/Model.cppm`,
  several `UI/Toolkit/*.cppm` - sweep per-module.

### 10.4 Grab-bag files to split *(distinct concerns in one unit)*
- `Render/RenderData.cppm` (641 ln, ~20 types) - render-data + lighting + probes + sky + `FrameArena`
  allocator + `CategoryRegistry`. Split by concern.
- `Render/Subsystem/Components.cppm` (846 ln, ~18) - components + managers + `EnvironmentSystem` +
  `PostProcessSystem`. Split.
- `Particles/Modules.cppm` (673 ln, 27) - Initializers / Behaviors / Collision.
- `RHI/Descriptors.cppm` (417 ln, 36 `*Desc`) - cohesive but oversized; split by category.
- `Net/Manager/Manager.cppm` (7 types) - split the `Net` script facade + `NetScriptBinding` +
  `NetworkStartup`/`NetworkRuntime` from `NetworkManager` itself (do alongside the §10.1 rename).

*(Cohesive-but-large files that are fine as-is: `Core/Reflection/Reflection.cppm`, `RHI/Resources.cppm`,
`Xml/Nodes.cppm`, `Fonts/Types.cppm` - do not split.)*

### 10.5 Module-aggregator naming normalization
- Add the `Module` suffix: `Graphics/Graphics.cppm`, `Profiler/Profiler.cppm`, `Shell/Shell.cppm`,
  `Settings/Settings.cppm` → `<Module>Module.cppm`.
- `Render/Subsystem/SubsystemModule.cppm` → `RenderSubsystemModule.cppm` (match Animation/Particles
  siblings).
- Pick one acronym-casing rule for aggregator filenames - today `VfsModule` vs `VGModule` vs `RhiModule`
  vs `DxModule` vs `VkModule` disagree. **DECIDED (2026-07-22):** treat acronyms as words → `VfsModule`,
  `VgModule`, `RhiModule`, `DxModule`, `VkModule` (PascalCase, one rule).
- Editor has no top-level aggregator (only `Editor<Sub>Module.cppm`) - internally consistent; leave.

### 10.6 Large-scale inline→`.cpp` extraction *(biggest effort - schedule last, per module)*
Whole modules ship implementation inline with **no non-test `.cpp`** - start here as the split model:
- **Render** (zero impl `.cpp`): `Pipeline.cppm` 1789 ln, `MeshRenderer.cppm` 1770, `IBLSystem.cppm` 780,
  `MeshShaders.cppm` 794, `RenderSubsystem.cppm` 557.
- **Scene** (zero impl `.cpp`): `SceneResource.cppm` **2270 ln**, `Scene.cppm` 823.
- **UI/Editor** (biggest units): `UI/Toolkit/Docking.cppm` **2292**, `Editor/Scene/InspectorView.cppm`
  2027, `Editor/App/Application.cppm` 1999, `EditContext.cppm` 1717, `AssetsView.cppm` 1662.
- Others: `Script/Subsystem/Subsystem.cppm` 1050, `Shell/Desktop/SDL3Shell.cppm` 1228,
  `Script/Wren/WrenScript.cppm` 1215, `Model/FBX/FbxLoader.cppm` 1190.

Template-heavy Core units (`Reflection.cppm` 805, `String.cppm` 679) are *defensibly* inline - evaluate
case-by-case, don't force-extract templates.

### 10.7 TODO normalization (not a purge - §5.4)
- Concentrated in the **DX12 RHI backend**: "not yet ported", "left as declarations to be resolved",
  "TODO: Blit pipeline" mark a staged/incomplete backend. These are **kept and normalized** to
  `TODO(rhi-dx12): …` - visible, greppable, never deleted (the work isn't done). Handle when the RHI
  pass comes up.
- Any **unmarked** placeholder/stub the pass encounters **gains** a `TODO` (§5.4) - the pass raises
  visibility of gaps, never hides them.
- The ~88 "for now"/"not yet" hits are mostly *accurate current-state* comments - those stay as-is;
  where one marks genuinely unfinished behavior, add/normalize a `TODO` alongside. Judge per §5.3-5.4.

### 10.8 Sequencing
Core → foundational consumers upward. Within each module, apply §9's checklist. Rough order:
**Core** (aliases + brace/header unify + §10.2 CoreReflection; already one-type-per-file) →
**Net, Input, Audio, Physics, Script, Animation, Particles** (the §10.1 renames + §10.2/10.3) →
**Scene, Runtime** (renames + Scene inline-extraction) → **Render, RHI** (splits + extraction + DX12
comment purge) → **UI, Editor** (largest extraction) → remaining (Graphics/Profiler/Shell/Settings
aggregator renames, Fonts/Model third-party pulls).

---

## 11. Accepted deviations (do NOT "fix" these)

1. **UI structs use PascalCase fields** instead of camelCase members (`view.Bounds`, `.Command`,
   `.Parent`). User-accepted, can stay for now - a large, low-value churn. New UI code should follow it
   for consistency within UI; the rest of the codebase uses `m_`-prefixed camelCase members (§4).
2. **`using namespace draconic::core`** as the single sanctioned whole-namespace using (§2.3).

---

## 12. Progress and remaining work (as of 2026-07-23)

**Codebase-wide, 100% done across all of `Code/` (Draconic + Samples + Tools):**
- **Namespace cleanup** (§2.2) - zero abbreviated aliases anywhere; every alias is the canonical
  leaf name, sub-namespaces reached through the parent (`ui::toolkit`, `vg::renderer`).
- **clang-format** (§7/§8) - the whole tree conforms to `.clang-format` (Allman, 4-space, 100-col).

**Full structural treatment DONE (14 modules)** - renames + reflect-body moves (§10.2) + §10.6
inline->`.cpp` extraction, all both-compiler-green:
Core, Net, Input, Audio, Physics, Script, Animation, Particles, Scene, Runtime, Shell (SDL3 backend
PIMPL'd out of the interface), Content, Render, Editor.

**Side-effect win:** the §10.6 extraction shrank the module BMIs enough that the clang-21.1 frontend
crash on the huge `Samples/Sandbox` / `DraconicExport` TUs is RESOLVED (see `KNOWN_ISSUES.md`); a full
`build/clang` is now clean end-to-end (gcc always was).

**REMAINING (deferred - get back to these another time):**

*Namespace-only by explicit user directive* (structural work intentionally skipped; namespace + format
already applied):
- VG, GUI, Imgui, Fonts, Texture, Model, ModelImporter, Geometry, UI, RHI
- Plus the four Render shader-source files (`MeshShaders`/`IBLShaders`/`AoShaders`/`SsrShaders`) - left
  inline deliberately (cohesive HLSL string banks; see the `leave-shader-source-inline` rule).

*Not yet given the structural pass* (namespace + format done; renames/reflect-moves/§10.6 extraction still
pending) - the natural next batch:
- **Materials, Resource, RenderGraph** - the larger ones, most likely to have inline-heavy files.
- **Graphics, Image, Profiler, Project, Settings, Shaders, VFS, Xml** - smaller; several may already be
  clean and need little more than a spot-check.

**How to resume:** follow §9 per module. The extraction tooling lives in the session scratchpad
(`extract3.py` for class member bodies - comment-aware, skips `class X;` forward decls; `extract_ns.py`
for free functions - comment-aware, keeps multi-line constexpr; `eex.sh` auto-detects module+namespace).
Recipe recap: extract member/free-function bodies into a new `module <name>;` impl unit, wire it CMake
`PRIVATE` (NOT the CXX_MODULES set), convert any `export import :X` -> `import :X` in the impl, qualify
nested return types with `Class::`, replicate the interface's GMF `#include`s + `using`/alias block, then
build + test BOTH compilers.

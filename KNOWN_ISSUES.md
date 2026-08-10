# Known issues

Tracked defects we've triaged but not yet fixed. Each entry: what, where, impact,
and the plan. Keep newest first.

---

## Sanitizer build dirs clobber Bin after the suffix-variable rename - TRIPWIRED

**Status:** MITIGATED (root CMakeLists tripwire). The debrand renamed
DRACONIC_OUTPUT_SUFFIX -> BUILDSYSTEM_OUTPUT_SUFFIX; a pre-rename build/asan
or build/tsan dir still carries the OLD cache entry while the new variable
reads empty - so a sanitizer build writes UNSUFFIXED output and clobbers
Bin/<Config>/<Plat>-<Comp> with sanitized binaries AND static libs (bit
Fable on 2026-08-09; the regular build then fails linking with __ubsan
references, and the fix is deleting Bin/.../lib/*.a + a full rebuild).
The root CMakeLists now FATALs on the stale-cache combination with the
one-line reconfigure in the message. ANY machine with pre-rename sanitizer
dirs (including Windows) must reconfigure:
`cmake -S . -B build/asan -DBUILDSYSTEM_OUTPUT_SUFFIX=-ASAN` (same for tsan).


## MSVC support (I9) - P2 COMPLETE, TREE BUILDS CLEAN

**Status:** P1 triage 2026-08-08, P2 fixes same day, on branch `msvc`.
**The full tree now compiles and links under MSVC with zero failures**, and ctest is at
parity with clang. Kept as the record of what MSVC needed and why, since several fixes
look arbitrary without it.

**Toolchain:** VS 2026 Community 18.7.0, cl 19.51.36247 (toolset 14.51.36231),
CMake 4.3.0, Ninja 1.11.1. Preset `msvc` (Ninja, Debug), which must be configured from a
Developer Command Prompt (vcvars64) - a preset cannot establish that environment.
Warnings-as-errors is **ON** - the tree builds clean under /W4 /WX (see the warnings
section below). Use `build-msvc.cmd` at the repo root, which finds the toolchain, enters
vcvars, then configures and builds.

P1 measured 66 failed build steps across 13 targets. Final state: 0.

### What it took, and what each thing actually was

Build system, no engine code (7 of the 13 targets):

- **C1041 x49** - every target pointed `CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY` at one shared
  dir, and CMake names compile PDBs from the compiler default (`vc<ver>.pdb`), not the
  target, so parallel cl jobs raced for the same file. `/FS` alone only got 49 -> 14;
  leaving the variable unset on MSVC (per-target build dirs) fixed it. Linker PDBs still
  go to Bin.
- **D8021 x1** - `Audio/CMakeLists.txt` set `COMPILE_OPTIONS "-Wno-error"` unguarded; cl
  read it as `/Wno-error` and rejected the TU. Ours, not MSVC's.
- **LNK2019 x9** - AngelScript's x64 trampoline was wired only for Clang (the GNU-syntax
  `.S`). MSVC needs the MASM `.asm` sibling plus `enable_language(ASM_MASM)`, which also
  cleared the `CMP194` configure warning.
- **C2015 x1** - `U'●'` in UISandbox. Without `/utf-8` cl decodes sources in the
  system ANSI codepage, so three UTF-8 bytes read as three characters. **This is not
  flagged by C4819** (which only fires when a character cannot be represented at all), so
  "no C4819" is not evidence `/utf-8` is unnecessary - that inference was made during
  triage and was wrong.
- **D9025 x9036** - CMake seeds `/EHsc`, our policy appends `/EHs-c-`, cl logs every
  override. Noise, but it buried the real diagnostics. Strip the default instead.

Our code (1 target):

- **C3083 x2** - `IBLSystem::IBLSystem::Context`. The doubled injected-class-name is legal
  and clang/gcc accept it; MSVC does not. A sweep found no other `X::X::` in the tree.

### The interesting class: MSVC cannot re-read what it wrote (3 targets)

Three failures shared a signature worth recognising, because the diagnostic actively
misleads: **the exporting module compiles fine, and the error appears in a consumer, quoting
a line in the module's source.** cl serialises something into the `.ifc` that it then cannot
materialise on import. It says so - "IFC import detected. If possible, please follow
instructions here ... https://aka.ms/report-cpp-modules-problem".

- **`<stop_token>` (RHI.DX12)** - `Core/Threading/JobSystem.cppm` included `<thread>` in its
  global module fragment for a single `std::this_thread::yield()`. On MSVC `<thread>` drags
  in `<stop_token>`, and importing draconic.core then died with C1116 on a
  `std::_Stop_callback_base::_Do_attach` specialization. Fixed by adding `sys::ThreadYield`
  to Core's existing thread backend and dropping the include. No module interface in the
  tree includes `<thread>` now.
- **`<filesystem>` (Editor.App)** - `ExportTemplate.cppm` included it in the INTERFACE for
  two inline functions; consumers then failed on
  `std::_Bitmask_includes_all<__std_fs_stats_flags>` with C2678. The impl unit already
  included `<filesystem>` for exactly this and never used it - only the bodies were in the
  wrong place. (Replacing std::filesystem with engine APIs was considered: Core has
  `RemoveDirectoryRecursive` and VFS has `CreateDirectories`, but there is no recursive-copy
  equivalent, so it would mean new engine surface. Deferred.)
- **Generic lambda (GUI.Tests)** - `DefaultWidgetFactory` registered widgets via
  `[&f](StringView, auto maker)`. Its `operator()<Maker>` instantiates in the importing TU
  and cl cannot re-parse the serialised body - reported as "C2187: syntax error: 'newline'
  was unexpected here". Typing the parameter as `WidgetFactory::Factory` removed the
  template.
- **Partition base class (Engine.Project, Editor.Core, Settings.Tests, Content.Tests)** -
  C2504 "'XmlNode': base class undefined" at `Document.cppm:34`, plus 24 cascading C3668.
  Draconic.Xml itself built and Xml.Tests linked; only consumers instantiating a template
  through the `.ifc` failed. `export import :nodes;` in `:document` gives cl a direct path.

**Rule of thumb this leaves us with:** on MSVC, keep STL headers and generic lambdas out of
module *interfaces*. Both are fine in implementation units.

**Two hypotheses tested and disproved** during triage, recorded so they are not retried:
`/permissive-` and `/Zc:preprocessor` fix neither the generic-lambda class nor its doctest
cascade (verified by recompiling those TUs with the flags). And a standalone repro of the
nested-lambda shape compiles fine - the `.ifc` round trip is required to reproduce it, which
is why the first triage misfiled it as a parser bug.

### Warnings-as-errors

Enabled after the build was green. /W4 /WX surfaced 12 warnings in our own code, all fixed
rather than suppressed - two genuinely uninitialized locals in TabView, three silent
narrowings, an unreachable-code idiom, and shadowing in three places (the worst being a
20-branch `else if (auto* m = Cast<T>())` chain, now named per type). `Abs` gained f64/i32/i64
overloads, which removed 10 narrowing warnings without touching any call site.

One suppression: **/wd4530** ("C++ exception handler used, but unwind semantics are not
enabled"). We disable exceptions via /EHs-c- while still using the STL, so cl emits this from
its own headers - 114 instances, none from `Code/`. It restates a deliberate choice.

Third-party needed no special handling: no vendored target links `draconic_policy`, and the
one vendored file compiled into one of ours (`stb_vorbis.c` via `MiniaudioImpl.cpp`) already
opts out of -Werror per compiler.

### Toolset coverage

Verified on **cl 19.51** (VS 2026, toolset 14.51) and **cl 19.44** (VS 2022, toolset 14.44).
Only one difference between them: 19.44 reports C4127 for a compile-time flag folded into a
runtime `&&`, which 19.51 accepts; fixed with `if constexpr`. Everything else - including all
four .ifc issues below - behaves the same on both, so none of this depends on a bleeding-edge
toolset.

Note on `build-msvc.cmd`: it selects by MSVC **toolset version**, not vswhere's answer. On this
machine vswhere stopped reporting a working VS 2026 install (an interrupted update seems to
have deregistered the instance while leaving it on disk and fully functional), and the "18" vs
"2022" directory naming schemes cannot be compared. `DRACONIC_VS_PATH` overrides the search.

### Test status

`ctest` on MSVC: **106/106**, same as clang (repeated runs on both). All three failures that
were open during this work turned out to be real bugs rather than MSVC quirks, and each has
its own entry below: the WebGPU read-only depth attachment, the GUI `@keyframes` use-after-move,
and the Win32 file share mode behind `Draconic.Editor.Scene.Tests`. None was MSVC-specific.


## GUI: @keyframes animations never started (use-after-move) - FIXED

**Status:** FIXED 2026-08-08. `Draconic.GUI.Tests` 349/349 cases, 1253/1253 assertions.

`StyleManager::ApplyTo` moved the resolved style into the cache and then handed the same
object to `ApplyAnimation`:

```cpp
m_cache.InsertOrAssign(&widget, core::Move(resolved));
...
ApplyAnimation(widget, resolved);          // moved-from - empty
```

So `Has("animation")` was always false and no `KeyframeAction` was ever spawned. Animations
did nothing at all, with no error or warning. Fixed by reading the cached copy.

Everything around it worked, which is what hid it: the sheet parsed, the rule matched, the
resolved value was exactly `fade 2s`, the keyframes were found, and the widget could reach the
SceneNode's ActionManager. Only the object passed to `ApplyAnimation` was hollow. Worth
remembering as a debugging pattern - when every input checks out, suspect the handoff.

**This was also the long-standing "GUI test segfaults" report.** doctest is built with
`DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS`, so a failing `REQUIRE` cannot unwind;
execution continued past `REQUIRE(r != nullptr)` straight into a null dereference. The crash
was this bug wearing a different hat. Note the general hazard: **any failing REQUIRE in these
suites keeps running and can crash**, which also means a crash truncates the rest of the run
(doctest then reports the remaining cases as "skipped").

## Editor.Scene: Win32 file share mode broke prefab regeneration - FIXED

**Status:** FIXED 2026-08-08. Was the long-standing `Draconic.Editor.Scene.Tests` failure
("model-prefab: manifest -> spawnable prefab; regeneration reuses the instance").

`FileOpen` in `Win32System.cpp` passed `FILE_SHARE_READ`, so an open read handle made any
later open-for-write on that path fail. The test opens the generated prefab's payload stream,
spawns from it, then regenerates the prefab while that stream is still open - and the
regeneration's `WriteData` could not open the file, so `GenerateModelPrefab` returned a null
instance and `regenerated == false`.

**Windows-only, which is why it looked pre-existing and inexplicable**: POSIX lets you rewrite
a file that has open readers, so the same test passes on Linux. Fixed by sharing read, write
and delete, matching the semantics the rest of the engine is written against.

Isolated by narrowing: two back-to-back `GenerateModelPrefab` calls succeed, and the second
only fails when the payload stream is held open across it. Then directly - `WriteData` with a
reader open returns false, with it closed returns true. An earlier guess that it bailed at
`Cast<ModelManifestAsset>` was wrong; `ReadObject` succeeds fine with the stream open.

## Draconic.GUI.Tests: intermittent SIGSEGV in the sorting-proxy TableView case - OPEN

**Status:** OPEN, seen once, not reproduced since. Recorded so it is not lost.

During one full `ctest` run, `SortingProxyModelTests.cpp:113` ("sorting-proxy: TableView header
click wired to ToggleSort re-sorts the view") crashed with SIGSEGV and zero failed assertions.
It has not recurred: five subsequent full runs (three clang, two MSVC) are 106/106, and the
case passes standalone, in isolation, and from both working directories.

So there is a latent intermittent fault in that path - most likely order- or timing-dependent
state, since the case itself is deterministic. Not investigated. If it resurfaces, the run
that caught it was a `-j4` full-suite run immediately after `Draconic.GUI.Shell.Tests`.

## WebGPU: read-only depth needs Sampled usage - FIXED

**Status:** FIXED 2026-08-08. Was the last failure in `Draconic.RHI.WebGPU.Tests`
("sky-shaped draw - z=1.0 vs cleared depth, read-only pass, MRT"). Suite is now 17/17 cases,
259/259 assertions, on both clang and MSVC.

**Root cause:** wgpu treats a `depthReadOnly` attachment as a READABLE resource and validates
the underlying texture for `TEXTURE_BINDING`. The test built its depth target with
`TextureDesc::DepthBuffer`, which declares only `TextureUsage::DepthStencil`, so the
read-only pass was rejected. Adding `TextureUsage::Sampled` fixes it. A real prepass-depth
reader declares Sampled anyway, so this was the test under-declaring, not an engine defect.

**Why it took so long to find - worth remembering:** the diagnostic is actively misleading.
The rejection is silent at `BeginRenderPass`; what surfaces is

```
In wgpuCommandEncoderFinish / In a pass parameter / Parent device is lost
thread '<unnamed>' panicked at src\lib.rs:605:5: Error in wgpuQueueSubmitForIndex
```

- reported two calls later, blamed on device loss, and delivered as a Rust panic that
crosses the FFI as an SEH exception. The device-lost callback never fires (not even with
`WGPUDeviceLostReason_Destroyed`, which we otherwise swallow), so "device lost" is not
literally true. **If you see "Parent device is lost" from wgpu, suspect a missing usage flag
on an attachment before you suspect device teardown.**

Ruled out along the way, each tested in isolation: MRT (fails with a single color target
too), the two-passes-per-encoder shape (the cube-faces test does that and passes), the
preceding depth-clear pass (fails without it), the `depthClearValue` we set on read-only
planes for browser validation, and the graphics backend - **d3d12 fails identically to
vulkan**, which is what ruled out a driver/HAL cause and pointed at wgpu-core validation.

Our translation was correct throughout: `WebGpuCommandEncoder::BeginRenderPass` sets
`depthReadOnly = 1` with load/store left `Undefined`, against a `depthWriteEnabled = false`
pipeline.

**Not done:** nothing in `Code/` sets `depthReadOnly` yet, so there is no engine-side
guard. When the forward pass starts reading prepass depth on WebGPU, the depth target must
declare Sampled. A cheap safeguard would be for the WebGPU backend to check the flag against
the texture's usage and log a real error instead of letting wgpu report a device loss;
that has not been added.


## Legacy 'draconic::' type names in serialized data - COMPAT FALLBACK ACTIVE

**Status:** MITIGATED (TypeRegistry::FindByLegacyName). The 2026-08 debrand renamed
every RTTI registration namespace (Foundation `draconic::X` -> `rtti::X`, Engine
`draconic::X` -> `rtti::engine::X`), but serialized data embeds qualified type
names: content envelopes (.rasset), settings sections, prefab/scene payloads.
Pre-debrand files stopped resolving - the editor read its own settings as
"unknown sections" and could not match assets to editor pages.

**Fix:** `FindByName` falls back on a miss of a `draconic::`-prefixed namespace to
the current spellings (Core RTTI, regression-tested). Data converges to the
new names when saved. FOUR mappings (third + fourth landed 2026-08-11/12 after
LIVE misses): Foundation/Engine renames; the editor->pipeline MOVE for cook
types; the STILL-EDITOR respelling `draconic::editor[::rest]` ->
`rtti::editor::editor[::rest]` (the editor settings' RecentProjectsSettings
section loaded as "unknown"); and the SUBSYSTEM-FLAVORED asset spelling
`draconic::<lib>` -> `rtti::pipeline::<lib>` (real pre-debrand envelopes store
'draconic::physics'::CollisionShapeAsset - NOT the editor-collection spelling
the first pass modeled; without it the cook said "stale schema" and pages said
"no editor registered"). LESSON: the compat surface is only proven against
REAL FILES - each of the last two mappings was found by reading a live
envelope, not by reasoning about the rename. **Removal condition:** delete `FindByLegacyName` (and its
test) once no pre-debrand project/content matters - grep serialized stores for
`draconic::` before pulling it.

**Related infra fix:** core `RemoveDirectory` is a bare rmdir; test scratch
cleanup silently no-oped on populated dirs for months, so suites accumulated
stale cross-run envelopes - which the rename then orphaned (phantom failures in
Resource/Texture/Fonts/Geometry/ModelImporter/AudioPipeline/EditorCore suites).
`RemoveDirectoryRecursive` (Core :filesystem) is the correct clean.
(Editor.Core/Export.cppm's root cleanup was audited: it has its own VFS-rooted
recursion and is correct.)


## clang 21.1 frontend crash compiling `Samples/Sandbox/main.cpp` - RESOLVED

**Status:** RESOLVED 2026-07-23 (as a side-effect of the code-quality pass). A full
`build/clang` keep-going build now compiles the ENTIRE tree - Sandbox and Draconic.Tools.Export
included - with zero failures, and Sandbox compiles cleanly on repeated clean rebuilds.
**Why it went away:** the sec 10.6 extraction pass moved ~16k lines of implementation out
of the interface units these huge TUs transitively import (all of Render, plus Scene /
Content / Runtime), shrinking the module BMIs the clang frontend must materialize - back
below the crash threshold. So reducing interface bloat fixed the crash that interface
bloat caused. Kept here (not deleted) as history + because it was resource-sensitive: if a
future TU re-inflates the interface surface it could recur.

--- original triage (2026-07-22) below ---

Pre-existing; NOT introduced by the code-quality pass (proven by reverting the only
structural Core change and rebuilding - the crash reproduced identically).

**What:** the clang 21.1 frontend crashes (varying SIGSEGV / SIGABRT, exit 139 / 134,
`Stack dump` with no source diagnostic) while compiling `Code/Samples/Sandbox/main.cpp`.
It is a *compile-time* crash, not a compile error and not runtime - the linked
`Bin/.../Sandbox` binary builds (when a compile succeeds) and runs fine.

**clang-specific:** GCC compiles the same TU cleanly, every time (a full `build/gcc`
keep-going build, including Sandbox, is 100% green). So this is a clang-21.1 frontend
bug on a very large TU, NOT a code defect - and `build/gcc` is the reliable full-build
verification path.

**Where:** `Code/Samples/Sandbox/main.cpp` - a very large module TU (the resulting debug
binary is ~60 MB from one TU). Same fragility the runtime-host work hit before, worked
around via `DefaultApplication::PrimaryScenes()` returning `SceneManager&` to avoid
materializing `GameInstance` in huge sample TUs. `Tools/Export/Main.cpp` (Draconic.Tools.Export)
is likely the same class of TU.

**Impact:** intermittent - it compiled successfully earlier the same day (binary
timestamp confirms), then crashes on retry, i.e. resource/memory-sensitive (the frontend
peaks very high on this TU; SIGABRT is consistent with `bad_alloc`). Breaks the *full*
build, not any engine/editor target. The cleanup pass verifies each module via that
module's own build + tests, so this does not gate it.

**Plan / options:**
1. **Reduce peak memory** on that TU: build samples with fewer parallel jobs (`-j2`) or
   split `Sandbox/main.cpp` into smaller TUs so the frontend's working set stays bounded.
2. **Shrink the materialized surface** in the sample the way the runtime host did
   (return references instead of by-value `GameInstance`-heavy types across the seam).
3. Upstream: a minimal reproducer for the LLVM frontend crash if it persists after (1).

Recommendation: (1) first (cheap, likely sufficient), then (2) if it recurs.

---

## ctest: intermittent `DraconicEditorCoreTests` failure under parallel load — FIXED

**Status:** FIXED 2026-07-20. Was the recurring "1 tests failed on the first run after a
build" flake seen many times across clang and gcc this session.

**Root cause (a real bug, not just a flaky test):** `EditorJobService::Update`
(`JobService.cppm`) drained a job's log, then on completion called `m_ctx.Reset()`
**without a final drain**. A log line the worker appended *between* that drain and the
reset was discarded — so a fast job could silently drop its last log lines. The
`jobs: ... sawLog` assertion (`JobServiceTests.cpp:54`) caught it: under peak parallel
load the "halfway" log landed in that window and was lost.

**Fix:** `Update` now does a final drain of `m_ctx->m_log` **after `JoinWorker()`** (worker
joined ⇒ no more appends) and before `m_ctx.Reset()`, so completion never drops a job's
last logs. Verified: two full gcc ctest runs 87/87 after the fix (previously failed nearly
every first run under load).

---

## AngelScript coroutine scheduler: `$func` GC warning at engine shutdown

**Status:** open — cosmetic, low priority (shutdown-only console warning, no functional leak).

**Symptom:** the AngelScript coroutine scheduler prints a `$func` garbage-collector warning
when the engine is destroyed (seen in the AngelScript conformance battery, whose coroutine
section holds coroutine contexts/funcdef handles). The isolated delegate test is
warning-free, so it's specific to the coroutine path.

**Cause:** the coroutine scheduler (own `asIScriptContext` per coroutine + AddRef'd
funcdef/function handles) doesn't release every held handle before engine teardown, so
AngelScript's GC reports a lingering reference at shutdown. Benign (the process is ending)
but it means the scheduler's teardown isn't releasing all coroutine handles.

**Plan:** audit the coroutine scheduler's teardown to release all held `asIScriptFunction*`
/ context handles before the engine is destroyed; the warning should disappear.

---

## AngelScript: misaligned `asPWORD` read in bytecode dispatch (UBSan)

**Status:** open — vendored third-party bug, benign on x86-64, fix upstream + carry a patch.

**Diagnostic** (reproduce: build `build/asan` — `-fsanitize=address,undefined
-fno-sanitize-recover=all` — and run `Bin/Debug/Linux64-Clang-ASAN/DraconicScriptAngelScriptTests`):

```
ThirdParty/angelscript/source/as_scriptfunction.cpp:1222:47: runtime error:
load of misaligned address 0x... for type '::asPWORD' (aka 'unsigned long'),
which requires 8 byte alignment
```

**Where:** `as_scriptfunction.cpp:1222`, the `asBC_ALLOC` case of the bytecode walk:
`asCObjectType *objType = (asCObjectType*)asBC_PTRARG(&bc[n]);`. `asBC_PTRARG`
reads an `asPWORD` (pointer-width) directly out of the packed bytecode array `bc[]`,
whose instruction stream is `asDWORD`-aligned (4 bytes), so a pointer argument can
land on a 4-byte (non-8-byte-aligned) offset. Same pattern recurs for every
`asBC_PTRARG`/`asBC_INTARG` pointer read across the interpreter and serializer.

**Trigger:** any script-class instantiation (`asBC_ALLOC` executes) — it is NOT
specific to our code. First seen on the pre-existing "instantiate a script class"
AngelScript test; the coroutine work did not introduce it.

**Impact:** none in practice on x86-64/ARM64, which permit unaligned loads — this is
strict-UB flagged by UBSan, not a functional fault. It only matters (a) as noise that
masks real UBSan findings in our own AngelScript-linked tests, and (b) on a
hypothetical alignment-strict target. AngelScript is a committed backend
([[scripting-backend-neutrality]]).

**Plan / options:**
1. **Upstream PR** to https://github.com/anjo76/angelscript (the repo we vendored
   2.39.0-WIP from): route pointer args through a `memcpy`-based read in `asBC_PTRARG`
   (the standard fix for packed-stream reads) so the load is alignment-safe. This is the
   correct long-term fix; AngelScript upstream has historically been aware of the packed
   pointer-arg design.
2. **Carry a local vendor patch** in `ThirdParty/angelscript` in the meantime (a
   `DRACONIC_PATCHES/` note + the diff) so our sanitizer builds are clean before upstream
   merges. Requires touching vendored source — do it deliberately, documented here.
3. **Suppress** via a UBSan `alignment` suppressions file scoped to
   `ThirdParty/angelscript/*` so our own code's UBSan signal stays clean without editing
   the vendor drop. Cheapest; hides the issue rather than fixing it.

Recommendation: (3) now to unblock clean sanitizer runs on our code, then (1) upstream.

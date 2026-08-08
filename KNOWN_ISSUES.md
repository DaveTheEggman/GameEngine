# Known issues

Tracked defects we've triaged but not yet fixed. Each entry: what, where, impact,
and the plan. Keep newest first.

---

## MSVC support (I9 P1) - TRIAGED, VERDICT: TRACTABLE

**Status:** P1 complete 2026-08-08. `msvc` preset added; triage build run; no fixes made.
**Verdict: small conformance work, NOT a compiler-support wait.** Details below drive P2.

**Toolchain:** VS 2026 Community 18.7.0, cl 19.51.36247 (toolset 14.51.36231),
CMake 4.3.0, Ninja 1.11.1. Preset `msvc` (Ninja, Debug). Must be configured from a
Developer Command Prompt (vcvars64) - the preset does not and cannot set that up.
Warnings-as-errors is OFF in the preset while triage is open.

**Headline:** `5516 / 6102` build steps succeeded, **13 targets failed**, six error
classes. Most of the engine - including all of Core, RHI (except DX12), VG, Fonts,
Scene, Render (except one TU), UI, and the bulk of the test suites - compiles and
links under MSVC today. C++20 modules broadly work; the module problems are narrow
and specific.

Raw error counts are misleading (115 errors / 9036 warnings) because two classes are
pure noise that repeat per-TU. By target:

| Class | Failing targets | Kind |
|---|---|---|
| C1041 shared PDB | UI.Tests, Animation.Editor.Tests, UISandbox | build system |
| D8021 `/Wno-error` | Audio | our build bug |
| LNK2019 `CallX64` | GameInstance.Tests, Net.Manager.Tests, AngelScript.Tests | build system |
| C2504/C3668 XmlNode | Engine.Project, Editor.Core, Settings.Tests, Content.Tests | **MSVC module bug** |
| C1116 `<stop_token>` | RHI.DX12 | **MSVC module bug** |
| C2059/C2187 nested lambda | GUI.Tests | **MSVC parser limit** |
| C3083 `IBLSystem::IBLSystem::` | Render | our code |

### Mechanical (build-system) - 7 of 13 targets, no engine code involved

1. **C1041 x49** - `cannot open program database ...\lib\vc140.pdb; if multiple CL.EXE
   write to the same .PDB, please use /FS`. Every static lib shares
   `CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY` with the default `vc140.pdb` name, so parallel
   Ninja jobs collide. Fix: `/FS`, or a per-target `COMPILE_PDB_NAME`. Nothing to do
   with modules; it just happens to hit the widest targets.

2. **D8021 x1** - `Code/Draconic/Foundation/Audio/CMakeLists.txt:27` sets
   `COMPILE_OPTIONS "-Wno-error"` on `MiniaudioImpl.cpp` with no compiler guard, so cl
   gets `/Wno-error` and rejects it. **Ours, not MSVC's.** One-line guard; the fix is
   compiler-portability, not MSVC-specific, so it should land as its own cherry-pickable
   commit.

3. **LNK2019/LNK1120 x12** - unresolved `CallX64`, `GetReturnedFloat`,
   `GetReturnedDouble`. `ThirdParty/CMakeLists.txt:216` adds AngelScript's x64
   native-call trampoline **only for Clang** (`as_callfunc_x64_msvc_clang.S`). MSVC needs
   the MASM sibling `as_callfunc_x64_msvc_asm.asm`, which is vendored but never compiled.
   Also requires the `ASM_MASM` language - `project()` currently declares plain `ASM`,
   which is why configure warns `CMP194: MSVC is not an assembler for language ASM`.

### Our code - 1 target, trivial

4. **C3083 x2** - `IBLSystemImpl.cpp:110` and `:462` write
   `IBLSystem::IBLSystem::Context` / `IBLSystem::IBLSystem::IblPush`. The doubled
   qualifier is the injected-class-name; legal C++ that clang and gcc accept, MSVC
   rejects with "the symbol to the left of a '::' must be a type". Deleting the
   redundant `IBLSystem::` is correct on every compiler - another cherry-pickable
   portability commit, not an MSVC workaround.

### Genuine MSVC issues - 5 targets

5. **C2504 + C3668 x32 - module partition visibility. The one that matters.**
   `XmlNode` (partition `:nodes`) is undefined at `Document.cppm:34` where
   `XmlDocument : public XmlNode` derives from it, cascading into 24 "method with
   override specifier did not override any base class method" errors.
   The import graph is acyclic and legal:

   ```
   :nodes    <- :lexer :ns :escape
   :writer   <- :nodes :escape
   :document <- :result :lexer :ns :nodes :writer     <- diamond on :nodes
   ```

   **Crucially, `Draconic.Xml` itself builds and `Draconic.Xml.Tests` links.** The
   failure only appears in *consumers* (Engine.Project, Editor.Core, Settings.Tests,
   Content.Tests) when they `import draconic.xml` - i.e. MSVC writes a `.ifc` it then
   cannot correctly re-materialize, losing the base class across the diamond. That
   makes it an `.ifc` round-trip bug rather than anything wrong with the source.
   Worth a minimal upstream reproducer. Local workaround to try in P2: have `:document`
   re-export or reorder its partition imports, or flatten the `:nodes`/`:writer`
   diamond.

6. **C1116 x2** - `stop_token(248): unrecoverable error importing module 'draconic.core'.
   Specialization of 'std::_Stop_callback_base::_Do_attach' with arguments 'false'`,
   compiling `DxModule.cppm`. MSVC's own STL interacting with an imported module; no
   Draconic code in the diagnostic. Blocks RHI.DX12 only. Least likely to be fixable on
   our side - this is the one candidate for "wait for a toolset update", and the first
   thing to re-test on a VS bump.

7. **C2059/C2143/C2187/C2065/C2297/C2660/C1903 x~14** - `MarkupLoader.cppm:98`,
   a lambda with a trailing return type nested inside a *generic* lambda:

   ```cpp
   auto reg = [&f](core::StringView name, auto maker)          // generic
   { f.Register(name, [maker]() -> RefPtr<Node> { return maker(); }); };   // line 98
   ```

   MSVC's parser gives up at the inner lambda; the doctest `DOCTEST_ANON_FUNC_2` and
   `consume` errors in `MarkupTests.cpp` are downstream cascade, not separate bugs.
   Confined to `Experimental/GUI`. Easily restructured (hoist the inner lambda or drop
   the trailing return type).

**Tested and ruled out:** `/permissive-` and `/Zc:preprocessor` fix neither (7) nor the
doctest cascade - verified by recompiling both TUs with the flags added. Do not assume
a conformance switch makes these go away. No `C4819` anywhere, so `/utf-8` is not needed.

**Noise to silence before P2 so real errors are visible:** `D9025 x9036`
(`overriding '/EHs' with '/EHs-'`) - CMake injects a default `/EHsc` that our policy's
`/EHs-c-` then overrides. Strip the default rather than override it. `C4530 x102`
(unwind semantics) and `LNK4099 x294` (missing PDB) follow from the same two causes.

**Suggested P2 order:** build-system items (1)(2)(3) and code item (4) first - they are
mechanical, unblock 8 of 13 targets, and (2)(4) are portability fixes that belong on
`master` regardless. Then (7). Then (5), the only one needing real thought. Leave (6)
parked pending a toolset update. Work happens on the `msvc` branch; keep
non-MSVC-specific fixes as isolated commits so they cherry-pick cleanly.

**Full log:** regenerate with `cmake --build --preset msvc -- -k 0` from a vcvars shell.


## Legacy 'draconic::' type names in serialized data - COMPAT FALLBACK ACTIVE

**Status:** MITIGATED (TypeRegistry::FindByLegacyName). The 2026-08 debrand renamed
every RTTI registration namespace (Foundation `draconic::X` -> `rtti::X`, Engine
`draconic::X` -> `rtti::engine::X`), but serialized data embeds qualified type
names: content envelopes (.rasset), settings sections, prefab/scene payloads.
Pre-debrand files stopped resolving - the editor read its own settings as
"unknown sections" and could not match assets to editor pages.

**Fix:** `FindByName` falls back on a miss of a `draconic::`-prefixed namespace to
the two current spellings (Core RTTI, regression-tested). Data converges to the
new names when saved. **Removal condition:** delete `FindByLegacyName` (and its
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

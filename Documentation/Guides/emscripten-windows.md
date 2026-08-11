# Building for Web (Emscripten) from Windows

Target toolchain: **emscripten 6.0.5** (same version the Linux build uses).

```
cmake --preset wasm
cmake --build --preset wasm
```

The cook step runs on the *host*, so a Windows host needs Windows cook tools. Those are
vendored: see `ThirdParty/Naga/PROVENANCE.md` and `ThirdParty/Tint/PROVENANCE.md`. CMake picks
them by `CMAKE_HOST_WIN32` rather than `WIN32` - under Emscripten `WIN32` is false and
`CMAKE_SYSTEM_NAME` is `Emscripten`, so the plain check silently selects the Linux binaries.

## Required emsdk patch: response files on the compile path

**A stock emsdk cannot build this project on Windows.** Apply `Tools/Emscripten/emcc-compile-response-file.patch`
to your emsdk checkout before building:

```
cd $EMSDK/upstream/emscripten
git apply /path/to/Raptor/Tools/Emscripten/emcc-compile-response-file.patch
#   ...or, without git:  patch -p1 < .../emcc-compile-response-file.patch
```

The patch touches only `emcc.py`, in two places. `emsdk install` replaces the whole tree, so
re-apply after upgrading emscripten. To check whether it is applied:

```
grep -c get_command_with_possible_response_file $EMSDK/upstream/emscripten/emcc.py   # 2 when applied
```

### Why

`em++` is a Python wrapper. Very early in `emcc.py` it calls `substitute_response_files()`,
which expands any `@file` argument into the full argument list. It then re-invokes the real
`clang` with that expanded list. On Windows `CreateProcess` caps a command line at 32767
characters, so a command that was *deliberately* short because it used a response file
becomes one that cannot be spawned:

```
FileNotFoundError: [WinError 206] The filename or extension is too long
```

Emscripten already has the fix - `building.get_command_with_possible_response_file()`, which
re-wraps an over-long command back into a response file. It is applied on the **link** path
(`tools/building.py`, `tools/link.py`, `tools/system_libs.py`) but was never applied on the
**compile** path. The patch routes the two compile invocations in `emcc.py` through the same
helper: the `Mode.COMPILE_ONLY` fast path (which `exec`s clang directly - this is the one
CMake hits) and `compile_source_file` (the compile-and-link path).

This is an upstream gap, not a project quirk; the patch is written to be upstreamable as-is.

### Why this project hits it and most don't

C++20 named modules. CMake passes each TU a `@...modmap` response file listing one
`-fmodule-file=<name>=<path>` entry per transitively reachable BMI. Draconic's leaf targets
reach ~470 modules, and each entry is ~137 characters, so the expanded line is ~65 KB - twice
the limit. `Engine.DefaultApp` is the worst.

Two mitigations were measured and are **not** sufficient on their own, which is why the patch
is required rather than optional:

| Mitigation | Effect on the worst modmap |
|---|---|
| baseline | 64,376 bytes |
| short binary dirs for every target | 53,290 bytes (-17%) |
| removing UI's contribution entirely | -15,015 bytes |

Even both together land around 38 KB, still over the limit, and the number grows with every
module added. Only the response-file fix is durable.

## Note: do not "fix" this by merging module partitions

Draconic.UI was the first target to hit the limit, so consolidating its ~140 module partitions
looks like the obvious fix. It is not, and it was tried and reverted. Merging 20 partitions cut
UI's own worst modmap from 39,416 to 29,828 bytes - but UI is only ~15 KB of the 64 KB in the
modmap that actually matters (`Draconic.Engine.DefaultApp`, 469 modules). Deleting UI's
contribution *entirely* would not bring that one under the limit.

Merging also costs incremental-build granularity: one partition per concept means editing one
theme rebuilds only that theme's consumers, and a merged `:builtin_themes` rebuilds all of them.

If a modmap ever looks too big, the answer is the patch above, not the module graph.

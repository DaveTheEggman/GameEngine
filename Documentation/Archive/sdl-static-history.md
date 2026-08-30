# Link SDL statically (task #119)  (archived)

> Status: ARCHIVED - fully built. Non-authoritative: this is the original build spec, kept
> as the record of what was built and why; present-tense truth is the code + tests.

Size: S. Scope: root `CMakeLists.txt` (~line 337) +
`Code/Draconic/Foundation/Draconic.Shell.Desktop/CMakeLists.txt`.

## Context

Desktop shell links the SYSTEM SDL3 via `find_package(SDL3 QUIET GLOBAL)` and
`SDL3::SDL3` (shared). That makes distributed builds depend on the user's
libSDL3.so version (the DXC sidecar lesson: relocated dists break on missing
runtime deps - see memory `dxc-runtime-sidecar`, fixed by staging the .so; for
SDL we want the opposite: no runtime file at all).

## Goal

Desktop binaries (editor, runners, exported games) carry SDL3 inside the
executable: no libSDL3.so dependency at runtime, on Linux AND Windows (the user
builds on both; verify Linux, keep the CMake portable).

## Design

1. Vendor SDL3 as source, consistent with how other third-party deps are done
   in this repo (check `Code/ThirdParty/` or the existing vendored deps -
   Dear ImGui 1.92.6 is vendored, miniaudia/stb are vendored; match that
   pattern and directory layout). Pin a release tag (latest stable SDL3).
   FetchContent is acceptable ONLY if the repo already uses it for another dep;
   otherwise vendor to keep offline builds working.
2. Build SDL3 as a static library: `SDL_SHARED=OFF`, `SDL_STATIC=ON`, tests
   and unneeded subsystems OFF. Link `SDL3::SDL3-static` from Shell.Desktop.
3. Keep an escape hatch: `option(DRACONIC_SYSTEM_SDL "use system SDL3" OFF)`
   preserving today's find_package path when ON (for distro packaging).
4. Static SDL on Linux pulls in X11/Wayland/pipewire etc. as RUNTIME dlopen
   deps (SDL loads them dynamically by design - this is fine and is what makes
   static SDL portable). Do NOT disable the dynamic loading (`SDL_X11_SHARED`
   etc. stay default ON).
5. Emscripten and Null shells are untouched (the SDL branch is already
   desktop-only in the root CMakeLists).

## Verification

- `ldd Bin/Debug/Linux64-Clang/<editor binary> | grep -i sdl` -> no libSDL3.
- Editor + a sample (VGSandbox or PhysicsPlayground) launch and get input.
  (User does the visual/interaction check; hand them the Debug binary.)
- Both compilers build green; a from-clean configure works without network if
  vendored (state which approach you took).
- Windows: the user builds there in parallel - flag in the handoff that the
  first Windows build after this lands needs a re-configure, and confirm the
  CMake does not hardcode Linux-only SDL options.

## Tests

CMake/link plumbing has no doctest surface; the acceptance checks above are
the test. Existing Shell.Desktop tests must stay green (they exercise SDL).

---

## State (appended 2026-08-29)

**DONE.** SDL3 3.4.10 vendored as source at `ThirdParty/SDL3` (34M, replacing the
58M prebuilt SDK - a net shrink) and built statically in-tree:
`ThirdParty/CMakeLists.txt` does `add_subdirectory(SDL3)` with `SDL_SHARED=OFF /
SDL_STATIC=ON` (tests/examples/install off) and exports `ENGINE_SDL_TARGET =
SDL3::SDL3-static`; the root CMakeLists adds `Foundation::Shell.Desktop` when that
target exists, and Shell.Desktop + Shell.Desktop.Tests link `${ENGINE_SDL_TARGET}`.
The escape hatch is `option(ENGINE_SYSTEM_SDL ... OFF)` -> `find_package(SDL3)` +
`SDL3::SDL3`. SDL keeps its runtime dlopen of X11/Wayland/PipeWire/ALSA (backends
`*_SHARED=ON`), which is what keeps the static binary portable.

Verified (Linux/clang and Linux/gcc, Debug): `ldd` on Tools.Editor, Engine.Player,
and Shell.Desktop.Tests shows no libSDL3 (SDL is inside the exe);
Shell.Desktop.Tests 10/10. Because SDL is compiled by the same toolchain as the
engine, its glibc floor matches the engine's rather than a prebuilt binary's floor,
which is also what lets the Steam Deck build lane ship no libSDL3.so. The CI/release
SDL install+cache steps and the Steam Deck Dockerfile's SDL build were removed (SDL
builds in-tree now).

Paths in the original spec referenced the pre-debrand `Code/Draconic/...` tree; the
real files are `Code/Foundation/Shell.Desktop*` and the option is `ENGINE_SYSTEM_SDL`
(not `DRACONIC_SYSTEM_SDL`). Windows build still needs its first re-configure after
this lands; the CMake has no Linux-only SDL options (SDL's own CMake handles the
per-platform backends).

### Original state (appended 2026-08-03)

NOT STARTED. No commits. Pure CMake/link plumbing (vendor SDL3 as source);
self-contained, pick up whenever desired.

### Size measurement (appended 2026-08-03, for the "where it lives" decision)

Measured a fresh SDL3 source checkout vs the current prebuilt:
- **SDL3 source, minimal vendor** (src + include + cmake + CMakeLists, no test/examples/docs):
  **~35M** (src/ alone 31M, include 3.8M).
- SDL3 source, full working tree (adds test 9M + examples 8M + docs): 57M.
- **Current `ThirdParty/SDL3` prebuilt SDK: 58M, ALREADY git-tracked** (105 files; 54M is the
  prebuilt `lib/` binaries).

So vendoring source in place of the prebuilt is a **net ~23M SHRINK**, not a growth. It would be
the largest SOURCE lib (vs JoltPhysics 4.8M/444 files, angelscript 2.9M) but the repo already
commits far larger PREBUILT blobs (DXC 51M, Tint 21M, WgpuNative 18M, Naga 10M). Recommendation:
keep it at `ThirdParty/SDL3`, swap `lib/` for `src/` + build via add_subdirectory; drop
test/examples/docs to hit ~35M. Pruning per-platform backend dirs saves only ~5-10M and is
fragile across SDL updates - not worth it.

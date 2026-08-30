# SDL3 - vendored source (static build)

- Upstream: https://github.com/libsdl-org/SDL
- Version: release-3.4.10 (see include/SDL3/SDL_version.h)
- License: zlib (LICENSE.txt, preserved verbatim - required by the license)

Vendored as SOURCE and built statically in-tree (ThirdParty/CMakeLists.txt:
SDL_SHARED=OFF / SDL_STATIC=ON, tests/examples/install off, EXCLUDE_FROM_ALL so
it compiles only for shell-linking targets). Building with the engine's own
toolchain keeps SDL's glibc floor equal to the engine's - the reason the prebuilt
SDK was replaced. SDL still dlopens X11/Wayland/PipeWire/ALSA at runtime
(backends *_SHARED=ON), which keeps a static SDL portable across distros.

## Prune set (what was removed from the upstream release tarball)

Kept: `src/ include/ cmake/ build-scripts/ wayland-protocols/ CMakeLists.txt
LICENSE.txt INSTALL.md WhatsNew.txt`.
Removed: tests, examples, docs, IDE project files (VisualC/Xcode/android-project).
A re-vendor must apply the same prune so diffs stay reviewable.

## Escape hatch

`-DENGINE_SYSTEM_SDL=ON` links a system SDL3 via find_package instead (distro
packaging); empty ENGINE_SDL_TARGET means no SDL and the desktop shell is skipped.

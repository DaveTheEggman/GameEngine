# recastnavigation - vendored

Source: https://github.com/recastnavigation/recastnavigation
Pinned release: **v1.6.0** (tag `v1.6.0`).
License: zlib (see `License.txt`).

Vendored BY COPY (house rule, like Jolt/AngelScript/astcenc; no submodule).
Only the library components are taken - the `RecastDemo`, `Tests`, `Docs`, and
build-system scaffolding of the upstream repo are NOT vendored.

## What is here

| Component | Include + Source | Compiled by `ThirdParty::Recast`? |
|---|---|---|
| `Recast/` | yes | YES - navmesh bake (rasterize + region + contour + polymesh) |
| `Detour/` | yes | YES - runtime navmesh + query |
| `DetourCrowd/` | yes | YES - agent crowd + local avoidance |
| `DetourTileCache/` | yes | NO (in-tree; joins in navigation P2 - dynamic obstacles) |
| `DebugUtils/` | yes | NO (in-tree; engine debug-draw uses the engine's own draw) |

The uncompiled components are kept in-tree so a later phase can add them without
re-vendoring.

## Build wiring

The `ThirdParty::Recast` house target lives in `ThirdParty/CMakeLists.txt`. It
compiles the three enabled components as one static lib, exposes their `Include/`
dirs PUBLIC + SYSTEM (so `foundation.navigation` can `#include <Recast.h>`,
`<DetourNavMesh.h>`, `<DetourCrowd.h>`), and builds without the engine's
`-Werror`. Pure exception-free C++; built for Emscripten too (gate like Jolt).

## Local modifications

None. If a patch is ever required, isolate it in its own commit and note it here.

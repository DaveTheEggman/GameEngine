# Game Engine

C++23 game engine (ported from the Sedulous engine and grown well past it). Built on C++
modules throughout. The GPU layer is an abstract RHI with Vulkan 1.3 as the primary desktop
backend and WebGPU as a first-class second backend - wgpu-native on desktop, the browser's
WebGPU when built for the web with Emscripten. Ships with a full editor (scene editing,
asset pipeline/cook, play-in-editor, export) driven by a Godot-style built-in project
manager, all in one executable.

## Requirements

### All platforms
- CMake 3.28+
- Ninja
- Vulkan SDK (1.3+)

### Windows
- Clang 17+ (via LLVM) - the project builds with clang, not MSVC
- Windows SDK

### Linux
- Clang 17+ and/or GCC 15+ (both toolchains are kept green; clang is the daily driver)
- Vulkan development libraries
- SDL3 build dependencies

#### Ubuntu / Debian

```bash
sudo apt install cmake ninja-build clang pkg-config \
    libvulkan-dev vulkan-tools vulkan-validationlayers mesa-vulkan-drivers \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
    libxtst-dev libwayland-dev wayland-protocols libxkbcommon-dev libasound2-dev
```

### Web (Emscripten)
- emsdk **6.0.5** with the project's response-file patch applied
  (`Tools/Emscripten/emcc-compile-response-file.patch` - a stock emsdk cannot compile the
  module-heavy targets on Windows). Setup details, including the Windows-host cook tools:
  `docs/emscripten-windows.md`.

## Building

Everything goes through CMake presets; build outputs (executables, cooked shader packs,
runtime sidecars) land in `Bin/<Config>/<Platform>-<Compiler>/`, e.g. `Bin/Debug/Linux64-Clang/`,
`Bin/Debug/Win64-Clang/`, `Bin/Debug/Emscripten-Clang/`.

| Preset | Meaning |
|---|---|
| `clang` / `gcc` | Debug (the development configuration) |
| `clang-reldbg` | RelWithDebInfo (perf validation) |
| `clang-release` / `gcc-release` | Release |
| `clang-shipping` / `gcc-shipping` | Shipping (no asserts, stripped) |
| `wasm` / `wasm-shipping` | Emscripten wasm32 |

### Linux

```bash
cmake --preset clang            # or: gcc
cmake --build --preset clang
ctest --preset clang            # unit tests
```

### Windows (clang)

```powershell
cmake --preset clang
cmake --build --preset clang
ctest --preset clang
```

### Web

```bash
# emsdk env active (source emsdk_env.sh / emsdk_env.bat first)
cmake --preset wasm
cmake --build --preset wasm
```

The web build cooks browser shaders (WGSL) on the host during the build; the host cook
tools (DXC, naga, tint) are vendored for both Linux and Windows hosts.

## The editor

One executable, Godot-style project management:

```bash
Bin/Debug/Linux64-Clang/Tools.Editor                    # PROJECT MANAGER screen
Bin/Debug/Linux64-Clang/Tools.Editor --project <dir>    # open a project directly
Bin/Debug/Linux64-Clang/Tools.Editor <dir>              # same, positional form
```

With no project argument the editor starts on the project manager: recent projects (stored
per-user in `<user-data>/gameengine/editor.settings.xml`, shared by every engine version on
the machine), open/create/remove, and an engine-version gate on open (backup-then-upgrade
prompt for older projects, a hard warning for projects saved by a newer engine).
File > Close Project returns to the manager. A CLI-opened editor keeps the single-project
lifecycle and scaffolds a fresh project if the directory has no `Project.xml` yet.

The full project/settings model is documented in `docs/design/project-and-settings.md`.

## WebScene - the renderer comparison scene

`Code/Samples/WebScene` is the full-renderer exercise scene, built for BOTH desktop and the
browser so the backends can be compared side by side: analytic sky -> IBL, CSM sun + shadowed
spot + orbiting point light, PBR sphere grid, SSR floor, reflection probe + chrome sphere,
instanced ring, decal, sprites, particles, debug draw, and an ImGui tweak panel.

### Desktop

```bash
cmake --build --preset clang --target WebScene Tools.ShaderPack

Bin/Debug/Linux64-Clang/WebScene --vulkan     # Vulkan reference
Bin/Debug/Linux64-Clang/WebScene --webgpu     # WebGPU via wgpu-native (SPIR-V ingestion)
```

To run the EXACT browser shaders (cooked WGSL) on desktop - the fast local repro for
web-render bugs - cook a WGSL pack beside the exe and force the WGSL path:

```bash
# Linux
Bin/Debug/Linux64-Clang/Tools.ShaderPack Data/Shaders Bin/Debug/Linux64-Clang/shaders.dpak wgsl spirv
OPTION_USE_SHADER_PACK=1 ENV_WEBGPU_WGSL=1 Bin/Debug/Linux64-Clang/WebScene --webgpu
```

```powershell
# Windows (PowerShell env syntax - `set X=1` is cmd-only and silently does nothing here)
Bin\Debug\Win64-Clang\Tools.ShaderPack.exe Data\Shaders Bin\Debug\Win64-Clang\shaders.dpak wgsl spirv
$env:OPTION_USE_SHADER_PACK="1"
$env:ENV_WEBGPU_WGSL="1"
Bin\Debug\Win64-Clang\WebScene.exe --webgpu
```

On `--webgpu` the backend logs every GPU adapter at startup and prefers a discrete GPU; on
multi-adapter machines where the pick is wrong, override it with
`ENV_WEBGPU_ADAPTER=<index from the logged list>`.

### Browser

```bash
cmake --build --preset wasm --target WebScene
cd Bin/Debug/Emscripten-Clang && python3 -m http.server 8080
# open http://localhost:8080/WebScene.html
```

If the plain server causes MIME/caching trouble, `Code/Engine/Engine.Player/serve.py`
serves a folder with the correct wasm MIME and no-store headers.

## Samples

Sample executables build into the same `Bin/` directory as everything else
(`OPTION_BUILD_SAMPLES=ON` by default). Highlights: `Sandbox` (the heavy desktop dev
harness), `WebScene` (above), `RHI/` (numbered RHI bring-up samples), `VG/VGSandbox`
(2D vector graphics), `UI` (widget toolkit), `PhysicsPlayground`, `AudioPlayground`,
`ParticleFX`, `ScriptPlayground`, `InputActions`, `NetEcho`, `RenderStressTest`,
`AnimStressTest`, `AnimatedCrowd`. GPU samples take `--vulkan` / `--webgpu`
(`--dx12` where staged).

## Directory layout

```
Code/
  Foundation/     Engine-agnostic libraries: Core (types/containers/math/RTTI),
                  RHI (+ Vulkan/WebGPU/Null backends, validation layer), Graphics,
                  Render + RenderGraph, Materials, Shaders (DXC + WGSL cook),
                  Scene (ECS), Geometry, Model, Image, Texture, Fonts, VG (2D vector
                  graphics), UI (+ toolkit/runtime/viewport), Audio, Input, Physics,
                  Particles, Net, Script (Wren + AngelScript + Luau), Content, Resource,
                  VFS, Xml, Settings, Profiler, Shell (OS integration), Runtime
  Engine/         The assembled game runtime: DefaultApp, GameInstance, Player,
                  Project, per-subsystem engine bindings (Render/Scene/Audio/...)
  Pipeline/       Asset cook/build pipeline: importers, builders, and the per-language
                  script cooks (Wren/AngelScript/Luau) behind one neutral registration root
  Integration/    Cross-subsystem composition (MCP agent host, physics<->script bridge, ...)
  Editor/         Editor libraries: Core (headless domain: project, registry, cook,
                  export), App (UI shell + project manager), per-subsystem editors
  Tools/          Executables: Tools.Editor, .Cook, .Export, .ShaderPack
  Extensions/     Extensions.Imgui (Dear ImGui debug-UI extension)
  Experimental/   Parked experiments (Experimental.GUI)
  Samples/
Data/
  Shaders/          Engine HLSL shader corpus (dev-compiled or cooked into packs)
  Assets/           Raw assets (fonts, models)
ThirdParty/         Vendored dependencies (see below)
docs/               Design docs + platform guides
```

## Third-party dependencies

- **SDL3** - windowing/input shell backend (pre-built on Windows; system package on Linux)
- **Vulkan SDK** - system install (headers + loader)
- **wgpu-native** - the desktop WebGPU implementation (runtime sidecar, loaded at run time)
- **DXC** - HLSL -> SPIR-V (runtime sidecar); **naga** + **tint** - the WGSL cook + validation toolchain
- **JoltPhysics** - physics
- **miniaudio** - audio
- **Wren** + **AngelScript** + **Luau** - scripting backends
- **Dear ImGui** - debug-UI extension
- **stb / cgltf / ufbx / msdfgen** - fonts, images, glTF, FBX, MSDF font baking
- **doctest** - unit tests

# Draconic

C++23 game engine ported from the Sedulous engine (Beef). Uses C++ modules, virtual inheritance for the RHI, and Vulkan 1.3 as the primary GPU backend with a DX12 backend on Windows.

## Requirements

### All platforms
- CMake 3.28+
- Ninja
- Vulkan SDK (1.3+)

### Windows
- Clang 17+ (via LLVM)
- Windows SDK

### Linux
- Clang 17+ (**required** - GCC's C++ module support is not sufficient)
- Vulkan development libraries
- SDL3 build dependencies

#### Ubuntu / Debian

```bash
sudo apt install cmake ninja-build clang pkg-config \
    libvulkan-dev vulkan-tools vulkan-validationlayers mesa-vulkan-drivers \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
    libxtst-dev libwayland-dev wayland-protocols libxkbcommon-dev libasound2-dev
```

## Building

### Debug (default)

```bash
cmake --preset clang
cmake --build --preset clang
```

### RelWithDebInfo

```bash
cmake --preset clang-reldbg
cmake --build --preset clang-reldbg
```

### Running tests

```bash
ctest --preset clang
# or
ctest --preset clang-reldbg
```

## Running samples

Executables are in `build/<preset>/Code/Samples/`. Examples:

```bash
# Windows
build/clang/Code/Samples/RHI/Sample001_Triangle/DraconicSample001_Triangle.exe
build/clang/Code/Samples/HelloWindow/HelloWindow.exe
build/clang/Code/Samples/VG/VGSandbox/VGSandbox.exe

# Linux (no .exe extension)
build/clang/Code/Samples/RHI/Sample001_Triangle/DraconicSample001_Triangle
```

RHI samples accept `--vk` or `--dx12` to select the GPU backend (default varies by platform).

## Directory layout

```
Code/
  Draconic/
    Core/           Types, memory, containers, math, threading, logging, RTTI
    Animation/      Skeleton, clip, sampler, pose
    Content/        Content database over VFS
    Editor/         Asset-pipeline authoring base
    Fonts/          Font types, interfaces, TTF backend, distance-field baker
    Geometry/       Static/skinned mesh runtime format
    Image/          Image types, pixel formats, I/O
    Materials/      Data-driven material model + pipeline state cache
    Model/          Model import (GLTF, FBX) + mesh I/O
    Profiler/       Scoped CPU profiler
    Render/         Scene-to-GPU renderer (forward path, shadows, clustering)
    RenderGraph/    Frame graph with transient resources + render bundles
    Resource/       Resource manager over content
    RHI/            Abstract GPU interface
      Vulkan/       Vulkan 1.3 backend
      DX12/         Direct3D 12 backend (Windows)
      Validation/   Validation wrapper layer
      Null/         Stub backend for headless testing
    Runtime/        Application host, platform, graphics device
    Scene/          ECS foundation (entities, transforms, components)
    Script/         Scripting (Wren)
    Shaders/        DXC shader compiler wrapper
    Texture/        Texture descriptors + GPU factory
    VFS/            Virtual file system
    VG/             2D vector graphics (paths, fills, strokes, text, SVG)
    Xml/            DOM XML parser + writer
  Samples/
    Framework/      SampleApp base class + helpers
    RHI/            RHI samples (30 numbered + smoketest)
    VG/             VG sandbox demo
    HelloWindow/    Minimal windowed app
    MultiWindow/    Multi-window demo
    Sandbox/        Dev harness (scene + renderer)
    RenderStressTest/  Render performance benchmark
ThirdParty/
  SDL3/             SDL3 (pre-built, Windows)
  DXC/              Vendored DXC headers + binaries
  stb/              stb_truetype, stb_image, stb_image_write
  cgltf/            glTF loader
  ufbx/             FBX loader
  msdfgen/          Multi-channel SDF generator (core-only)
Data/
  Assets/           Raw assets (fonts, models)
```

## Third-party dependencies

- **SDL3** - pre-built development libraries (Windows); system package on Linux.
- **DXC** - vendored headers and pre-built binaries for HLSL compilation. No manual setup needed.
- **Vulkan SDK** - system install required for `vulkan.h` and the Vulkan loader.
- **stb** - header-only libraries (truetype, image, image_write).
- **cgltf / ufbx** - header-only model loaders (glTF, FBX).
- **msdfgen** - multi-channel signed distance field generator for font atlas baking.

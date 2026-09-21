# scripts/

Repo helper scripts.

## build-export-templates.{sh,ps1} - build export templates

An **export template** is a portable, prebuilt RUNTIME bundle for one `(platform, config)`: the
`Engine.Player` executable + that build's runtime sidecars + a `template.xml` manifest. It is the
runtime you ship against - never game content, never editor code, never a toolchain. A project's
export preset references a template and adds the game half (cooked content + naming). Full design:
[`Documentation/Systems/export-templates.md`](../Documentation/Systems/export-templates.md).

These scripts build the player for each platform and run `Tools.Export --template create <buildDir>`
to package it. Templates default to **Release** (the product ships stripped Release).

### Linux + Web (from a Linux host)

```sh
scripts/build-export-templates.sh [linux|web|all]     # default: all
#   JOBS=N     build parallelism (default 4; higher OOMs the engine build)
#   OUT=<dir>  write each platform's bundle under <dir>/<platform>/ (to zip); unset = --install locally
```

- **Linux** builds `Engine.Player` + the native `Tools.Export` in `build/clang-release`
  (`Bin/Release/Linux64-Clang`) and packages it.
- **Web** builds `Engine.Player` in `build/wasm-shipping` (emscripten; needs `emcc` on PATH or
  `~/emsdk/emsdk_env.sh`) and the SAME native exporter synthesizes a `Web` template from the
  `.html`/`.js`/`.wasm` bundle. The wasm build dir must be emscripten-configured first (the script
  prints the one-time `emcmake cmake ...` line if it is missing).

### Windows (on a Windows agent)

```powershell
pwsh scripts/build-export-templates.ps1 [-Out <dir>] [-Jobs <n>] [-Compiler MSVC|Clang]
```

The Windows script was written on Linux and has not been run on Windows. Run it from a Developer
PowerShell / VS dev environment so `cl`/`clang-cl` + `ninja` are on PATH, and confirm the Windows
runtime sidecars (the DXC runtime `dxcompiler.dll`, wgpu-native) stage into the output; the
script's header lists the specific things to confirm on Windows.

### Verify

```sh
Bin/Release/Linux64-Clang/Tools.Export --template list
```

## build-editor-dist.{sh,ps1} - package the editor for download

Assemble a portable, unzip-and-run **editor** distribution: `Tools.Editor` + its runtime
sidecars (DXC - the editor cooks/recompiles shaders) + the `Data` root (`Assets` + the `.dataroot`
marker) beside the exe, with the cooked pack inside it at `Data/Shaders/shaders.dpak` (pack mode,
no `.hlsl` source shipped; that path is the one the runtime opens through the data mount).
`FindDataRoot()` discovers `Data/` beside the executable and `$ORIGIN` on the RUNPATH finds the
sidecars, so the folder relocates to any machine. This is distinct from the export TEMPLATES
above: those package the game RUNTIME (`Engine.Player`); this packages the AUTHORING TOOL.

Not bundled: the Vulkan/GPU system runtime (the target machine's drivers + loader).

### Linux (from a Linux host)

```sh
scripts/build-editor-dist.sh
#   JOBS=N       build parallelism (default 4; higher OOMs the modules build)
#   OUT=<dir>    the dist folder (default: dist/Editor-Linux64)
#   FORMATS=".." shader-pack formats (default: spirv)
```

`OUT` is the LABEL; the version from `project(VERSION)` in the root CMakeLists is inserted before
the platform suffix, so the default produces `dist/Editor-<version>-Linux64/` and its `.tar.gz`
(e.g. `Editor-0.1.0-Linux64.tar.gz`) - the same version the binary reports via `--version`. Smoke
test on the build machine: `( cd dist/Editor-<version>-Linux64 && ./Tools.Editor --exit-after 3 )`.
The conclusive check is to unzip on a machine with **no source tree and no dev toolchain** and
confirm it launches.

### Windows (on a Windows agent)

```powershell
pwsh scripts/build-editor-dist.ps1 [-Out <dir>] [-Jobs <n>] [-Formats "dxil spirv"]
```

Written on Linux and not yet run on Windows. Run from a Developer PowerShell so `cl`/`clang-cl`
+ `ninja` are on PATH; confirm the Bin layout path and that `dxcompiler.dll`/`SDL3.dll` stage.

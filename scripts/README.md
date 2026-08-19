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

Best-effort scaffold (written from Linux). Run it from a Developer PowerShell / VS dev environment
so `cl`/`clang-cl` + `ninja` are on PATH, and verify the Windows runtime sidecars (the DXC runtime
`dxcompiler.dll`, wgpu-native) actually stage - the script's header lists the finish-here checks.

### Verify

```sh
Bin/Release/Linux64-Clang/Tools.Export --template list
```

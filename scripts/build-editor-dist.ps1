# SPDX-License-Identifier: MIT
# Copyright (c) 2026-Present Robert Campbell

<#
.SYNOPSIS
  Assemble a portable, downloadable EDITOR distribution for Windows (x64).

.DESCRIPTION
  The Windows counterpart of build-editor-dist.sh. Produces an unzip-and-run folder:
  Tools.Editor.exe + its runtime DLLs (dxcompiler.dll, SDL3.dll - staged by the build's
  util_copy_runtime_deps into Bin\...\ and listed in Tools.Editor.runtime-libs), a cooked
  Data root (Assets + .dataroot) beside the exe with the cooked shader pack INSIDE it
  (Data\Shaders\shaders.dpak - the path the runtime opens; pack mode, no .hlsl source shipped). FindDataRoot() discovers Data\ beside the exe; Windows searches the exe
  directory for bare DLLs, so no rpath is needed.

  NOT bundled: the Vulkan/DX12 system runtime (the target machine's GPU drivers).

  Written on Linux and not yet run on Windows. Run from a Developer PowerShell / VS dev
  environment so cl/clang-cl + ninja are on PATH. On Windows, confirm: the shader pack cooks
  for the Windows backend (dxil - DX12; add spirv if the editor runs on Vulkan), and that
  dxcompiler.dll + SDL3.dll land in the dist (check Tools.Editor.runtime-libs).

.PARAMETER Out
  The dist folder (default: dist\Editor-Win64).
.PARAMETER Jobs
  Build parallelism (default 4; higher can OOM the modules build).
.PARAMETER Formats
  Shader-pack formats (default: "dxil"; add "spirv" for a Vulkan editor).
#>
param(
    [string]$Out = "dist\Editor-Win64",
    [int]$Jobs = 4,
    [string]$Formats = "dxil"
)
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

# Version stamp: the SINGLE source of truth is project(VERSION) in the root CMakeLists (the same
# value the binary reports via --version). Folded into the dist folder + zip name so a download
# self-identifies (Editor-0.1.0-Win64). $Out is the LABEL; the version goes before the platform.
$cmake = Get-Content "CMakeLists.txt" -Raw
$Version = if ($cmake -match 'project\(\s*GameEngine\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') { $Matches[1] } else { "0.0.0" }
$rawDir  = Split-Path -Parent $Out
$rawName = Split-Path -Leaf   $Out           # e.g. Editor-Win64
$prefix  = $rawName.Substring(0, $rawName.LastIndexOf('-'))
$suffix  = $rawName.Substring($rawName.LastIndexOf('-') + 1)
$Out     = Join-Path $rawDir "$prefix-$Version-$suffix"   # e.g. dist\Editor-0.1.0-Win64

# Release: DXC/SDL3 staged beside the editor by util_copy_runtime_deps.
$Build = "build\msvc-release"
$Bin   = "Bin\Release\Win64-MSVC"   # verify the actual <Config>\<Platform>-<Compiler> layout

Write-Host "== Building Tools.Editor + Tools.ShaderPack ($Build) =="
if (-not (Test-Path (Join-Path $Build "CMakeCache.txt"))) {
    cmake -S . -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release
}
cmake --build $Build --target Tools.Editor Tools.ShaderPack -j $Jobs

Write-Host "== Staging into $Out =="
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force -Path (Join-Path $Out "Data") | Out-Null

# Editor exe + its runtime DLLs (from the .runtime-libs manifest).
Copy-Item (Join-Path $Bin "Tools.Editor.exe") $Out
$manifest = Join-Path $Bin "Tools.Editor.runtime-libs"
if (Test-Path $manifest) {
    Get-Content $manifest | Where-Object { $_ -ne "" } | ForEach-Object {
        $src = Join-Path $Bin $_
        if (Test-Path $src) { Copy-Item $src $Out } else { Write-Warning "sidecar missing: $_" }
    }
}

# The pack goes INTO the dist's data root: Data\Shaders\shaders.dpak is the one path the runtime
# opens (ShaderSystemHost, kShaderPackPath) - a pack beside the exe is never found.
Write-Host "== Cooking Data\Shaders\shaders.dpak ($Formats) =="
New-Item -ItemType Directory -Force -Path (Join-Path $Out "Data\Shaders") | Out-Null
& (Join-Path $Bin "Tools.ShaderPack.exe") "Data\Shaders" (Join-Path $Out "Data\Shaders\shaders.dpak") $Formats.Split(" ")

Copy-Item -Recurse "Data\Assets"   (Join-Path $Out "Data\Assets")
Copy-Item          "Data\.dataroot" (Join-Path $Out "Data\.dataroot")

@"
Editor (Windows x64)

Run:  Tools.Editor.exe

Requires GPU drivers with Vulkan or DX12 support. Everything else (fonts, assets,
shader pack, dxcompiler.dll) ships in this folder and is found relative to the exe.
"@ | Set-Content (Join-Path $Out "README.txt")

$Zip = "$Out.zip"
Write-Host "== Packaging $Zip =="
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path $Out -DestinationPath $Zip

Write-Host "editor dist folder:  $Out"
Write-Host "editor dist archive: $Zip"

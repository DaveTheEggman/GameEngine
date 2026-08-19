<#
build-export-templates.ps1 - build the WINDOWS export template (a prebuilt runtime bundle:
Engine.Player + its runtime sidecars + template.xml). Sibling of build-export-templates.sh
(which does Linux + Web); this covers the Win64 template that cannot be built on Linux.

A template is NOT game content and NOT a toolchain - it is the prebuilt runtime you ship
against. See Documentation/Systems/export-templates.md.

What it does: configure a Release build dir if missing, build Engine.Player (+ the native
Tools.Export), then run
    Tools.Export.exe --template create Bin\Release\Win64-<Compiler>
which reads the build's runtime-libs manifest, copies the player + sidecars, writes template.xml.

  Usage:   pwsh scripts/build-export-templates.ps1 [-Out <dir>] [-Jobs <n>] [-Compiler MSVC|Clang]
           -Out <dir>   write a self-contained bundle under <dir> (for zip/distribution);
                        omitted = --install into the local templates root (usable immediately)
           -Jobs <n>    build parallelism (default 4)
           -Compiler    MSVC (default) or Clang (clang-cl); sets the Bin\...\Win64-<Compiler> tag

  ============================ WINDOWS AGENT: FINISH HERE ============================
  This is a BEST-EFFORT scaffold written from Linux - a Windows agent should verify:
    * Run from a Developer PowerShell / VS dev environment so cl/clang-cl + ninja are on PATH
      (or swap the generator below to a Visual Studio one).
    * That the Win64 runtime SIDECARS actually stage: the DXC runtime (dxcompiler.dll) and
      wgpu-native are dlopen/runtime deps - export-templates.md + the dxc-runtime-sidecar note.
      Confirm they land in Engine.Player.runtime-libs and beside the exe (the +x/rpath concerns
      are POSIX-only; on Windows it is DLL-search-path / same-dir).
    * That `Tools.Export.exe --template list` shows the new draconic-win64-release template.
    * Adjust $Plat/$Comp if the platform tag differs from Win64-<Compiler>.
  ===================================================================================
#>
[CmdletBinding()]
param(
    [string]$Out = "",
    [int]$Jobs = 4,
    [ValidateSet("MSVC", "Clang")]
    [string]$Compiler = "MSVC"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot   # repo root (scripts/..)
Set-Location $Root

$BuildDir = "build/msvc-release"
$Plat     = "Win64"
$Comp     = $Compiler
$BinDir   = "Bin/Release/$Plat-$Comp"
$Exporter = Join-Path $BinDir "Tools.Export.exe"

Write-Host "== Windows ($Compiler, Release) template ==" -ForegroundColor Cyan

# 1) Configure a Release build dir if it does not exist yet. Ninja + a dev environment is assumed;
#    switch to `-G "Visual Studio 17 2022" -A x64` (and drop -DCMAKE_BUILD_TYPE) if you prefer VS.
if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Host ">> configuring $BuildDir (Release)"
    $genArgs = @("-S", ".", "-B", $BuildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
    if ($Compiler -eq "Clang") {
        $genArgs += @("-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_CXX_COMPILER=clang-cl")
    }
    & cmake @genArgs
}

# 2) Build the player + the native exporter.
& cmake --build $BuildDir --target Engine.Player Tools.Export -j $Jobs

# 3) Package the build dir into a template.
if (-not (Test-Path $Exporter)) {
    throw "$Exporter not found - did the build produce it? (check the platform tag $Plat-$Comp)"
}
if ($Out -ne "") {
    # --out writes the bundle FLAT into the folder; use a per-platform subfolder so it never
    # collides with the Linux/Web bundles when they share one -Out root.
    $dest = Join-Path $Out "win64-release"
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    & $Exporter --template create $BinDir --out $dest
    Write-Host "template written under: $dest  (zip the folder to distribute)"
} else {
    & $Exporter --template create $BinDir --install
    Write-Host "template installed into the local templates root (Tools.Export.exe --template list to verify)"
}

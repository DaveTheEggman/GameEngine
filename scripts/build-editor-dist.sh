#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026-Present Robert Campbell

#
# build-editor-dist.sh - assemble a portable, downloadable EDITOR distribution for Linux.
#
# The result is an unzip-and-run folder: the Tools.Editor executable, its runtime sidecars
# (DXC - the editor cooks/recompiles shaders), a cooked engine shader pack (shaders.dpak, so
# the editor runs in pack mode with no .hlsl source shipped), and the Data root (Assets + the
# .dataroot marker) beside the exe. FindDataRoot() discovers Data/ beside the exe, and $ORIGIN
# on the exe's RUNPATH finds the sidecars - so the folder relocates to any machine.
#
# NOT bundled: Vulkan (a system dependency - the target needs GPU drivers + the Vulkan loader,
# same as the build deps in README.md).
#
# Windows is not built here (no toolchain on Linux) - use scripts/build-editor-dist.ps1 on a
# Windows agent for the Win64 editor dist.
#
#   Usage:  scripts/build-editor-dist.sh
#   Env:    JOBS=N     build parallelism (default 4; higher OOMs the modules build)
#           OUT=<dir>  the dist folder (default: dist/Editor-Linux64)
#           FORMATS=".." shader-pack formats (default: spirv, the Linux/Vulkan backend)
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

JOBS="${JOBS:-4}"
FORMATS="${FORMATS:-spirv}"
BUILD="build/clang-release"                 # Release: $ORIGIN rpath + DXC staged beside the editor
BIN="Bin/Release/Linux64-Clang"

# Version stamp: the SINGLE source of truth is project(VERSION) in the root CMakeLists (the same
# value the binary reports via --version). Folded into the dist folder + archive name so a
# download self-identifies (Editor-0.1.0-Linux64). Callers pass OUT as the LABEL (Editor-Linux64);
# the version is inserted before the platform suffix.
VERSION="$(grep -oP '^\s*VERSION\s+\K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | head -1)"
VERSION="${VERSION:-0.0.0}"
RAW="${OUT:-dist/Editor-Linux64}"
RAW_DIR="$(dirname "$RAW")"
RAW_NAME="$(basename "$RAW")"                # e.g. Editor-Linux64
DIST="$RAW_DIR/${RAW_NAME%-*}-${VERSION}-${RAW_NAME##*-}"  # e.g. dist/Editor-0.1.0-Linux64

log() { printf '\n== %s ==\n' "$*"; }

# 1. Build the editor + the shader-pack cooker (Release).
log "Building Tools.Editor + Tools.ShaderPack ($BUILD)"
if [[ ! -f "$BUILD/CMakeCache.txt" ]]; then
    # Pin clang EXPLICITLY: the Bin/ output dir is derived from CMAKE_CXX_COMPILER_ID, and $BIN
    # above hardcodes Linux64-Clang - a default-compiler (GCC) configure on a fresh machine (CI!)
    # would build into Linux64-GCC and every cp below would fail. CXX_COMPILER env overrides for
    # runners that install a versioned clang (clang++-21).
    CXX_BIN="${CXX_COMPILER:-clang++}"
    C_BIN="${C_COMPILER:-clang}"
    cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$C_BIN" -DCMAKE_CXX_COMPILER="$CXX_BIN" \
        -DCMAKE_ASM_COMPILER="$CXX_BIN"
fi
cmake --build "$BUILD" --target Tools.Editor Tools.ShaderPack -j"$JOBS"

# 2. Fresh dist tree.
log "Staging into $DIST"
rm -rf "$DIST"
mkdir -p "$DIST/Data"

# 3. The editor exe + its runtime sidecars (the .runtime-libs manifest lists DXC on Linux).
cp "$BIN/Tools.Editor" "$DIST/"
if [[ -f "$BIN/Tools.Editor.runtime-libs" ]]; then
    while IFS= read -r lib; do
        [[ -n "$lib" ]] || continue
        if [[ -f "$BIN/$lib" ]]; then
            cp "$BIN/$lib" "$DIST/"
        else
            echo "!! sidecar listed but not found in $BIN: $lib" >&2
        fi
    done < "$BIN/Tools.Editor.runtime-libs"
fi
# Copy DXC directly as well, in case the manifest omits it (the editor cooks shaders, so it needs it).
[[ -f "$BIN/libdxcompiler.so" && ! -f "$DIST/libdxcompiler.so" ]] && cp "$BIN/libdxcompiler.so" "$DIST/"
strip "$DIST/Tools.Editor" 2>/dev/null || true

# 4. Cook the engine shader pack beside the exe -> pack mode, no source .hlsl shipped.
log "Cooking shaders.dpak ($FORMATS)"
"$BIN/Tools.ShaderPack" "Data/Shaders" "$DIST/shaders.dpak" $FORMATS

# 5. Stage the Data root: Assets + the marker. Skip Data/Output (build output) and Data/Shaders
#    (source .hlsl - unneeded in pack mode).
cp -r "Data/Assets" "$DIST/Data/Assets"
cp "Data/.dataroot" "$DIST/Data/.dataroot"

# 6. A short run note next to the binary.
cat > "$DIST/README.txt" <<'EOF'
Editor (Linux x86_64)

Run:  ./Tools.Editor

Requires GPU drivers with Vulkan support and the Vulkan loader installed
(e.g. on Ubuntu:  sudo apt install libvulkan1 mesa-vulkan-drivers).

Everything else (fonts, assets, shader pack, DXC) ships in this folder and is
found relative to the executable - the folder can be moved anywhere.
EOF

# 7. Package.
TARBALL="${DIST}.tar.gz"
log "Packaging $TARBALL"
tar -czf "$TARBALL" -C "$(dirname "$DIST")" "$(basename "$DIST")"

log "done"
echo "editor dist folder: $DIST"
echo "editor dist archive: $TARBALL"
echo "smoke test on THIS machine:  ( cd '$DIST' && ./Tools.Editor --exit-after 3 )"

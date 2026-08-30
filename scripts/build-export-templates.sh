#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026-Present Robert Campbell

#
# build-export-templates.sh - build export TEMPLATES (prebuilt runtime bundles) for the
# supported platforms, so `Tools.Export` / the editor can stamp shippable dists against them.
#
# A template = the Engine.Player runtime for one (platform, config) + its runtime sidecars +
# a template.xml manifest. It is NOT game content and NOT a toolchain - it is the prebuilt
# runtime you ship against.
#
# What this does per platform: build Engine.Player (+ the native Tools.Export once), then run
#   Tools.Export --template create <Bin/<Config>/<Platform>-<Compiler>>
# which reads the build's runtime-libs manifest, copies the player + sidecars, writes template.xml.
#
# Windows is NOT built here (no toolchain on Linux) - use scripts/build-export-templates.ps1 on a
# Windows agent for the Win64 template.
#
#   Usage:  scripts/build-export-templates.sh [linux|web|all]      (default: all)
#   Env:    JOBS=N     build parallelism (default 4; higher OOMs the engine build)
#           OUT=<dir>  write self-contained bundles under <dir> (for zip/distribution);
#                      unset = --install into the local templates root (usable immediately)
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

JOBS="${JOBS:-4}"
OUT="${OUT:-}"
WHICH="${1:-all}"

# Release is the product default for templates (ships stripped Release).
LINUX_BUILD="build/clang-release"
LINUX_BIN="Bin/Release/Linux64-Clang"
WEB_BUILD="build/wasm-shipping"        # emscripten, Release (no output suffix -> Emscripten-Clang)
WEB_BIN="Bin/Release/Emscripten-Clang"

# The native exporter packages EVERY platform's bundle (it runs on the host, pointing at the
# target's Bin dir - including the wasm one, which it synthesizes into a "Web" template).
EXPORTER="$LINUX_BIN/Tools.Export"

log() { printf '\n== %s ==\n' "$*"; }

create_template() { # <config-dir> <out-subdir-tag>
    local cfg="$1"
    local tag="$2"
    if [[ ! -x "$EXPORTER" ]]; then
        echo "!! $EXPORTER not built yet (the Linux step builds it)" >&2
        return 1
    fi
    if [[ -n "$OUT" ]]; then
        # --out writes the bundle FLAT into the given folder, so each platform needs its OWN
        # subfolder or a multi-platform run would clobber one template.xml with the next.
        local dest="$OUT/$tag"
        mkdir -p "$dest"
        "$EXPORTER" --template create "$cfg" --out "$dest"
    else
        "$EXPORTER" --template create "$cfg" --install   # --install already lands each under its <id>
    fi
}

build_linux() {
    log "Linux (Release) template"
    if [[ ! -f "$LINUX_BUILD/CMakeCache.txt" ]]; then
        echo ">> configuring $LINUX_BUILD (Release)"
        cmake -S . -B "$LINUX_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
    fi
    cmake --build "$LINUX_BUILD" --target Engine.Player Tools.Export -j"$JOBS"
    create_template "$LINUX_BIN" "linux64-release"
}

build_web() {
    log "Web (Release) template"
    if ! command -v emcc >/dev/null 2>&1; then
        if [[ -f "$HOME/emsdk/emsdk_env.sh" ]]; then
            # shellcheck disable=SC1091
            source "$HOME/emsdk/emsdk_env.sh" >/dev/null
        fi
    fi
    if ! command -v emcc >/dev/null 2>&1; then
        echo "!! emcc not on PATH (and ~/emsdk/emsdk_env.sh not found) - skipping web template" >&2
        return 0
    fi
    if [[ ! -f "$WEB_BUILD/CMakeCache.txt" ]]; then
        echo "!! $WEB_BUILD is not configured. Configure it once with the emscripten toolchain, e.g.:" >&2
        echo "     source ~/emsdk/emsdk_env.sh && emcmake cmake -S . -B $WEB_BUILD -G Ninja -DCMAKE_BUILD_TYPE=Release" >&2
        return 1
    fi
    cmake --build "$WEB_BUILD" --target Engine.Player -j"$JOBS"
    # Packaged by the NATIVE exporter (built in the Linux step) -> a "Web" template.
    create_template "$WEB_BIN" "web-release"
}

case "$WHICH" in
    linux) build_linux ;;
    web)   build_linux; build_web ;;   # web needs the native exporter, so build Linux first
    all)   build_linux; build_web ;;
    *) echo "usage: $0 [linux|web|all]   (env: JOBS=N OUT=<dir>)"; exit 2 ;;
esac

log "done"
if [[ -n "$OUT" ]]; then
    echo "templates written under: $OUT/<platform>/   (zip each platform folder to distribute)"
else
    echo "templates installed into the local templates root (Tools.Export --template list to verify)"
fi

#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026-Present Robert Campbell

#
# build-steamdeck.sh - build Steam Deck-compatible Linux binaries (editor + player + all tools).
#
# The dev box (Ubuntu 26.04, glibc 2.43) and even CI (24.04, glibc 2.39) produce binaries whose
# glibc floor is too high for the Steam Deck host (~2.37). This builds inside a portable container
# (Ubuntu 22.04 / glibc 2.35, GCC 14 libstdc++ for std::print, Clang 21 for the C++23 modules) and
# links libstdc++/libgcc statically, so the output runs on the Deck. See scripts/steamdeck/.
#
# Requires podman or docker:  sudo apt-get install -y podman
# Output: dist/SteamDeck/{Editor,Player} + a glibc-floor report (fails if anything is too new).
#
#   Usage:  scripts/build-steamdeck.sh
#   Env:    JOBS=N     build parallelism (default 4)
#           RT=podman  force a container runtime (default: first of podman, docker found)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

IMAGE="raptor-steamdeck-build:22.04"
JOBS="${JOBS:-4}"

# Pick a container runtime.
RT="${RT:-}"
if [[ -z "$RT" ]]; then
    for c in podman docker; do command -v "$c" >/dev/null 2>&1 && { RT="$c"; break; }; done
fi
if [[ -z "$RT" ]]; then
    echo "!! need podman or docker. Install one, e.g.:  sudo apt-get install -y podman" >&2
    exit 1
fi
echo "== container runtime: $RT =="

# Build the portable image (layers cache across runs; only re-runs on Dockerfile change).
echo "== building image $IMAGE (first run installs Clang 21 + GCC 14; SDL3 is vendored source) =="
"$RT" build -t "$IMAGE" -f scripts/steamdeck/Dockerfile scripts/steamdeck

# Run the build inside the container with the repo bind-mounted at /work.
# :Z relabels for SELinux hosts (Fedora/RHEL); harmless elsewhere. Rootless podman maps the
# current user automatically; DOCKER does not, and would leave build/steamdeck, dist/ and Bin/
# root-owned in the host tree (the next host cmake then fails with EACCES) - so pass the host
# uid:gid explicitly there.
echo "== building the engine inside the container =="
USER_ARGS=()
if [[ "$RT" == "docker" ]]; then
    USER_ARGS=(--user "$(id -u):$(id -g)")
fi
"$RT" run --rm \
    "${USER_ARGS[@]}" \
    -v "$ROOT":/work:Z \
    -w /work \
    -e JOBS="$JOBS" \
    "$IMAGE" \
    bash scripts/steamdeck/in-container-build.sh

echo
echo "== Steam Deck bundles ready: dist/SteamDeck/ =="
echo "   copy dist/SteamDeck/Editor and dist/SteamDeck/Player to the Deck and run the binary inside each."

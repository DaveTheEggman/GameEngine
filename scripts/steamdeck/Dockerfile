# Portable Linux build environment for Steam Deck compatibility.
#
# The tension: Steam Deck's host is glibc ~2.37, but this codebase's C++23 use (std::print, 127
# call sites) needs a libstdc++ from GCC 14+, and its modules need Clang 21. Threading it:
#
#   base    = Ubuntu 22.04  -> glibc 2.35 (<= Deck host 2.37); the binary's glibc floor
#   GCC 14  = ubuntu-toolchain-r PPA -> a libstdc++ new enough for std::print (does NOT move glibc)
#   Clang 21 = apt.llvm.org -> compiles the C++23 modules
#
# build-steamdeck.sh links libstdc++/libgcc STATICALLY, so the binary carries its own C++ runtime
# and only the glibc 2.35 floor remains - it runs on the Deck without the Deck's (older) libstdc++.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Base tooling + the engine's X11/Wayland/Vulkan/audio build deps (mirrors the CI dependency list).
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates wget gnupg lsb-release software-properties-common \
        cmake ninja-build git pkg-config \
        libvulkan-dev vulkan-tools mesa-vulkan-drivers \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
        libxtst-dev libwayland-dev wayland-protocols libxkbcommon-dev libasound2-dev \
    && rm -rf /var/lib/apt/lists/*

# GCC 14 for a modern libstdc++ (std::print et al.). This PPA ships a newer compiler/libstdc++ but
# NOT a newer glibc, so the 2.35 floor is preserved. libstdc++-14-dev also provides libstdc++.a,
# which is what -static-libstdc++ links.
RUN add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update && apt-get install -y --no-install-recommends \
        gcc-14 g++-14 libstdc++-14-dev \
    && rm -rf /var/lib/apt/lists/*

# Clang 21 (C++23 modules) from apt.llvm.org.
RUN wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh \
    && chmod +x /tmp/llvm.sh && /tmp/llvm.sh 21 \
    && rm -f /tmp/llvm.sh

# SDL3 (matched to the vendored Windows libs, release-3.4.10) built here so libSDL3.so carries the
# 2.35 floor; the build script bundles it beside the binaries (the Deck has no system SDL3). SDL is
# C, so libSDL3.so has no libstdc++/GLIBCXX dependency - only the glibc floor.
RUN git clone --depth 1 --branch release-3.4.10 https://github.com/libsdl-org/SDL.git /tmp/SDL3 \
    && cmake -S /tmp/SDL3 -B /tmp/SDL3/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF \
    && cmake --build /tmp/SDL3/build && cmake --install /tmp/SDL3/build \
    && ldconfig && rm -rf /tmp/SDL3

WORKDIR /work

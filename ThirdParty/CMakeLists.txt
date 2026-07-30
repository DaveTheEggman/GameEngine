# Draconic third-party dependencies.

# SDL3 — pre-built development libraries (Windows). Point find_package(SDL3)
# to the vendored copy and store the DLL path for post-build copies.
if(WIN32 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/SDL3/cmake/SDL3Config.cmake)
    set(SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDL3/cmake" CACHE PATH "SDL3 CMake config" FORCE)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/SDL3/lib/x64/SDL3.dll)
        set(DRACONIC_SDL3_DLL "${CMAKE_CURRENT_SOURCE_DIR}/SDL3/lib/x64/SDL3.dll" CACHE FILEPATH "" FORCE)
    endif()
endif()

# DXC — vendored headers + pre-built runtime binaries (HLSL -> SPIR-V/DXIL).
# Headers are an INTERFACE target; the runtime library is loaded at run time
# (dlopen / LoadLibrary), so we only publish its path + an rpath for dev builds.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/DXC/include/dxc/dxcapi.h)
    add_library(draconic_dxc_headers INTERFACE)
    add_library(Draconic::DXCHeaders ALIAS draconic_dxc_headers)
    # SYSTEM so the vendored headers (DXC's WinAdapter shim has unused-parameter
    # paths under NDEBUG) are exempt from our -Wall -Wextra -Werror.
    target_include_directories(draconic_dxc_headers
        SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/DXC/include)

    # DRACONIC_DXC_LIB (cache): the absolute path to the DXC runtime library, so apps that ship
    # (RaptorPlayer) can STAGE it beside the executable as a sidecar - DXC is loaded at runtime
    # (renderer compiles HLSL on demand), so a dist that lacks it renders nothing on any machine
    # but this one (where the compiled-in DRACONIC_DXC_PATH rpath still resolves into the tree).
    if(WIN32)
        set(_dxc_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/DXC/lib/win-x64")
        if(EXISTS "${_dxc_lib_dir}/dxcompiler.dll")
            target_compile_definitions(draconic_dxc_headers
                INTERFACE DRACONIC_DXC_PATH="${_dxc_lib_dir}/dxcompiler.dll")
            set(DRACONIC_DXC_LIB "${_dxc_lib_dir}/dxcompiler.dll" CACHE INTERNAL
                "DXC runtime library to stage beside shipping apps")
            message(STATUS "DXC runtime: ${_dxc_lib_dir}")
        endif()
    elseif(UNIX AND NOT APPLE)
        set(_dxc_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/DXC/lib/linux-x86_64")
        if(EXISTS "${_dxc_lib_dir}/libdxcompiler.so")
            target_compile_definitions(draconic_dxc_headers
                INTERFACE DRACONIC_DXC_PATH="${_dxc_lib_dir}/libdxcompiler.so")
            set(DRACONIC_DXC_LIB "${_dxc_lib_dir}/libdxcompiler.so" CACHE INTERNAL
                "DXC runtime library to stage beside shipping apps")
            list(APPEND CMAKE_BUILD_RPATH "${_dxc_lib_dir}")
            set(CMAKE_BUILD_RPATH "${CMAKE_BUILD_RPATH}" PARENT_SCOPE)
            message(STATUS "DXC runtime: ${_dxc_lib_dir}")
        endif()
    endif()
else()
    message(STATUS "ThirdParty/DXC not present — draconic.shaders will be skipped.")
endif()

# wgpu-native — vendored standard webgpu.h header + pre-built runtime binaries
# (gfx-rs/wgpu-native, see WgpuNative/VERSION). The desktop WebGPU RHI backend is
# written against webgpu.h and loads wgpu_native at RUN TIME (dlopen / LoadLibrary),
# the same runtime-sidecar pattern as DXC: headers are an INTERFACE target; only the
# library's path (+ a dev-build rpath) is published. Browser builds link the
# emscripten-provided implementation instead - no sidecar there.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/WgpuNative/include/webgpu/webgpu.h)
    add_library(draconic_wgpu_headers INTERFACE)
    add_library(Draconic::WgpuHeaders ALIAS draconic_wgpu_headers)
    target_include_directories(draconic_wgpu_headers
        SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/WgpuNative/include)

    if(WIN32)
        set(_wgpu_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/WgpuNative/lib/win-x64")
        if(EXISTS "${_wgpu_lib_dir}/wgpu_native.dll")
            target_compile_definitions(draconic_wgpu_headers
                INTERFACE DRACONIC_WGPU_PATH="${_wgpu_lib_dir}/wgpu_native.dll")
            set(DRACONIC_WGPU_LIB "${_wgpu_lib_dir}/wgpu_native.dll" CACHE INTERNAL
                "wgpu-native runtime library to stage beside shipping apps")
            message(STATUS "wgpu-native runtime: ${_wgpu_lib_dir}")
        endif()
    elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
        set(_wgpu_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/WgpuNative/lib/linux-x86_64")
        if(EXISTS "${_wgpu_lib_dir}/libwgpu_native.so")
            target_compile_definitions(draconic_wgpu_headers
                INTERFACE DRACONIC_WGPU_PATH="${_wgpu_lib_dir}/libwgpu_native.so")
            set(DRACONIC_WGPU_LIB "${_wgpu_lib_dir}/libwgpu_native.so" CACHE INTERNAL
                "wgpu-native runtime library to stage beside shipping apps")
            list(APPEND CMAKE_BUILD_RPATH "${_wgpu_lib_dir}")
            set(CMAKE_BUILD_RPATH "${CMAKE_BUILD_RPATH}" PARENT_SCOPE)
            message(STATUS "wgpu-native runtime: ${_wgpu_lib_dir}")
        endif()
    endif()
else()
    message(STATUS "ThirdParty/WgpuNative not present — draconic.rhi.webgpu will be skipped.")
endif()

# stb / cgltf / ufbx — vendored header(+source) libraries (copied, not submodules).
# Header include-dir INTERFACE targets; the *_impl translation units that define
# the implementations live in the engine libraries that use them.
add_library(draconic_stb INTERFACE)
add_library(Draconic::Stb ALIAS draconic_stb)
target_include_directories(draconic_stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(draconic_cgltf INTERFACE)
add_library(Draconic::Cgltf ALIAS draconic_cgltf)
target_include_directories(draconic_cgltf SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cgltf)

add_library(draconic_ufbx INTERFACE)
add_library(Draconic::Ufbx ALIAS draconic_ufbx)
target_include_directories(draconic_ufbx SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/ufbx)

# miniaudio — vendored single-header audio library (github.com/mackron/miniaudio v0.11.25,
# public domain / MIT-0). Header include-dir INTERFACE target; the MINIAUDIO_IMPLEMENTATION
# translation unit lives in draconic.audio. Ogg Vorbis decoding is provided by
# ThirdParty/stb/stb_vorbis.c (public domain), compiled inside the same implementation TU.
add_library(draconic_miniaudio INTERFACE)
add_library(Draconic::Miniaudio ALIAS draconic_miniaudio)
target_include_directories(draconic_miniaudio SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)

# doctest — vendored single-header test framework (github.com/doctest/doctest v2.4.11, MIT).
# A copy of the source (not a submodule / not FetchContent); tests link doctest::doctest.
add_library(draconic_doctest INTERFACE)
add_library(doctest::doctest ALIAS draconic_doctest)
target_include_directories(draconic_doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/doctest)

# Dear ImGui — vendored (copied from upstream, not a submodule). Compiled as a static library;
# the include dir is PUBLIC + SYSTEM so consumers (the draconic.imgui lib + apps) can
# #include "imgui.h" and call ImGui:: directly. Does NOT link draconic_policy, so the vendored
# sources build without our -Werror. No stock backends — Draconic has its own RHI-based renderer.
add_library(draconic_imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp)
add_library(Draconic::ImGui ALIAS draconic_imgui)
target_include_directories(draconic_imgui SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
set_target_properties(draconic_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

# msdfgen — multi-channel signed distance field generator (compiled from source).
# Import-time only: used by the font atlas baker, not linked at runtime.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/msdfgen/CMakeLists.txt)
    set(MSDFGEN_CORE_ONLY          ON  CACHE BOOL "" FORCE)
    set(MSDFGEN_BUILD_STANDALONE   OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_USE_OPENMP         OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_USE_SKIA           OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_USE_VCPKG          OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_INSTALL            OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_DYNAMIC_RUNTIME    ON  CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    add_subdirectory(msdfgen)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    add_library(Draconic::MsdfgenCore ALIAS msdfgen-core)
endif()

# Jolt Physics v5.3.0 — vendored BY COPY (Jolt/ source tree + LICENSE; no submodule).
# Compiled from source as one static lib with a fixed, engine-chosen configuration
# (deterministic-enough defaults; no Jolt CMake options surface). Include dir is
# PUBLIC + SYSTEM so draconic.physics can #include <Jolt/...>; consumers MUST define
# the same JPH_* macros, so they are PUBLIC compile definitions here. Does not link
# draconic_policy (vendored code builds without our -Werror).
file(GLOB_RECURSE draconic_jolt_sources ${CMAKE_CURRENT_SOURCE_DIR}/JoltPhysics/Jolt/*.cpp)
add_library(draconic_jolt STATIC ${draconic_jolt_sources})
add_library(Draconic::Jolt ALIAS draconic_jolt)
target_include_directories(draconic_jolt SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/JoltPhysics)
target_compile_features(draconic_jolt PUBLIC cxx_std_17)
target_compile_definitions(draconic_jolt PUBLIC
    $<$<CONFIG:Debug>:JPH_ENABLE_ASSERTS>)
set_target_properties(draconic_jolt PROPERTIES POSITION_INDEPENDENT_CODE ON)

# AngelScript 2.39.0 WIP — vendored BY COPY (github.com/anjo76/angelscript, sdk library
# + the scriptstdstring and scriptbuilder add-ons; no submodule). Compiled from source as
# one static lib; per-architecture native-call sources self-select via as_config.h #ifdefs,
# so globbing every .cpp is safe. Include dirs are PUBLIC + SYSTEM so draconic.script.angelscript
# can #include <angelscript.h> / <scriptstdstring/scriptstdstring.h> /
# <scriptbuilder/scriptbuilder.h> from its module IMPLEMENTATION unit. Does not link
# draconic_policy (vendored code builds without our -Werror). scriptstdstring_utils.cpp is
# deliberately not vendored (needs scriptarray). scriptbuilder parses [metadata] ahead of
# declarations (the AngelScript editor cook harvests behavior properties through it).
file(GLOB draconic_angelscript_sources ${CMAKE_CURRENT_SOURCE_DIR}/angelscript/source/*.cpp)
# Clang on Windows (x86_64-pc-windows-msvc) needs the .S native-call trampoline for x64.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND draconic_angelscript_sources
        ${CMAKE_CURRENT_SOURCE_DIR}/angelscript/source/as_callfunc_x64_msvc_clang.S)
endif()
add_library(draconic_angelscript STATIC
    ${draconic_angelscript_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/angelscript/add_on/scriptstdstring/scriptstdstring.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/angelscript/add_on/scriptbuilder/scriptbuilder.cpp)
add_library(Draconic::AngelScript ALIAS draconic_angelscript)
target_include_directories(draconic_angelscript SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/angelscript/include
    ${CMAKE_CURRENT_SOURCE_DIR}/angelscript/add_on)
target_compile_features(draconic_angelscript PRIVATE cxx_std_17)
set_target_properties(draconic_angelscript PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(draconic_angelscript PUBLIC Threads::Threads)

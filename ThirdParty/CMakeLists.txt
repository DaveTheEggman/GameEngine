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

    if(WIN32)
        set(_dxc_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/DXC/lib/win-x64")
        if(EXISTS "${_dxc_lib_dir}/dxcompiler.dll")
            target_compile_definitions(draconic_dxc_headers
                INTERFACE DRACONIC_DXC_PATH="${_dxc_lib_dir}/dxcompiler.dll")
            message(STATUS "DXC runtime: ${_dxc_lib_dir}")
        endif()
    elseif(UNIX AND NOT APPLE)
        set(_dxc_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/DXC/lib/linux-x86_64")
        if(EXISTS "${_dxc_lib_dir}/libdxcompiler.so")
            target_compile_definitions(draconic_dxc_headers
                INTERFACE DRACONIC_DXC_PATH="${_dxc_lib_dir}/libdxcompiler.so")
            list(APPEND CMAKE_BUILD_RPATH "${_dxc_lib_dir}")
            set(CMAKE_BUILD_RPATH "${CMAKE_BUILD_RPATH}" PARENT_SCOPE)
            message(STATUS "DXC runtime: ${_dxc_lib_dir}")
        endif()
    endif()
else()
    message(STATUS "ThirdParty/DXC not present — draconic.shaders will be skipped.")
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

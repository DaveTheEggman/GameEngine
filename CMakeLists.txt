# Raptor — game engine root build
#
# C++23, C++ modules. Requires CMake >= 3.28 and a modules-capable toolchain
# (Clang 17+, GCC 14+, or MSVC 19.36+) with the Ninja generator.

cmake_minimum_required(VERSION 3.28)

project(Raptor
    VERSION 0.1.0
    DESCRIPTION "Raptor game engine"
    LANGUAGES CXX)

# --- Global language settings ------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position-independent code so the static engine libraries can be linked into
# dynamically-loaded plugin shared objects (Raptor::Runtime plugin host).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Single-config generators default to Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# --- Options -----------------------------------------------------------------
option(RAPTOR_BUILD_TESTS "Build Raptor unit tests" ON)
option(RAPTOR_BUILD_SAMPLES "Build Raptor sample applications" ON)
option(RAPTOR_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

# Enable testing before any subdirectory so add_test() calls made by modules that
# register their own Tests/ (e.g. Runtime, Script/Wren) are picked up by ctest.
if(RAPTOR_BUILD_TESTS)
    enable_testing()
endif()

# --- Engine-wide compile policy ----------------------------------------------
# Exceptions and C++ RTTI are disabled engine-wide (see Documentation/Planning/Core.md §1).
add_library(raptor_policy INTERFACE)
add_library(Raptor::Policy ALIAS raptor_policy)

target_compile_features(raptor_policy INTERFACE cxx_std_23)

target_compile_definitions(raptor_policy INTERFACE
    $<$<CONFIG:Debug>:RAPTOR_DEBUG=1>
    $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:RAPTOR_RELEASE=1>)

target_compile_options(raptor_policy INTERFACE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-fno-exceptions;-fno-rtti>
    $<$<CXX_COMPILER_ID:MSVC>:/EHs-c-;/GR->)

# Warnings
target_compile_options(raptor_policy INTERFACE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

if(RAPTOR_WARNINGS_AS_ERRORS)
    target_compile_options(raptor_policy INTERFACE
        $<$<CXX_COMPILER_ID:Clang,GNU>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

# --- Targets -----------------------------------------------------------------
add_subdirectory(Code/Raptor/Core)
add_subdirectory(Code/Raptor/RHI)   # depends only on Core (foundational sibling of Runtime)
add_subdirectory(Code/Raptor/Script)
add_subdirectory(Code/Raptor/Runtime)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Code/Raptor/Core/Tests)
    add_subdirectory(Code/Raptor/Script/Tests)
endif()

if(RAPTOR_BUILD_SAMPLES)
    add_subdirectory(Code/Samples)
endif()

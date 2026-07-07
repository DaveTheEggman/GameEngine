# Draconic - game engine root build
#
# C++23, C++ modules. Requires CMake >= 3.28 and a modules-capable toolchain
# (Clang 17+, GCC 14+, or MSVC 19.36+) with the Ninja generator.

cmake_minimum_required(VERSION 3.28)

project(Draconic
    VERSION 0.1.0
    DESCRIPTION "Draconic game engine"
    LANGUAGES CXX)

# --- Global language settings ------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position-independent code so the static engine libraries can be linked into
# dynamically-loaded plugin shared objects (Draconic::Runtime plugin host).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Single-config generators default to Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# --- Options -----------------------------------------------------------------
option(DRACONIC_BUILD_TESTS "Build Draconic unit tests" ON)
option(DRACONIC_BUILD_SAMPLES "Build Draconic sample applications" ON)
option(DRACONIC_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

# Enable testing before any subdirectory so add_test() calls made by modules that
# register their own Tests/ (e.g. Runtime, Script/Wren) are picked up by ctest.
if(DRACONIC_BUILD_TESTS)
    enable_testing()
endif()

# --- Engine-wide compile policy ----------------------------------------------
# Exceptions and C++ RTTI are disabled engine-wide (see Documentation/Planning/Core.md §1).
add_library(draconic_policy INTERFACE)
add_library(Draconic::Policy ALIAS draconic_policy)

target_compile_features(draconic_policy INTERFACE cxx_std_23)

target_compile_definitions(draconic_policy INTERFACE
    $<$<CONFIG:Debug>:DRACONIC_DEBUG=1>
    $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:DRACONIC_RELEASE=1>
    # Instrumentation on in Debug/RelWithDebInfo; off (compiles to nothing) in shipping builds.
    $<$<CONFIG:Debug,RelWithDebInfo>:DRACONIC_PROFILING=1>)

target_compile_options(draconic_policy INTERFACE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-fno-exceptions;-fno-rtti>
    $<$<CXX_COMPILER_ID:MSVC>:/EHs-c-;/GR->)

# Warnings
target_compile_options(draconic_policy INTERFACE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

if(DRACONIC_WARNINGS_AS_ERRORS)
    target_compile_options(draconic_policy INTERFACE
        $<$<CXX_COMPILER_ID:Clang,GNU>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
    # Clang on Windows emits a spurious "argument unused during compilation: '-c'"
    # during the CMake module-dependency scan step. Suppress just that diagnostic
    # so -Werror doesn't kill the build.
    # Clang 22+ warns that __COUNTER__ is a "C2y extension" when used in C++ mode;
    # doctest (and other widely-used macros) rely on it, so suppress the diagnostic.
    target_compile_options(draconic_policy INTERFACE
        $<$<CXX_COMPILER_ID:Clang>:-Wno-error=unused-command-line-argument;-Wno-c2y-extensions>)
endif()

# --- Targets -----------------------------------------------------------------
add_subdirectory(ThirdParty)
add_subdirectory(Code/Draconic/Core)
add_subdirectory(Code/Draconic/Profiler)  # CPU scope profiler over Core (instrumented across the engine)
add_subdirectory(Code/Draconic/Xml)       # DOM XML parser + writer over Core
add_subdirectory(Code/Draconic/VFS)       # byte access over Core stream primitives
add_subdirectory(Code/Draconic/Content)   # content database over Core + VFS
add_subdirectory(Code/Draconic/Resource)  # resource manager over content
add_subdirectory(Code/Draconic/Editor)     # asset-pipeline authoring base (tooling)
add_subdirectory(Code/Draconic/RHI)   # depends only on Core (foundational sibling of Runtime)
add_subdirectory(Code/Draconic/RenderGraph)  # render graph over RHI
add_subdirectory(Code/Draconic/Shaders)
add_subdirectory(Code/Draconic/Image)
add_subdirectory(Code/Draconic/Model)
add_subdirectory(Code/Draconic/Geometry)  # engine runtime mesh format (StaticMesh/SkinnedMesh + primitives)
add_subdirectory(Code/Draconic/Animation) # skeletal-animation foundation (Sedulous.Animation port; Core-only)
add_subdirectory(Code/Draconic/Particles) # CPU particle runtime (Sedulous.Particles port; Core-only)
add_subdirectory(Code/Draconic/Scene)     # scene / ECS foundation (entities, transforms, components, SceneSubsystem)
add_subdirectory(Code/Draconic/Render)    # renderer: scene->render extraction + (later) the GPU forward path
add_subdirectory(Code/Draconic/Animation/Subsystem) # skeletal-animation scene integration (links RenderSubsystem; after Render)
add_subdirectory(Code/Draconic/Fonts)     # font types/interfaces over Core + Image
add_subdirectory(Code/Draconic/Texture)   # texture descriptors + cooked resource/GPU factory + editor
add_subdirectory(Code/Draconic/Materials) # data-driven material model (PipelineConfig + bind-group-inferring MaterialSystem)
add_subdirectory(Code/Draconic/ModelImporter) # top-level model import: model IR -> cooked geometry/material/texture/animation resources
add_subdirectory(Code/Draconic/VG)        # 2D vector-graphics core over Core + Image
add_subdirectory(Code/Draconic/Script)
add_subdirectory(Code/Draconic/Shell)     # raw platform/OS service (IShell: window/input/events) over Core; below Runtime
add_subdirectory(Code/Draconic/Graphics)  # GraphicsDevice/RenderWindow render host over RHI+Shell; below Runtime
add_subdirectory(Code/Draconic/Runtime)
add_subdirectory(Code/Draconic/Imgui)     # Dear ImGui debug UI (RHI renderer + subsystem) over Runtime

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Code/Draconic/Core/Tests)
    add_subdirectory(Code/Draconic/Script/Tests)
endif()

if(DRACONIC_BUILD_SAMPLES)
    add_subdirectory(Code/Samples)
endif()

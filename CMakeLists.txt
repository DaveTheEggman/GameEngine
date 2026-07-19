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

# --- Output layout: Bin/<Config>/<Platform>-<Compiler> at the repo root -------
# Final artifacts (exes, DLLs/.so, static libs, PDBs) land here instead of being
# scattered through the build tree mirroring the source layout. Object files and
# C++20-module BMIs stay in the build tree - only deliverables move. The compiler
# suffix keeps concurrent toolchains (e.g. clang + gcc, both Debug) from clobbering
# each other. $<CONFIG> resolves for single- AND multi-config generators, and stops
# a multi-config generator from appending its own extra per-config subdirectory.
if(WIN32)
    set(_dr_plat "Win64")
elseif(APPLE)
    set(_dr_plat "Mac64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_dr_plat "Linux64")
else()
    set(_dr_plat "${CMAKE_SYSTEM_NAME}")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(_dr_comp "GCC")
else()
    set(_dr_comp "${CMAKE_CXX_COMPILER_ID}")   # Clang / MSVC / AppleClang / ...
endif()

# Sanitizer builds get their OWN output dir: build/asan sharing Bin with build/clang once
# clobbered the regular libs with sanitized ones (and purging the shared dir cost a user
# project that lived beside the binaries). Set DRACONIC_OUTPUT_SUFFIX (e.g. "-ASAN") in
# sanitizer configure runs.
set(DRACONIC_OUTPUT_SUFFIX "" CACHE STRING "Suffix for the Bin output dir (e.g. -ASAN)")
set(DRACONIC_BIN_DIR "${CMAKE_SOURCE_DIR}/Bin/$<CONFIG>/${_dr_plat}-${_dr_comp}${DRACONIC_OUTPUT_SUFFIX}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY     "${DRACONIC_BIN_DIR}")       # exes + Windows DLLs
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY     "${DRACONIC_BIN_DIR}")       # .so plugins/modules (beside the exe)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY     "${DRACONIC_BIN_DIR}/lib")   # static .a/.lib + MSVC import libs
set(CMAKE_PDB_OUTPUT_DIRECTORY         "${DRACONIC_BIN_DIR}")       # MSVC linker PDBs (beside the exe)
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${DRACONIC_BIN_DIR}/lib")   # MSVC static-lib compile PDBs

# Reusable helpers (draconic_copy_runtime_deps, ...).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DraconicHelpers.cmake)

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
    # GCC modules: eager gcm loading. The default lazy loader intermittently fails to merge
    # template pendings across module imports ("failed to read compiled module cluster" /
    # "failed to load pendings for ...") - deterministic in RaptorCook's import mix on GCC 15.2.
    $<$<CXX_COMPILER_ID:GNU>:-fno-module-lazy>
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
add_subdirectory(Code/Draconic/Settings)  # typed/versioned settings store over Core (runtime + editor)
add_subdirectory(Code/Draconic/Profiler)  # CPU scope profiler over Core (instrumented across the engine)
add_subdirectory(Code/Draconic/Xml)       # DOM XML parser + writer over Core
add_subdirectory(Code/Draconic/VFS)       # byte access over Core stream primitives
add_subdirectory(Code/Draconic/Project)   # project manifest/layout (runtime-side, editor-free)
add_subdirectory(Code/Draconic/Content)   # content database over Core + VFS
add_subdirectory(Code/Draconic/Resource)  # resource manager over content
add_subdirectory(Code/Draconic/Editor)     # asset-pipeline authoring base (tooling)
add_subdirectory(Code/Draconic/Editor/Cook) # incremental cook driver + pipeline DB (tooling, headless)
add_subdirectory(Code/Draconic/Editor/Core) # headless editor domain: context/registries/commands/pages/project (tooling)
add_subdirectory(Code/Draconic/RHI)   # depends only on Core (foundational sibling of Runtime)
add_subdirectory(Code/Draconic/RenderGraph)  # render graph over RHI
add_subdirectory(Code/Draconic/Shaders)
add_subdirectory(Code/Draconic/Image)
add_subdirectory(Code/Draconic/Model)
add_subdirectory(Code/Draconic/Geometry)  # engine runtime mesh format (StaticMesh/SkinnedMesh + primitives)
add_subdirectory(Code/Draconic/Animation) # skeletal-animation foundation (Sedulous.Animation port; Core-only)
add_subdirectory(Code/Draconic/Particles) # CPU particle runtime (Sedulous.Particles port; Core-only)
add_subdirectory(Code/Draconic/Physics)   # Jolt-backed rigid-body core (world/bodies/queries/events; JPH never public)
add_subdirectory(Code/Draconic/Physics/Resource)  # cooked collision shapes + physical materials
add_subdirectory(Code/Draconic/Physics/Editor)    # collision/material assets + builders (tooling)
add_subdirectory(Code/Draconic/Audio)     # miniaudio-backed audio engine core (voices/buses/handles; ma_* never public)
add_subdirectory(Code/Draconic/Audio/Resource)    # cooked audio clips + factory
add_subdirectory(Code/Draconic/Audio/Editor)      # audio clip asset/builder/importer (tooling)
add_subdirectory(Code/Draconic/Scene)     # scene / ECS foundation (entities, transforms, components, SceneSubsystem)
add_subdirectory(Code/Draconic/Render/Api) # light scene-rendering interface (ISceneRenderer) - tools drive rendering w/o linking the renderer
add_subdirectory(Code/Draconic/Render)    # renderer: scene->render extraction + (later) the GPU forward path
add_subdirectory(Code/Draconic/Animation/Subsystem) # skeletal-animation scene integration (links RenderSubsystem; after Render)
add_subdirectory(Code/Draconic/Particles/Subsystem) # particle scene/render integration (renderer + provider seam; after Render)
add_subdirectory(Code/Draconic/Physics/Subsystem) # physics scene integration (components + per-scene Jolt world + interpolation; after Render)
add_subdirectory(Code/Draconic/Fonts)     # font types/interfaces over Core + Image
add_subdirectory(Code/Draconic/Texture)   # texture descriptors + cooked resource/GPU factory + editor
add_subdirectory(Code/Draconic/Materials) # data-driven material model (PipelineConfig + bind-group-inferring MaterialSystem)
add_subdirectory(Code/Draconic/Model/Resource) # cooked model manifest + runtime composite (runtime side; no tooling deps)
add_subdirectory(Code/Draconic/UI/Resource)   # cooked UI documents + themes (game-ui; UI-framework-free records)
add_subdirectory(Code/Draconic/UI/Editor)     # UI document/theme assets + validating builders (tooling)
add_subdirectory(Code/Draconic/UI/Subsystem)  # game screen tier: canvases + overlay render + consumption
add_subdirectory(Code/Draconic/ModelImporter) # top-level model import: model IR -> cooked geometry/material/texture/animation resources
add_subdirectory(Code/Draconic/VG)        # 2D vector-graphics core over Core + Image
add_subdirectory(Code/Draconic/UI)        # retained-mode UI framework (faithful Sedulous.UI port) over Core (+ VG/Fonts as phases land)
add_subdirectory(Code/Draconic/GUI)       # fresh eepp-derived UI framework, progressively sliced onto VG/Fonts (docs/design/gui-port.md)
add_subdirectory(Code/Draconic/Script)
add_subdirectory(Code/Draconic/Shell)     # raw platform/OS service (IShell: window/input/events) over Core; below Runtime
add_subdirectory(Code/Draconic/Input)     # action-mapping layer (InputMap + ActionRuntime + asset/cooked) over Core + Shell
add_subdirectory(Code/Draconic/GUI/Shell)  # draconic.gui.shell: platform input bridge (Shell InputEvents -> GUI EventDispatcher)
add_subdirectory(Code/Draconic/GUI/VFS)   # draconic.gui.vfs: VFS-backed IResourceProvider (background-image: url() loading)
add_subdirectory(Code/Draconic/Graphics)  # GraphicsDevice/RenderWindow render host over RHI+Shell; below Runtime
add_subdirectory(Code/Draconic/Runtime)
add_subdirectory(Code/Draconic/Input/Subsystem)  # input runtime hookup (tick + device provider seam) over Runtime
add_subdirectory(Code/Draconic/Imgui)     # Dear ImGui debug UI (RHI renderer + subsystem) over Runtime
add_subdirectory(Code/Draconic/Editor/App) # editor UI shell (chrome + layout persistence + EditorApplication)
add_subdirectory(Code/Draconic/Editor/Input) # input-map editing page (editor exe only)
add_subdirectory(Code/Draconic/Editor/Scene) # scene editor plugin (SceneEditorPage + RegisterSceneEditor)
add_subdirectory(Code/Tools/Editor)        # RaptorEditor executable (editor assembly)
add_subdirectory(Code/Tools/Cook)          # RaptorCook CLI cooker (headless)
add_subdirectory(Code/Tools/Player)        # RaptorPlayer generic game runner
add_subdirectory(Code/Tools/Export)        # RaptorExport dist packager

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Code/Draconic/Core/Tests)
    add_subdirectory(Code/Draconic/Script/Tests)
endif()

if(DRACONIC_BUILD_SAMPLES)
    add_subdirectory(Code/Samples)
endif()

# Draconic - game engine root build
#
# C++23, C++ modules. Requires CMake >= 3.28 and a modules-capable toolchain
# (Clang 17+, GCC 14+, or MSVC 19.36+) with the Ninja generator.

cmake_minimum_required(VERSION 3.28)

project(Draconic
    VERSION 0.1.0
    DESCRIPTION "Draconic game engine"
    LANGUAGES CXX ASM)

# MSVC assembles with ml64, not cl, so the plain ASM language above does not apply to it
# (hence "CMP194: MSVC is not an assembler for language ASM" at configure). AngelScript's x64
# native-call trampoline is hand-written assembly and is the only consumer; without MASM its
# CallX64/GetReturnedFloat/GetReturnedDouble go unresolved at link.
if(MSVC)
    enable_language(ASM_MASM)
endif()

# --- Global language settings ------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position-independent code so the static engine libraries can be linked into
# dynamically-loaded plugin shared objects (Foundation::Runtime plugin host).
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
# project that lived beside the binaries). Set BUILDSYSTEM_OUTPUT_SUFFIX (e.g. "-ASAN") in
# sanitizer configure runs.
set(BUILDSYSTEM_OUTPUT_SUFFIX "" CACHE STRING "Suffix for the Bin output dir (e.g. -ASAN)")
set(BUILDSYSTEM_BIN_DIR "${CMAKE_SOURCE_DIR}/Bin/$<CONFIG>/${_dr_plat}-${_dr_comp}${BUILDSYSTEM_OUTPUT_SUFFIX}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY     "${BUILDSYSTEM_BIN_DIR}")       # exes + Windows DLLs
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY     "${BUILDSYSTEM_BIN_DIR}")       # .so plugins/modules (beside the exe)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY     "${BUILDSYSTEM_BIN_DIR}/lib")   # static .a/.lib + MSVC import libs
set(CMAKE_PDB_OUTPUT_DIRECTORY         "${BUILDSYSTEM_BIN_DIR}")       # MSVC linker PDBs (beside the exe)
# NOT set for MSVC: compile PDBs are per-OBJECT intermediates, and CMake names them from the
# compiler default (vc<ver>.pdb) rather than the target, so pointing every target at one shared
# directory makes all of them write the same file. Parallel cl.exe jobs then lose the race with
# "C1041: cannot open program database" (49 failures in the first triage build; /FS alone still
# left 14). Left unset, each target gets its own build directory and the collision cannot happen.
# The LINKER PDBs - the ones worth having beside the binaries - are CMAKE_PDB_OUTPUT_DIRECTORY
# above and are unaffected.
if(NOT MSVC)
    set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${BUILDSYSTEM_BIN_DIR}/lib")
endif()

# Reusable helpers (util_copy_runtime_deps, ...).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Helpers.cmake)

# Add a module's flat-sibling doctest suite, but only when the module target was actually built -
# so the web subset and platform-gated backends (Shell.Desktop, ...) skip cleanly; a test never
# outlives its module. The whole cluster is wrapped once in if(OPTION_BUILD_TESTS) at the call site.
macro(util_test_suite dir target)
    if(TARGET ${target})
        add_subdirectory(${dir})
    endif()
endmacro()

# --- Options -----------------------------------------------------------------
option(OPTION_BUILD_TESTS "Build Draconic unit tests" ON)
option(OPTION_BUILD_SAMPLES "Build Draconic sample applications" ON)
option(OPTION_BUILD_TOOLS "Build editor/authoring tooling (the editor libraries + CLI exes)" ON)
# Experimental modules and optional extensions are independently switchable; each exposes a
# OPTION_HAS_* compile define (see the policy block) so C++ can guard usage with #if.
option(OPTION_EXPERIMENTAL_GUI "Build the experimental GUI framework (experimental.gui + shell/vfs)" ON)
option(OPTION_EXTENSION_IMGUI  "Build the Dear ImGui debug-UI extension (extensions.imgui)" ON)
option(OPTION_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

# A web (Emscripten) build ships a game, not the authoring/side toolchain: force the tooling,
# experimental, and extension groups off up front, before the policy defines below capture them.
if(EMSCRIPTEN)
    set(OPTION_BUILD_TOOLS OFF)
    set(OPTION_EXPERIMENTAL_GUI OFF)
    # OPTION_EXTENSION_IMGUI stays available on web: Dear ImGui + extensions.imgui compile for
    # wasm (the renderer is pure RHI, the subsystem consumes the shell's input), so the debug
    # panel works in the browser too (WebScene's tweak UI).
endif()

# --- Product binary basenames: single source of truth ------------------------
# A few executables are referenced by name/path from OUTSIDE their own target, so the name cannot
# live only in the target definition:
#   - the export host-template stages the player and reads "<player>.runtime-libs" beside the tool
#     (Editor.Core gets the name below as the BUILTIN_PLAYER_BASENAME compile define)
#   - the web build invokes the NATIVE shader-pack cook by PATH (cross-config: the wasm build reaches
#     into the desktop Bin dir, so it cannot use $<TARGET_FILE:>) - Player/WebScene read this name
# Change a name here and every reference (OUTPUT_NAME, the C++ constant, the sidecar, the cook path)
# follows. Dotted, matching the target names.
set(BUILTIN_PLAYER_BASENAME         "Engine.Player")
set(BUILDSYSTEM_SHADERPACKCOOK_BASENAME "Tools.ShaderPack")

# Where the DESKTOP build put its executables, as seen from a CROSS build (wasm). The web
# build shells out to native cook tools by path - it cannot use $<TARGET_FILE:> because those
# targets do not exist in the wasm configure - so it needs the host's Bin dir. Derived from
# CMAKE_HOST_* (CMAKE_SYSTEM_NAME is "Emscripten" here, so the _dr_plat above would be wrong),
# and the exe suffix is the HOST's - CMAKE_EXECUTABLE_SUFFIX is ".js" under Emscripten.
# Assumes the desktop build used Clang (the documented dev toolchain); override the cache
# variable for a GCC/MSVC host build or a non-default Bin location.
if(CMAKE_HOST_WIN32)
    set(_dr_host_plat "Win64")
    set(BUILDSYSTEM_HOST_EXE_SUFFIX ".exe")
elseif(CMAKE_HOST_APPLE)
    set(_dr_host_plat "Mac64")
    set(BUILDSYSTEM_HOST_EXE_SUFFIX "")
else()
    set(_dr_host_plat "Linux64")
    set(BUILDSYSTEM_HOST_EXE_SUFFIX "")
endif()
set(BUILDSYSTEM_HOST_PLATFORM "${_dr_host_plat}" CACHE STRING
    "Host platform folder name under Bin/<config>/ (cross builds locate native tools there)")
set(BUILDSYSTEM_HOST_BIN_DIR "${CMAKE_SOURCE_DIR}/Bin/Debug/${BUILDSYSTEM_HOST_PLATFORM}-Clang"
    CACHE PATH "Desktop Bin dir holding the native cook tools a cross (web) build shells out to")

# Enable testing before any subdirectory so add_test() calls made by modules that
# register their own Tests/ (e.g. Runtime, Script/Wren) are picked up by ctest.
if(OPTION_BUILD_TESTS)
    enable_testing()
endif()

# --- Engine-wide compile policy ----------------------------------------------
# Exceptions and C++ RTTI are disabled engine-wide (see Documentation/Planning/Core.md §1).
add_library(policy INTERFACE)
add_library(Foundation::Policy ALIAS policy)

target_compile_features(policy INTERFACE cxx_std_23)

# Shipping flavor (Prelude: "define exactly one" of DEBUG/RELEASE/SHIPPING): an optimized
# build with asserts compiled out + stripped binaries - the dist/size-measurement config.
# Presets clang-shipping/gcc-shipping turn it on (Release + BUILDSYSTEM_OUTPUT_SUFFIX so it
# never clobbers a plain Release Bin dir).
option(OPTION_SHIPPING_BUILD "Shipping config: asserts compiled out, binaries stripped" OFF)

target_compile_definitions(policy INTERFACE
    $<$<CONFIG:Debug>:BUILD_DEBUG=1>
    $<$<AND:$<CONFIG:Release,RelWithDebInfo,MinSizeRel>,$<NOT:$<BOOL:${OPTION_SHIPPING_BUILD}>>>:BUILD_RELEASE=1>
    $<$<BOOL:${OPTION_SHIPPING_BUILD}>:BUILD_SHIPPING=1>
    # Instrumentation on in Debug/RelWithDebInfo; off (compiles to nothing) in shipping builds.
    $<$<CONFIG:Debug,RelWithDebInfo>:BUILD_PROFILING=1>
    # Build identity as strings, matching the Bin/<Config>/<Platform>-<Compiler> layout: the export
    # host template stamps itself with the config/compiler that built the running tool (export-templates.md).
    BUILD_CONFIG=\"$<CONFIG>\"
    BUILD_COMPILER=\"${_dr_comp}\"
    # Optional-module presence flags (0/1) - guard usage in C++ with e.g.
    # `#if OPTION_HAS_EXTENSION_IMGUI` / `#if OPTION_HAS_EXPERIMENTAL_GUI`.
    OPTION_HAS_EXPERIMENTAL_GUI=$<BOOL:${OPTION_EXPERIMENTAL_GUI}>
    OPTION_HAS_EXTENSION_IMGUI=$<BOOL:${OPTION_EXTENSION_IMGUI}>)

target_link_options(policy INTERFACE
    # Shipping: strip symbol tables at link (what a dist ships; the honest size number).
    $<$<AND:$<BOOL:${OPTION_SHIPPING_BUILD}>,$<CXX_COMPILER_ID:Clang,GNU>>:-s>)

# MSVC: CMake seeds CMAKE_CXX_FLAGS with "/EHsc". Appending /EHs-c- below does win, but cl
# reports every override - 9036 "D9025: overriding '/EHs' with '/EHs-'" lines in one build,
# which buries real diagnostics. Remove the default instead of fighting it; the policy flag
# right below is the single source of truth for exception behavior.
if(MSVC)
    string(REGEX REPLACE "[-/]EH[acsr-]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(STRIP "${CMAKE_CXX_FLAGS}" CMAKE_CXX_FLAGS)
endif()

target_compile_options(policy INTERFACE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-fno-exceptions;-fno-rtti>
    # Every static lib shares CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY and none sets a PDB name, so
    # parallel cl.exe jobs all target one vc140.pdb and lose the race ("C1041: cannot open
    # program database"). /FS funnels those writes through mspdbsrv, which is what the
    # diagnostic itself recommends.
    $<$<CXX_COMPILER_ID:MSVC>:/FS>
    # The tree is UTF-8 and uses u8"..." and non-ASCII character literals (e.g. U'●').
    # Without /utf-8 cl decodes sources in the system ANSI codepage and silently mis-reads
    # them - a 3-byte UTF-8 scalar in a char32_t literal becomes "C2015: too many characters
    # in constant". Note this does NOT show up as C4819; that only fires when a character
    # cannot be represented at all, so the absence of C4819 is not evidence /utf-8 is unneeded.
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    # GCC modules: eager gcm loading. The default lazy loader intermittently fails to merge
    # template pendings across module imports ("failed to read compiled module cluster" /
    # "failed to load pendings for ...") - deterministic in Tools.Cook's import mix on GCC 15.2.
    $<$<CXX_COMPILER_ID:GNU>:-fno-module-lazy>
    $<$<CXX_COMPILER_ID:MSVC>:/EHs-c-;/GR->)

# Warnings
target_compile_options(policy INTERFACE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

if(OPTION_WARNINGS_AS_ERRORS)
    target_compile_options(policy INTERFACE
        $<$<CXX_COMPILER_ID:Clang,GNU>:-Werror>
        # /wd4530: "C++ exception handler used, but unwind semantics are not enabled". We disable
        # exceptions on purpose (/EHs-c- above) while still using the STL, so cl emits this from
        # ITS OWN headers - 114 instances in a full build, none from Code/. It is a restatement of
        # a deliberate choice, not a finding, and under /WX it fails ~113 otherwise-clean targets.
        $<$<CXX_COMPILER_ID:MSVC>:/WX;/wd4530>)
    # Clang on Windows emits a spurious "argument unused during compilation: '-c'"
    # during the CMake module-dependency scan step. Suppress just that diagnostic
    # so -Werror doesn't kill the build.
    # Clang 22+ warns that __COUNTER__ is a "C2y extension" when used in C++ mode;
    # doctest (and other widely-used macros) rely on it, so suppress the diagnostic.
    target_compile_options(policy INTERFACE
        $<$<CXX_COMPILER_ID:Clang>:-Wno-error=unused-command-line-argument;-Wno-c2y-extensions>)
    # em++ re-invokes clang++ for the module precompile and does NOT forward
    # -Wno-error=unused-command-line-argument to that inner driver, so the same spurious
    # "'-c' unused" diagnostic is fatal again on the wasm build. Suppress it outright here
    # (it is never actionable: CMake, not us, passes both --precompile and -c).
    if(EMSCRIPTEN)
        target_compile_options(policy INTERFACE -Wno-unused-command-line-argument)
    endif()
endif()

# --- Include roots -----------------------------------------------------------
# Role-grouped reorg: modules live at Code/Draconic/<Role>/Draconic.<Name> and expose their
# path-prefixed public headers as "Draconic.<Name>/...". The ROLE dirs are the global include
# roots so a header resolves by its dotted name regardless of which role owns it (moving a module
# between roles never touches an #include). Code/Draconic stays a root during the migration so
# not-yet-moved modules keep their legacy "Runtime/..."/"Shell/..." prefixes until their role lands.
include_directories(
    ${CMAKE_SOURCE_DIR}/Code/Draconic            # transitional: unmigrated modules' legacy prefixes
    ${CMAKE_SOURCE_DIR}/Code/Draconic/Foundation
    ${CMAKE_SOURCE_DIR}/Code/Draconic/Engine
    ${CMAKE_SOURCE_DIR}/Code/Draconic/Editor
    ${CMAKE_SOURCE_DIR}/Code/Draconic/Tools
    ${CMAKE_SOURCE_DIR}/Code/Draconic/Experimental
    ${CMAKE_SOURCE_DIR}/Code/Draconic/Extensions)

# --- Targets -----------------------------------------------------------------
# ONE canonical, dependency-ordered list for every platform. Order is a valid topological order -
# a few libs probe siblings at configure time with if(TARGET ...) (Graphics->RHI backends,
# Editor.Core->Shaders, Script->AngelScript). Web builds the SAME list: the groups it cannot run
# yet - tooling, experimental, extensions - are forced off for EMSCRIPTEN above, so they skip in
# place. No separate web climb to keep in sync; web-only samples + return() sit at the tail.
if(EMSCRIPTEN)
    # Emscripten defaults are sized for tiny demos: 64KB stack (engine code overflows it) and a
    # fixed 16MB heap. 2MB stack + growable heap; link-time-only knobs applied to every target below.
    add_link_options(-sSTACK_SIZE=2097152 -sALLOW_MEMORY_GROWTH=1)
endif()

add_subdirectory(ThirdParty)
add_subdirectory(Code/Draconic/Foundation/Core)
add_subdirectory(Code/Draconic/Foundation/Settings)  # typed/versioned settings store over Core (runtime + editor)
add_subdirectory(Code/Draconic/Foundation/Profiler)  # CPU scope profiler over Core (instrumented across the engine)
add_subdirectory(Code/Draconic/Foundation/Net)       # real-time game networking: wire + INetTransport + loopback sim (over Core)
add_subdirectory(Code/Draconic/Foundation/Net.Replication)  # StateReplication foundation: NetworkId + reflection-driven field codec (over Net + Core)
add_subdirectory(Code/Draconic/Foundation/Xml)       # DOM XML parser + writer over Core
    add_subdirectory(Code/Draconic/Foundation/Xml.Serialization)
add_subdirectory(Code/Draconic/Foundation/VFS)       # byte access over Core stream primitives
    add_subdirectory(Code/Draconic/Foundation/VFS.Pak)
add_subdirectory(Code/Draconic/Engine/Engine.Project)   # project manifest/layout (runtime-side, editor-free)
add_subdirectory(Code/Draconic/Foundation/Content)   # content database over Core + VFS
add_subdirectory(Code/Draconic/Foundation/Resource)  # resource manager over content
add_subdirectory(Code/Draconic/Foundation/RHI)   # depends only on Core (foundational sibling of Runtime)
# --- RHI backends (Null/Validation always; GPU backends by platform + availability) ---
add_subdirectory(Code/Draconic/Foundation/RHI.Null)
add_subdirectory(Code/Draconic/Foundation/RHI.Validation)
find_package(Vulkan QUIET GLOBAL)
if(Vulkan_FOUND)
    add_subdirectory(Code/Draconic/Foundation/RHI.Vulkan)
else()
    message(STATUS "Vulkan not found - skipping Foundation::RHI.Vulkan")
endif()
if(WIN32)
    add_subdirectory(Code/Draconic/Foundation/RHI.DX12)
endif()
if(EMSCRIPTEN OR TARGET ThirdParty::WgpuHeaders)
    add_subdirectory(Code/Draconic/Foundation/RHI.WebGPU)
else()
    message(STATUS "Skipping Foundation::RHI.WebGPU (no wgpu headers)")
endif()
add_subdirectory(Code/Draconic/Foundation/RenderGraph)  # render graph over RHI
add_subdirectory(Code/Draconic/Foundation/Shaders)
add_subdirectory(Code/Draconic/Foundation/Shaders.System)
add_subdirectory(Code/Draconic/Foundation/Shaders.Resource)
add_subdirectory(Code/Draconic/Foundation/Image)
add_subdirectory(Code/Draconic/Foundation/Image.IO)
add_subdirectory(Code/Draconic/Foundation/Image.Resource)
add_subdirectory(Code/Draconic/Foundation/Model)
    add_subdirectory(Code/Draconic/Foundation/Model.IO)
    add_subdirectory(Code/Draconic/Foundation/Model.GLTF)
    add_subdirectory(Code/Draconic/Foundation/Model.FBX)
add_subdirectory(Code/Draconic/Foundation/Geometry)  # engine runtime mesh format (StaticMesh/SkinnedMesh + primitives)
add_subdirectory(Code/Draconic/Foundation/Geometry.Resource)
add_subdirectory(Code/Draconic/Foundation/Animation) # skeletal-animation foundation (Sedulous.Animation port; Core-only)
add_subdirectory(Code/Draconic/Foundation/Animation.Resource)
add_subdirectory(Code/Draconic/Foundation/Particles) # CPU particle runtime (Sedulous.Particles port; Core-only)
add_subdirectory(Code/Draconic/Foundation/Particles.Resource)
add_subdirectory(Code/Draconic/Foundation/Physics)   # Jolt-backed rigid-body core (world/bodies/queries/events; JPH never public)
add_subdirectory(Code/Draconic/Foundation/Physics.Resource)  # cooked collision shapes + physical materials
add_subdirectory(Code/Draconic/Foundation/Audio)     # miniaudio-backed audio engine core (voices/buses/handles; ma_* never public)
add_subdirectory(Code/Draconic/Foundation/Audio.Resource)    # cooked audio clips + factory
add_subdirectory(Code/Draconic/Foundation/Scene)     # scene / ECS foundation (entities, transforms, components, SceneSubsystem)
add_subdirectory(Code/Draconic/Engine/Engine.Scene)
add_subdirectory(Code/Draconic/Foundation/Scene.Resource)
add_subdirectory(Code/Draconic/Foundation/Render.Api) # light scene-rendering interface (ISceneRenderer) - tools drive rendering w/o linking the renderer
add_subdirectory(Code/Draconic/Foundation/Render)    # renderer: scene->render extraction + (later) the GPU forward path
add_subdirectory(Code/Draconic/Engine/Engine.Render)
add_subdirectory(Code/Draconic/Engine/Engine.Animation) # skeletal-animation scene integration (links RenderSubsystem; after Render)
add_subdirectory(Code/Draconic/Engine/Engine.Particles) # particle scene/render integration (renderer + provider seam; after Render)
add_subdirectory(Code/Draconic/Engine/Engine.Physics) # physics scene integration (components + per-scene Jolt world + interpolation; after Render)
add_subdirectory(Code/Draconic/Engine/Engine.Audio)   # audio scene integration (components + per-scene voice group + listener sync; after Render)
add_subdirectory(Code/Draconic/Foundation/Fonts)     # font types/interfaces over Core + Image
add_subdirectory(Code/Draconic/Foundation/Fonts.Baked)
add_subdirectory(Code/Draconic/Foundation/Fonts.IO)
add_subdirectory(Code/Draconic/Foundation/Fonts.TTF)
add_subdirectory(Code/Draconic/Foundation/Fonts.Importer)
add_subdirectory(Code/Draconic/Foundation/Fonts.DistanceField)
add_subdirectory(Code/Draconic/Foundation/Fonts.Resource)
if(TARGET ThirdParty::MsdfgenCore)  # DF baker (editor-time TTF -> baked); needs the vendored msdfgen
    add_subdirectory(Code/Draconic/Foundation/Fonts.DistanceField.Baker)
endif()
add_subdirectory(Code/Draconic/Foundation/Texture)   # texture descriptors + cooked resource/GPU factory + editor
add_subdirectory(Code/Draconic/Foundation/Texture.Resource)
add_subdirectory(Code/Draconic/Foundation/Materials) # data-driven material model (PipelineConfig + bind-group-inferring MaterialSystem)
add_subdirectory(Code/Draconic/Foundation/Materials.PipelineCache)
add_subdirectory(Code/Draconic/Foundation/Materials.Resource)
add_subdirectory(Code/Draconic/Foundation/Model.Resource) # cooked model manifest + runtime composite (runtime side; no tooling deps)
add_subdirectory(Code/Draconic/Foundation/UI.Resource)   # cooked UI documents + themes (game-ui; UI-framework-free records)
add_subdirectory(Code/Draconic/Engine/Engine.UI)  # game screen tier: canvases + overlay render + consumption
add_subdirectory(Code/Draconic/Foundation/VG)        # 2D vector-graphics core over Core + Image
add_subdirectory(Code/Draconic/Foundation/VG.SVG)
add_subdirectory(Code/Draconic/Foundation/VG.Renderer)
add_subdirectory(Code/Draconic/Foundation/UI)        # retained-mode UI framework (faithful Sedulous.UI port) over Core (+ VG/Fonts as phases land)
add_subdirectory(Code/Draconic/Foundation/UI.VFS)
add_subdirectory(Code/Draconic/Foundation/UI.Shell)
add_subdirectory(Code/Draconic/Foundation/UI.Toolkit)
add_subdirectory(Code/Draconic/Foundation/UI.Viewport)
# --- UI desktop hosts (UIHost + docking workbench; link Graphics/Runtime.Client, desktop-only) ---
if(NOT EMSCRIPTEN)
    add_subdirectory(Code/Draconic/Foundation/UI.Runtime)
    add_subdirectory(Code/Draconic/Foundation/UI.Application)
endif()
if(OPTION_EXPERIMENTAL_GUI)
    add_subdirectory(Code/Draconic/Experimental/GUI)   # fresh eepp-derived UI framework (docs/design/gui-port.md)
endif()
add_subdirectory(Code/Draconic/Foundation/Script)
# --- Script backends (Wren always; AngelScript where the vendored lib built) ---
add_subdirectory(Code/Draconic/Foundation/Script.Wren)
if(TARGET ThirdParty::AngelScript)
    add_subdirectory(Code/Draconic/Foundation/Script.AngelScript)
endif()
add_subdirectory(Code/Draconic/Foundation/Script.Resource)   # cooked script classes + factory (source + harvested metadata)
add_subdirectory(Code/Draconic/Foundation/Script.Facades)    # curated behavior facades (Entity/Log/Time/Random) - shared by cook VM + runtime
add_subdirectory(Code/Draconic/Foundation/Net.Manager)       # per-instance networked endpoint (NetworkManager) + Net script facade (over Net + Script)
add_subdirectory(Code/Draconic/Engine/Engine.Net)     # once-per-context NetworkSubsystem (injects NetworkComponentManager into scenes)
add_subdirectory(Code/Draconic/Foundation/Shell)     # raw platform/OS service (IShell: window/input/events) over Core; below Runtime
# --- Shell backends (Null always; browser shell on emscripten, SDL3 desktop otherwise) ---
add_subdirectory(Code/Draconic/Foundation/Shell.Null)
if(EMSCRIPTEN)
    add_subdirectory(Code/Draconic/Foundation/Shell.Web)
else()
    find_package(SDL3 QUIET GLOBAL)  # GLOBAL: consumers across roles (Shell.Desktop, its tests, runners) link SDL3::SDL3
    if(SDL3_FOUND)
        add_subdirectory(Code/Draconic/Foundation/Shell.Desktop)
    else()
        message(STATUS "SDL3 not found - skipping Foundation::Shell.Desktop")
    endif()
endif()
add_subdirectory(Code/Draconic/Foundation/Input)     # action-mapping layer (InputMap + ActionRuntime + asset/cooked) over Core + Shell
add_subdirectory(Code/Draconic/Foundation/Input.Resource)
if(OPTION_EXPERIMENTAL_GUI)
    add_subdirectory(Code/Draconic/Experimental/GUI.Shell)  # platform input bridge (Shell InputEvents -> GUI EventDispatcher)
    add_subdirectory(Code/Draconic/Experimental/GUI.VFS)   # VFS-backed IResourceProvider (background-image: url())
endif()
add_subdirectory(Code/Draconic/Foundation/Graphics)  # GraphicsDevice/RenderWindow render host over RHI+Shell; below Runtime
add_subdirectory(Code/Draconic/Foundation/Runtime)
add_subdirectory(Code/Draconic/Foundation/Runtime.Client)
add_subdirectory(Code/Draconic/Engine/Engine.GameInstance)
add_subdirectory(Code/Draconic/Engine/Engine.DefaultApp)
# --- Runtime runner backend (browser rAF loop vs blocking desktop loop) ---
if(EMSCRIPTEN)
    add_subdirectory(Code/Draconic/Foundation/Runtime.Web)
else()
    add_subdirectory(Code/Draconic/Foundation/Runtime.Desktop)
endif()
add_subdirectory(Code/Draconic/Engine/Engine.Input)  # input runtime hookup (tick + device provider seam) over Runtime
add_subdirectory(Code/Draconic/Engine/Engine.Script) # entity behaviors (ScriptComponent + the run's ONE gameplay script context) over Runtime
add_subdirectory(Code/Draconic/Engine/Engine.Integration) # cross-subsystem composition helpers (physics-contact -> script bridge) over Physics + Script
if(OPTION_EXTENSION_IMGUI)
    add_subdirectory(Code/Draconic/Extensions/Imgui) # Dear ImGui debug UI (RHI renderer + subsystem) over Runtime
endif()
add_subdirectory(Code/Draconic/Engine/Engine.Player)        # DraconicPlayer generic game runner


# --- Authoring tooling: the editor libraries + the CLI executables that assemble them.
# Gated by OPTION_BUILD_TOOLS (forced OFF for web / runtime-only / shipping builds) so the
# whole editor/cook surface drops cleanly. The in-folder *.Editor sub-targets are gated the
# same way inside their Foundation libs; every consumer of them lives inside this guard. ---
if(OPTION_BUILD_TOOLS)
    add_subdirectory(Code/Draconic/Pipeline/Pipeline.Core)  # asset-pipeline authoring base: Asset + builders (tooling)
    add_subdirectory(Code/Draconic/Pipeline/Pipeline.Cook)  # incremental cook driver + pipeline DB (tooling, headless)
    add_subdirectory(Code/Draconic/Editor/Editor.Core) # headless editor domain: context/registries/commands/pages/project (tooling)
    add_subdirectory(Code/Draconic/Pipeline/Physics.Pipeline)    # collision/material assets + builders (tooling)
    add_subdirectory(Code/Draconic/Pipeline/Audio.Pipeline)      # audio clip asset/builder/importer (tooling)
    add_subdirectory(Code/Draconic/Pipeline/UI.Pipeline)     # UI document/theme assets + validating builders (tooling)
    add_subdirectory(Code/Draconic/Pipeline/ModelImporter) # top-level model import: model IR -> cooked geometry/material/texture/animation resources
    add_subdirectory(Code/Draconic/Pipeline/Script.Pipeline)  # script class asset/builder/importer (cook; neutral)
    add_subdirectory(Code/Draconic/Pipeline/Script.Wren.Pipeline)  # Wren cook service
    add_subdirectory(Code/Draconic/Editor/Editor.Script.Wren)  # Wren editor-UI (CodeEditView lexers)
    add_subdirectory(Code/Draconic/Pipeline/Script.AngelScript.Pipeline)  # AngelScript cook service
    add_subdirectory(Code/Draconic/Editor/Editor.Script.AngelScript)  # AngelScript editor-UI (lexers)
    add_subdirectory(Code/Draconic/Editor/Editor.App) # editor UI shell (chrome + layout persistence + EditorApplication)
    add_subdirectory(Code/Draconic/Editor/Editor.Input) # input-map editing page (editor exe only)
    add_subdirectory(Code/Draconic/Editor/Editor.GameUI) # UI document page: text + live preview (editor exe only)
    add_subdirectory(Code/Draconic/Editor/Editor.Audio)  # audio clip audition page: waveform + play (editor exe only)
    add_subdirectory(Code/Draconic/Editor/Editor.Texture) # texture asset page: import settings + preview (editor exe only)
    add_subdirectory(Code/Draconic/Editor/Editor.Image) # image asset page: inspect + color-space intent (editor exe only)
    add_subdirectory(Code/Draconic/Editor/Editor.Generic) # fallback asset page: serialize-driven property form (editor exe only)
    add_subdirectory(Code/Draconic/Editor/Editor.Script) # script behavior source page: edit + recook + inline errors (editor exe only)
    add_subdirectory(Code/Draconic/Editor/Editor.Scene) # scene editor plugin (SceneEditorPage + RegisterSceneEditor)

    add_subdirectory(Code/Draconic/Pipeline/Animation.Pipeline)
    add_subdirectory(Code/Draconic/Pipeline/Geometry.Pipeline)
    add_subdirectory(Code/Draconic/Pipeline/Image.Pipeline)
    add_subdirectory(Code/Draconic/Pipeline/Input.Pipeline)
    add_subdirectory(Code/Draconic/Pipeline/Materials.Pipeline)
    add_subdirectory(Code/Draconic/Pipeline/Particles.Pipeline)
    add_subdirectory(Code/Draconic/Pipeline/Shaders.Pipeline)
    add_subdirectory(Code/Draconic/Pipeline/Texture.Pipeline)
    if(TARGET Foundation::Fonts.DistanceField.Baker)
        add_subdirectory(Code/Draconic/Pipeline/Fonts.Pipeline)
        add_subdirectory(Code/Draconic/Editor/Editor.Fonts) # font asset page: bake intent + live atlas preview
        add_subdirectory(Code/Draconic/Editor/Editor.Physics) # collision-shape asset page: mesh picker + cook
    endif()

    add_subdirectory(Code/Draconic/Tools/Tools.Editor)        # DraconicEditor executable (editor assembly)
    add_subdirectory(Code/Draconic/Tools/Tools.Cook)          # Tools.Cook CLI cooker (headless)
    add_subdirectory(Code/Draconic/Tools/Tools.Export)        # DraconicExport dist packager
    add_subdirectory(Code/Draconic/Tools/Tools.ShaderPack) # DraconicShaderPackCook: cook shaders.dpak (WGSL for web)
endif()

# --- Tests: every module's flat-sibling doctest suite (guarded by its module target so the
# web subset + platform-gated backends skip when the module was not built) ---
if(OPTION_BUILD_TESTS)
    util_test_suite(Code/Draconic/Pipeline/Animation.Pipeline.Tests              Pipeline::Animation)
    util_test_suite(Code/Draconic/Pipeline/Audio.Pipeline.Tests                  Pipeline::Audio)
    util_test_suite(Code/Draconic/Editor/Editor.App.Tests                    Editor::App)
    util_test_suite(Code/Draconic/Editor/Editor.Audio.Tests                  Editor::Audio)
    util_test_suite(Code/Draconic/Pipeline/Pipeline.Cook.Tests                   Pipeline::Cook)
    util_test_suite(Code/Draconic/Editor/Editor.Core.Tests                   Editor::Core)
    util_test_suite(Code/Draconic/Editor/Editor.Generic.Tests                Editor::Generic)
    util_test_suite(Code/Draconic/Editor/Editor.Image.Tests                  Editor::Image)
    util_test_suite(Code/Draconic/Editor/Editor.Scene.Tests                  Editor::Scene)
    util_test_suite(Code/Draconic/Editor/Editor.Script.Tests                 Editor::Script)
    util_test_suite(Code/Draconic/Editor/Editor.Tests                        Pipeline::Core)
    util_test_suite(Code/Draconic/Editor/Editor.Texture.Tests                Editor::Texture)
    util_test_suite(Code/Draconic/Pipeline/Geometry.Pipeline.Tests               Pipeline::Geometry)
    util_test_suite(Code/Draconic/Pipeline/Image.Pipeline.Tests                  Pipeline::Image)
    util_test_suite(Code/Draconic/Pipeline/Input.Pipeline.Tests                  Pipeline::Input)
    util_test_suite(Code/Draconic/Pipeline/Materials.Pipeline.Tests              Pipeline::Materials)
    util_test_suite(Code/Draconic/Pipeline/ModelImporter.Tests                 Pipeline::ModelImporter)
    util_test_suite(Code/Draconic/Pipeline/Particles.Pipeline.Tests              Pipeline::Particles)
    util_test_suite(Code/Draconic/Pipeline/Physics.Pipeline.Tests                Pipeline::Physics)
    util_test_suite(Code/Draconic/Pipeline/Script.AngelScript.Pipeline.Tests     Pipeline::Script.AngelScript)
    util_test_suite(Code/Draconic/Pipeline/Script.Pipeline.Tests                 Pipeline::Script)
    util_test_suite(Code/Draconic/Pipeline/Script.Wren.Pipeline.Tests            Pipeline::Script.Wren)
    util_test_suite(Code/Draconic/Pipeline/Shaders.Pipeline.Tests                Pipeline::Shaders)
    util_test_suite(Code/Draconic/Pipeline/Texture.Pipeline.Tests                Pipeline::Texture)
    util_test_suite(Code/Draconic/Pipeline/Fonts.Pipeline.Tests                  Pipeline::Fonts)
    util_test_suite(Code/Draconic/Pipeline/UI.Pipeline.Tests                     Pipeline::UI)
    util_test_suite(Code/Draconic/Engine/Engine.Animation.Tests              Engine::Animation)
    util_test_suite(Code/Draconic/Engine/Engine.Audio.Tests                  Engine::Audio)
    util_test_suite(Code/Draconic/Engine/Engine.GameInstance.Tests Foundation::Script.AngelScript)  # links the AngelScript backend; builds wherever it exists (incl. web now)
    util_test_suite(Code/Draconic/Engine/Engine.Integration.Tests            Engine::Integration)
    util_test_suite(Code/Draconic/Engine/Engine.Net.Tests                    Engine::Net)
    util_test_suite(Code/Draconic/Engine/Engine.Physics.Tests                Engine::Physics)
    util_test_suite(Code/Draconic/Engine/Engine.Particles.Tests              Engine::Particles)
    util_test_suite(Code/Draconic/Engine/Engine.Input.Tests                  Engine::Input)
    util_test_suite(Code/Draconic/Engine/Engine.Render.Tests                 Engine::Render)
    util_test_suite(Code/Draconic/Engine/Engine.Scene.Tests                  Engine::Scene)
    util_test_suite(Code/Draconic/Engine/Engine.Script.Tests Foundation::Script.AngelScript)  # links the AngelScript backend; builds wherever it exists (incl. web now)
    util_test_suite(Code/Draconic/Engine/Engine.UI.Tests                     Engine::UI)
    util_test_suite(Code/Draconic/Experimental/GUI.Shell.Tests               Experimental::GUI.Shell)
    util_test_suite(Code/Draconic/Experimental/GUI.Tests                     Experimental::GUI)
    util_test_suite(Code/Draconic/Experimental/GUI.VFS.Tests                 Experimental::GUI.VFS)
    util_test_suite(Code/Draconic/Foundation/Animation.Resource.Tests        Foundation::Animation.Resource)
    util_test_suite(Code/Draconic/Foundation/Animation.Tests                 Foundation::Animation)
    util_test_suite(Code/Draconic/Foundation/Audio.Tests                     Foundation::Audio)
    util_test_suite(Code/Draconic/Foundation/Content.Tests                   Foundation::Content)
    util_test_suite(Code/Draconic/Foundation/Core.Tests                      Foundation::Core)
    util_test_suite(Code/Draconic/Foundation/Fonts.Baked.Tests               Foundation::Fonts.Baked)
    util_test_suite(Code/Draconic/Foundation/Fonts.DistanceField.Baker.Tests Foundation::Fonts.DistanceField.Baker)
    util_test_suite(Code/Draconic/Foundation/Fonts.DistanceField.Tests       Foundation::Fonts.DistanceField)
    util_test_suite(Code/Draconic/Foundation/Fonts.IO.Tests                  Foundation::Fonts.IO)
    util_test_suite(Code/Draconic/Foundation/Fonts.Importer.Tests            Foundation::Fonts.Importer)
    util_test_suite(Code/Draconic/Foundation/Fonts.TTF.Tests                 Foundation::Fonts.TTF)
    util_test_suite(Code/Draconic/Foundation/Fonts.Tests                     Foundation::Fonts)
    util_test_suite(Code/Draconic/Foundation/Geometry.Resource.Tests         Foundation::Geometry.Resource)
    util_test_suite(Code/Draconic/Foundation/Geometry.Tests                  Foundation::Geometry)
    util_test_suite(Code/Draconic/Foundation/Graphics.Tests                  Foundation::Graphics)
    util_test_suite(Code/Draconic/Foundation/Image.Tests                     Foundation::Image)
    util_test_suite(Code/Draconic/Foundation/Input.Tests                     Foundation::Input)
    util_test_suite(Code/Draconic/Foundation/Materials.PipelineCache.Tests   Foundation::Materials.PipelineCache)
    util_test_suite(Code/Draconic/Foundation/Materials.Resource.Tests        Foundation::Materials.Resource)
    util_test_suite(Code/Draconic/Foundation/Materials.Tests                 Foundation::Materials)
    util_test_suite(Code/Draconic/Foundation/Model.Tests                     Foundation::Model)
    util_test_suite(Code/Draconic/Foundation/Net.Manager.Tests Foundation::Script.AngelScript)  # links the AngelScript backend; builds wherever it exists (incl. web now)
    util_test_suite(Code/Draconic/Foundation/Net.Replication.Tests           Foundation::Net.Replication)
    util_test_suite(Code/Draconic/Foundation/Net.Tests                       Foundation::Net)
    util_test_suite(Code/Draconic/Foundation/Particles.Resource.Tests        Foundation::Particles.Resource)
    util_test_suite(Code/Draconic/Foundation/Particles.Tests                 Foundation::Particles)
    util_test_suite(Code/Draconic/Foundation/Physics.Tests                   Foundation::Physics)
    util_test_suite(Code/Draconic/Foundation/Profiler.Tests                  Foundation::Profiler)
    util_test_suite(Code/Draconic/Foundation/RHI.Null.Tests                  Foundation::RHI.Null)
    util_test_suite(Code/Draconic/Foundation/RHI.Validation.Tests            Foundation::RHI.Validation)
    if(NOT EMSCRIPTEN)  # spins up a real device; no headless web path yet
        util_test_suite(Code/Draconic/Foundation/RHI.WebGPU.Tests Foundation::RHI.WebGPU)
        if(TARGET Foundation::RHI.Vulkan AND TARGET Foundation::RHI.WebGPU)
            # Cross-backend pixel probe (needs BOTH backends compiled; GPUs checked at runtime).
            add_subdirectory(Code/Draconic/Foundation/Render.Backend.Tests)
            add_subdirectory(Code/Draconic/Foundation/VG.Backend.Tests)
        endif()
    endif()
    util_test_suite(Code/Draconic/Foundation/Render.Tests                    Foundation::Render)
    util_test_suite(Code/Draconic/Foundation/RenderGraph.Tests               Foundation::RenderGraph)
    util_test_suite(Code/Draconic/Foundation/Resource.Tests                  Foundation::Resource)
    util_test_suite(Code/Draconic/Foundation/Runtime.Client.Tests            Foundation::Runtime.Client)
    util_test_suite(Code/Draconic/Foundation/Runtime.Tests                   Foundation::Runtime)
    util_test_suite(Code/Draconic/Foundation/Scene.Resource.Tests            Foundation::Scene.Resource)
    util_test_suite(Code/Draconic/Foundation/Scene.Tests                     Foundation::Scene)
    util_test_suite(Code/Draconic/Foundation/Script.AngelScript.Tests        Foundation::Script.AngelScript)
    util_test_suite(Code/Draconic/Foundation/Script.Resource.Tests           Foundation::Script.Resource)
    util_test_suite(Code/Draconic/Foundation/Script.Tests                    Foundation::Script)
    util_test_suite(Code/Draconic/Foundation/Script.Wren.Tests               Foundation::Script.Wren)
    util_test_suite(Code/Draconic/Foundation/Settings.Tests                  Foundation::Settings)
    util_test_suite(Code/Draconic/Foundation/Shaders.Resource.Tests          Foundation::Shaders.Resource)
    util_test_suite(Code/Draconic/Foundation/Shaders.System.Tests            Foundation::Shaders.System)
    util_test_suite(Code/Draconic/Foundation/Shaders.Tests                   Foundation::Shaders)
    util_test_suite(Code/Draconic/Foundation/Shell.Desktop.Tests             Foundation::Shell.Desktop)
    util_test_suite(Code/Draconic/Foundation/Shell.Null.Tests                Foundation::Shell.Null)
    util_test_suite(Code/Draconic/Foundation/Texture.Resource.Tests          Foundation::Texture.Resource)
    util_test_suite(Code/Draconic/Foundation/Fonts.Resource.Tests            Foundation::Fonts.Resource)
    util_test_suite(Code/Draconic/Foundation/Texture.Tests                   Foundation::Texture)
    util_test_suite(Code/Draconic/Foundation/UI.Shell.Tests                  Foundation::UI.Shell)
    util_test_suite(Code/Draconic/Foundation/UI.Tests                        Foundation::UI)
    util_test_suite(Code/Draconic/Foundation/UI.Toolkit.Tests                Foundation::UI.Toolkit)
    util_test_suite(Code/Draconic/Foundation/UI.VFS.Tests                    Foundation::UI.VFS)
    util_test_suite(Code/Draconic/Foundation/UI.Viewport.Tests               Foundation::UI.Viewport)
    util_test_suite(Code/Draconic/Foundation/VFS.Pak.Tests                   Foundation::VFS.Pak)
    util_test_suite(Code/Draconic/Foundation/VFS.Tests                       Foundation::VFS)
    util_test_suite(Code/Draconic/Foundation/VG.Renderer.Tests               Foundation::VG.Renderer)
    util_test_suite(Code/Draconic/Foundation/VG.SVG.Tests                    Foundation::VG.SVG)
    util_test_suite(Code/Draconic/Foundation/VG.Tests                        Foundation::VG)
    util_test_suite(Code/Draconic/Foundation/Xml.Serialization.Tests         Foundation::Xml.Serialization)
    util_test_suite(Code/Draconic/Foundation/Xml.Tests                       Foundation::Xml)
endif()

# Web-only samples, then stop - the rest of Code/Samples is desktop-only. Honor
# OPTION_BUILD_SAMPLES (both wasm presets default it OFF): a samples-off wasm build
# must not build the samples NOR inherit WebScene's native shader-cook tool requirement.
if(EMSCRIPTEN)
    if(OPTION_BUILD_SAMPLES)
        add_subdirectory(Code/Samples/WebTriangle)  # WebShell + web runner + WebGPU factory -> WebTriangle.html
        add_subdirectory(Code/Samples/WebScene)     # DefaultApplication + a lit PBR scene -> WebScene.html
    endif()
    return()
endif()

if(OPTION_BUILD_SAMPLES)
    add_subdirectory(Code/Samples)
endif()

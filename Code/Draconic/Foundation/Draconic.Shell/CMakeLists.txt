# Draconic::Shell - `draconic.shell` (IShell/IWindow, the raw OS/windowing service - what
# Sedulous calls the "shell"). Interfaces only; native + null backends implement them.

add_library(Draconic.Shell STATIC)
add_library(Draconic::Shell ALIAS Draconic.Shell)

target_link_libraries(Draconic.Shell PUBLIC Draconic::Core Draconic::Policy)

target_sources(Draconic.Shell
    PUBLIC
        FILE_SET shell_modules TYPE CXX_MODULES FILES
            InputTypes.cppm
            Input.cppm
            InputSurface.cppm
            Dialog.cppm
            ShellModule.cppm)

# Null backend needs only the interface. The Desktop (SDL3) backend needs the interface + SDL3
# (its run loop moved to draconic.runtime.desktop, so it no longer depends on client/graphics) -
# built when SDL3 is available.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Shell.Null ${CMAKE_CURRENT_BINARY_DIR}/Null)

if(EMSCRIPTEN)
    # The browser backend: a single-canvas IShell over Emscripten's HTML5 API.
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Shell.Web ${CMAKE_CURRENT_BINARY_DIR}/Web)
else()
    find_package(SDL3 QUIET GLOBAL)  # GLOBAL: the hoisted Shell.Desktop.Tests (added at root) links SDL3::SDL3
    if(SDL3_FOUND)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Shell.Desktop ${CMAKE_CURRENT_BINARY_DIR}/Desktop)
    else()
        message(STATUS "SDL3 not found - skipping Draconic::Shell.Desktop")
    endif()
endif()

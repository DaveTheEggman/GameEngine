# Draconic::Shell - `draconic.shell` (IShell/IWindow, the raw OS/windowing service - what
# Sedulous calls the "shell"). Interfaces only; native + null backends implement them.

add_library(DraconicShell STATIC)
add_library(Draconic::Shell ALIAS DraconicShell)

target_link_libraries(DraconicShell PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicShell
    PUBLIC
        FILE_SET shell_modules TYPE CXX_MODULES FILES
            InputTypes.cppm
            Input.cppm
            InputSurface.cppm
            Dialog.cppm
            Shell.cppm)

# Null backend needs only the interface. The Desktop (SDL3) backend needs the interface + SDL3
# (its run loop moved to draconic.runtime.desktop, so it no longer depends on client/graphics) -
# built when SDL3 is available.
add_subdirectory(Null)

find_package(SDL3 QUIET)
if(SDL3_FOUND)
    add_subdirectory(Desktop)
else()
    message(STATUS "SDL3 not found - skipping Draconic::ShellDesktop")
endif()

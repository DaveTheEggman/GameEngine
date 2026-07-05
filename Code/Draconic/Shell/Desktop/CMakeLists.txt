# Draconic::ShellDesktop — `draconic.shell.desktop` (SDL3-based IShell for Windows/Linux/macOS:
# windowing, input, events). The desktop run loop lives in draconic.runtime.desktop, so this
# backend needs only the shell interface + SDL3 — no client/graphics dependency.

add_library(DraconicShellDesktop STATIC)
add_library(Draconic::ShellDesktop ALIAS DraconicShellDesktop)

target_link_libraries(DraconicShellDesktop
    PUBLIC Draconic::Shell Draconic::Policy
    PRIVATE SDL3::SDL3)

target_sources(DraconicShellDesktop
    PUBLIC
        FILE_SET shell_desktop_modules TYPE CXX_MODULES FILES
            SDL3Shell.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

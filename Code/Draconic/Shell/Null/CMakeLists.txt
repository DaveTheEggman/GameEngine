# Draconic::ShellNull — `draconic.shell.null` (headless IShell).

add_library(DraconicShellNull STATIC)
add_library(Draconic::ShellNull ALIAS DraconicShellNull)

target_link_libraries(DraconicShellNull PUBLIC Draconic::Shell Draconic::Policy)

target_sources(DraconicShellNull
    PUBLIC
        FILE_SET shell_null_modules TYPE CXX_MODULES FILES
            NullShell.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

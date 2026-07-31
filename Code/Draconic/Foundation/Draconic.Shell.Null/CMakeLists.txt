# Draconic::ShellNull - `draconic.shell.null` (headless IShell).

add_library(Draconic.Shell.Null STATIC)
add_library(Draconic::ShellNull ALIAS Draconic.Shell.Null)

target_link_libraries(Draconic.Shell.Null PUBLIC Draconic::Shell Draconic::Policy)

target_sources(Draconic.Shell.Null
    PUBLIC
        FILE_SET shell_null_modules TYPE CXX_MODULES FILES
            NullShell.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Shell.Null.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()

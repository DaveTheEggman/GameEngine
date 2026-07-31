# Draconic::Runtime.Client - `draconic.runtime.client` (the Application base class).
# Loop-agnostic: the desktop runner (draconic.runtime.desktop) drives Start/Tick/Stop against the shell.

add_library(Draconic.Runtime.Client STATIC)
add_library(Draconic::Runtime.Client ALIAS Draconic.Runtime.Client)

target_link_libraries(Draconic.Runtime.Client
    PUBLIC Draconic::Runtime Draconic::Shell Draconic::Graphics Draconic::Profiler Draconic::Policy)

target_sources(Draconic.Runtime.Client
    PUBLIC
        FILE_SET runtime_client_modules TYPE CXX_MODULES FILES
            Application.cppm
            EmbeddedHost.cppm
            ApplicationHost.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Runtime.Client.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()

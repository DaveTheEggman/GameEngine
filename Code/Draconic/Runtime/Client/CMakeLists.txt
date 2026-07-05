# Draconic::RuntimeClient — `draconic.runtime.client` (the Application base class).
# Loop-agnostic: the desktop runner (draconic.runtime.desktop) drives Start/Tick/Stop against the shell.

add_library(DraconicRuntimeClient STATIC)
add_library(Draconic::RuntimeClient ALIAS DraconicRuntimeClient)

target_link_libraries(DraconicRuntimeClient
    PUBLIC Draconic::Runtime Draconic::Shell Draconic::RuntimeGraphics Draconic::Profiler Draconic::Policy)

target_sources(DraconicRuntimeClient
    PUBLIC
        FILE_SET runtime_client_modules TYPE CXX_MODULES FILES
            Application.cppm
            ApplicationHost.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

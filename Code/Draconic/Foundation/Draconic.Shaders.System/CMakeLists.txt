# Draconic::ShaderSystem - `draconic.shaders.system` (variant compile-on-demand + cache,
# plus the dev file-backed source provider). Needs the RHI (to create GPU modules) on
# top of the RHI-free draconic.shaders; the VFS for the file provider.

add_library(Draconic.Shaders.System STATIC)
add_library(Draconic::ShaderSystem ALIAS Draconic.Shaders.System)

target_link_libraries(Draconic.Shaders.System
    PUBLIC Draconic::Core Draconic::RHI Draconic::Shaders Draconic::VFS Draconic::Policy)

target_sources(Draconic.Shaders.System
    PUBLIC
        FILE_SET shader_system_modules TYPE CXX_MODULES FILES
            ShadersSystemModule.cppm
            ShaderSystem.cppm
            FileShaderSourceProvider.cppm
            ShaderSystemHost.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Shaders.System.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()

# Draconic::ShaderSystem - `draconic.shaders.system` (variant compile-on-demand + cache,
# plus the dev file-backed source provider). Needs the RHI (to create GPU modules) on
# top of the RHI-free draconic.shaders; the VFS for the file provider.

add_library(DraconicShaderSystem STATIC)
add_library(Draconic::ShaderSystem ALIAS DraconicShaderSystem)

target_link_libraries(DraconicShaderSystem
    PUBLIC Draconic::Core Draconic::RHI Draconic::Shaders Draconic::VFS Draconic::Policy)

target_sources(DraconicShaderSystem
    PUBLIC
        FILE_SET shader_system_modules TYPE CXX_MODULES FILES
            ShadersSystemModule.cppm
            ShaderSystem.cppm
            FileShaderSourceProvider.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

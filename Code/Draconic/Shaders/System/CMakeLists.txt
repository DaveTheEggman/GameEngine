# Draconic::ShaderSystem - `draconic.shaders.system` (variant compile-on-demand + cache).
# Needs the RHI (to create GPU modules) on top of the RHI-free draconic.shaders.

add_library(DraconicShaderSystem STATIC)
add_library(Draconic::ShaderSystem ALIAS DraconicShaderSystem)

target_link_libraries(DraconicShaderSystem
    PUBLIC Draconic::Core Draconic::RHI Draconic::Shaders Draconic::Policy)

target_sources(DraconicShaderSystem
    PUBLIC
        FILE_SET shader_system_modules TYPE CXX_MODULES FILES
            ShaderSystem.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

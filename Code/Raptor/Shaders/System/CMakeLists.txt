# Raptor::ShaderSystem — `raptor.shaders.system` (variant compile-on-demand + cache).
# Needs the RHI (to create GPU modules) on top of the RHI-free raptor.shaders.

add_library(RaptorShaderSystem STATIC)
add_library(Raptor::ShaderSystem ALIAS RaptorShaderSystem)

target_link_libraries(RaptorShaderSystem
    PUBLIC Raptor::Core Raptor::RHI Raptor::Shaders Raptor::Policy)

target_sources(RaptorShaderSystem
    PUBLIC
        FILE_SET shader_system_modules TYPE CXX_MODULES FILES
            ShaderSystem.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

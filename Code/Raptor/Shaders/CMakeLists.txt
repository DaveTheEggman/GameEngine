# Raptor::Shaders — `raptor.shaders` (HLSL compilation via DXC). The Compiler is
# a third-party edge (DXC's wide-char API), so it uses std strings/containers;
# the public Types use Raptor types.

if(NOT TARGET Raptor::DXCHeaders)
    message(STATUS "Skipping Raptor::Shaders — DXC headers not available")
    return()
endif()

add_library(RaptorShaders STATIC)
add_library(Raptor::Shaders ALIAS RaptorShaders)

target_link_libraries(RaptorShaders
    PUBLIC Raptor::Core Raptor::DXCHeaders Raptor::Policy
    PRIVATE ${CMAKE_DL_LIBS})

target_sources(RaptorShaders
    PUBLIC
        FILE_SET shaders_modules TYPE CXX_MODULES FILES
            Types.cppm
            Flags.cppm
            Compiler.cppm
            ShadersModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# The shader system (variant compile-on-demand + cache) needs the RHI to create
# GPU modules, so it lives in its own module/library, gated on the RHI.
add_subdirectory(System)

# Shaders as resources (ShaderSource -> ShaderResource) + the editor asset side.
add_subdirectory(Resource)
add_subdirectory(Editor)

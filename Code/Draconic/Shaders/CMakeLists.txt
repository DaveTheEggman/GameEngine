# Draconic::Shaders - `draconic.shaders` (HLSL compilation via DXC). The Compiler is
# a third-party edge (DXC's wide-char API), so it uses std strings/containers;
# the public Types use Draconic types.

if(NOT TARGET Draconic::DXCHeaders)
    message(STATUS "Skipping Draconic::Shaders - DXC headers not available")
    return()
endif()

add_library(DraconicShaders STATIC)
add_library(Draconic::Shaders ALIAS DraconicShaders)

target_link_libraries(DraconicShaders
    PUBLIC Draconic::Core Draconic::DXCHeaders Draconic::Policy Draconic::ShaderCookTools
    PRIVATE ${CMAKE_DL_LIBS})

target_sources(DraconicShaders
    PUBLIC
        FILE_SET shaders_modules TYPE CXX_MODULES FILES
            Types.cppm
            Flags.cppm
            Variants.cppm
            Pack.cppm
            Compiler.cppm
            Cook/WgslTranslator.cppm
            ShadersModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# The shader system (variant compile-on-demand + cache) needs the RHI to create
# GPU modules, so it lives in its own module/library, gated on the RHI.
add_subdirectory(System)

# Shaders as resources (ShaderSource -> ShaderResource) + the editor asset side.
add_subdirectory(Resource)
add_subdirectory(Editor)

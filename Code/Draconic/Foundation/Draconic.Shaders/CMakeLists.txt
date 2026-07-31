# Draconic::Shaders - `draconic.shaders` (HLSL compilation via DXC). The Compiler is
# a third-party edge (DXC's wide-char API), so it uses std strings/containers;
# the public Types use Draconic types.

if(NOT TARGET Draconic::DXCHeaders)
    message(STATUS "Skipping Draconic::Shaders - DXC headers not available")
    return()
endif()

add_library(Draconic.Shaders STATIC)
add_library(Draconic::Shaders ALIAS Draconic.Shaders)

target_link_libraries(Draconic.Shaders
    PUBLIC Draconic::Core Draconic::DXCHeaders Draconic::Policy Draconic::ShaderCookTools
    PRIVATE ${CMAKE_DL_LIBS})

target_sources(Draconic.Shaders
    PUBLIC
        FILE_SET shaders_modules TYPE CXX_MODULES FILES
            Types.cppm
            Flags.cppm
            Variants.cppm
            Pack.cppm
            Compiler.cppm
            Cook/WgslTranslator.cppm
            Cook/ShaderPackCooker.cppm
            ShadersModule.cppm)

# The shader system (variant compile-on-demand + cache) needs the RHI to create
# GPU modules, so it lives in its own module/library, gated on the RHI.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Shaders.System ${CMAKE_CURRENT_BINARY_DIR}/System)

# Shaders as resources (ShaderSource -> ShaderResource) + the editor asset side.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Shaders.Resource ${CMAKE_CURRENT_BINARY_DIR}/Resource)
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Editor/Draconic.Shaders.Editor ${CMAKE_CURRENT_BINARY_DIR}/Editor)
endif()

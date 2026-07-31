# Draconic::Materials.Editor - `draconic.materials.editor` (tooling: MaterialAsset ->
# MaterialSource cook). Never linked by the runtime.
add_library(Draconic.Materials.Editor STATIC)
add_library(Draconic::Materials.Editor ALIAS Draconic.Materials.Editor)

target_link_libraries(Draconic.Materials.Editor
    PUBLIC Draconic::Core Draconic::Editor Draconic::Content
           Draconic::Materials Draconic::Materials.Resource Draconic::Policy)

target_sources(Draconic.Materials.Editor
    PUBLIC
        FILE_SET material_editor_modules TYPE CXX_MODULES FILES
            MaterialAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Materials.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()

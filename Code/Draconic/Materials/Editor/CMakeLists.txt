# Draconic::MaterialEditor - `draconic.materials.editor` (tooling: MaterialAsset ->
# MaterialSource cook). Never linked by the runtime.
add_library(DraconicMaterialEditor STATIC)
add_library(Draconic::MaterialEditor ALIAS DraconicMaterialEditor)

target_link_libraries(DraconicMaterialEditor
    PUBLIC Draconic::Core Draconic::Editor Draconic::Content
           Draconic::Materials Draconic::MaterialResource Draconic::Policy)

target_sources(DraconicMaterialEditor
    PUBLIC
        FILE_SET material_editor_modules TYPE CXX_MODULES FILES
            MaterialAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

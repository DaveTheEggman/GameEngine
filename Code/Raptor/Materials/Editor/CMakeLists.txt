# Raptor::MaterialEditor — `raptor.materials.editor` (tooling: MaterialAsset ->
# MaterialSource cook). Never linked by the runtime.
add_library(RaptorMaterialEditor STATIC)
add_library(Raptor::MaterialEditor ALIAS RaptorMaterialEditor)

target_link_libraries(RaptorMaterialEditor
    PUBLIC Raptor::Core Raptor::Editor Raptor::Content
           Raptor::Materials Raptor::MaterialResource Raptor::Policy)

target_sources(RaptorMaterialEditor
    PUBLIC
        FILE_SET material_editor_modules TYPE CXX_MODULES FILES
            MaterialAsset.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Draconic::SceneEditor - `draconic.scene.editor` (tooling: the scene SAVE/cook path).
# Never linked by the runtime.
add_library(DraconicSceneEditor STATIC)
add_library(Draconic::SceneEditor ALIAS DraconicSceneEditor)

target_link_libraries(DraconicSceneEditor
    PUBLIC Draconic::Core Draconic::Content Draconic::Scene Draconic::SceneResource Draconic::Policy)

target_sources(DraconicSceneEditor
    PUBLIC
        FILE_SET scene_editor_modules TYPE CXX_MODULES FILES
            SceneEditor.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

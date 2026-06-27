# Raptor::SceneEditor — `raptor.scene.editor` (tooling: the scene SAVE/cook path).
# Never linked by the runtime.
add_library(RaptorSceneEditor STATIC)
add_library(Raptor::SceneEditor ALIAS RaptorSceneEditor)

target_link_libraries(RaptorSceneEditor
    PUBLIC Raptor::Core Raptor::Content Raptor::Scene Raptor::SceneResource Raptor::Policy)

target_sources(RaptorSceneEditor
    PUBLIC
        FILE_SET scene_editor_modules TYPE CXX_MODULES FILES
            SceneEditor.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

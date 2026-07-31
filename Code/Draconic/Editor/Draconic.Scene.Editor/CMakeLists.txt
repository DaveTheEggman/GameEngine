# Draconic::SceneEditor - `draconic.scene.editor` (tooling: the scene SAVE/cook path).
# Never linked by the runtime.
add_library(Draconic.Scene.Editor STATIC)
add_library(Draconic::SceneEditor ALIAS Draconic.Scene.Editor)

target_link_libraries(Draconic.Scene.Editor
    PUBLIC Draconic::Core Draconic::Content Draconic::Scene Draconic::SceneResource Draconic::Policy
           Draconic::XmlSerialization)

target_sources(Draconic.Scene.Editor
    PUBLIC
        FILE_SET scene_editor_modules TYPE CXX_MODULES FILES
            SceneEditor.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Scene.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()

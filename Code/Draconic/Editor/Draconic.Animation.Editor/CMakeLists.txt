# Draconic::AnimationEditor - `draconic.animation.editor`: authoring assets + builders
# that cook skeletons/clips into the content DB. Tooling only (runtime loads cooked).
add_library(Draconic.Animation.Editor STATIC)
add_library(Draconic::AnimationEditor ALIAS Draconic.Animation.Editor)

target_link_libraries(Draconic.Animation.Editor
    PUBLIC Draconic::Core Draconic::Editor Draconic::Content
           Draconic::Animation Draconic::AnimationResource Draconic::Policy)

target_sources(Draconic.Animation.Editor
    PUBLIC
        FILE_SET animation_editor_modules TYPE CXX_MODULES FILES
            AnimationAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Animation.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()

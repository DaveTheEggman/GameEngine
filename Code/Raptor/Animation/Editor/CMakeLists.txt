# Raptor::AnimationEditor — `raptor.animation.editor`: authoring assets + builders
# that cook skeletons/clips into the content DB. Tooling only (runtime loads cooked).
add_library(RaptorAnimationEditor STATIC)
add_library(Raptor::AnimationEditor ALIAS RaptorAnimationEditor)

target_link_libraries(RaptorAnimationEditor
    PUBLIC Raptor::Core Raptor::Editor Raptor::Content
           Raptor::Animation Raptor::AnimationResource Raptor::Policy)

target_sources(RaptorAnimationEditor
    PUBLIC
        FILE_SET animation_editor_modules TYPE CXX_MODULES FILES
            AnimationAsset.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

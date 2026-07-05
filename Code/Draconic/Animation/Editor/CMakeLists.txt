# Draconic::AnimationEditor - `draconic.animation.editor`: authoring assets + builders
# that cook skeletons/clips into the content DB. Tooling only (runtime loads cooked).
add_library(DraconicAnimationEditor STATIC)
add_library(Draconic::AnimationEditor ALIAS DraconicAnimationEditor)

target_link_libraries(DraconicAnimationEditor
    PUBLIC Draconic::Core Draconic::Editor Draconic::Content
           Draconic::Animation Draconic::AnimationResource Draconic::Policy)

target_sources(DraconicAnimationEditor
    PUBLIC
        FILE_SET animation_editor_modules TYPE CXX_MODULES FILES
            AnimationAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Pipeline::PropertyAnimation - `propertyanimation.pipeline` (tooling: clip asset + builder).
add_library(PropertyAnimation.Pipeline STATIC)
add_library(Pipeline::PropertyAnimation ALIAS PropertyAnimation.Pipeline)

target_link_libraries(PropertyAnimation.Pipeline
    PUBLIC Foundation::Core Pipeline::Core Foundation::PropertyAnimation Foundation::PropertyAnimation.Resource
    PRIVATE Foundation::Policy)

target_sources(PropertyAnimation.Pipeline
    PUBLIC
        FILE_SET propertyanimation_pipeline_modules TYPE CXX_MODULES FILES
            PropertyAnimationAsset.cppm)

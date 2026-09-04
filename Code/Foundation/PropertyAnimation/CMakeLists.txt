# Foundation::PropertyAnimation - `foundation.propertyanimation`. The clip/track/curve data model +
# evaluation + the reflection binding resolver. Depends on Core reflection ONLY (deliberately NOT
# foundation.animation - a headless consumer evaluates a property clip without skeletal machinery).
util_add_engine_library(PropertyAnimation ALIAS Foundation::PropertyAnimation)
target_link_libraries(PropertyAnimation
    PUBLIC Foundation::Core
    PRIVATE Foundation::Policy)

target_sources(PropertyAnimation
    PRIVATE
        PropertyAnimationImpl.cpp
    PUBLIC
        FILE_SET propertyanimation_modules TYPE CXX_MODULES FILES
            PropertyAnimationModule.cppm)

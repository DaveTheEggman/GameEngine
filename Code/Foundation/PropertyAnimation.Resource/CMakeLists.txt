# Foundation::PropertyAnimation.Resource - `foundation.propertyanimation.resource`: the cooked
# property-animation clip as a runtime resource (Source -> Factory -> PropertyAnimationClipResource).
add_library(PropertyAnimation.Resource STATIC)
add_library(Foundation::PropertyAnimation.Resource ALIAS PropertyAnimation.Resource)

target_link_libraries(PropertyAnimation.Resource
    PUBLIC Foundation::Core Foundation::Resource Foundation::Content Foundation::PropertyAnimation
    PRIVATE Foundation::Policy)

target_sources(PropertyAnimation.Resource
    PUBLIC
        FILE_SET propertyanimation_resource_modules TYPE CXX_MODULES FILES
            PropertyAnimationResource.cppm)

# Foundation::Input.Resource - `foundation.input.resource`: cooked input map + factory.
add_library(Input.Resource STATIC)
add_library(Foundation::Input.Resource ALIAS Input.Resource)

target_link_libraries(Input.Resource
    PUBLIC Foundation::Core Foundation::Content Foundation::Resource Foundation::Input Foundation::Policy)

target_sources(Input.Resource
    PUBLIC
        FILE_SET input_resource_modules TYPE CXX_MODULES FILES
            InputResource.cppm)

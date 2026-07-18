# Draconic::InputResource - `draconic.input.resource`: cooked input map + factory.
add_library(DraconicInputResource STATIC)
add_library(Draconic::InputResource ALIAS DraconicInputResource)

target_link_libraries(DraconicInputResource
    PUBLIC Draconic::Core Draconic::Content Draconic::Resource Draconic::Input Draconic::Policy)

target_sources(DraconicInputResource
    PUBLIC
        FILE_SET input_resource_modules TYPE CXX_MODULES FILES
            InputResource.cppm)

# Foundation::Resource - `draconic.resource` (resource manager: source->product via
# factories, replaceable handles + proxies, caching + hot reload over content).

add_library(Resource STATIC)
add_library(Foundation::Resource ALIAS Resource)

target_link_libraries(Resource PUBLIC Foundation::Core Foundation::Content Foundation::Policy)

target_sources(Resource
    PUBLIC
        FILE_SET resource_modules TYPE CXX_MODULES FILES
            ResourceModule.cppm)


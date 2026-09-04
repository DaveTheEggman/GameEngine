# Foundation::Resource - `foundation.resource` (resource manager: source->product via
# factories, replaceable handles + proxies, caching + hot reload over content).

util_add_engine_library(Resource ALIAS Foundation::Resource)
target_link_libraries(Resource PUBLIC Foundation::Core Foundation::Content Foundation::Policy)

target_sources(Resource
    PUBLIC
        FILE_SET resource_modules TYPE CXX_MODULES FILES
            ResourceModule.cppm)


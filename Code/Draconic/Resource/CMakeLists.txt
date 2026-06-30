# Draconic::Resource — `draconic.resource` (resource manager: source->product via
# factories, replaceable handles + proxies, caching + hot reload over content).

add_library(DraconicResource STATIC)
add_library(Draconic::Resource ALIAS DraconicResource)

target_link_libraries(DraconicResource PUBLIC Draconic::Core Draconic::Content Draconic::Policy)

target_sources(DraconicResource
    PUBLIC
        FILE_SET resource_modules TYPE CXX_MODULES FILES
            ResourceModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

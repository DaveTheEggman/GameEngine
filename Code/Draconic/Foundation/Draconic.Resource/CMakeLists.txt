# Draconic::Resource - `draconic.resource` (resource manager: source->product via
# factories, replaceable handles + proxies, caching + hot reload over content).

add_library(Draconic.Resource STATIC)
add_library(Draconic::Resource ALIAS Draconic.Resource)

target_link_libraries(Draconic.Resource PUBLIC Draconic::Core Draconic::Content Draconic::Policy)

target_sources(Draconic.Resource
    PUBLIC
        FILE_SET resource_modules TYPE CXX_MODULES FILES
            ResourceModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Resource.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()

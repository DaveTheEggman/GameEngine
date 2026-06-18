# Raptor::Resource — `raptor.resource` (resource manager: source->product via
# factories, replaceable handles + proxies, caching + hot reload over content).

add_library(RaptorResource STATIC)
add_library(Raptor::Resource ALIAS RaptorResource)

target_link_libraries(RaptorResource PUBLIC Raptor::Core Raptor::Content Raptor::Policy)

target_sources(RaptorResource
    PUBLIC
        FILE_SET resource_modules TYPE CXX_MODULES FILES
            ResourceModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

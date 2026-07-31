# Draconic::ScriptResource - `draconic.script.resource` (cooked script classes + factory).
add_library(Draconic.Script.Resource STATIC)
add_library(Draconic::ScriptResource ALIAS Draconic.Script.Resource)
target_link_libraries(Draconic.Script.Resource PUBLIC
    Draconic::Core Draconic::Resource Draconic::Content Draconic::Policy)
target_sources(Draconic.Script.Resource PUBLIC FILE_SET script_resource_modules TYPE CXX_MODULES FILES ScriptResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Script.Resource.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()

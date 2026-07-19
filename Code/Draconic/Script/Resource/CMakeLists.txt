# Draconic::ScriptResource - `draconic.script.resource` (cooked script classes + factory).
add_library(DraconicScriptResource STATIC)
add_library(Draconic::ScriptResource ALIAS DraconicScriptResource)
target_link_libraries(DraconicScriptResource PUBLIC
    Draconic::Core Draconic::Resource Draconic::Content Draconic::Policy)
target_sources(DraconicScriptResource PUBLIC FILE_SET script_resource_modules TYPE CXX_MODULES FILES ScriptResource.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

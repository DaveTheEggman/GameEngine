# Foundation::Script.Resource - `draconic.script.resource` (cooked script classes + factory).
add_library(Script.Resource STATIC)
add_library(Foundation::Script.Resource ALIAS Script.Resource)
target_link_libraries(Script.Resource PUBLIC
    Foundation::Core Foundation::Resource Foundation::Content Foundation::Policy)
target_sources(Script.Resource PUBLIC FILE_SET script_resource_modules TYPE CXX_MODULES FILES ScriptResource.cppm)


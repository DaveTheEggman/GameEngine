# Foundation::Script - the `foundation.script` module.
#
# A thin, VM-agnostic scripting abstraction over Core's reflection. Concrete VM
# backends (Lua, ...) are separate plugins implementing these interfaces.

add_library(Script STATIC)
add_library(Foundation::Script ALIAS Script)

# PUBLIC Foundation::Core: propagates the reflection ABI + the Code include
# root (so module GMFs can #include "Core/Prelude.h").
target_link_libraries(Script PUBLIC Foundation::Core Foundation::Policy)

target_sources(Script
    PUBLIC
        FILE_SET script_modules TYPE CXX_MODULES FILES
            ScriptModule.cppm
            IScriptDelegate.cppm
            ScriptIntrospection.cppm
            ScriptDebug.cppm
            IScriptContext.cppm
            IScriptManager.cppm
            RegisterTypes.cppm
            BackendRegistry.cppm)

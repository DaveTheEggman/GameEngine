# Raptor::Script — the `raptor.script` module.
#
# A thin, VM-agnostic scripting abstraction over Core's reflection. Concrete VM
# backends (Lua, ...) are separate plugins implementing these interfaces.

add_library(RaptorScript STATIC)
add_library(Raptor::Script ALIAS RaptorScript)

# PUBLIC Raptor::Core: propagates the reflection ABI + the Code/Raptor include
# root (so module GMFs can #include "Core/Prelude.h").
target_link_libraries(RaptorScript PUBLIC Raptor::Core Raptor::Policy)

target_sources(RaptorScript
    PUBLIC
        FILE_SET script_modules TYPE CXX_MODULES FILES
            ScriptModule.cppm
            IScriptContext.cppm
            IScriptManager.cppm
            RegisterTypes.cppm)

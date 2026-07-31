# Draconic::Script - the `draconic.script` module.
#
# A thin, VM-agnostic scripting abstraction over Core's reflection. Concrete VM
# backends (Lua, ...) are separate plugins implementing these interfaces.

add_library(Draconic.Script STATIC)
add_library(Draconic::Script ALIAS Draconic.Script)

# PUBLIC Draconic::Core: propagates the reflection ABI + the Code/Draconic include
# root (so module GMFs can #include "Core/Prelude.h").
target_link_libraries(Draconic.Script PUBLIC Draconic::Core Draconic::Policy)

target_sources(Draconic.Script
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

# VM backends. Wren is vendored+compiled here; the AngelScript backend links the vendored
# Draconic::AngelScript third-party, which is gated off for Emscripten (its native-call trampolines
# are not wired for wasm) - so the backend only builds where that target exists. ThirdParty is always
# configured first, so this TARGET guard is order-safe.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Script.Wren ${CMAKE_CURRENT_BINARY_DIR}/Wren)
if(TARGET Draconic::AngelScript)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Script.AngelScript ${CMAKE_CURRENT_BINARY_DIR}/AngelScript)
endif()

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Script.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()

# Draconic::RuntimeScript - `draconic.runtime.script` (ScriptSubsystem: scripting in
# the runtime). Bridges Draconic::Runtime and Draconic::Script; the VM backend is
# injected at runtime, so this does not depend on any concrete backend.

add_library(DraconicRuntimeScript STATIC)
add_library(Draconic::RuntimeScript ALIAS DraconicRuntimeScript)

target_link_libraries(DraconicRuntimeScript PUBLIC Draconic::Runtime Draconic::Script Draconic::Policy)

target_sources(DraconicRuntimeScript
    PUBLIC
        FILE_SET runtime_script_modules TYPE CXX_MODULES FILES
            ScriptSubsystem.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

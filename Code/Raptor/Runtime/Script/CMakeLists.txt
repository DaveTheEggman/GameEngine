# Raptor::RuntimeScript — `raptor.runtime.script` (ScriptSubsystem: scripting in
# the runtime). Bridges Raptor::Runtime and Raptor::Script; the VM backend is
# injected at runtime, so this does not depend on any concrete backend.

add_library(RaptorRuntimeScript STATIC)
add_library(Raptor::RuntimeScript ALIAS RaptorRuntimeScript)

target_link_libraries(RaptorRuntimeScript PUBLIC Raptor::Runtime Raptor::Script Raptor::Policy)

target_sources(RaptorRuntimeScript
    PUBLIC
        FILE_SET runtime_script_modules TYPE CXX_MODULES FILES
            ScriptSubsystem.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

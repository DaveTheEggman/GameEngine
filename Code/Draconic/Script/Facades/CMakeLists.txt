# Draconic::ScriptFacades - `draconic.script.facades` (the curated behavior facades:
# Entity/Log/Time/Random + the run-service binding). Shared by the runtime SUBSYSTEM
# and the editor's COOK so both VMs register the same "main"-module surface.
add_library(DraconicScriptFacades STATIC)
add_library(Draconic::ScriptFacades ALIAS DraconicScriptFacades)
target_link_libraries(DraconicScriptFacades PUBLIC
    Draconic::Core Draconic::Scene Draconic::Script Draconic::Policy)
target_sources(DraconicScriptFacades
    PRIVATE
        ScriptFacadesImpl.cpp   # implementation unit: the DRACONIC_REFLECT_* bodies (GCC gcm rule)
    PUBLIC
        FILE_SET script_facades_modules TYPE CXX_MODULES FILES ScriptFacades.cppm)

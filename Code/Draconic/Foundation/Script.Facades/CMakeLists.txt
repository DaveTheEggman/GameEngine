# Foundation::Script.Facades - `draconic.script.facades` (the curated behavior facades:
# Entity/Log/Time/Random + the run-service binding). Shared by the runtime SUBSYSTEM
# and the editor's COOK so both VMs register the same "main"-module surface.
add_library(Script.Facades STATIC)
add_library(Foundation::Script.Facades ALIAS Script.Facades)
target_link_libraries(Script.Facades PUBLIC
    Foundation::Core Foundation::Scene Foundation::Script Foundation::Policy)
target_sources(Script.Facades
    PRIVATE
        ScriptFacadesImpl.cpp   # implementation unit: the DRACONIC_REFLECT_* bodies (GCC gcm rule)
    PUBLIC
        FILE_SET script_facades_modules TYPE CXX_MODULES FILES ScriptFacades.cppm)

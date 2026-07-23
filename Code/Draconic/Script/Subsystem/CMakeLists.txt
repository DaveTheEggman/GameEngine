# Draconic::ScriptSubsystem - `draconic.script.subsystem`: entity behaviors
# (ScriptComponent + per-scene dispatch + the run's ONE gameplay script context).
# Backend-neutral: links Draconic::Script for the REGISTRY only - entry points
# register concrete backends.
add_library(DraconicScriptSubsystem STATIC)
add_library(Draconic::ScriptSubsystem ALIAS DraconicScriptSubsystem)

target_link_libraries(DraconicScriptSubsystem
    PUBLIC Draconic::Core Draconic::Runtime Draconic::Scene Draconic::SceneSubsystem
           Draconic::Script Draconic::ScriptResource Draconic::ScriptFacades
           Draconic::Resource Draconic::Content
           Draconic::Profiler Draconic::Policy)

target_sources(DraconicScriptSubsystem
    PRIVATE
        ScriptSubsystemImpl.cpp   # implementation unit: the DRACONIC_REFLECT_* bodies (GCC gcm rule)
    PUBLIC
        FILE_SET script_subsystem_modules TYPE CXX_MODULES FILES
            ScriptSubsystem.cppm
            ScriptComponents.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

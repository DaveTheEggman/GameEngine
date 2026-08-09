# Engine::Script - `engine.script`: entity behaviors
# (ScriptComponent + per-scene dispatch + the run's ONE gameplay script context).
# Backend-neutral: links Foundation::Script for the REGISTRY only - entry points
# register concrete backends.
add_library(Engine.Script STATIC)
add_library(Engine::Script ALIAS Engine.Script)

target_link_libraries(Engine.Script
    PUBLIC Foundation::Core Foundation::Runtime Foundation::Scene Engine::Scene
           Foundation::Script Foundation::Script.Resource Foundation::Script.Facades
           Foundation::Resource Foundation::Content
           Foundation::Profiler Foundation::Policy)

target_sources(Engine.Script
    PRIVATE
        ScriptSubsystemImpl.cpp   # implementation unit: the REFLECT_* bodies (GCC gcm rule)
    PUBLIC
        FILE_SET script_subsystem_modules TYPE CXX_MODULES FILES
            ScriptSubsystem.cppm
            ScriptComponents.cppm)


# Draconic::ScriptSubsystem - `draconic.engine.script`: entity behaviors
# (ScriptComponent + per-scene dispatch + the run's ONE gameplay script context).
# Backend-neutral: links Draconic::Script for the REGISTRY only - entry points
# register concrete backends.
add_library(Draconic.Engine.Script STATIC)
add_library(Draconic::ScriptSubsystem ALIAS Draconic.Engine.Script)

target_link_libraries(Draconic.Engine.Script
    PUBLIC Draconic::Core Draconic::Runtime Draconic::Scene Draconic::SceneSubsystem
           Draconic::Script Draconic::ScriptResource Draconic::ScriptFacades
           Draconic::Resource Draconic::Content
           Draconic::Profiler Draconic::Policy)

target_sources(Draconic.Engine.Script
    PRIVATE
        ScriptSubsystemImpl.cpp   # implementation unit: the DRACONIC_REFLECT_* bodies (GCC gcm rule)
    PUBLIC
        FILE_SET script_subsystem_modules TYPE CXX_MODULES FILES
            ScriptSubsystem.cppm
            ScriptComponents.cppm)

# Tests link the AngelScript backend, which is not built for web - desktop-only.
if(DRACONIC_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Engine.Script.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()

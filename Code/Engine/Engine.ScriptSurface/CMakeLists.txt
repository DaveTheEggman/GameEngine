# Engine::ScriptSurface - `engine.scriptsurface`: the script-surface composition root. Links every
# subsystem that contributes a script facade and exposes RegisterAllScriptFacades() - the complete
# bound scripting surface. EVERY host calls it: surface-describing hosts (MCP, cook, script_api)
# AND the runtime (DefaultApplication) - a hand-rolled per-subsystem list in the runtime once
# drifted past the run facade, so exported games could not resolve `run` (week-2026-08-22).
# Metadata only: it never instantiates a subsystem, so it is headless (the subsystem libs are
# headless-linkable - RHI.Null, audio Null mode).

util_add_engine_library(Engine.ScriptSurface ALIAS Engine::ScriptSurface)
target_link_libraries(Engine.ScriptSurface PUBLIC
    Foundation::Core Foundation::Script.Facades
    Engine::Physics Engine::Navigation Engine::Audio Engine::Input Engine::UI Engine::UI.Script Engine::Render
    Engine::Particles Engine::Animation Engine::Spline Engine::GameInstance
    Foundation::Net.Manager Foundation::Net.Replication
    Foundation::Policy)

target_sources(Engine.ScriptSurface
    PRIVATE
        ScriptSurfaceImpl.cpp
    PUBLIC
        FILE_SET engine_scriptsurface_modules TYPE CXX_MODULES FILES
            ScriptSurface.cppm)

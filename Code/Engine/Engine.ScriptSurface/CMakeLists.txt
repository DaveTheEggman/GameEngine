# Engine::ScriptSurface - `engine.scriptsurface`: the script-surface composition root. Links every
# subsystem that contributes a script facade and exposes RegisterAllScriptFacades() - the complete
# bound scripting surface for surface-DESCRIBING hosts (MCP, a future API-browser export). Metadata
# only: it never instantiates a subsystem, so it is headless (the subsystem libs are headless-
# linkable - RHI.Null, audio Null mode). NOT for the runtime, which registers per-subsystem.

add_library(Engine.ScriptSurface STATIC)
add_library(Engine::ScriptSurface ALIAS Engine.ScriptSurface)

target_link_libraries(Engine.ScriptSurface PUBLIC
    Foundation::Core Foundation::Script.Facades
    Engine::Physics Engine::Navigation Engine::Audio Engine::Input Engine::UI Engine::Render
    Engine::Particles Engine::Animation Engine::GameInstance
    Foundation::Net.Manager Foundation::Net.Replication
    Foundation::Policy)

target_sources(Engine.ScriptSurface
    PRIVATE
        ScriptSurfaceImpl.cpp
    PUBLIC
        FILE_SET engine_scriptsurface_modules TYPE CXX_MODULES FILES
            ScriptSurface.cppm)

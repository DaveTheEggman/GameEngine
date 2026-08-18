# Engine::SceneSurface - `engine.scenesurface`: the scene-surface composition root (the
# serialization sibling of Engine::ScriptSurface). Aggregates every domain's
# Add<Domain>SceneManagers - the SAME functions the subsystems' OnSceneCreated delegate to - so
# headless scene consumers (export transcode, MCP scene_validate) deserialize with the full
# manager set and can never drift from the runtime injection. Headless-safe: managers are value
# pools; the settings systems construct inert.

add_library(Engine.SceneSurface STATIC)
add_library(Engine::SceneSurface ALIAS Engine.SceneSurface)

target_link_libraries(Engine.SceneSurface PUBLIC
    Foundation::Core Foundation::Scene
    Engine::Render Engine::Animation Engine::Particles Engine::Physics Engine::Navigation
    Engine::Audio Engine::Script Engine::UI
    Foundation::Net.Replication
    Foundation::Policy)

target_sources(Engine.SceneSurface
    PRIVATE
        SceneSurfaceImpl.cpp
    PUBLIC
        FILE_SET engine_scenesurface_modules TYPE CXX_MODULES FILES
            SceneSurface.cppm)

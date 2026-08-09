# Engine::DefaultApp - `draconic.engine.defaultapp` (DefaultApplication: the
# opinionated IApplication that registers the standard engine subsystems). Kept in
# its own library so the base client (Foundation::Runtime.Client) never pulls in the
# engine subsystem libraries - only apps that opt into the defaults link this.
# As engine subsystems land, add them as link dependencies HERE.

add_library(Engine.DefaultApp STATIC)
add_library(Engine::DefaultApp ALIAS Engine.DefaultApp)

target_link_libraries(Engine.DefaultApp
    PUBLIC Foundation::Core Foundation::RHI Foundation::Runtime.Client Engine::GameInstance Foundation::Shell Foundation::Graphics
           Foundation::Scene Engine::Scene Engine::Render Engine::Animation
           Engine::Particles Foundation::Physics Engine::Physics
           Foundation::Audio Foundation::Audio.Resource Engine::Audio
           Foundation::Input Engine::Input Foundation::Script
           Foundation::Script.Resource Engine::Script Engine::Integration
           Foundation::Resource Foundation::Content Foundation::Scene.Resource
           Foundation::Geometry.Resource Foundation::Materials.Resource Foundation::Animation.Resource
           Foundation::Particles.Resource Foundation::Input.Resource Foundation::Physics.Resource
           Foundation::Texture.Resource Foundation::Image.Resource Foundation::Model.Resource Foundation::UI.Resource Engine::UI
           Foundation::Net Foundation::Net.Manager Engine::Net
           Foundation::Profiler Foundation::Policy)

# Script VM backends - Wren (primary) and AngelScript (second). Each is independently toggleable via
# its option; a run resolves the backend by the game script's LANGUAGE, so with both ON a project can
# ship either. Both build on every platform now (AngelScript climbs to web via as_config.h's
# AS_MAX_PORTABILITY). Linked by forward-referenced name (resolves at generate time), so ordering vs
# the Script subdir - which configures AFTER Runtime on the web branch - does not matter. Turn one OFF
# to drop that backend from the player/app entirely; keep at least one for scripted games.
option(OPTION_ENABLE_WREN "Build the Wren script backend into DefaultApplication" ON)
option(OPTION_ENABLE_ANGELSCRIPT "Build the AngelScript script backend into DefaultApplication" ON)
if(OPTION_ENABLE_WREN)
    target_link_libraries(Engine.DefaultApp PUBLIC Foundation::Script.Wren)
    target_compile_definitions(Engine.DefaultApp PUBLIC OPTION_HAS_WREN)
endif()
if(OPTION_ENABLE_ANGELSCRIPT)
    target_link_libraries(Engine.DefaultApp PUBLIC Foundation::Script.AngelScript)
    target_compile_definitions(Engine.DefaultApp PUBLIC OPTION_HAS_ANGELSCRIPT)
endif()

target_sources(Engine.DefaultApp
    PRIVATE
        DefaultApplicationImpl.cpp   # out-of-line defs (see DefaultApplication.cppm)
    PUBLIC
        FILE_SET runtime_defaultapp_modules TYPE CXX_MODULES FILES
            DefaultApplication.cppm)

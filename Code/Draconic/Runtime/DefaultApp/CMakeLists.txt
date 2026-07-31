# Draconic::RuntimeDefaultApp - `draconic.runtime.defaultapp` (DefaultApplication: the
# opinionated IApplication that registers the standard engine subsystems). Kept in
# its own library so the base client (Draconic::RuntimeClient) never pulls in the
# engine subsystem libraries - only apps that opt into the defaults link this.
# As engine subsystems land, add them as link dependencies HERE.

add_library(DraconicRuntimeDefaultApp STATIC)
add_library(Draconic::RuntimeDefaultApp ALIAS DraconicRuntimeDefaultApp)

target_link_libraries(DraconicRuntimeDefaultApp
    PUBLIC Draconic::Core Draconic::RHI Draconic::RuntimeClient Draconic::RuntimeGameInstance Draconic::Shell Draconic::Graphics
           Draconic::Scene Draconic::SceneSubsystem Draconic::RenderSubsystem Draconic::AnimationSubsystem
           Draconic::ParticlesSubsystem Draconic::Physics Draconic::PhysicsSubsystem
           Draconic::Audio Draconic::AudioResource Draconic::AudioSubsystem
           Draconic::Input Draconic::InputSubsystem Draconic::Script
           Draconic::ScriptResource Draconic::ScriptSubsystem
           Draconic::Resource Draconic::Content Draconic::SceneResource
           Draconic::MeshResource Draconic::MaterialResource Draconic::AnimationResource
           Draconic::ParticlesResource Draconic::InputResource Draconic::PhysicsResource
           Draconic::TextureResource Draconic::ImageResource Draconic::ModelResource Draconic::UIResource Draconic::UISubsystem
           Draconic::Net Draconic::NetManager Draconic::NetSubsystem
           Draconic::Profiler Draconic::Policy)

# Script VM backends - Wren (primary) and AngelScript (second). Each is independently toggleable via
# its option; a run resolves the backend by the game script's LANGUAGE, so with both ON a project can
# ship either. Both build on every platform now (AngelScript climbs to web via as_config.h's
# AS_MAX_PORTABILITY). Linked by forward-referenced name (resolves at generate time), so ordering vs
# the Script subdir - which configures AFTER Runtime on the web branch - does not matter. Turn one OFF
# to drop that backend from the player/app entirely; keep at least one for scripted games.
option(DRACONIC_ENABLE_WREN "Build the Wren script backend into DefaultApplication" ON)
option(DRACONIC_ENABLE_ANGELSCRIPT "Build the AngelScript script backend into DefaultApplication" ON)
if(DRACONIC_ENABLE_WREN)
    target_link_libraries(DraconicRuntimeDefaultApp PUBLIC Draconic::ScriptWren)
    target_compile_definitions(DraconicRuntimeDefaultApp PUBLIC DRACONIC_HAS_WREN)
endif()
if(DRACONIC_ENABLE_ANGELSCRIPT)
    target_link_libraries(DraconicRuntimeDefaultApp PUBLIC Draconic::ScriptAngelScript)
    target_compile_definitions(DraconicRuntimeDefaultApp PUBLIC DRACONIC_HAS_ANGELSCRIPT)
endif()

target_sources(DraconicRuntimeDefaultApp
    PRIVATE
        DefaultApplicationImpl.cpp   # out-of-line defs (see DefaultApplication.cppm)
    PUBLIC
        FILE_SET runtime_defaultapp_modules TYPE CXX_MODULES FILES
            DefaultApplication.cppm)

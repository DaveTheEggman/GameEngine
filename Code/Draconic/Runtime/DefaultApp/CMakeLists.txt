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
           Draconic::Input Draconic::InputSubsystem Draconic::Script Draconic::ScriptWren
           Draconic::ScriptAngelScript
           Draconic::ScriptResource Draconic::ScriptSubsystem
           Draconic::Resource Draconic::Content Draconic::SceneResource
           Draconic::MeshResource Draconic::MaterialResource Draconic::AnimationResource
           Draconic::ParticlesResource Draconic::InputResource Draconic::PhysicsResource
           Draconic::TextureResource Draconic::ImageResource Draconic::ModelResource Draconic::UIResource Draconic::UISubsystem
           Draconic::Net Draconic::NetManager Draconic::NetSubsystem
           Draconic::Profiler Draconic::Policy)

# AngelScript is the opt-in SECOND script backend (Wren stays primary everywhere). It builds on every
# platform now - desktop, and web via as_config.h auto-enabling AS_MAX_PORTABILITY on __EMSCRIPTEN__
# (the backend is all-generic). Linked unconditionally alongside Wren above; this flag gates the
# import + RegisterAngelScriptBackend() in DefaultApplication. NOT a TARGET guard: DefaultApp is
# configured before the Script subdir on the web branch, so an immediate if(TARGET) would miss it,
# whereas forward-referenced link names (like Draconic::ScriptWren above) resolve at generate time.
target_compile_definitions(DraconicRuntimeDefaultApp PUBLIC DRACONIC_HAS_ANGELSCRIPT)

target_sources(DraconicRuntimeDefaultApp
    PRIVATE
        DefaultApplicationImpl.cpp   # out-of-line defs (see DefaultApplication.cppm)
    PUBLIC
        FILE_SET runtime_defaultapp_modules TYPE CXX_MODULES FILES
            DefaultApplication.cppm)

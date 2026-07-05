# Draconic::RuntimeDefaultApp — `draconic.runtime.defaultapp` (DefaultApplication: the
# opinionated IApplication that registers the standard engine subsystems). Kept in
# its own library so the base client (Draconic::RuntimeClient) never pulls in the
# engine subsystem libraries — only apps that opt into the defaults link this.
# As engine subsystems land, add them as link dependencies HERE.

add_library(DraconicRuntimeDefaultApp STATIC)
add_library(Draconic::RuntimeDefaultApp ALIAS DraconicRuntimeDefaultApp)

target_link_libraries(DraconicRuntimeDefaultApp
    PUBLIC Draconic::Core Draconic::RHI Draconic::RuntimeClient Draconic::Shell Draconic::Graphics
           Draconic::Scene Draconic::SceneSubsystem Draconic::RenderSubsystem Draconic::AnimationSubsystem
           Draconic::Profiler Draconic::Policy)

target_sources(DraconicRuntimeDefaultApp
    PUBLIC
        FILE_SET runtime_defaultapp_modules TYPE CXX_MODULES FILES
            DefaultApplication.cppm)

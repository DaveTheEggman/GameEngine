# Sandbox — the running dev harness: a windowed DefaultApplication with a scene we
# grow the engine in. Needs the desktop platform + Vulkan GPU factory (to present)
# plus the default-app + scene libraries.

add_executable(Sandbox main.cpp)

target_link_libraries(Sandbox PRIVATE
    Raptor::Core
    Raptor::Runtime
    Raptor::RuntimeClient
    Raptor::RuntimePlatformDesktop
    Raptor::RuntimeGraphics
    Raptor::RuntimeGraphicsGpu
    Raptor::RuntimeDefaultApp
    Raptor::Scene
    Raptor::SceneSubsystem
    Raptor::RenderSubsystem
    Raptor::Geometry
    Raptor::Materials
    Raptor::Policy)
raptor_copy_sdl3_dll(Sandbox)

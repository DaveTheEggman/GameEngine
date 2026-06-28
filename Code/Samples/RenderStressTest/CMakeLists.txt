# RenderStressTest — a worst-case renderer benchmark (grid of all-visible spheres, shared/unique
# materials, sin-wave bob) we use to keep performance honest as rendering features land. Same
# dependency set as the Sandbox: desktop platform + Vulkan GPU factory + default-app/scene/render.

add_executable(RenderStressTest main.cpp)

target_link_libraries(RenderStressTest PRIVATE
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
raptor_copy_sdl3_dll(RenderStressTest)

# RenderStressTest — a worst-case renderer benchmark (grid of all-visible spheres, shared/unique
# materials, sin-wave bob) we use to keep performance honest as rendering features land. Same
# dependency set as the Sandbox: desktop shell + Vulkan GPU factory + default-app/scene/render.

add_executable(RenderStressTest main.cpp)

target_link_libraries(RenderStressTest PRIVATE
    Draconic::Core
    Draconic::Runtime
    Draconic::RuntimeClient
    Draconic::ShellDesktop Draconic::RuntimeDesktop
    Draconic::Graphics
    Draconic::GraphicsGpu
    Draconic::RuntimeDefaultApp
    Draconic::Scene
    Draconic::SceneSubsystem
    Draconic::RenderSubsystem
    Draconic::Imgui
    Draconic::Geometry
    Draconic::Materials
    Draconic::Policy)
draconic_copy_sdl3_dll(RenderStressTest)

# PhysicsPlayground - physics P1 consumer proof: crates/trigger/sweeper/raycast shove on
# the Jolt-backed subsystem, drawn via the physics debug wireframes.
add_executable(PhysicsPlayground main.cpp)

target_link_libraries(PhysicsPlayground PRIVATE
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
    Draconic::Physics Draconic::PhysicsResource Draconic::PhysicsSubsystem
    Draconic::UI Draconic::UIResource Draconic::UISubsystem
    Draconic::Imgui
    Draconic::Policy)
draconic_copy_sdl3_dll(PhysicsPlayground)

# PhysicsPlayground - physics P1 consumer proof: crates/trigger/sweeper/raycast shove on
# the Jolt-backed subsystem, drawn via the physics debug wireframes.
add_executable(PhysicsPlayground main.cpp)

target_link_libraries(PhysicsPlayground PRIVATE
    Foundation::Core
    Foundation::Runtime
    Foundation::Runtime.Client
    Foundation::Shell.Desktop Foundation::Runtime.Desktop
    Foundation::Graphics
    Foundation::Graphics.Gpu
    Engine::DefaultApp
    Foundation::Scene
    Engine::Scene
    Engine::Render
    Foundation::Physics Foundation::Physics.Resource Engine::Physics
    Foundation::UI Foundation::UI.Resource Engine::UI
    Extensions::Imgui
    Foundation::Policy)
draconic_copy_sdl3_dll(PhysicsPlayground)

target_compile_definitions(PhysicsPlayground PRIVATE
    DRACONIC_PLAYGROUND_FONT="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf")

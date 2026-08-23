# TerrainPlayground - the terrain showcase (in-code heightfield -> engine.terrain's chunked geo-mipmap
# renderer, with an ImGui HUD for the sun / LOD bias / heightfield regen). No pipeline: the terrain is
# an in-memory resource (Ref<> direct products), so this needs only the runtime host set + engine.terrain.

add_executable(TerrainPlayground main.cpp)

target_link_libraries(TerrainPlayground PRIVATE
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
    Foundation::Render
    Extensions::Imgui
    Foundation::Heightfield
    Foundation::Terrain.Resource
    Engine::Terrain
    Foundation::Policy)

util_copy_sdl3_dll(TerrainPlayground)

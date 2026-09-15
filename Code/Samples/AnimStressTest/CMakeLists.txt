# AnimStressTest - the running dev harness: a windowed DefaultApplication with a scene we
# grow the engine in. Needs the desktop shell + Vulkan GPU factory (to present)
# plus the default-app + scene libraries.

add_executable(AnimStressTest main.cpp)

target_link_libraries(AnimStressTest PRIVATE
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
    Extensions::Imgui
    Foundation::Geometry
    Foundation::Materials
    Pipeline::ModelImporter
    Foundation::Model
    Foundation::Content
    Foundation::Resource
    Foundation::VFS
    Foundation::Geometry.Resource
    Foundation::Materials.Resource
    Foundation::Texture.Resource
    Foundation::Animation
    Engine::Animation
    Foundation::Profiler
    Foundation::Policy)

util_copy_sdl3_dll(AnimStressTest)

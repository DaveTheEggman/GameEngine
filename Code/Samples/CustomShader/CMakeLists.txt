# CustomShader - a low-level custom shader end to end: a toon shader registered by name, a material
# built in code whose properties lay out its set-2 block, drawn by the material-driven renderer.
add_executable(CustomShader main.cpp)

target_link_libraries(CustomShader PRIVATE
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
    Foundation::Geometry
    Foundation::Materials
    Foundation::Shaders
    Foundation::Shaders.System
    Foundation::VFS
    Engine::Composition
    Pipeline::ModelImporter
    Foundation::Model
    Foundation::Content
    Foundation::Resource
    Foundation::Geometry.Resource
    Foundation::Materials.Resource
    Foundation::Texture.Resource
    Foundation::Animation
    Engine::Animation
    Foundation::Policy)
util_copy_sdl3_dll(CustomShader)

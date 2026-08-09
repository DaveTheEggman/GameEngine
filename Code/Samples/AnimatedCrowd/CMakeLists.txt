# AnimatedCrowd - the running dev harness: a windowed DefaultApplication with a scene we
# grow the engine in. Needs the desktop shell + Vulkan GPU factory (to present)
# plus the default-app + scene libraries.

add_executable(AnimatedCrowd main.cpp)

target_link_libraries(AnimatedCrowd PRIVATE
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

# Absolute asset paths (cwd-independent), mirroring the VGSandbox font-path pattern:
# the raw model files (read by the loaders) + the cooked-resource output database.
target_compile_definitions(AnimatedCrowd PRIVATE
    SAMPLE_SANDBOX_MODEL_DIR="${CMAKE_SOURCE_DIR}/Data/Assets/models"
    SAMPLE_SANDBOX_OUTPUT_DIR="${CMAKE_SOURCE_DIR}/Data/Output/AnimatedCrowd")   # per-sample cooked-output DB (isolated from other samples)

util_copy_sdl3_dll(AnimatedCrowd)

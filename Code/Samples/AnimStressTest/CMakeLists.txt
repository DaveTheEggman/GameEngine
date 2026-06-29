# AnimStressTest — the running dev harness: a windowed DefaultApplication with a scene we
# grow the engine in. Needs the desktop platform + Vulkan GPU factory (to present)
# plus the default-app + scene libraries.

add_executable(AnimStressTest main.cpp)

target_link_libraries(AnimStressTest PRIVATE
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
    Raptor::ModelImporter
    Raptor::Model
    Raptor::Content
    Raptor::Resource
    Raptor::VFS
    Raptor::MeshResource
    Raptor::MaterialResource
    Raptor::TextureResource
    Raptor::Animation
    Raptor::Profiler
    Raptor::Policy)

# Absolute asset paths (cwd-independent), mirroring the VGSandbox font-path pattern:
# the raw model files (read by the loaders) + the cooked-resource output database.
target_compile_definitions(AnimStressTest PRIVATE
    RAPTOR_SANDBOX_MODEL_DIR="${CMAKE_SOURCE_DIR}/Data/Assets/models"
    RAPTOR_SANDBOX_OUTPUT_DIR="${CMAKE_SOURCE_DIR}/Data/Output")

raptor_copy_sdl3_dll(AnimStressTest)

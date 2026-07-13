# RaptorPlayer - the generic game runner (engine + project content + game script; no native
# game code required). See docs/design/roadmap.md "Milestone: MVP-to-Export".
add_executable(RaptorPlayer Main.cpp)
target_link_libraries(RaptorPlayer PRIVATE
    Draconic::Core
    Draconic::Runtime Draconic::RuntimeClient Draconic::RuntimeDesktop Draconic::RuntimeDefaultApp
    Draconic::ShellDesktop Draconic::Shell Draconic::Graphics Draconic::GraphicsGpu
    Draconic::SceneSubsystem Draconic::SceneResource
    Draconic::RenderSubsystem Draconic::AnimationSubsystem Draconic::ParticlesSubsystem
    Draconic::Resource Draconic::MeshResource Draconic::MaterialResource Draconic::TextureResource
    Draconic::AnimationResource Draconic::ParticlesResource Draconic::ImageResource
    Draconic::ModelImporter
    Draconic::Script Draconic::ScriptWren
    Draconic::Project Draconic::VFSPak Draconic::Content Draconic::VFS Draconic::XmlSerialization)

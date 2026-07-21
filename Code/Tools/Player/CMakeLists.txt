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
    Draconic::ModelResource
    Draconic::Script Draconic::ScriptWren
    Draconic::Input Draconic::InputResource Draconic::InputSubsystem
    Draconic::UIResource Draconic::UISubsystem
    Draconic::Physics Draconic::PhysicsResource Draconic::PhysicsSubsystem
    Draconic::Project Draconic::VFSPak Draconic::Content Draconic::VFS Draconic::XmlSerialization)

# Stage the player's runtime deps beside it + emit RaptorPlayer.runtime-libs (the export host-template
# reads that list for its sidecars). SDL3 rides in as EXTRA (no-op when DRACONIC_SDL3_DLL is unset);
# DXC (libdxcompiler.so / dxcompiler.dll) is dlopen'd at runtime by the renderer's shader compiler and
# is NOT a link dependency, so it must be staged explicitly - without it a copied dist finds no DXC and
# the renderer stays inert. On Linux, $ORIGIN on the player's RUNPATH lets the loader find the staged
# copy beside the exe (Windows searches the exe dir for a bare DLL by default).
draconic_copy_runtime_deps(RaptorPlayer EXTRA ${DRACONIC_SDL3_DLL} ${DRACONIC_DXC_LIB})
if(UNIX AND NOT APPLE)
    set_property(TARGET RaptorPlayer APPEND PROPERTY BUILD_RPATH "$ORIGIN")
    set_property(TARGET RaptorPlayer PROPERTY INSTALL_RPATH "$ORIGIN")
endif()

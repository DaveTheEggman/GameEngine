# Draconic.Tools.Export - packages a project into a shippable dist (cook -> stage scenes -> Content.pak
# + player.xml + Draconic.Engine.Player). See docs/design/roadmap.md "Milestone: MVP-to-Export".
add_executable(Draconic.Tools.Export Main.cpp)
add_dependencies(Draconic.Tools.Export Draconic.Engine.Player)   # stages the player exe when present
target_link_libraries(Draconic.Tools.Export PRIVATE
    Draconic::Core Draconic::Scene.Resource
    Draconic::Pipeline.Core Draconic::Pipeline.Cook Draconic::Editor.Core
    Draconic::Texture.Pipeline Draconic::Fonts.Pipeline Draconic::Image.Pipeline Draconic::Image.Resource Draconic::Geometry.Pipeline
    Draconic::Animation.Pipeline Draconic::Materials.Pipeline Draconic::Shaders.Pipeline
    Draconic::Particles.Pipeline Draconic::Input Draconic::Input.Resource Draconic::Input.Pipeline Draconic::ModelImporter
    Draconic::Engine.Render Draconic::Engine.Animation Draconic::Engine.Particles
    Draconic::Physics Draconic::Physics.Resource Draconic::Physics.Pipeline Draconic::Engine.Physics
    Draconic::Audio Draconic::Audio.Resource Draconic::Audio.Pipeline
    Draconic::Script Draconic::Script.Wren Draconic::Script.AngelScript Draconic::Script.Resource Draconic::Script.Facades
    Draconic::Script.Pipeline Draconic::Script.Wren.Pipeline Draconic::Script.AngelScript.Pipeline
    Draconic::UI.Resource Draconic::UI.Pipeline)


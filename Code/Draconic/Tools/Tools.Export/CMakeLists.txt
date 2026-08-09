# Tools.Export - packages a project into a shippable dist (cook -> stage scenes -> Content.pak
# + player.xml + Engine.Player). See docs/design/roadmap.md "Milestone: MVP-to-Export".
add_executable(Tools.Export Main.cpp)
add_dependencies(Tools.Export Engine.Player)   # stages the player exe when present
target_link_libraries(Tools.Export PRIVATE
    Foundation::Core Foundation::Scene.Resource
    Pipeline::Core Pipeline::Cook Editor::Core
    Pipeline::Texture Pipeline::Fonts Pipeline::Image Foundation::Image.Resource Pipeline::Geometry
    Pipeline::Animation Pipeline::Materials Pipeline::Shaders
    Pipeline::Particles Foundation::Input Foundation::Input.Resource Pipeline::Input Pipeline::ModelImporter
    Engine::Render Engine::Animation Engine::Particles
    Foundation::Physics Foundation::Physics.Resource Pipeline::Physics Engine::Physics
    Foundation::Audio Foundation::Audio.Resource Pipeline::Audio
    Foundation::Script Foundation::Script.Wren Foundation::Script.AngelScript Foundation::Script.Resource Foundation::Script.Facades
    Pipeline::Script Pipeline::Script.Wren Pipeline::Script.AngelScript
    Foundation::UI.Resource Pipeline::UI)


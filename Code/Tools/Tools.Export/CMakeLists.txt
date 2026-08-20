# Tools.Export - packages a project into a shippable dist (cook -> stage scenes -> Content.pak
# + player.xml + Engine.Player). See docs/design/roadmap.md "Milestone: MVP-to-Export".
add_executable(Tools.Export Main.cpp)
add_dependencies(Tools.Export Engine.Player)   # stages the player exe when present
target_link_libraries(Tools.Export PRIVATE
    Foundation::Core Foundation::Scene.Resource
    Pipeline::Registration # the composition root: every builder/type registration
    Pipeline::Core Pipeline::Cook Editor::Core
    Pipeline::Texture Pipeline::Fonts Pipeline::Image Foundation::Image.Resource Pipeline::Geometry
    Pipeline::Animation Pipeline::Materials Pipeline::Shaders
    Pipeline::Particles Foundation::Input Foundation::Input.Resource Pipeline::Input Pipeline::ModelImporter
    Engine::SceneSurface # the FULL manager set + component reflection for the transcode scratch
    Engine::ScriptSurface # RegisterAllScriptFacades: the COMPLETE engine facade surface the
                          # script cooks compile against (run/ui/physics/audio/... facades)
    Foundation::Physics Foundation::Physics.Resource Pipeline::Physics
    Foundation::Audio Foundation::Audio.Resource Pipeline::Audio
    # Both script backends + their cooks come transitively from Pipeline::Registration (PUBLIC),
    # so export cooks scripts of either language (angelscript | luau) without naming a backend here.
    Foundation::Script Foundation::Script.Resource Foundation::Script.Facades
    Pipeline::Script
    Foundation::UI.Resource Pipeline::UI)


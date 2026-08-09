# Tools.Cook - the command-line cooker (headless: no shell/GPU).
add_executable(Tools.Cook Main.cpp)
target_link_libraries(Tools.Cook PRIVATE
    Foundation::Core Foundation::Content Foundation::VFS
    Pipeline::Core Editor::Core Pipeline::Cook
    Pipeline::Texture Pipeline::Fonts Pipeline::Image Pipeline::Geometry
    Pipeline::Animation Pipeline::Materials Pipeline::Shaders
    Pipeline::Particles Foundation::Input Foundation::Input.Resource Pipeline::Input Pipeline::ModelImporter Foundation::Image.Resource
    Foundation::Physics Foundation::Physics.Resource Pipeline::Physics
    Foundation::Audio Foundation::Audio.Resource Pipeline::Audio
    Foundation::Script Foundation::Script.Wren Foundation::Script.AngelScript Foundation::Script.Resource Foundation::Script.Facades
    Pipeline::Script Pipeline::Script.Wren Pipeline::Script.AngelScript
    Foundation::UI.Resource Pipeline::UI)


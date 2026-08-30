# Editor::Physics - `editor.physics` (the CollisionShapeAsset authoring page:
# typed mesh picker + cook-kind toggle + Cook now + status). Tooling only.

add_library(Editor.Physics STATIC)
add_library(Editor::Physics ALIAS Editor.Physics)

target_link_libraries(Editor.Physics
    PUBLIC Foundation::Core Foundation::Content Foundation::Runtime Foundation::Runtime.Client
           Foundation::Resource Pipeline::Physics Foundation::Physics.Resource
           Foundation::Render Foundation::Graphics
           Foundation::Scene Engine::Scene Engine::Render Foundation::Geometry Foundation::Materials
           Foundation::UI Foundation::UI.Runtime Foundation::UI.Toolkit
           Editor::Core Editor::App Editor::Preview
           Foundation::Policy)

target_sources(Editor.Physics
    PRIVATE
        CollisionShapePageImpl.cpp
        CollisionThumbnailImpl.cpp
    PUBLIC
        FILE_SET editor_physics_modules TYPE CXX_MODULES FILES
            CollisionShapePage.cppm CollisionThumbnail.cppm)

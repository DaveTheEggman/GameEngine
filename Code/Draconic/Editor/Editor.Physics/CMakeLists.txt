# Editor::Physics - `draconic.editor.physics` (the CollisionShapeAsset authoring page:
# typed mesh picker + cook-kind toggle + Cook now + status). Tooling only.

add_library(Editor.Physics STATIC)
add_library(Editor::Physics ALIAS Editor.Physics)

target_link_libraries(Editor.Physics
    PUBLIC Foundation::Core Foundation::Content Pipeline::Physics
           Foundation::UI Editor::Core Editor::App
           Foundation::Policy)

target_sources(Editor.Physics
    PRIVATE
        CollisionShapePageImpl.cpp
    PUBLIC
        FILE_SET editor_physics_modules TYPE CXX_MODULES FILES
            CollisionShapePage.cppm)

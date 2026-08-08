# Draconic::Editor.Physics - `draconic.editor.physics` (the CollisionShapeAsset authoring page:
# typed mesh picker + cook-kind toggle + Cook now + status). Tooling only.

add_library(Draconic.Editor.Physics STATIC)
add_library(Draconic::Editor.Physics ALIAS Draconic.Editor.Physics)

target_link_libraries(Draconic.Editor.Physics
    PUBLIC Draconic::Core Draconic::Content Draconic::Physics.Editor
           Draconic::UI Draconic::Editor.Core Draconic::Editor.App
           Draconic::Policy)

target_sources(Draconic.Editor.Physics
    PRIVATE
        CollisionShapePageImpl.cpp
    PUBLIC
        FILE_SET editor_physics_modules TYPE CXX_MODULES FILES
            CollisionShapePage.cppm)

# Draconic::PhysicsEditor - `draconic.physics.editor` (tooling: collision shape + physical material assets/builders).
add_library(DraconicPhysicsEditor STATIC)
add_library(Draconic::PhysicsEditor ALIAS DraconicPhysicsEditor)
target_link_libraries(DraconicPhysicsEditor PUBLIC
    Draconic::Core Draconic::Editor Draconic::Content Draconic::Geometry Draconic::MeshEditor
    Draconic::MeshResource Draconic::Physics Draconic::PhysicsResource Draconic::Policy)
target_sources(DraconicPhysicsEditor PUBLIC FILE_SET physics_editor_modules TYPE CXX_MODULES FILES PhysicsAsset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

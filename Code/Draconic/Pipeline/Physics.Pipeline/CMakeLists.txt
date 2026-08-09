# Pipeline::Physics - `draconic.physics.editor` (tooling: collision shape + physical material assets/builders).
add_library(Physics.Pipeline STATIC)
add_library(Pipeline::Physics ALIAS Physics.Pipeline)
target_link_libraries(Physics.Pipeline PUBLIC
    Foundation::Core Pipeline::Core Foundation::Content Foundation::Geometry Pipeline::Geometry
    Foundation::Geometry.Resource Foundation::Physics Foundation::Physics.Resource Foundation::Policy)
target_sources(Physics.Pipeline
    PRIVATE
        PhysicsAssetImpl.cpp
    PUBLIC FILE_SET physics_editor_modules TYPE CXX_MODULES FILES PhysicsAsset.cppm)


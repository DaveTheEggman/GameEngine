# Draconic::Physics.Pipeline - `draconic.physics.editor` (tooling: collision shape + physical material assets/builders).
add_library(Draconic.Physics.Pipeline STATIC)
add_library(Draconic::Physics.Pipeline ALIAS Draconic.Physics.Pipeline)
target_link_libraries(Draconic.Physics.Pipeline PUBLIC
    Draconic::Core Draconic::Pipeline.Core Draconic::Content Draconic::Geometry Draconic::Geometry.Pipeline
    Draconic::Geometry.Resource Draconic::Physics Draconic::Physics.Resource Draconic::Policy)
target_sources(Draconic.Physics.Pipeline
    PRIVATE
        PhysicsAssetImpl.cpp
    PUBLIC FILE_SET physics_editor_modules TYPE CXX_MODULES FILES PhysicsAsset.cppm)


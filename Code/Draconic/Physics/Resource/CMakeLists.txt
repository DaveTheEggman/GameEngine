# Draconic::PhysicsResource - `draconic.physics.resource` (cooked collision shapes + physical materials).
add_library(DraconicPhysicsResource STATIC)
add_library(Draconic::PhysicsResource ALIAS DraconicPhysicsResource)
target_link_libraries(DraconicPhysicsResource PUBLIC
    Draconic::Core Draconic::Resource Draconic::Content Draconic::Policy)
target_sources(DraconicPhysicsResource PUBLIC FILE_SET physics_resource_modules TYPE CXX_MODULES FILES PhysicsResource.cppm)

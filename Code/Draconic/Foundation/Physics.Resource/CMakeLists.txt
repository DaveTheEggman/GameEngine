# Foundation::Physics.Resource - `draconic.physics.resource` (cooked collision shapes + physical materials).
add_library(Physics.Resource STATIC)
add_library(Foundation::Physics.Resource ALIAS Physics.Resource)
target_link_libraries(Physics.Resource PUBLIC
    Foundation::Core Foundation::Resource Foundation::Content Foundation::Policy)
target_sources(Physics.Resource PUBLIC FILE_SET physics_resource_modules TYPE CXX_MODULES FILES PhysicsResource.cppm)

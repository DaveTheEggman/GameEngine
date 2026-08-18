# Foundation::Navigation.Resource - `foundation.navigation.resource` (cooked navmesh zones).
# The collision-asset precedent: a Source record (serialized navmesh blob) produced by a bake
# action, and a runtime product (a loaded NavigationMesh) a NavMeshZoneComponent's Ref<> binds.
add_library(Navigation.Resource STATIC)
add_library(Foundation::Navigation.Resource ALIAS Navigation.Resource)
target_link_libraries(Navigation.Resource PUBLIC
    Foundation::Core Foundation::Resource Foundation::Content Foundation::Policy
    Foundation::Navigation)
target_sources(Navigation.Resource PUBLIC
    FILE_SET navigation_resource_modules TYPE CXX_MODULES FILES NavigationResource.cppm)

# Foundation::Navigation - `foundation.navigation`: the Recast/Detour-backed navmesh core.
# Recast/Detour are PRIVATE: rc*/dt* types never cross the module surface (API hygiene +
# GCC's module serializer chokes on the third-party header mass in an interface unit).
# The BAKE is a pure function (triangle soup in, serialized navmesh out) with no scene,
# geometry, or device dependency - it runs headless. Scene integration (components, zones,
# per-scene crowds, debug draw) lives in engine.navigation.
util_add_engine_library(Navigation ALIAS Foundation::Navigation)
target_link_libraries(Navigation
    PUBLIC Foundation::Core Foundation::Policy
    PRIVATE ThirdParty::Recast Foundation::Profiler)

target_sources(Navigation
    PRIVATE
        NavigationImpl.cpp   # module IMPLEMENTATION unit: ALL Recast/Detour contact
    PUBLIC
        FILE_SET navigation_modules TYPE CXX_MODULES FILES
            NavigationModule.cppm
            NavigationBuilder.cppm
            NavigationMesh.cppm)

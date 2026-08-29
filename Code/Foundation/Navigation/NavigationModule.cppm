// Foundation::Navigation - the `foundation.navigation` module.
//
// The navmesh core over vendored recastnavigation:
// the Recast BAKE (triangle soup + agent profile -> serialized single-tile navmesh) and
// the Detour RUNTIME (navmesh load, path query, agent crowd + local avoidance). Recast
// and Detour are the committed backends with NO abstraction layer, but rc*/dt* types
// never cross the public surface. The bake is a pure function - no scene, no geometry
// module, no device - so the whole thing is testable headless.

export module foundation.navigation;

export import :builder;
export import :mesh;

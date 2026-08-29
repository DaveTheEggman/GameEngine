// Editor::Navigation - the `editor.navigation` module (interface).
//
// The "Bake Navigation" flow: collect every
// static mesh whose world AABB intersects a zone's box, transform its triangles into ZONE-LOCAL
// space (so the baked navmesh rides the zone entity's transform to any placement without a
// rebake), run the Recast bake, and write the result into the zone's NavigationZoneAsset sidecar.
//
// This interface stays LEAN: the heavy imports (engine.render, geometry, navigation.pipeline) and
// the bake logic live in NavigationBakeImpl.cpp (GCC module hygiene - heavy interface units blow
// up the gcm cluster). Signatures speak only scene + content + core types. Editor-only; the
// player never links this.

module;
#include "Core/Prelude.h"

export module editor.navigation;

import foundation.core;
import foundation.scene;
import foundation.content;

using namespace foundation::core;

export namespace editor::navigation
{
    namespace scene = foundation::scene;

    // Collect the triangle soup (in the zone entity's LOCAL space) for a bake: every MeshComponent
    // whose world bounds intersect the zone box contributes its triangles. Returns the triangle
    // count (0 = nothing to bake).
    [[nodiscard]] usize CollectNavigationGeometry(scene::Scene& scene, scene::EntityHandle zoneEntity,
                                                  Float3 zoneExtents, Array<Float3>& outVertices,
                                                  Array<u32>& outIndices);

    struct BakeResult
    {
        bool baked = false;      // a navmesh was produced and written
        usize triangleCount = 0; // input triangles collected
    };

    // Bake the zone owned by `zoneEntity` and write the result into `targetAsset` (the zone's
    // NavigationZoneAsset instance). The bake params come from the zone component. An empty
    // collection or a degenerate bake writes an EMPTY asset (a valid "no navmesh yet" state).
    [[nodiscard]] BakeResult BakeNavigationZone(scene::Scene& scene, scene::EntityHandle zoneEntity,
                                                foundation::content::Instance& targetAsset);
}

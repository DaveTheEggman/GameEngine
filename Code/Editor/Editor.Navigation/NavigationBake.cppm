// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

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
#include "Core/Reflection/Reflect.h" // RTTI_OBJECT (the settings section)

export module editor.navigation;

import foundation.core;
import foundation.scene;
import foundation.content;
import editor.core; // EditorContext (the domain-contributed settings seam)

using namespace foundation::core;

export namespace editor::navigation
{
    namespace scene = foundation::scene;

    // The navigation domain's PER-USER editor settings section (lives in the user store the
    // app owns; the domain owns the shape - the first domain-contributed settings category).
    class NavigationEditorSettings final : public ISerializable
    {
        RTTI_OBJECT(NavigationEditorSettings, ISerializable)
    public:
        bool parallelBake = true; // bake tiles across workers (output is byte-identical)

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "parallelBake", parallelBake);
        }
    };

    // TYPE registration - the composition root calls this BEFORE the app constructs (the
    // user settings file loads during app boot; an unregistered section would deserialize
    // untyped and read as defaults).
    void RegisterNavigationEditorSettingsTypes();
    // The Preferences contribution (needs the live context) - called from registerEditors.
    void RegisterNavigationEditorSettings(editor::EditorContext& context);

    // The live toggle value (defaults apply when no store is wired - headless/tests).
    [[nodiscard]] bool ParallelBakeEnabled(const editor::EditorContext& context);

    // Collect the triangle soup (in the zone entity's LOCAL space) for a bake: every MeshComponent
    // whose world bounds intersect the zone box contributes its triangles, and every terrain's
    // heightfield surface inside the box triangulates in (sampled no finer than `cellSize` -
    // Recast re-voxelizes to its own cells, so feeding denser grids is pure waste). Returns the
    // triangle count (0 = nothing to bake).
    [[nodiscard]] usize CollectNavigationGeometry(scene::Scene& scene, scene::EntityHandle zoneEntity,
                                                  Float3 zoneExtents, f32 cellSize,
                                                  Array<Float3>& outVertices,
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
                                                foundation::content::Instance& targetAsset,
                                                bool parallelBake = true);

    struct RegionRebakeResult
    {
        bool rebaked = false;    // the asset was updated
        bool fullBake = false;   // no tiled blob to patch - fell back to a full bake
        usize tilesRebuilt = 0;  // tiles regenerated (incl. ones that became empty)
    };

    // PARTIAL REBAKE (the per-tile consumer): regenerate only the tiles whose bounds
    // intersect `worldMin`..`worldMax` (an edited region - a moved obstacle, a terrain
    // sculpt), patching them into the zone's existing tiled blob. Everything outside the
    // region keeps its exact bytes, and a patched blob equals a full rebake of the same
    // scene byte-for-byte (the grid is anchored by the original bake). Falls back to a full
    // BakeNavigationZone when the asset has no tiled blob yet.
    [[nodiscard]] RegionRebakeResult
    RebakeNavigationZoneRegion(scene::Scene& scene, scene::EntityHandle zoneEntity,
                               foundation::content::Instance& targetAsset, Float3 worldMin,
                               Float3 worldMax, bool parallelBake = true);

    RTTI_DEFINE_OBJECT(NavigationEditorSettings, "rtti::editor::navigation")
}

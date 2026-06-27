/// Raptor::RenderSubsystem — the `:extract` partition.
///
/// Extraction: read a Scene's render components into a render::ExtractedScene (world-space
/// RenderData) + a render::ViewCamera, both pushed to the (scene-agnostic) renderer. This is
/// the one-way seam — this layer depends on both raptor.scene and raptor.render; the renderer
/// depends on neither. Run after the scene's transforms are current (the tick).
///
/// These are the providers in the design's terms (§5): a MeshComponent provider and the
/// camera reader. As more component types land (lights, probes), each gets its own provider
/// writing its own RenderData category into the snapshot.

module;
#include "Core/Prelude.h"

export module raptor.render.subsystem:extract;

import raptor.core;
import raptor.scene;
import raptor.render;          // ExtractedScene / MeshRenderData / ViewCamera / categories
import raptor.materials;       // BlendMode (category mapping)
import :components;

using namespace raptor::core;

export namespace raptor::render {

// Packs an entity handle into the opaque MeshRenderData::entityId (for pick; opaque to core).
[[nodiscard]] inline u64 PackEntity(scene::EntityHandle e) noexcept {
    return (static_cast<u64>(e.generation) << 32) | static_cast<u64>(e.index);
}

// Maps a material's blend preset to a render category (the dispatch + sort key).
[[nodiscard]] inline RenderCategory CategoryForMaterial(const materials::Material* m) noexcept {
    if (m == nullptr) { return RenderCategories::Opaque; }
    switch (m->pipeline.blendMode) {
        case materials::BlendMode::Opaque: return RenderCategories::Opaque;
        case materials::BlendMode::Masked: return RenderCategories::Masked;
        default:                           return RenderCategories::Transparent;
    }
}

// Fills `out` with one MeshRenderData per visible MeshComponent in `scene`. Assumes the
// scene's world transforms are current. `out` should be Reset by the caller before use.
inline void ExtractSceneInto(scene::Scene& scene, ExtractedScene& out) {
    if (auto* meshes = scene.GetSystem<MeshComponentManager>()) {
        meshes->ForEach([&](MeshComponent& mc, scene::EntityHandle e) {
            if (!mc.visible || mc.mesh.Get() == nullptr) { return; }
            MeshRenderData* rd = out.Add<MeshRenderData>();
            if (rd == nullptr) { return; }
            rd->world       = scene.GetWorldMatrix(e);
            rd->worldCenter = TransformPoint(Vec3{ 0, 0, 0 }, rd->world);   // mesh bounds center later
            rd->color       = mc.color;
            rd->mesh        = mc.mesh.Get();
            rd->material    = mc.material.Get();
            rd->entityId    = PackEntity(e);
            rd->category    = CategoryForMaterial(mc.material.Get());
        });
    }
}

// Reads the scene's primary camera into `out` (view = inverse world; projection from its
// fields). Returns false if no primary CameraComponent exists.
[[nodiscard]] inline bool ExtractPrimaryCamera(scene::Scene& scene, ViewCamera& out) {
    bool found = false;
    if (auto* cameras = scene.GetSystem<CameraComponentManager>()) {
        cameras->ForEach([&](CameraComponent& cam, scene::EntityHandle e) {
            if (found || !cam.primary) { return; }
            found = true;
            const Mat4 world = scene.GetWorldMatrix(e);
            out.view       = Inverse(world);
            out.projection = Mat4::PerspectiveFovRH(cam.fovYRadians, cam.aspect, cam.nearZ, cam.farZ);
            out.position   = TransformPoint(Vec3{ 0, 0, 0 }, world);
            out.farZ       = cam.farZ;
        });
    }
    return found;
}

} // namespace raptor::render

/// Raptor::RenderSubsystem — the `:extract` partition.
///
/// Extraction: read a Scene's render components into a render::ExtractedView that is
/// pushed to the (scene-agnostic) renderer. This is the one-way seam — this layer
/// depends on both raptor.scene and raptor.render; the renderer depends on neither.
/// Run after the scene's transforms are current (Scene::UpdateTransforms / the tick).

module;
#include "Core/Prelude.h"

export module raptor.render.subsystem:extract;

import raptor.core;
import raptor.scene;
import raptor.render;          // ExtractedView / Renderable (the render-data contract)
import :components;

using namespace raptor::core;

export namespace raptor::render {

// Packs an entity handle into the opaque Renderable::id (for sort/pick; opaque to the renderer).
[[nodiscard]] inline u64 PackEntity(scene::EntityHandle e) noexcept {
    return (static_cast<u64>(e.generation) << 32) | static_cast<u64>(e.index);
}

// Builds the draw list from `scene`. The camera is the first primary CameraComponent
// (its view = inverse of the entity's world matrix). Assumes transforms are current.
inline ExtractedView ExtractScene(scene::Scene& scene) {
    ExtractedView out;

    if (auto* cameras = scene.GetSystem<CameraComponentManager>()) {
        bool found = false;
        cameras->ForEach([&](CameraComponent& cam, scene::EntityHandle e) {
            if (found || !cam.primary) { return; }
            found = true;
            out.hasCamera  = true;
            out.view       = Inverse(scene.GetWorldMatrix(e));
            out.projection = Mat4::PerspectiveFovRH(cam.fovYRadians, cam.aspect, cam.nearZ, cam.farZ);
        });
    }

    if (auto* meshes = scene.GetSystem<MeshComponentManager>()) {
        out.renderables.Reserve(meshes->Count());
        meshes->ForEach([&](MeshComponent& mc, scene::EntityHandle e) {
            if (!mc.visible || mc.mesh.Get() == nullptr) { return; }
            out.renderables.PushBack(Renderable{
                scene.GetWorldMatrix(e), mc.mesh.Get(), mc.material.Get(), PackEntity(e) });
        });
    }

    return out;
}

} // namespace raptor::render

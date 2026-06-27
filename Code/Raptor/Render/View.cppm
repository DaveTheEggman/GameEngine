/// Raptor::Render — the `:view` partition.
///
/// Extraction: turn a Scene into a per-frame, renderer-agnostic draw list. This is the
/// seam between the scene/ECS world and the GPU renderer — whatever renderer we build
/// (or port) consumes an ExtractedView, never the Scene directly. ExtractScene reads
/// the mesh + camera component managers and snapshots each renderable's world matrix
/// (so the GPU path needs no scene access). Run after the scene's transforms are up to
/// date (Scene::UpdateTransforms / the scene tick).

module;
#include "Core/Prelude.h"

export module raptor.render:view;

import raptor.core;
import raptor.scene;
import raptor.geometry;
import raptor.materials;
import :components;

using namespace raptor::core;

export namespace raptor::render {

// One thing to draw: a mesh + material at a world transform. Pointers are borrowed for
// the duration of the frame (the components keep the resources alive).
struct Renderable {
    Mat4                  worldMatrix;
    geometry::StaticMesh* mesh     = nullptr;
    materials::Material*  material = nullptr;
    scene::EntityHandle   entity{};
};

// A frame's worth of renderables + the camera that views them.
struct ExtractedView {
    Mat4              view       = Mat4::Identity();
    Mat4              projection = Mat4::Identity();
    bool              hasCamera  = false;
    Array<Renderable> renderables;
};

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
                scene.GetWorldMatrix(e), mc.mesh.Get(), mc.material.Get(), e });
        });
    }

    return out;
}

} // namespace raptor::render

/// Raptor::RenderSubsystem — the `:components` partition.
///
/// The render-facing scene components + their managers — the scene-coupled side of the
/// renderer (raptor.render itself stays scene-agnostic). A MeshComponent references a
/// mesh + material to draw at its entity's transform; a CameraComponent describes a
/// view frustum (its view comes from the entity's world transform). The RenderSubsystem
/// injects these managers into each scene (via ISceneAware); extraction (:extract) reads
/// them into a render::ExtractedView that gets pushed to the renderer.

module;
#include "Core/Prelude.h"

export module raptor.render.subsystem:components;

import raptor.core;
import raptor.scene;
import raptor.geometry;
import raptor.materials;

using namespace raptor::core;

export namespace raptor::render {

// What to draw at an entity: a mesh + the material to draw it with. Borrowed-strong
// (RefPtr) so the component keeps its resources alive while attached. `color` is a
// per-instance tint (multiplied into the shaded color) — distinct per entity even when
// many share one mesh + material, so it rides the per-instance data path.
struct MeshComponent {
    RefPtr<geometry::StaticMesh> mesh;
    RefPtr<materials::Material>  material;
    Color                        color   = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool                         visible = true;
};

// A camera frustum. The view transform is the inverse of the entity's world matrix;
// these fields define the projection. `primary` marks the camera the renderer uses.
struct CameraComponent {
    f32  fovYRadians = 1.04719755f;          // 60 degrees
    f32  aspect      = 16.0f / 9.0f;
    f32  nearZ       = 0.1f;
    f32  farZ        = 1000.0f;
    bool primary     = true;
};

class MeshComponentManager   final : public scene::ComponentManager<MeshComponent>   {};
class CameraComponentManager final : public scene::ComponentManager<CameraComponent> {};

} // namespace raptor::render

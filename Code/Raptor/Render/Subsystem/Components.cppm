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
// `clearColor` is the backdrop the view is cleared to (per-camera, like Unity/Godot);
// defaults to the cornflower sentinel. (Clear *mode* — skybox/solid/depth-only — later.)
struct CameraComponent {
    f32   fovYRadians = 1.04719755f;          // 60 degrees
    f32   aspect      = 16.0f / 9.0f;
    f32   nearZ       = 0.1f;
    f32   farZ        = 1000.0f;
    Color clearColor  = Color{ 0.392f, 0.584f, 0.929f, 1.0f };   // cornflower sentinel
    bool  primary     = true;
};

// A light on an entity. Directional uses the entity's forward (-Z); Point/Spot use its world
// position (+ range). Extraction packs these into render::GpuLight shading inputs.
enum class LightType : u32 { Directional = 0, Point = 1, Spot = 2 };

// How a spot/point light's shadow updates (phase 5.4 static caching). Realtime = re-render every
// frame (default). Static = render once into the cached atlas layer; the caster geometry is assumed
// not to move (the cache only refreshes if the LIGHT itself moves / changes). Cheap for static scenes.
enum class ShadowUpdateMode : u32 { Realtime = 0, Static = 1 };

struct LightComponent {
    LightType        type       = LightType::Directional;
    Color            color      = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    f32              intensity  = 1.0f;
    f32              range      = 10.0f;          // point/spot falloff distance
    f32              innerAngle = 0.5f;           // spot cone inner half-angle (radians)
    f32              outerAngle = 0.6f;           // spot cone outer half-angle (radians)
    ShadowUpdateMode shadowUpdate = ShadowUpdateMode::Realtime;   // spot/point shadow caching (5.4)
    bool             enabled      = true;
    bool             castsShadows = false;        // phase 5 shadow caster
};

class MeshComponentManager   final : public scene::ComponentManager<MeshComponent>   {};
class CameraComponentManager final : public scene::ComponentManager<CameraComponent> {};
class LightComponentManager  final : public scene::ComponentManager<LightComponent>  {};

// The scene's environment — ONE per scene (not a component). Holds the ambient indirect term
// for now (skybox / IBL later). A plain SceneSystem injected by the RenderSubsystem; extraction
// reads it into the snapshot. `ambientColor × ambientIntensity` is applied as the forward ambient.
struct EnvironmentSettings {
    Color ambientColor     = Color{ 0.10f, 0.12f, 0.16f, 1.0f };
    f32   ambientIntensity = 0.3f;
};

class EnvironmentSystem final : public scene::SceneSystem {
public:
    [[nodiscard]] EnvironmentSettings&       Environment()       noexcept { return m_env; }
    [[nodiscard]] const EnvironmentSettings& Environment() const noexcept { return m_env; }
private:
    EnvironmentSettings m_env;
};

} // namespace raptor::render

/// Draconic::RenderSubsystem — the `:components` partition.
///
/// The render-facing scene components + their managers — the scene-coupled side of the
/// renderer (draconic.render itself stays scene-agnostic). A MeshComponent references a
/// mesh + material to draw at its entity's transform; a CameraComponent describes a
/// view frustum (its view comes from the entity's world transform). The RenderSubsystem
/// injects these managers into each scene (via ISceneAware); extraction (:extract) reads
/// them into a render::ExtractedView that gets pushed to the renderer.

module;
#include "Core/Prelude.h"

export module draconic.render.subsystem:components;

import draconic.core;
import draconic.scene;
import draconic.geometry;
import draconic.materials;
import draconic.rhi;      // rhi::TextureView (a SpriteComponent references a texture to draw)
import draconic.render;   // SkySnapshot/SkyMode (snapshot layer; render.subsystem depends on render)

using namespace draconic::core;

export namespace draconic::render {

// What to draw at an entity: a mesh + the material to draw it with. Borrowed-strong
// (RefPtr) so the component keeps its resources alive while attached. `color` is a
// per-instance tint (multiplied into the shaded color) — distinct per entity even when
// many share one mesh + material, so it rides the per-instance data path.
struct MeshComponent {
    RefPtr<geometry::StaticMesh> mesh;
    RefPtr<materials::Material>  material;
    // Optional per-submesh materials (multi-material meshes): indexed by SubMesh::materialIndex. When
    // non-empty the renderer draws each submesh with its own material; otherwise `material` covers all.
    Array<RefPtr<materials::Material>> submeshMaterials;
    Color                        color   = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool                         visible = true;
    // GPU skinning: per-bone skinning matrices for a skinned mesh, supplied per frame by the owner
    // (e.g. an AnimationPlayer's GetSkinningMatrices()). Borrowed — valid for the frame it's set;
    // null => the mesh draws static (bind pose). Extraction copies the pointer into MeshRenderData.
    const Matrix4*                  boneMatrices = nullptr;
    const Matrix4*                  prevBoneMatrices = nullptr;   // previous-frame matrices (motion vectors); null => reuse current
    u32                          boneCount    = 0;
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

// A textured billboard on an entity — drawn at the entity's world position, sized in world units,
// facing the camera (or world-aligned). `texture` is borrowed: the app/resource owns it and must keep
// it alive while the component is attached. Extraction reads this into a render::SpriteRenderData.
struct SpriteComponent {
    rhi::TextureView* texture = nullptr;
    Vector2  size        = Vector2{ 1.0f, 1.0f };                 // world-unit width/height
    Vector4  uvRect      = Vector4{ 0.0f, 0.0f, 1.0f, 1.0f };     // atlas sub-rect (u, v, w, h) — whole texture by default
    Color tint        = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    u32   orientation = 0;      // 0 = camera-facing, 1 = camera-facing about world-Y, 2 = world-aligned (XY)
    bool  additive    = false;  // false = alpha over, true = additive (glow)
    bool  visible     = true;
};

// A screen-space projected decal on an entity — sprays `texture` onto whatever surface is under its
// oriented box. The box projects along the entity's local +Z; `size` is the box extents (x,y = the
// footprint, z = how far along the projection axis it reaches). Orient the entity so local +Z points
// into the surface (e.g. rotate so +Z points down to project onto a floor). `texture` is borrowed.
struct DecalComponent {
    rhi::TextureView* texture = nullptr;
    Vector3  size      = Vector3{ 1.0f, 1.0f, 1.0f };
    Color color     = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    f32   fadeStart = 0.0f;     // angle-fade start (radians)
    f32   fadeEnd   = 1.30f;    // angle-fade end (radians ~75deg)
    bool  visible   = true;
};

// A local reflection probe: captures the scene into a cubemap from the entity's world position and gives
// parallax-corrected specular reflections to surfaces inside its box volume. The box (world-axis-aligned,
// from `halfExtents`) is BOTH the influence volume and the parallax proxy; `blendDistance` softens the
// influence toward the box edge so overlapping probes blend without a seam. `update` selects the capture
// cadence (ProbeUpdateMode, from the snapshot layer). Extraction reads this into a render::ReflectionProbe.
struct ReflectionProbeComponent {
    Vector3            halfExtents   = Vector3{ 5.0f, 5.0f, 5.0f };   // box influence/proxy half-extents (world units)
    f32             blendDistance = 1.0f;                       // soft falloff width inward from the box edge
    f32             intensity     = 1.0f;                       // reflection multiplier
    u32             resolution    = 128;                        // captured cube face size (64/128/256)
    u32             priority      = 0;                          // tie-break when volumes overlap (higher wins)
    ProbeUpdateMode update        = ProbeUpdateMode::Static;
    bool            parallax      = true;                       // box-project the reflection ray (vs infinite env)
    bool            enabled       = true;
};

class MeshComponentManager   final : public scene::ComponentManager<MeshComponent>   {};
class SpriteComponentManager final : public scene::ComponentManager<SpriteComponent> {};
class DecalComponentManager  final : public scene::ComponentManager<DecalComponent>  {};
class CameraComponentManager final : public scene::ComponentManager<CameraComponent> {};
class LightComponentManager  final : public scene::ComponentManager<LightComponent>  {};
class ReflectionProbeComponentManager final : public scene::ComponentManager<ReflectionProbeComponent> {};

// SkyMode is defined in the snapshot layer (draconic.render :data) and reused here.

// The scene's environment — ONE per scene (not a component). Drives both the IBL ambient and the
// (upcoming) visible sky. A plain SceneSystem injected by the RenderSubsystem; extraction reads it
// into the snapshot. When IBL is active these sky settings drive shading; `ambientColor ×
// ambientIntensity` remains the flat fallback used when no environment is active.
struct EnvironmentSettings {
    Color   ambientColor     = Color{ 0.10f, 0.12f, 0.16f, 1.0f };   // flat fallback (IBL off)
    f32     ambientIntensity = 0.3f;

    SkyMode skyMode      = SkyMode::Procedural;
    f32     skyIntensity = 1.0f;                                      // multiplier on env radiance
    f32     skyRotation  = 0.0f;                                      // yaw (radians) for HDR/cubemap
    // Procedural sky:
    Color   skyHorizon   = Color{ 0.60f, 0.70f, 0.85f, 1.0f };
    Color   skyZenith    = Color{ 0.15f, 0.30f, 0.65f, 1.0f };       // also the Color-mode color
    Color   skyGround    = Color{ 0.30f, 0.28f, 0.25f, 1.0f };
    f32     sunIntensity = 1.0f;
    f32     sunAngularSize = 0.5f;                                    // sun disc size (degrees)
    f32     turbidity    = 3.0f;                                      // Analytic (Preetham) haze (~2..10)
};

class EnvironmentSystem final : public scene::SceneSystem {
public:
    [[nodiscard]] EnvironmentSettings&       Environment()       noexcept { return m_env; }
    [[nodiscard]] const EnvironmentSettings& Environment() const noexcept { return m_env; }
private:
    EnvironmentSettings m_env;
};

} // namespace draconic::render

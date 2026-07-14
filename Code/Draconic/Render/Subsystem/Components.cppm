/// Draconic::RenderSubsystem - the `:components` partition.
///
/// The render-facing scene components + their managers - the scene-coupled side of the
/// renderer (draconic.render itself stays scene-agnostic). A MeshComponent references a
/// mesh + material to draw at its entity's transform; a CameraComponent describes a
/// view frustum (its view comes from the entity's world transform). The RenderSubsystem
/// injects these managers into each scene (via ISceneAware); extraction (:extract) reads
/// them into a render::ExtractedView that gets pushed to the renderer.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.render.subsystem:components;

import draconic.core;
import draconic.resource;
import draconic.scene;
import draconic.geometry;
import draconic.materials;
import draconic.rhi;      // rhi::TextureView (a SpriteComponent references a texture to draw)
import draconic.texture.resource;   // texture::Texture (cooked product behind sprite/decal texture refs)
import draconic.render;   // SkySnapshot/SkyMode (snapshot layer; render.subsystem depends on render)

using namespace draconic::core;

export namespace draconic::render {

// What to draw at an entity: a mesh + the material to draw it with. Resource refs
// (resource::Ref): a serialized Guid resolved through the ResourceManager's proxy handles
// (hot reload swaps the product behind every holder), or a direct RefPtr for code-created
// meshes (samples/procedural - the direct object wins and is never serialized). `color` is a
// per-instance tint (multiplied into the shaded color) - distinct per entity even when
// many share one mesh + material, so it rides the per-instance data path.
struct MeshComponent {
    draconic::resource::Ref<geometry::StaticMesh> mesh;
    draconic::resource::Ref<materials::Material>  material;
    // Optional per-submesh materials (multi-material meshes): indexed by SubMesh::materialIndex. When
    // non-empty the renderer draws each submesh with its own material; otherwise `material` covers all.
    // Runtime-only for now (RefPtr, not serialized): the model-spawn path fills it; per-submesh
    // material REFS land with prefabs (phase 7), which owns the model->entity workflow.
    Array<RefPtr<materials::Material>> submeshMaterials;
    Color                        color   = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool                         visible = true;
    // GPU skinning: per-bone skinning matrices for a skinned mesh, supplied per frame by the owner
    // (e.g. an AnimationPlayer's GetSkinningMatrices()). Borrowed - valid for the frame it's set;
    // null => the mesh draws static (bind pose). Extraction copies the pointer into MeshRenderData.
    const Float4x4*                  boneMatrices = nullptr;
    const Float4x4*                  prevBoneMatrices = nullptr;   // previous-frame matrices (motion vectors); null => reuse current
    u32                          boneCount    = 0;
};

// An INSTANCED mesh ("MultiMesh"): ONE shared mesh + material drawn at N per-instance transforms that
// live (on the GPU) in a persistent buffer owned by the renderer. Its per-frame CPU cost is O(1) in the
// instance count - the set is extracted as ONE render item, culled as one merged AABB, and drawn once
// per pass (depth, forward, every shadow cascade all read the same buffer). Use it for static crowds /
// scatter (foliage, props, debris); the per-entity MeshComponent stays for genuinely dynamic objects.
// `instances` is the CPU source of truth; every mutator bumps `version`, and the renderer re-uploads the
// GPU buffer (and extraction recomputes the merged bounds) ONLY when the version changes. See
// docs/design/instanced-mesh.md.
struct InstancedMeshComponent {
    draconic::resource::Ref<geometry::StaticMesh> mesh;
    draconic::resource::Ref<materials::Material>  material;
    // Seed one identity instance so a freshly added component (editor workflow) renders its mesh
    // at the entity's transform immediately; authored sets replace it (SetInstances/load).
    InstancedMeshComponent() { instances.PushBack(Float4x4::Identity()); }
    // Optional per-submesh materials (multi-material meshes): indexed by SubMesh::materialIndex. When
    // non-empty each submesh draws with its own material; otherwise `material` covers the whole mesh.
    // Runtime-only (RefPtr, not serialized) like MeshComponent's: per-submesh material REFS land
    // with prefabs (phase 7), which owns the model->entity workflow.
    Array<RefPtr<materials::Material>> submeshMaterials;
    // Per-instance transforms, ENTITY-RELATIVE: instance i draws at instances[i] * entityWorld,
    // so moving the owning entity moves the whole set (extraction composes + caches the world
    // array; a set on an unmoved identity entity costs the same as before).
    Array<Float4x4>               instances;
    Color                        color   = Color{ 1.0f, 1.0f, 1.0f, 1.0f };  // shared tint (used when `tints` is empty)
    // Optional per-instance tint (parallel to `instances`): when its size matches, each instance uses its
    // own tint; otherwise `color` covers all. Read at upload, so set it BEFORE SetInstances (which bumps version).
    Array<Color>                 tints;
    bool                         visible = true;

    // GPU-skinned crowds: a shared POSE POOL of `poseCount` skinning palettes (each `boneCount` matrices),
    // set per frame by the InstancedSkinning companion (draconic.animation.subsystem). When posePool is
    // non-null the set draws SKINNED, and instance i uses pose (i % poseCount) - so N animated instances
    // cost only M = poseCount palette computes, not N. Borrowed (valid for the frame it's set); null => the
    // set draws static. The mesh must be a skinned mesh (has a skin stream). See docs/design/instanced-mesh.md SS7.
    const Float4x4*               posePool     = nullptr;
    const Float4x4*               prevPosePool = nullptr;   // last frame's palettes (per-bone motion vectors); null => reuse current
    u32                          poseCount    = 0;   // M unique phase buckets
    u32                          boneCount    = 0;   // bones per palette

    // Pose-selection policy for the shared pool (see render::PoseAssignment). Default Hashed = each
    // instance scattered to an unrelated pose (independent-agent crowd). Set to Explicit and fill
    // `poseIndices` (parallel to `instances`, values in [0,poseCount)) for layout-aware looks the
    // renderer can't derive from the flat index - columns, spatial clusters, gameplay. Explicit with an
    // empty/mismatched array falls back to Hashed. Not a mutator (doesn't bump version - it's read each
    // frame during the offsets fill, not uploaded to the instance buffer).
    PoseAssignment               poseAssignment = PoseAssignment::Hashed;
    Array<u32>                   poseIndices;

    // Change counter: bumped by every mutator so the renderer knows to re-upload and extraction knows to
    // recompute the merged bounds. Starts at 1 so the first extract (uploadedVersion 0) always uploads.
    u32                          version = 1;
    // Composed world-space transforms (instances[i] * entityWorld), rebuilt by extraction when the
    // authored set OR the entity's world matrix changed; `composedVersion` is what the renderer
    // sees, so an entity move re-uploads the GPU buffer like any other mutation. Runtime-only.
    Array<Float4x4>               worldTransforms;
    Float4x4                      composedEntityWorld = Float4x4::Identity();
    u32                          composedFromVersion = 0;   // authored `version` the cache was built from (0 = never)
    u32                          composedVersion     = 0;   // bumped on every recompose (renderer upload key)
    // Cached merged world-space bounds (center + sphere radius), recomputed at extraction when
    // `boundsVersion != composedVersion`. Lets a static set skip the O(N) bounds pass every frame.
    Float3                      cachedCenter  = Float3{ 0, 0, 0 };
    f32                          cachedRadius  = 0.0f;
    u32                          boundsVersion = 0;

    [[nodiscard]] u32 Count() const noexcept { return static_cast<u32>(instances.Size()); }

    // Replace the whole set in one shot (fast path for static content) - a single version bump.
    void SetInstances(Span<const Float4x4> xf) {
        instances.Resize(xf.Size());
        if (!xf.IsEmpty()) { MemCopy(instances.Data(), xf.Data(), xf.Size() * sizeof(Float4x4)); }
        ++version;
    }
    void Add(const Float4x4& m)               { instances.PushBack(m); ++version; }
    void SetInstance(u32 i, const Float4x4& m){ if (i < instances.Size()) { instances[i] = m; ++version; } }
    void Reserve(u32 n)                      { instances.Reserve(n); }
    void Clear()                             { instances.Clear(); ++version; }
};

// A camera frustum. The view transform is the inverse of the entity's world matrix;
// these fields define the projection. `primary` marks the camera the renderer uses.
// `clearColor` is the backdrop the view is cleared to (per-camera, like Unity/Godot);
// defaults to the cornflower sentinel. (Clear *mode* - skybox/solid/depth-only - later.)
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

// A textured billboard on an entity - drawn at the entity's world position, sized in world units,
// facing the camera (or world-aligned). `texture` is borrowed: the app/resource owns it and must keep
// it alive while the component is attached. Extraction reads this into a render::SpriteRenderData.
// How a sprite billboard orients itself (mirrors the shader's orientation mode).
enum class SpriteOrientation : u32 {
    CameraFacing  = 0,   // full billboard - always faces the camera
    CameraFacingY = 1,   // rotates about world-Y only (trees/characters)
    WorldAligned  = 2,   // fixed world XY plane (decal-like flat art)
};

struct SpriteComponent {
    rhi::TextureView* texture = nullptr;   // runtime override (samples/procedural); wins over textureAsset
    draconic::resource::Ref<texture::Texture> textureAsset;   // cooked texture (editor picker/serialized)
    Float2  size        = Float2{ 1.0f, 1.0f };                 // world-unit width/height
    Float4  uvRect      = Float4{ 0.0f, 0.0f, 1.0f, 1.0f };     // atlas sub-rect (u, v, w, h) - whole texture by default
    Color tint        = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    SpriteOrientation orientation = SpriteOrientation::CameraFacing;
    bool  additive    = false;  // false = alpha over, true = additive (glow)
    bool  visible     = true;
};

// A screen-space projected decal on an entity - sprays `texture` onto whatever surface is under its
// oriented box. The box projects along the entity's local +Z; `size` is the box extents (x,y = the
// footprint, z = how far along the projection axis it reaches). Orient the entity so local +Z points
// into the surface (e.g. rotate so +Z points down to project onto a floor). `texture` is borrowed.
struct DecalComponent {
    rhi::TextureView* texture = nullptr;   // runtime override (samples/procedural); wins over textureAsset
    draconic::resource::Ref<texture::Texture> textureAsset;   // cooked texture (editor picker/serialized)
    Float3  size      = Float3{ 1.0f, 1.0f, 1.0f };
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
    Float3            halfExtents   = Float3{ 5.0f, 5.0f, 5.0f };   // box influence/proxy half-extents (world units)
    f32             blendDistance = 1.0f;                       // soft falloff width inward from the box edge
    f32             intensity     = 1.0f;                       // reflection multiplier
    u32             resolution    = 128;                        // captured cube face size (64/128/256)
    u32             priority      = 0;                          // tie-break when volumes overlap (higher wins)
    ProbeUpdateMode update        = ProbeUpdateMode::Static;
    bool            parallax      = true;                       // box-project the reflection ray (vs infinite env)
    bool            enabled       = true;
};

// MeshComponent persists (scene round-trip): refs serialize their Guids; the direct
// pointers and per-frame skinning state never touch disk.
inline void Serialize(ISerializer& ar, MeshComponent& c) {
    draconic::core::Serialize(ar, "mesh", c.mesh);
    draconic::core::Serialize(ar, "material", c.material);
    draconic::core::Serialize(ar, "color", c.color);
    draconic::core::Serialize(ar, "visible", c.visible);
}

inline void ResolveResources(draconic::resource::ResourceManager& manager, MeshComponent& c) {
    c.mesh.Bind(manager);
    c.material.Bind(manager);
}

class MeshComponentManager final : public scene::SerializableComponentManager<MeshComponent> {
public:
    MeshComponentManager() : scene::SerializableComponentManager<MeshComponent>(u8"mesh") {}
};
// Data-only components PERSIST (scene round-trip + full destroy-undo restore - a destroyed
// entity's components are snapshotted through serializable managers only). Sprite/Decal/
// InstancedMesh still hold raw GPU pointers and stay runtime-only until they move to refs.
inline void Serialize(ISerializer& ar, LightComponent& c) {
    u8 type = static_cast<u8>(c.type);
    u8 shadowUpdate = static_cast<u8>(c.shadowUpdate);
    draconic::core::Serialize(ar, "type", type);
    draconic::core::Serialize(ar, "color", c.color);
    draconic::core::Serialize(ar, "intensity", c.intensity);
    draconic::core::Serialize(ar, "range", c.range);
    draconic::core::Serialize(ar, "innerAngle", c.innerAngle);
    draconic::core::Serialize(ar, "outerAngle", c.outerAngle);
    draconic::core::Serialize(ar, "shadowUpdate", shadowUpdate);
    draconic::core::Serialize(ar, "enabled", c.enabled);
    draconic::core::Serialize(ar, "castsShadows", c.castsShadows);
    c.type = static_cast<LightType>(type);
    c.shadowUpdate = static_cast<ShadowUpdateMode>(shadowUpdate);
}

inline void Serialize(ISerializer& ar, CameraComponent& c) {
    draconic::core::Serialize(ar, "fovYRadians", c.fovYRadians);
    draconic::core::Serialize(ar, "aspect", c.aspect);
    draconic::core::Serialize(ar, "nearZ", c.nearZ);
    draconic::core::Serialize(ar, "farZ", c.farZ);
    draconic::core::Serialize(ar, "clearColor", c.clearColor);
    draconic::core::Serialize(ar, "primary", c.primary);
}

inline void Serialize(ISerializer& ar, ReflectionProbeComponent& c) {
    u8 update = static_cast<u8>(c.update);
    draconic::core::Serialize(ar, "halfExtents", c.halfExtents);
    draconic::core::Serialize(ar, "blendDistance", c.blendDistance);
    draconic::core::Serialize(ar, "intensity", c.intensity);
    draconic::core::Serialize(ar, "resolution", c.resolution);
    draconic::core::Serialize(ar, "priority", c.priority);
    draconic::core::Serialize(ar, "update", update);
    draconic::core::Serialize(ar, "parallax", c.parallax);
    draconic::core::Serialize(ar, "enabled", c.enabled);
    c.update = static_cast<ProbeUpdateMode>(update);
}

// Persist the refs + the authored placement data (instances/tints are the authored content of a
// scatter set); the skinning pose pool + caches are runtime-only. Loaded sets start at version 1
// with boundsVersion 0, so the first extract re-uploads and recomputes bounds.
inline void Serialize(ISerializer& ar, InstancedMeshComponent& c) {
    draconic::core::Serialize(ar, "mesh", c.mesh);
    draconic::core::Serialize(ar, "material", c.material);
    draconic::core::Serialize(ar, "instances", c.instances);
    draconic::core::Serialize(ar, "color", c.color);
    draconic::core::Serialize(ar, "tints", c.tints);
    draconic::core::Serialize(ar, "visible", c.visible);
}
inline void ResolveResources(draconic::resource::ResourceManager& manager, InstancedMeshComponent& c) {
    c.mesh.Bind(manager);
    c.material.Bind(manager);
}

class InstancedMeshComponentManager final : public scene::SerializableComponentManager<InstancedMeshComponent> {
public:
    InstancedMeshComponentManager()
        : scene::SerializableComponentManager<InstancedMeshComponent>(u8"instanced_mesh") {}
};
// Persist the texture ref + plain fields; the raw view override is runtime-only.
inline void Serialize(ISerializer& ar, SpriteComponent& c) {
    draconic::core::Serialize(ar, "texture", c.textureAsset);
    draconic::core::Serialize(ar, "size", c.size);
    draconic::core::Serialize(ar, "uvRect", c.uvRect);
    draconic::core::Serialize(ar, "tint", c.tint);
    u32 orientation = static_cast<u32>(c.orientation);
    draconic::core::Serialize(ar, "orientation", orientation);
    c.orientation = static_cast<SpriteOrientation>(orientation);
    draconic::core::Serialize(ar, "additive", c.additive);
    draconic::core::Serialize(ar, "visible", c.visible);
}
inline void ResolveResources(draconic::resource::ResourceManager& manager, SpriteComponent& c) {
    c.textureAsset.Bind(manager);
}

inline void Serialize(ISerializer& ar, DecalComponent& c) {
    draconic::core::Serialize(ar, "texture", c.textureAsset);
    draconic::core::Serialize(ar, "size", c.size);
    draconic::core::Serialize(ar, "color", c.color);
    draconic::core::Serialize(ar, "fadeStart", c.fadeStart);
    draconic::core::Serialize(ar, "fadeEnd", c.fadeEnd);
    draconic::core::Serialize(ar, "visible", c.visible);
}
inline void ResolveResources(draconic::resource::ResourceManager& manager, DecalComponent& c) {
    c.textureAsset.Bind(manager);
}

class SpriteComponentManager final : public scene::SerializableComponentManager<SpriteComponent> {
public:
    SpriteComponentManager() : scene::SerializableComponentManager<SpriteComponent>(u8"sprite") {}
};
class DecalComponentManager final : public scene::SerializableComponentManager<DecalComponent> {
public:
    DecalComponentManager() : scene::SerializableComponentManager<DecalComponent>(u8"decal") {}
};
class CameraComponentManager final : public scene::SerializableComponentManager<CameraComponent> {
public:
    CameraComponentManager() : scene::SerializableComponentManager<CameraComponent>(u8"camera") {}
};
class LightComponentManager final : public scene::SerializableComponentManager<LightComponent> {
public:
    LightComponentManager() : scene::SerializableComponentManager<LightComponent>(u8"light") {}
};
class ReflectionProbeComponentManager final : public scene::SerializableComponentManager<ReflectionProbeComponent> {
public:
    ReflectionProbeComponentManager() : scene::SerializableComponentManager<ReflectionProbeComponent>(u8"reflection_probe") {}
};

// SkyMode is defined in the snapshot layer (draconic.render :data) and reused here.

// The scene's environment - ONE per scene (not a component). Drives both the IBL ambient and the
// (upcoming) visible sky. A plain SceneSystem injected by the RenderSubsystem; extraction reads it
// into the snapshot. When IBL is active these sky settings drive shading; `ambientColor ×
// ambientIntensity` remains the flat fallback used when no environment is active.
struct EnvironmentSettings {
    // Flat ambient FILL: adds on top of the environment's image-based ambient in every sky
    // mode (intensity 0 = pure IBL); when IBL is unavailable it is the only ambient.
    Color   ambientColor     = Color{ 0.10f, 0.12f, 0.16f, 1.0f };
    f32     ambientIntensity = 0.3f;

    SkyMode skyMode      = SkyMode::Procedural;
    f32     skyIntensity = 1.0f;                                      // multiplier on env radiance
    f32     skyRotation  = 0.0f;                                      // yaw (radians) for HDR/cubemap
    // The textured modes' source: HDREquirect = a 2D .hdr texture asset; Cubemap = a
    // cube-shaped texture asset. Ignored by the untextured modes. (The programmatic
    // RenderSubsystem::SetSkyEquirect/SetSkyCubemap pixel paths remain for tools/samples.)
    draconic::resource::Ref<texture::Texture> skyTexture;
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

    // Scene-level settings seam: the editor's scene inspector edits m_env through the
    // reflected type; SerializeScene persists it (wrapped in the type's versioned payload -
    // reflection registration stamps dataVersion 1, so future fields gate on ar.Version()).
    [[nodiscard]] const TypeInfo* SettingsType() const noexcept override { return &TypeOf<EnvironmentSettings>(); }
    [[nodiscard]] void* SettingsInstance() noexcept override { return &m_env; }
    [[nodiscard]] StringView SettingsId() const noexcept override { return u8"environment"; }
    void ResolveResources(draconic::resource::ResourceManager& manager) override {
        m_env.skyTexture.Bind(manager);
    }
    void SerializeSettings(ISerializer& ar) override {
        if (ar.Version() >= 2) {   // v2 added the sky texture reference
            draconic::core::Serialize(ar, "skyTexture", m_env.skyTexture);
        }
        draconic::core::Serialize(ar, "ambientColor",     m_env.ambientColor);
        draconic::core::Serialize(ar, "ambientIntensity", m_env.ambientIntensity);
        u32 mode = static_cast<u32>(m_env.skyMode);
        draconic::core::Serialize(ar, "skyMode", mode);
        if (ar.Mode() == SerializeMode::Read) { m_env.skyMode = static_cast<SkyMode>(mode); }
        draconic::core::Serialize(ar, "skyIntensity",   m_env.skyIntensity);
        draconic::core::Serialize(ar, "skyRotation",    m_env.skyRotation);
        draconic::core::Serialize(ar, "skyHorizon",     m_env.skyHorizon);
        draconic::core::Serialize(ar, "skyZenith",      m_env.skyZenith);
        draconic::core::Serialize(ar, "skyGround",      m_env.skyGround);
        draconic::core::Serialize(ar, "sunIntensity",   m_env.sunIntensity);
        draconic::core::Serialize(ar, "sunAngularSize", m_env.sunAngularSize);
        draconic::core::Serialize(ar, "turbidity",      m_env.turbidity);
    }

private:
    EnvironmentSettings m_env;
};

} // namespace draconic::render (exported)

// ============================================================================================
// Reflection (tooling: the editor inspector auto-generates property grids from these).
// Pointer/RefPtr/array fields (mesh, material, textures, bone matrices) are deliberately not
// reflected yet - they need resource-picker editors (editor phase 6). NON-export namespace:
// the macros expand static helpers (internal linkage), per the CoreReflection.cppm pattern.
// ============================================================================================
namespace draconic::render
{

DRACONIC_REFLECT_ENUM(LightType, "draconic::render")
{
    builder.Value("Directional", LightType::Directional);
    builder.Value("Point", LightType::Point);
    builder.Value("Spot", LightType::Spot);
}

DRACONIC_REFLECT_ENUM(ShadowUpdateMode, "draconic::render")
{
    builder.Value("Realtime", ShadowUpdateMode::Realtime);
    builder.Value("Static", ShadowUpdateMode::Static);
}

DRACONIC_REFLECT_ENUM(ProbeUpdateMode, "draconic::render")
{
    builder.Value("Static", ProbeUpdateMode::Static);
    builder.Value("Realtime", ProbeUpdateMode::Realtime);
    builder.Value("Manual", ProbeUpdateMode::Manual);
}

DRACONIC_REFLECT_VALUE(MeshComponent, "draconic::render")
{
    builder.Property<&MeshComponent::mesh>("mesh")
           .Property<&MeshComponent::material>("material")
           .Property<&MeshComponent::color>("color")
           .Property<&MeshComponent::visible>("visible");
}

DRACONIC_REFLECT_VALUE(InstancedMeshComponent, "draconic::render")
{
    builder.Property<&InstancedMeshComponent::mesh>("mesh")
           .Property<&InstancedMeshComponent::material>("material")
           .Property<&InstancedMeshComponent::color>("color")
           .Property<&InstancedMeshComponent::visible>("visible");
}

DRACONIC_REFLECT_VALUE(CameraComponent, "draconic::render")
{
    builder.Property<&CameraComponent::fovYRadians>("fovYRadians")
               .PropAttribute("displayName", String(u8"Field Of View"))
               .PropAttribute("description", String(u8"Vertical field of view (radians)"))
               .PropAttribute("range", Float4{ 0.10f, 3.04f, 0.01f, 0.0f })
           .Property<&CameraComponent::aspect>("aspect")
           .Property<&CameraComponent::nearZ>("nearZ")
           .Property<&CameraComponent::farZ>("farZ")
           .Property<&CameraComponent::clearColor>("clearColor")
           .Property<&CameraComponent::primary>("primary");
}

DRACONIC_REFLECT_VALUE(LightComponent, "draconic::render")
{
    builder.Property<&LightComponent::type>("type")
           .Property<&LightComponent::color>("color")
           .Property<&LightComponent::intensity>("intensity")
               .PropAttribute("range", Float4{ 0.0f, 50.0f, 0.1f, 0.0f })
           .Property<&LightComponent::range>("range")
               .PropAttribute("range", Float4{ 0.0f, 500.0f, 0.5f, 0.0f })
               .PropAttribute("visibleWhen", String(u8"type=1,2"))
               .PropAttribute("description", String(u8"Falloff distance (point/spot lights)"))
           .Property<&LightComponent::innerAngle>("innerAngle")
               .PropAttribute("range", Float4{ 0.0f, 1.55f, 0.01f, 0.0f })
               .PropAttribute("visibleWhen", String(u8"type=2"))
               .PropAttribute("description", String(u8"Spot cone inner half-angle (radians)"))
           .Property<&LightComponent::outerAngle>("outerAngle")
               .PropAttribute("range", Float4{ 0.0f, 1.55f, 0.01f, 0.0f })
               .PropAttribute("visibleWhen", String(u8"type=2"))
               .PropAttribute("description", String(u8"Spot cone outer half-angle (radians)"))
           .Property<&LightComponent::shadowUpdate>("shadowUpdate")
               .PropAttribute("visibleWhen", String(u8"castsShadows"))
           .Property<&LightComponent::enabled>("enabled")
           .Property<&LightComponent::castsShadows>("castsShadows");
}

DRACONIC_REFLECT_ENUM(SpriteOrientation, "draconic::render")
{
    builder.Value("CameraFacing", SpriteOrientation::CameraFacing);
    builder.Value("CameraFacingY", SpriteOrientation::CameraFacingY);
    builder.Value("WorldAligned", SpriteOrientation::WorldAligned);
}

DRACONIC_REFLECT_VALUE(SpriteComponent, "draconic::render")
{
    builder.Property<&SpriteComponent::textureAsset>("texture")
           .Property<&SpriteComponent::size>("size")
           .Property<&SpriteComponent::uvRect>("uvRect")
           .Property<&SpriteComponent::tint>("tint")
           .Property<&SpriteComponent::orientation>("orientation")
           .Property<&SpriteComponent::additive>("additive")
           .Property<&SpriteComponent::visible>("visible");
}

DRACONIC_REFLECT_VALUE(DecalComponent, "draconic::render")
{
    builder.Property<&DecalComponent::textureAsset>("texture")
           .Property<&DecalComponent::size>("size")
           .Property<&DecalComponent::color>("color")
           .Property<&DecalComponent::fadeStart>("fadeStart")
           .Property<&DecalComponent::fadeEnd>("fadeEnd")
           .Property<&DecalComponent::visible>("visible");
}

DRACONIC_REFLECT_ENUM(SkyMode, "draconic::render")
{
    builder.Value("Procedural", SkyMode::Procedural);
    builder.Value("Analytic", SkyMode::Analytic);
    builder.Value("Color", SkyMode::Color);
    builder.Value("HDREquirect", SkyMode::HDREquirect);
    builder.Value("Cubemap", SkyMode::Cubemap);
}

DRACONIC_REFLECT_VALUE(EnvironmentSettings, "draconic::render")
{
    builder.DataVersion(2)
           .Property<&EnvironmentSettings::ambientColor>("ambientColor")
           .Property<&EnvironmentSettings::ambientIntensity>("ambientIntensity")
               .PropAttribute("range", Float4{ 0.0f, 2.0f, 0.01f, 0.0f })
               .PropAttribute("description", String(u8"Flat ambient fill added on top of the image-based ambient (0 = pure IBL)"))
           .Property<&EnvironmentSettings::skyMode>("skyMode")
           .Property<&EnvironmentSettings::skyTexture>("skyTexture")
               .PropAttribute("visibleWhen", String(u8"skyMode=3,4"))
               .PropAttribute("description", String(u8"HDR (equirect) or cube texture for the textured sky modes"))
           .Property<&EnvironmentSettings::skyIntensity>("skyIntensity")
               .PropAttribute("range", Float4{ 0.0f, 10.0f, 0.05f, 0.0f })
           .Property<&EnvironmentSettings::skyRotation>("skyRotation")
               .PropAttribute("range", Float4{ 0.0f, 6.2832f, 0.01f, 0.0f })
               .PropAttribute("visibleWhen", String(u8"skyMode=3,4"))
               .PropAttribute("description", String(u8"Sky yaw (radians)"))
           .Property<&EnvironmentSettings::skyHorizon>("skyHorizon")
               .PropAttribute("visibleWhen", String(u8"skyMode=0"))
           .Property<&EnvironmentSettings::skyZenith>("skyZenith")
               .PropAttribute("visibleWhen", String(u8"skyMode=0,2"))
               .PropAttribute("displayName", String(u8"Sky Zenith / Color"))
               .PropAttribute("description", String(u8"Zenith color (procedural sky); the flat color in Color mode"))
           .Property<&EnvironmentSettings::skyGround>("skyGround")
               .PropAttribute("visibleWhen", String(u8"skyMode=0"))
           .Property<&EnvironmentSettings::sunIntensity>("sunIntensity")
               .PropAttribute("range", Float4{ 0.0f, 10.0f, 0.05f, 0.0f })
               .PropAttribute("visibleWhen", String(u8"skyMode=0,1,2"))
           .Property<&EnvironmentSettings::sunAngularSize>("sunAngularSize")
               .PropAttribute("range", Float4{ 0.05f, 10.0f, 0.05f, 0.0f })
               .PropAttribute("visibleWhen", String(u8"skyMode=0,1,2"))
               .PropAttribute("description", String(u8"Sun disc size (degrees)"))
           .Property<&EnvironmentSettings::turbidity>("turbidity")
               .PropAttribute("range", Float4{ 2.0f, 10.0f, 0.1f, 0.0f })
               .PropAttribute("visibleWhen", String(u8"skyMode=1"))
               .PropAttribute("description", String(u8"Preetham haze (2 = clear, 10 = hazy)"));
}

DRACONIC_REFLECT_VALUE(ReflectionProbeComponent, "draconic::render")
{
    builder.Property<&ReflectionProbeComponent::halfExtents>("halfExtents")
           .Property<&ReflectionProbeComponent::blendDistance>("blendDistance")
               .PropAttribute("range", Float4{ 0.0f, 10.0f, 0.1f, 0.0f })
               .PropAttribute("description", String(u8"Fade width at the probe volume's edge"))
           .Property<&ReflectionProbeComponent::intensity>("intensity")
               .PropAttribute("range", Float4{ 0.0f, 5.0f, 0.05f, 0.0f })
           .Property<&ReflectionProbeComponent::resolution>("resolution")
           .Property<&ReflectionProbeComponent::priority>("priority")
           .Property<&ReflectionProbeComponent::update>("update")
           .Property<&ReflectionProbeComponent::parallax>("parallax")
           .Property<&ReflectionProbeComponent::enabled>("enabled");
}

} // namespace draconic::render (reflection bodies)

// Registers all render component/enum reflection (idempotent). Called by RenderSubsystem::OnInit
// so every app with a renderer gets reflected components for free. Non-inline: the body touches
// module-linkage registration functions, which an exported inline definition may not.
export namespace draconic::render
{
    void RegisterRenderComponentReflection();
}

namespace draconic::render
{
    void RegisterRenderComponentReflection()
    {
        static const bool once = []() {
            DraconicRegisterEnum_LightType();
            DraconicRegisterEnum_SpriteOrientation();
            DraconicRegisterEnum_ShadowUpdateMode();
            DraconicRegisterEnum_ProbeUpdateMode();
            DraconicRegisterEnum_SkyMode();
            DraconicRegisterValue_EnvironmentSettings();
            DraconicRegisterValue_MeshComponent();
        DraconicRegisterValue_InstancedMeshComponent();
            DraconicRegisterValue_CameraComponent();
            DraconicRegisterValue_LightComponent();
            DraconicRegisterValue_SpriteComponent();
            DraconicRegisterValue_DecalComponent();
            DraconicRegisterValue_ReflectionProbeComponent();
            return true;
        }();
        (void)once;
    }
} // namespace draconic::render

/// Draconic::RenderSubsystem — the `:extract` partition.
///
/// Extraction: read a Scene's render components into a render::ExtractedScene (world-space
/// RenderData) + a render::ViewCamera, both pushed to the (scene-agnostic) renderer. This is
/// the one-way seam — this layer depends on both draconic.scene and draconic.render; the renderer
/// depends on neither. Run after the scene's transforms are current (the tick).
///
/// These are the providers in the design's terms (§5): a MeshComponent provider and the
/// camera reader. As more component types land (lights, probes), each gets its own provider
/// writing its own RenderData category into the snapshot.

module;
#include "Core/Prelude.h"

export module draconic.render.subsystem:extract;

import draconic.core;
import draconic.scene;
import draconic.render;          // ExtractedScene / MeshRenderData / ViewCamera / categories
import draconic.materials;       // BlendMode (category mapping)
import draconic.geometry;        // StaticMesh::bounds (world bounding sphere for shadow-caster culling)
import :components;

using namespace draconic::core;

export namespace draconic::render {

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

// Below this many mesh components, parallel extraction's overhead isn't worth it — extract
// serially. (Tuned conservatively; the win is at thousands of renderables.)
inline constexpr u32 kParallelExtractThreshold = 256;

// World-space bounding-sphere radius of a local AABB under a transform: the diagonal half-extent
// scaled by the largest axis scale (basis-row length, row-vector convention) — conservative but
// cheap. Used for sphere-vs-light culling of shadow casters (phase 5.4).
[[nodiscard]] inline f32 WorldBoundsRadius(const AABB& local, const Mat4& world) {
    const f32 sx = Length(Vec3{ world.m[0][0], world.m[0][1], world.m[0][2] });
    const f32 sy = Length(Vec3{ world.m[1][0], world.m[1][1], world.m[1][2] });
    const f32 sz = Length(Vec3{ world.m[2][0], world.m[2][1], world.m[2][2] });
    return Length(local.Extents()) * Max(sx, Max(sy, sz));
}

// Fill one MeshRenderData from a component (a pure read of precomputed transforms + borrowed
// resource pointers — safe to call concurrently across components after UpdateTransforms).
inline void FillMeshRenderData(scene::Scene& scene, const MeshComponent& mc, scene::EntityHandle e,
                               MeshRenderData& rd) {
    rd.world       = scene.GetWorldMatrix(e);
    const AABB lb  = (mc.mesh.Get() != nullptr) ? mc.mesh->bounds : AABB{ Vec3{ 0, 0, 0 }, Vec3{ 0, 0, 0 } };
    rd.worldCenter = TransformPoint(lb.Center(), rd.world);       // bounds center (cull + depth sort)
    rd.worldRadius = WorldBoundsRadius(lb, rd.world);
    rd.color       = mc.color;
    rd.mesh        = mc.mesh.Get();
    rd.material    = mc.material.Get();
    rd.entityId    = PackEntity(e);
    rd.category    = CategoryForMaterial(mc.material.Get());
    // Batch-cluster key for the sort (opaque draws stay contiguous by mesh+material). rendererId keeps
    // its default 0 — the MeshRenderer is the first-registered renderer, so mesh data routes to it.
    rd.sortBatchKey = BatchKey(mc.mesh.Get(), mc.material.Get());
    rd.boneMatrices = mc.boneMatrices;   // borrowed for the frame (GPU skinning); null => static
    rd.prevBoneMatrices = mc.prevBoneMatrices;   // borrowed; null => reuse current (no motion)
    rd.boneCount    = mc.boneCount;
    rd.submeshMaterials     = mc.submeshMaterials.IsEmpty() ? nullptr : mc.submeshMaterials.Data();
    rd.submeshMaterialCount = static_cast<u32>(mc.submeshMaterials.Size());
}

// Fills `out` with one MeshRenderData per visible MeshComponent in `scene`, allocating from
// `out`'s own arena. Serial. Assumes transforms are current; `out` should be Reset beforehand.
inline void ExtractSceneInto(scene::Scene& scene, ExtractedScene& out) {
    if (auto* meshes = scene.GetSystem<MeshComponentManager>()) {
        meshes->ForEach([&](MeshComponent& mc, scene::EntityHandle e) {
            if (!mc.visible || mc.mesh.Get() == nullptr) { return; }
            if (MeshRenderData* rd = out.Add<MeshRenderData>()) { FillMeshRenderData(scene, mc, e, *rd); }
        });
    }
}

// Same, but extraction is parallelized across the global JobSystem when present + worthwhile:
// each worker fills its own RenderContext arena + item list (no contention), then a
// single-threaded merge gathers them into `out`. Falls back to serial (into ctx slot 0) when
// the job system is absent or the scene is small. `out` is Reset; `ctx` arenas accumulate
// across the frame (the caller BeginFrame's it once per frame).
inline void ExtractSceneInto(scene::Scene& scene, ExtractedScene& out, RenderContext& ctx) {
    out.Reset();
    auto* meshes = scene.GetSystem<MeshComponentManager>();
    if (meshes == nullptr) { return; }
    const u32 count = meshes->Count();
    if (count == 0) { return; }

    ctx.ResetItems();
    const Span<MeshComponent>            comps  = meshes->Dense();
    const Span<const scene::EntityHandle> owners = meshes->Owners();

    const bool parallel = HasGlobalJobSystem() && count >= kParallelExtractThreshold;
    if (parallel) {
        JobSystem& jobs = GlobalJobs();
        jobs.ParallelFor(count, [&](u32 i) {
            const MeshComponent& mc = comps[i];
            if (!mc.visible || mc.mesh.Get() == nullptr) { return; }
            const u32 slot = jobs.CurrentSlot();
            MeshRenderData* rd = ctx.Arena(slot).New<MeshRenderData>();
            if (rd == nullptr) { return; }
            FillMeshRenderData(scene, mc, owners[i], *rd);
            ctx.Items(slot).PushBack(static_cast<RenderData*>(rd));
        });
    } else {
        FrameArena& arena = ctx.Arena(0);
        Array<RenderData*>& items = ctx.Items(0);
        for (u32 i = 0; i < count; ++i) {
            const MeshComponent& mc = comps[i];
            if (!mc.visible || mc.mesh.Get() == nullptr) { continue; }
            MeshRenderData* rd = arena.New<MeshRenderData>();
            if (rd == nullptr) { continue; }
            FillMeshRenderData(scene, mc, owners[i], *rd);
            items.PushBack(static_cast<RenderData*>(rd));
        }
    }
    ctx.MergeInto(out);
}

// Fills `out` with one SpriteRenderData per visible SpriteComponent (serial — sprites are few). Stamps
// the sprite renderer's dispatch id so emission routes them to the SpriteRenderer, and category
// Transparent so they sort back-to-front and ride the blended forward pass alongside transparent meshes.
inline void ExtractSpritesInto(scene::Scene& scene, ExtractedScene& out, u16 spriteRendererId) {
    auto* sprites = scene.GetSystem<SpriteComponentManager>();
    if (sprites == nullptr) { return; }
    sprites->ForEach([&](SpriteComponent& sc, scene::EntityHandle e) {
        if (!sc.visible || sc.texture == nullptr) { return; }
        SpriteRenderData* rd = out.Add<SpriteRenderData>();
        if (rd == nullptr) { return; }
        rd->category    = RenderCategories::Transparent;
        rd->rendererId  = spriteRendererId;
        rd->worldCenter = TransformPoint(Vec3{ 0, 0, 0 }, scene.GetWorldMatrix(e));
        rd->size        = sc.size;
        rd->uvRect      = sc.uvRect;
        rd->tint        = sc.tint;
        rd->orientation = sc.orientation;
        rd->additive    = sc.additive;
        rd->texture     = sc.texture;
    });
}

// Fills `out`'s decal list from the scene's DecalComponents (serial — decals are few). The box world
// bakes the component `size` as an extra scale on top of the entity transform (Scale then world, row-
// vector order), so the entity's rotation orients the projection axis and `size` sets the box extents.
inline void ExtractDecalsInto(scene::Scene& scene, ExtractedScene& out) {
    auto* decals = scene.GetSystem<DecalComponentManager>();
    if (decals == nullptr) { return; }
    decals->ForEach([&](DecalComponent& dc, scene::EntityHandle e) {
        if (!dc.visible || dc.texture == nullptr) { return; }
        DecalInstance di;
        di.world     = Mat4::Scale(dc.size) * scene.GetWorldMatrix(e);
        di.color     = dc.color;
        di.fadeStart = dc.fadeStart;
        di.fadeEnd   = dc.fadeEnd;
        di.texture   = dc.texture;
        out.AddDecal(di);
    });
}

// Reads the scene's primary camera into `out` (view = inverse world; projection from its
// fields). When `outClear` is given, also writes the camera's clear color. Returns false if
// no primary CameraComponent exists.
[[nodiscard]] inline bool ExtractPrimaryCamera(scene::Scene& scene, ViewCamera& out, Color* outClear = nullptr) {
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
            if (outClear != nullptr) { *outClear = cam.clearColor; }
        });
    }
    return found;
}

// Packs every enabled LightComponent into `out` as a GpuLight shading input (world position +
// forward direction from the entity's transform). Assumes transforms are current. The FIRST enabled
// directional light flagged castsShadows becomes the scene's shadow caster (its cascades are fit to
// the camera frustum later, in RenderFrame).
inline void ExtractLightsInto(scene::Scene& scene, ExtractedScene& out) {
    auto* lights = scene.GetSystem<LightComponentManager>();
    if (lights == nullptr) { return; }
    bool haveShadow = false;
    u32  rtTiles = 0, stTiles = 0;   // tiles used per atlas layer (realtime / static); capped separately
    u32  flatEntries = 0;            // running GpuLocalShadow entry index = the next caster's shadowIndex
    lights->ForEach([&](LightComponent& lc, scene::EntityHandle e) {
        if (!lc.enabled) { return; }
        const Mat4 world = scene.GetWorldMatrix(e);
        GpuLight g;
        g.positionWS  = TransformPoint(Vec3{ 0, 0, 0 }, world);
        // Forward is -Z (row 2 negated) in world space (row-major, row-vector convention).
        g.directionWS = Normalized(Vec3{ -world.m[2][0], -world.m[2][1], -world.m[2][2] });
        g.range       = lc.range;
        g.color       = Vec3{ lc.color.r, lc.color.g, lc.color.b };
        g.intensity   = lc.intensity;
        g.type        = static_cast<f32>(static_cast<u32>(lc.type));
        g.innerCos    = Cos(lc.innerAngle);
        g.outerCos    = Cos(lc.outerAngle);
        if (!haveShadow && lc.castsShadows && lc.type == LightType::Directional) {
            haveShadow = true;
            g.shadowIndex = 0.0f;       // marks this light as shadowed in the forward shader
            DirectionalShadow ds;
            ds.direction = g.directionWS;
            ds.valid     = true;
            out.SetDirectionalShadow(ds);
        }
        // Local (spot/point) shadow casters (5.3): assign shadowIndex = the caster's BASE atlas tile
        // here, then register it; the ShadowSystem builds the perspective matrix/matrices at frame
        // time. A spot uses 1 tile, a point 6 (cube faces). Capped at the atlas tile budget.
        const bool localCaster = lc.castsShadows && (lc.type == LightType::Spot || lc.type == LightType::Point);
        const u32  tilesNeeded = (lc.type == LightType::Point) ? 6u : 1u;
        const bool isStatic    = (lc.shadowUpdate == ShadowUpdateMode::Static);
        u32&       layerTiles  = isStatic ? stTiles : rtTiles;   // each atlas layer has its own budget
        if (localCaster && layerTiles + tilesNeeded <= kMaxLocalShadowTiles) {
            g.shadowIndex = static_cast<f32>(flatEntries);       // base entry into the GpuLocalShadow buffer
            LocalShadowCaster c;
            c.type        = static_cast<u32>(lc.type);
            c.positionWS  = g.positionWS;
            c.directionWS = g.directionWS;
            c.range       = lc.range;
            c.outerAngle  = lc.outerAngle;
            c.isStatic    = isStatic;
            out.AddLocalShadowCaster(c);
            layerTiles  += tilesNeeded;
            flatEntries += tilesNeeded;
        }
        out.AddLight(g);
    });
}

// Reads the scene's environment (ambient) into the snapshot. Defaults to the snapshot's own dim
// ambient when the scene has no EnvironmentSystem. Premultiplies color × intensity.
inline void ExtractEnvironmentInto(scene::Scene& scene, ExtractedScene& out) {
    if (auto* env = scene.GetSystem<EnvironmentSystem>()) {
        const EnvironmentSettings& e = env->Environment();
        out.SetAmbient(Vec3{ e.ambientColor.r, e.ambientColor.g, e.ambientColor.b } * e.ambientIntensity);
        SkySnapshot s{};
        s.mode      = e.skyMode;
        s.intensity = e.skyIntensity;
        s.rotation  = e.skyRotation;
        s.horizon   = Vec3{ e.skyHorizon.r, e.skyHorizon.g, e.skyHorizon.b };
        s.zenith    = Vec3{ e.skyZenith.r, e.skyZenith.g, e.skyZenith.b };
        s.ground    = Vec3{ e.skyGround.r, e.skyGround.g, e.skyGround.b };
        s.sunIntensity   = e.sunIntensity;
        s.sunAngularSize = e.sunAngularSize;
        s.turbidity      = e.turbidity;
        out.SetSky(s);
    }
}

} // namespace draconic::render

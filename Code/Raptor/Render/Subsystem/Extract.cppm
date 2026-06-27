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

// Below this many mesh components, parallel extraction's overhead isn't worth it — extract
// serially. (Tuned conservatively; the win is at thousands of renderables.)
inline constexpr u32 kParallelExtractThreshold = 256;

// Fill one MeshRenderData from a component (a pure read of precomputed transforms + borrowed
// resource pointers — safe to call concurrently across components after UpdateTransforms).
inline void FillMeshRenderData(scene::Scene& scene, const MeshComponent& mc, scene::EntityHandle e,
                               MeshRenderData& rd) {
    rd.world       = scene.GetWorldMatrix(e);
    rd.worldCenter = TransformPoint(Vec3{ 0, 0, 0 }, rd.world);   // mesh bounds center later
    rd.color       = mc.color;
    rd.mesh        = mc.mesh.Get();
    rd.material    = mc.material.Get();
    rd.entityId    = PackEntity(e);
    rd.category    = CategoryForMaterial(mc.material.Get());
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
// forward direction from the entity's transform). Assumes transforms are current.
inline void ExtractLightsInto(scene::Scene& scene, ExtractedScene& out) {
    auto* lights = scene.GetSystem<LightComponentManager>();
    if (lights == nullptr) { return; }
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
        out.AddLight(g);
    });
}

} // namespace raptor::render

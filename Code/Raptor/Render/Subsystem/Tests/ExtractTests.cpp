// The scene->render extraction bridge: spawn a camera + mesh entities in a scene and
// verify ExtractSceneInto snapshots the per-mesh world matrices + mesh/material into an
// ExtractedScene (the data the scene-agnostic renderer consumes), and ExtractPrimaryCamera
// reads the camera view/projection.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import raptor.core;
import raptor.scene;
import raptor.geometry;
import raptor.materials;
import raptor.render;             // ExtractedScene / MeshRenderData / ViewCamera (scene-agnostic)
import raptor.render.subsystem;   // components + ExtractSceneInto / ExtractPrimaryCamera

using namespace raptor::core;
using namespace raptor::render;
namespace sc = raptor::scene;
namespace geo = raptor::geometry;
namespace mat = raptor::materials;

namespace { bool Near(f32 a, f32 b) { return Abs(a - b) < 1e-3f; } }

TEST_CASE("ExtractSceneInto builds the draw list; ExtractPrimaryCamera reads the camera")
{
    sc::Scene scene(u8"world");
    auto* meshes  = scene.AddSystem<MeshComponentManager>();
    auto* cameras = scene.AddSystem<CameraComponentManager>();

    sc::EntityHandle camEntity = scene.CreateEntity(u8"camera");
    scene.SetLocalPosition(camEntity, Vec3{ 0, 0, 5 });
    CameraComponent& cam = cameras->Add(camEntity);
    cam.aspect = 1.0f;

    RefPtr<geo::StaticMesh> cube = geo::Primitives::Cube(1.0f);
    RefPtr<mat::Material> material = mat::MaterialBuilder(u8"lit").Shader(u8"forward").Build();

    sc::EntityHandle a = scene.CreateEntity(u8"a");
    scene.SetLocalPosition(a, Vec3{ -2, 0, 0 });
    { MeshComponent& m = meshes->Add(a); m.mesh = cube; m.material = material; m.color = Color{ 0.2f, 0.4f, 0.8f, 1.0f }; }

    sc::EntityHandle b = scene.CreateEntity(u8"b");
    scene.SetLocalPosition(b, Vec3{ 3, 0, 0 });
    { MeshComponent& m = meshes->Add(b); m.mesh = cube; m.material = material; }

    scene.UpdateTransforms();

    ViewCamera vc;
    REQUIRE(ExtractPrimaryCamera(scene, vc));
    CHECK(Near(vc.view.m[3][2], -5.0f));                 // view = inverse(camera world)
    CHECK_FALSE(Near(vc.projection.m[2][3], 0.0f));      // a real perspective projection

    ExtractedScene snapshot;
    ExtractSceneInto(scene, snapshot);
    REQUIRE(snapshot.Size() == 2);

    f32 sumX = 0.0f;
    bool sawBlue = false;
    for (RenderData* rd : snapshot.Items()) {
        const auto* m = static_cast<const MeshRenderData*>(rd);
        CHECK(m->category == RenderCategories::Opaque);  // opaque material -> opaque category
        CHECK(m->mesh == cube.Get());
        CHECK(m->material == material.Get());
        CHECK(m->entityId != 0);                         // tagged with a packed entity handle
        sumX += m->world.m[3][0];
        if (Near(m->color.b, 0.8f)) { sawBlue = true; }  // per-instance color carried through
    }
    CHECK(Near(sumX, 1.0f));                              // -2 + 3
    CHECK(sawBlue);
}

TEST_CASE("ExtractSceneInto skips invisible + mesh-less components; no primary camera reported")
{
    sc::Scene scene;
    auto* meshes  = scene.AddSystem<MeshComponentManager>();
    auto* cameras = scene.AddSystem<CameraComponentManager>();

    RefPtr<geo::StaticMesh> mesh = geo::Primitives::Quad();

    sc::EntityHandle visible = scene.CreateEntity();
    { MeshComponent& m = meshes->Add(visible); m.mesh = mesh; }

    sc::EntityHandle hidden = scene.CreateEntity();
    { MeshComponent& m = meshes->Add(hidden); m.mesh = mesh; m.visible = false; }

    meshes->Add(scene.CreateEntity());                   // no mesh assigned

    sc::EntityHandle cam2 = scene.CreateEntity();
    { CameraComponent& c = cameras->Add(cam2); c.primary = false; }

    scene.UpdateTransforms();

    ViewCamera vc;
    CHECK_FALSE(ExtractPrimaryCamera(scene, vc));         // no primary camera

    ExtractedScene snapshot;
    ExtractSceneInto(scene, snapshot);
    REQUIRE(snapshot.Size() == 1);                        // only the visible, meshed one
}

TEST_CASE("ExtractSceneInto on a scene without render managers yields an empty snapshot")
{
    sc::Scene scene;
    scene.CreateEntity();
    scene.UpdateTransforms();

    ViewCamera vc;
    CHECK_FALSE(ExtractPrimaryCamera(scene, vc));

    ExtractedScene snapshot;
    ExtractSceneInto(scene, snapshot);
    CHECK(snapshot.Size() == 0);
}

namespace {
// Builds a scene of `n` quad meshes at world x = 0..n-1; returns the mesh/material alive.
void BuildBigScene(sc::Scene& scene, int n, RefPtr<geo::StaticMesh>& mesh, RefPtr<mat::Material>& material) {
    auto* meshes = scene.AddSystem<MeshComponentManager>();
    mesh = geo::Primitives::Quad();
    material = mat::MaterialBuilder(u8"lit").Shader(u8"forward").Build();
    for (int i = 0; i < n; ++i) {
        sc::EntityHandle e = scene.CreateEntity();
        scene.SetLocalPosition(e, Vec3{ static_cast<f32>(i), 0, 0 });
        MeshComponent& m = meshes->Add(e); m.mesh = mesh; m.material = material;
    }
    scene.UpdateTransforms();
}
// The world-x sum is a drop/dup-proof invariant: each entity contributes its index exactly once.
f64 SumWorldX(const ExtractedScene& s) {
    f64 sum = 0;
    for (RenderData* rd : s.Items()) { sum += static_cast<MeshRenderData*>(rd)->world.m[3][0]; }
    return sum;
}
}

TEST_CASE("ExtractSceneInto (parallel) extracts every renderable exactly once")
{
    InitGlobalJobSystem(4);
    {
        constexpr int N = 2000;                              // > kParallelExtractThreshold
        sc::Scene scene;
        RefPtr<geo::StaticMesh> mesh; RefPtr<mat::Material> material;
        BuildBigScene(scene, N, mesh, material);

        RenderContext ctx;
        ctx.BeginFrame(GlobalJobs().SlotCount());
        ExtractedScene out;
        ExtractSceneInto(scene, out, ctx);                   // takes the parallel path

        REQUIRE(out.Size() == static_cast<usize>(N));        // no drops, no duplicates
        CHECK(SumWorldX(out) == static_cast<f64>(N) * (N - 1) / 2.0);
    }
    ShutdownGlobalJobSystem();
}

TEST_CASE("ExtractSceneInto (ctx) falls back to serial with no job system")
{
    REQUIRE_FALSE(HasGlobalJobSystem());                     // none started in this test binary
    sc::Scene scene;
    RefPtr<geo::StaticMesh> mesh; RefPtr<mat::Material> material;
    BuildBigScene(scene, 50, mesh, material);

    RenderContext ctx;
    ctx.BeginFrame(1);
    ExtractedScene out;
    ExtractSceneInto(scene, out, ctx);

    REQUIRE(out.Size() == 50u);
    CHECK(SumWorldX(out) == static_cast<f64>(50) * 49 / 2.0);
}

TEST_CASE("ExtractSceneInto maps a transparent material to the Transparent category")
{
    sc::Scene scene;
    auto* meshes = scene.AddSystem<MeshComponentManager>();

    RefPtr<geo::StaticMesh> mesh = geo::Primitives::Quad();
    RefPtr<mat::Material> glass = mat::MaterialBuilder(u8"glass").Shader(u8"forward").Transparent().Build();

    sc::EntityHandle e = scene.CreateEntity();
    { MeshComponent& m = meshes->Add(e); m.mesh = mesh; m.material = glass; }
    scene.UpdateTransforms();

    ExtractedScene snapshot;
    ExtractSceneInto(scene, snapshot);
    REQUIRE(snapshot.Size() == 1);
    const auto* md = static_cast<const MeshRenderData*>(snapshot.Items()[0]);
    CHECK(md->category == RenderCategories::Transparent);
}

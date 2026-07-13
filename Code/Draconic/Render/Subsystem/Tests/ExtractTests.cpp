// The scene->render extraction bridge: spawn a camera + mesh entities in a scene and
// verify ExtractSceneInto snapshots the per-mesh world matrices + mesh/material into an
// ExtractedScene (the data the scene-agnostic renderer consumes), and ExtractPrimaryCamera
// reads the camera view/projection.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.scene;
import draconic.geometry;
import draconic.materials;
import draconic.render;             // ExtractedScene / MeshRenderData / ViewCamera (scene-agnostic)
import draconic.render.subsystem;   // components + ExtractSceneInto / ExtractPrimaryCamera
import draconic.resource;
import draconic.rhi;
import draconic.texture.resource;   // texture::Texture (the sky-texture product)

using namespace draconic::core;
using namespace draconic::render;
namespace scene = draconic::scene;
namespace geometry = draconic::geometry;
namespace materials = draconic::materials;

namespace { bool Near(f32 a, f32 b) { return Abs(a - b) < 1e-3f; } }

TEST_CASE("ExtractSceneInto builds the draw list; ExtractPrimaryCamera reads the camera")
{
    scene::Scene scene(u8"world");
    auto* meshes  = scene.AddSystem<MeshComponentManager>();
    auto* cameras = scene.AddSystem<CameraComponentManager>();

    scene::EntityHandle camEntity = scene.CreateEntity(u8"camera");
    scene.SetLocalPosition(camEntity, Float3{ 0, 0, 5 });
    CameraComponent& cam = cameras->Add(camEntity);
    cam.aspect = 1.0f;

    RefPtr<geometry::StaticMesh> cube = geometry::Primitives::Cube(1.0f);
    RefPtr<materials::Material> material = materials::MaterialBuilder(u8"lit").Shader(u8"forward").Build();

    scene::EntityHandle a = scene.CreateEntity(u8"a");
    scene.SetLocalPosition(a, Float3{ -2, 0, 0 });
    { MeshComponent& m = meshes->Add(a); m.mesh = cube; m.material = material; m.color = Color{ 0.2f, 0.4f, 0.8f, 1.0f }; }

    scene::EntityHandle b = scene.CreateEntity(u8"b");
    scene.SetLocalPosition(b, Float3{ 3, 0, 0 });
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
    scene::Scene scene;
    auto* meshes  = scene.AddSystem<MeshComponentManager>();
    auto* cameras = scene.AddSystem<CameraComponentManager>();

    RefPtr<geometry::StaticMesh> mesh = geometry::Primitives::Quad();

    scene::EntityHandle visible = scene.CreateEntity();
    { MeshComponent& m = meshes->Add(visible); m.mesh = mesh; }

    scene::EntityHandle hidden = scene.CreateEntity();
    { MeshComponent& m = meshes->Add(hidden); m.mesh = mesh; m.visible = false; }

    meshes->Add(scene.CreateEntity());                   // no mesh assigned

    scene::EntityHandle cam2 = scene.CreateEntity();
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
    scene::Scene scene;
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
void BuildBigScene(scene::Scene& scene, int n, RefPtr<geometry::StaticMesh>& mesh, RefPtr<materials::Material>& material) {
    auto* meshes = scene.AddSystem<MeshComponentManager>();
    mesh = geometry::Primitives::Quad();
    material = materials::MaterialBuilder(u8"lit").Shader(u8"forward").Build();
    for (int i = 0; i < n; ++i) {
        scene::EntityHandle e = scene.CreateEntity();
        scene.SetLocalPosition(e, Float3{ static_cast<f32>(i), 0, 0 });
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
        scene::Scene scene;
        RefPtr<geometry::StaticMesh> mesh; RefPtr<materials::Material> material;
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
    scene::Scene scene;
    RefPtr<geometry::StaticMesh> mesh; RefPtr<materials::Material> material;
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
    scene::Scene scene;
    auto* meshes = scene.AddSystem<MeshComponentManager>();

    RefPtr<geometry::StaticMesh> mesh = geometry::Primitives::Quad();
    RefPtr<materials::Material> glass = materials::MaterialBuilder(u8"glass").Shader(u8"forward").Transparent().Build();

    scene::EntityHandle e = scene.CreateEntity();
    { MeshComponent& m = meshes->Add(e); m.mesh = mesh; m.material = glass; }
    scene.UpdateTransforms();

    ExtractedScene snapshot;
    ExtractSceneInto(scene, snapshot);
    REQUIRE(snapshot.Size() == 1);
    const auto* md = static_cast<const MeshRenderData*>(snapshot.Items()[0]);
    CHECK(md->category == RenderCategories::Transparent);
}

TEST_CASE("instanced-mesh: seeded identity instance + entity-relative composition")
{
    scene::Scene scene(u8"world");
    auto* mgr = scene.AddSystem<InstancedMeshComponentManager>();
    RefPtr<geometry::StaticMesh> cube = geometry::Primitives::Cube(1.0f);

    // A fresh component is seeded with ONE identity instance (editor workflow: assign a mesh,
    // see it render at the entity), and instances compose with the entity's world transform.
    scene::EntityHandle e = scene.CreateEntity(u8"scatter");
    scene.SetLocalPosition(e, Float3{ 5, 0, 0 });
    InstancedMeshComponent& c = mgr->Add(e);
    REQUIRE(c.Count() == 1u);
    c.mesh = cube;
    scene.UpdateTransforms();

    ExtractedScene out;
    ExtractInstancedMeshesInto(scene, out);
    REQUIRE(out.Items().Size() == 1u);
    const auto* rd = static_cast<const MultiMeshRenderData*>(out.Items()[0]);
    REQUIRE(rd->instanceCount == 1u);
    CHECK(Near(rd->transforms[0].m[3][0], 5.0f));        // identity instance * entity world
    CHECK(Near(rd->worldCenter.x, 5.0f));
    const u32 firstVersion = rd->version;

    // Moving the ENTITY moves the set: the composed transforms change and the renderer's upload
    // key (version) bumps even though the authored set didn't change.
    scene.SetLocalPosition(e, Float3{ 5, 7, 0 });
    scene.UpdateTransforms();
    ExtractedScene out2;
    ExtractInstancedMeshesInto(scene, out2);
    const auto* rd2 = static_cast<const MultiMeshRenderData*>(out2.Items()[0]);
    CHECK(Near(rd2->transforms[0].m[3][1], 7.0f));
    CHECK(rd2->version != firstVersion);

    // Unmoved + unchanged: no recompose, same version (static sets stay zero-cost).
    ExtractedScene out3;
    ExtractInstancedMeshesInto(scene, out3);
    const auto* rd3 = static_cast<const MultiMeshRenderData*>(out3.Items()[0]);
    CHECK(rd3->version == rd2->version);

    // Authored instances are entity-relative: replace the seed with two local offsets.
    const Float4x4 xf[2] = { Float4x4::Translation(Float3{ 1, 0, 0 }),
                             Float4x4::Translation(Float3{ -1, 0, 0 }) };
    c.SetInstances(Span<const Float4x4>{ xf, 2 });
    ExtractedScene out4;
    ExtractInstancedMeshesInto(scene, out4);
    const auto* rd4 = static_cast<const MultiMeshRenderData*>(out4.Items()[0]);
    REQUIRE(rd4->instanceCount == 2u);
    CHECK(Near(rd4->transforms[0].m[3][0], 6.0f));       // 1 + entity x=5
    CHECK(Near(rd4->transforms[1].m[3][0], 4.0f));       // -1 + entity x=5
}

TEST_CASE("ExtractEnvironmentInto carries the sky texture product (uid identity, cube flag)")
{
    draconic::scene::Scene scene(u8"s");
    auto* env = scene.AddSystem<draconic::render::EnvironmentSystem>();
    env->Environment().skyMode = draconic::render::SkyMode::Cubemap;

    // A cube-shaped product (no GPU objects needed - identity/shape are what extraction reads).
    RefPtr<draconic::texture::Texture> sky = MakeRef<draconic::texture::Texture>(DefaultAllocator());
    sky->Adopt(nullptr, nullptr, nullptr, nullptr, 64, 64, rhi::TextureFormat::RGBA8Unorm, /*isCube*/ true);
    env->Environment().skyTexture = sky.Get();   // direct override (picker/serialized path binds by guid)

    draconic::render::ExtractedScene out;
    draconic::render::ExtractEnvironmentInto(scene, out);
    CHECK(out.Sky().mode == draconic::render::SkyMode::Cubemap);
    CHECK(out.Sky().textureUid == sky->Uid());
    CHECK(out.Sky().textureUid != 0u);
    CHECK(out.Sky().textureIsCube);

    // No texture -> no identity (the IBL keeps its programmatic/procedural source).
    env->Environment().skyTexture = draconic::resource::Ref<draconic::texture::Texture>{};
    draconic::render::ExtractedScene out2;
    draconic::render::ExtractEnvironmentInto(scene, out2);
    CHECK(out2.Sky().textureUid == 0u);
}

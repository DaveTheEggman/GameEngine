// Slice 1 — the scene->render extraction bridge: spawn a camera + mesh entities in a
// scene and verify ExtractScene snapshots the camera view/projection and the per-mesh
// world matrices + mesh/material into a renderer-agnostic ExtractedView (the data the
// scene-agnostic renderer consumes).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import raptor.core;
import raptor.scene;
import raptor.geometry;
import raptor.materials;
import raptor.render;             // ExtractedView / Renderable (scene-agnostic)
import raptor.render.subsystem;   // components + ExtractScene (scene-coupled)

using namespace raptor::core;
using namespace raptor::render;
namespace sc = raptor::scene;
namespace geo = raptor::geometry;
namespace mat = raptor::materials;

namespace { bool Near(f32 a, f32 b) { return Abs(a - b) < 1e-3f; } }

TEST_CASE("ExtractScene builds the draw list + camera from a scene")
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
    { MeshComponent& m = meshes->Add(a); m.mesh = cube; m.material = material; }

    sc::EntityHandle b = scene.CreateEntity(u8"b");
    scene.SetLocalPosition(b, Vec3{ 3, 0, 0 });
    { MeshComponent& m = meshes->Add(b); m.mesh = cube; m.material = material; }

    scene.UpdateTransforms();
    ExtractedView ev = ExtractScene(scene);

    REQUIRE(ev.hasCamera);
    CHECK(Near(ev.view.m[3][2], -5.0f));                 // view = inverse(camera world)
    CHECK_FALSE(Near(ev.projection.m[2][3], 0.0f));      // a real perspective projection

    REQUIRE(ev.renderables.Size() == 2);
    f32 sumX = 0.0f;
    for (const Renderable& r : ev.renderables) {
        CHECK(r.mesh == cube.Get());
        CHECK(r.material == material.Get());
        CHECK(r.id != 0);                                // tagged with a packed entity handle
        sumX += r.worldMatrix.m[3][0];
    }
    CHECK(Near(sumX, 1.0f));                              // -2 + 3
}

TEST_CASE("ExtractScene skips invisible + mesh-less components, and non-primary cameras")
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
    ExtractedView ev = ExtractScene(scene);

    CHECK_FALSE(ev.hasCamera);                            // no primary camera
    REQUIRE(ev.renderables.Size() == 1);                 // only the visible, meshed one
}

TEST_CASE("ExtractScene on a scene without render managers yields an empty view")
{
    sc::Scene scene;
    scene.CreateEntity();
    scene.UpdateTransforms();
    ExtractedView ev = ExtractScene(scene);
    CHECK_FALSE(ev.hasCamera);
    CHECK(ev.renderables.Size() == 0);
}

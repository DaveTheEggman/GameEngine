// The GPU splat-texture cache (engine.terrain :splattexture): UID-keyed (two rasters at the same
// address must not alias), a paint's version bump RETIRES the old texture/view through the queue
// (never a direct in-flight destroy), Clear frees at shutdown, and the manager's extract derives one
// splat texture per splatmap-bearing terrain. Mirrors the height-texture cache contract.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.rhi;
import foundation.rhi.null;
import foundation.render;
import foundation.scene;
import foundation.heightfield;
import foundation.terrain.resource;
import engine.terrain;

using namespace foundation::core;
namespace rhi = foundation::rhi;
namespace render = foundation::render;
namespace scene = foundation::scene;
namespace terrain = foundation::terrain;

TEST_CASE("splat cache: GetOrCreate caches by uid+version; a version bump retires + rebuilds")
{
    rhi::null::NullDevice device{DefaultAllocator()};
    render::GpuRetireQueue retire;
    retire.Initialize(&device, 2);

    engine::terrain::TerrainSplatTextureCache cache;
    cache.SetRetireQueue(&retire);

    RefPtr<terrain::Splatmap> sm =
        MakeRef<terrain::Splatmap>(DefaultAllocator(), 8, 8);
    sm->SeedLayer0();

    rhi::TextureView* v1 = cache.GetOrCreate(device, *sm, sm->Version());
    REQUIRE(v1 != nullptr);
    CHECK(cache.Size() == 1u);
    // Same uid + version returns the SAME view (no rebuild).
    CHECK(cache.GetOrCreate(device, *sm, sm->Version()) == v1);
    CHECK(cache.Size() == 1u);
    CHECK(retire.PendingCount() == 0u);

    // A paint bumps the version: the entry rebuilds in place and the old texture+view are RETIRED
    // (not destroyed) - a submitted frame still samples the old view through the set-3 bind group.
    sm->BumpVersion();
    rhi::TextureView* v2 = cache.GetOrCreate(device, *sm, sm->Version());
    REQUIRE(v2 != nullptr);
    CHECK(v2 != v1);
    CHECK(cache.Size() == 1u);          // still one entry (rebuilt in place)
    CHECK(retire.PendingCount() == 2u); // old texture + old view queued, not freed
}

TEST_CASE("splat cache: keyed by uid, so two distinct rasters never alias")
{
    rhi::null::NullDevice device{DefaultAllocator()};
    engine::terrain::TerrainSplatTextureCache cache;

    RefPtr<terrain::Splatmap> a = MakeRef<terrain::Splatmap>(DefaultAllocator(), 4, 4);
    RefPtr<terrain::Splatmap> b = MakeRef<terrain::Splatmap>(DefaultAllocator(), 4, 4);
    CHECK(a->uid != b->uid);

    rhi::TextureView* va = cache.GetOrCreate(device, *a, a->Version());
    rhi::TextureView* vb = cache.GetOrCreate(device, *b, b->Version());
    REQUIRE(va != nullptr);
    REQUIRE(vb != nullptr);
    CHECK(va != vb);
    CHECK(cache.Size() == 2u);

    cache.Clear(device);
    CHECK(cache.Size() == 0u);
}

TEST_CASE("splat cache: an empty raster yields no texture")
{
    rhi::null::NullDevice device{DefaultAllocator()};
    engine::terrain::TerrainSplatTextureCache cache;
    terrain::Splatmap empty; // default = 0x0
    CHECK(cache.GetOrCreate(device, empty, empty.Version()) == nullptr);
    CHECK(cache.Size() == 0u);
}

TEST_CASE("engine.terrain: a splatmap-bearing terrain derives one splat texture on extract")
{
    rhi::null::NullDevice device{DefaultAllocator()};

    scene::Scene sceneObj;
    engine::terrain::AddTerrainSceneManagers(sceneObj);
    auto* mgr = sceneObj.GetSystem<engine::terrain::TerrainComponentManager>();
    REQUIRE(mgr != nullptr);
    mgr->SetRenderContext(&device, 7, nullptr);

    // In-memory terrain: heightfield (for the draw) + a CPU splatmap (the painted source of truth).
    RefPtr<foundation::heightfield::Heightfield> grid = MakeRef<foundation::heightfield::Heightfield>(
        DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
    RefPtr<terrain::Splatmap> splat = MakeRef<terrain::Splatmap>(DefaultAllocator(), 32, 32);
    splat->SeedLayer0();
    auto res = MakeRef<terrain::TerrainResource>(DefaultAllocator());
    res->heightfield = grid.Get();
    res->splatmap = splat.Get();
    // One layer albedo so the material path engages (splatmap + >=1 layer -> splat, not the ramp).
    res->layers.PushBack(terrain::TerrainResource::Layer{});

    const scene::EntityHandle e = sceneObj.CreateEntity(u8"terrain");
    engine::terrain::TerrainComponent& c = mgr->Add(e);
    c.terrain = res.Get();
    sceneObj.Start();

    foundation::render::ExtractedScene snapshot;
    mgr->ExtractRenderData(snapshot);
    CHECK(mgr->SplatTextureCount() == 1u); // the extract built + cached the GPU splat texture

    mgr->ClearGpu();
    CHECK(mgr->SplatTextureCount() == 0u); // freed through the live device
}

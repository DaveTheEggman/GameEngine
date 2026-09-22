// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// engine.vegetation - the component's reflection + wire, and the manager's contract: sets built
// only for chunks in range, one MultiMeshRenderData per (layer, chunk) with the fade prefix as its
// count, region-scoped regrow on a version bump, the build budget, and nothing extra without a
// layer.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

#include <initializer_list>

import foundation.core;
import foundation.scene;
import foundation.geometry;
import foundation.heightfield;
import foundation.terrain;
import foundation.terrain.resource;
import foundation.vegetation;
import foundation.render;
import engine.terrain;
import engine.vegetation;

using namespace foundation::core;
namespace scene = foundation::scene;
namespace hf = foundation::heightfield;
namespace tmodel = foundation::terrain;
namespace veg = foundation::vegetation;
namespace render = foundation::render;
namespace geometry = foundation::geometry;
using engine::vegetation::VegetationLayerComponent;
using engine::vegetation::VegetationLayerComponentManager;

namespace
{
    constexpr i32 kGrid = 129;     // 2 x 2 chunks
    constexpr f32 kWorld = 128.0f; // 1 m per quad; chunks are 64 x 64 m, centred on the origin

    RefPtr<hf::Heightfield> MakeFlat(f32 height)
    {
        auto grid = MakeRef<hf::Heightfield>(DefaultAllocator(), kGrid, Float2{kWorld, kWorld},
                                             0.0f, 40.0f);
        const hf::Height sample = grid->WorldYToSample(height);
        for (i32 z = 0; z < kGrid; ++z)
        {
            for (i32 x = 0; x < kGrid; ++x)
            {
                grid->SetSample(x, z, sample);
            }
        }
        return grid;
    }

    // Palette layer 0 one-hot on the left half (x < 0), base on the right.
    RefPtr<tmodel::SplatWeights> MakeHalfSplat()
    {
        constexpr i32 n = 64;
        auto sw = MakeRef<tmodel::SplatWeights>(DefaultAllocator(), n, n);
        Span<u8> idx = sw->Indices();
        Span<u8> wts = sw->Weights();
        for (i32 y = 0; y < n; ++y)
        {
            for (i32 x = 0; x < n / 2; ++x)
            {
                const usize at = sw->TexelOffset(x, y);
                idx[at + 0] = 0;
                wts[at + 0] = 255;
            }
        }
        sw->BumpVersion();
        return sw;
    }

    // A scene with a terrain entity and one grass layer entity under it.
    struct Fixture
    {
        scene::Scene scene{DefaultAllocator()};
        RefPtr<hf::Heightfield> grid;
        RefPtr<tmodel::SplatWeights> splat;
        RefPtr<tmodel::TerrainResource> resource;
        RefPtr<geometry::StaticMesh> mesh;
        scene::EntityHandle terrain{};
        scene::EntityHandle grass{};
        VegetationLayerComponentManager* mgr = nullptr;

        explicit Fixture(bool withSplat = true)
        {
            engine::terrain::AddTerrainSceneManagers(scene);
            engine::vegetation::AddVegetationSceneManagers(scene);
            mgr = scene.GetSystem<VegetationLayerComponentManager>();
            REQUIRE(mgr != nullptr);

            grid = MakeFlat(2.0f);
            resource = MakeRef<tmodel::TerrainResource>(DefaultAllocator());
            resource->heightfield = grid.Get();
            if (withSplat)
            {
                splat = MakeHalfSplat();
                resource->weights = splat.Get();
            }
            terrain = scene.CreateEntity(u8"terrain");
            scene.GetSystem<engine::terrain::TerrainComponentManager>()->Add(terrain).terrain =
                resource.Get();

            mesh = geometry::Primitives::Cube(DefaultAllocator(), 0.5f);
            grass = scene.CreateEntity(u8"grass");
            scene.SetParent(grass, terrain);
            VegetationLayerComponent& c = mgr->Add(grass);
            c.mesh = mesh.Get();
            c.placement = withSplat ? veg::VegetationPlacement::Splat
                                    : veg::VegetationPlacement::Uniform;
            c.splatLayer = 0;
            c.density = 0.25f; // 1024 candidates per chunk
            c.maxSlopeDegrees = 90.0f;
            c.fadeStart = 40.0f;
            c.fadeEnd = 80.0f;
            scene.Start();
        }

        VegetationLayerComponent& Layer() { return *mgr->Get(grass); }

        // Extract with the view at `origin` (or headless), returning the emitted sets.
        Array<const render::MultiMeshRenderData*> Extract(render::ExtractedScene& snapshot,
                                                          const Float3* origin)
        {
            snapshot.Reset();
            if (origin != nullptr)
            {
                snapshot.SetViewOrigin(*origin);
            }
            mgr->ExtractRenderData(snapshot);
            Array<const render::MultiMeshRenderData*> sets;
            for (render::RenderData* item : snapshot.Items())
            {
                REQUIRE(item->kind == render::RenderDataKind::Mesh);
                const auto* md = static_cast<const render::MeshRenderData*>(item);
                REQUIRE(md->multiMesh);
                sets.PushBack(static_cast<const render::MultiMeshRenderData*>(md));
            }
            return sets;
        }
    };

    u32 TotalInstances(const Array<const render::MultiMeshRenderData*>& sets)
    {
        u32 n = 0;
        for (const render::MultiMeshRenderData* s : sets)
        {
            n += s->instanceCount;
        }
        return n;
    }
}

TEST_CASE("engine.vegetation: the component reflects with the inspector attributes and round-trips its wire")
{
    engine::vegetation::RegisterVegetationComponentReflection();
    engine::vegetation::RegisterVegetationComponentReflection(); // idempotent
    const TypeInfo& type = TypeOf<VegetationLayerComponent>();
    for (const char* name : {"mesh", "material", "placement", "splatLayer", "splatThreshold",
                             "maskPlane", "density", "scaleRange", "maxSlopeDegrees",
                             "heightRange", "alignToNormal", "fadeStart", "fadeEnd",
                             "castShadows", "maxInstancesPerChunk", "visible"})
    {
        INFO(name);
        CHECK(FindProperty(type, name) != nullptr);
    }
    const Variant* display = FindAttribute(type, "displayName");
    REQUIRE(display != nullptr);
    CHECK(*display->TryGet<String>() == String(u8"Vegetation Layer"));
    const Variant* category = FindAttribute(type, "category");
    REQUIRE(category != nullptr);
    CHECK(*category->TryGet<String>() == String(u8"Terrain"));

    VegetationLayerComponent authored;
    authored.placement = veg::VegetationPlacement::Uniform;
    authored.splatLayer = 3;
    authored.splatThreshold = 0.5f;
    authored.density = 7.5f;
    authored.scaleRange = Float2{0.5f, 2.5f};
    authored.maxSlopeDegrees = 12.0f;
    authored.heightRange = Float2{-3.0f, 30.0f};
    authored.alignToNormal = true;
    authored.fadeStart = 10.0f;
    authored.fadeEnd = 20.0f;
    authored.castShadows = true;
    authored.maxInstancesPerChunk = 512;
    authored.visible = false;

    MemoryStream buffer;
    {
        BinarySerializer writer(buffer, SerializeMode::Write);
        Serialize(writer, authored);
    }
    (void)buffer.Seek(0, SeekOrigin::Begin);
    VegetationLayerComponent loaded;
    {
        BinarySerializer reader(buffer, SerializeMode::Read);
        Serialize(reader, loaded);
    }
    CHECK(loaded.placement == veg::VegetationPlacement::Uniform);
    CHECK(loaded.splatLayer == 3u);
    CHECK(loaded.splatThreshold == 0.5f);
    CHECK(loaded.density == 7.5f);
    CHECK(loaded.scaleRange.y == 2.5f);
    CHECK(loaded.maxSlopeDegrees == 12.0f);
    CHECK(loaded.heightRange.x == -3.0f);
    CHECK(loaded.alignToNormal);
    CHECK(loaded.fadeStart == 10.0f);
    CHECK(loaded.fadeEnd == 20.0f);
    CHECK(loaded.castShadows);
    CHECK(loaded.maxInstancesPerChunk == 512u);
    CHECK(!loaded.visible);

    // ToLayer mirrors the scatter fields.
    const veg::VegetationLayer layer = loaded.ToLayer();
    CHECK(layer.density == 7.5f);
    CHECK(layer.castShadows);
    CHECK(veg::LayerScatterHash(layer) == veg::LayerScatterHash(authored.ToLayer()));
}

TEST_CASE("engine.vegetation: one set per (layer, chunk) in range; the splat picks the chunks; shadows follow the layer")
{
    Fixture f;
    f.mgr->SetBuildBudget(100);
    render::ExtractedScene snapshot{DefaultAllocator()};

    // Headless (no view origin): every chunk is in range; only the painted half grows, so the
    // two x < 0 chunks emit and the two x > 0 chunks scatter to nothing.
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuildCount() == 4u); // all four chunks scattered
    REQUIRE(sets.Size() == 2u);
    CHECK(f.mgr->BuiltSetCount() == 2u);
    CHECK(f.mgr->InstanceCount() == TotalInstances(sets));
    for (const render::MultiMeshRenderData* s : sets)
    {
        CHECK(s->key != 0u);
        CHECK(s->instanceCount > 0u);
        CHECK(s->instanceCount == 1024u); // Splat share 1 on the painted half keeps every candidate
        CHECK(s->transforms != nullptr);
        CHECK(s->mesh == f.mesh.Get());
        CHECK(s->version > 0u);
        CHECK(!s->castShadows); // grass default
        CHECK(s->category == render::RenderCategories::Opaque);
        CHECK(s->worldCenter.x < 0.0f); // the painted (x < 0) chunks
        CHECK(s->worldRadius > 32.0f);
        CHECK(render::EntityTag::Index(s->entityId) == f.grass.index);
        for (u32 i = 0; i < s->instanceCount; ++i)
        {
            CHECK(s->transforms[i].m[3][0] < 0.0f);
            CHECK(s->transforms[i].m[3][1] == doctest::Approx(2.0f).epsilon(0.01));
        }
    }
    CHECK(sets[0]->key != sets[1]->key);

    // A rock layer casts.
    f.Layer().castShadows = true;
    sets = f.Extract(snapshot, nullptr);
    REQUIRE(sets.Size() == 2u);
    CHECK(sets[0]->castShadows);
    CHECK(f.mgr->BuildCount() == 4u); // fade/shadow changes never rescatter

    // A second extraction re-emits the same sets: same keys, same version (no re-upload).
    const u64 key0 = sets[0]->key;
    const u32 version0 = sets[0]->version;
    sets = f.Extract(snapshot, nullptr);
    REQUIRE(sets.Size() == 2u);
    CHECK(sets[0]->key == key0);
    CHECK(sets[0]->version == version0);
}

TEST_CASE("engine.vegetation: the fade prefix thins by distance and out-of-range chunks are absent")
{
    Fixture f(/*withSplat*/ false); // Uniform: all four chunks grow
    f.mgr->SetBuildBudget(100);
    render::ExtractedScene snapshot{DefaultAllocator()};

    // The view over the (-32, -32) chunk's centre, 10 m up: that chunk is at distance 0, the
    // diagonal one (32, 32) is ~ 90 - 45 = 45 m from its bounds (inside the fade), so it
    // draws a partial prefix; the near chunks are full.
    const Float3 near{-32.0f, 12.0f, -32.0f};
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, &near);
    REQUIRE(sets.Size() == 4u);
    u32 full = 0;
    u32 partial = 0;
    for (const render::MultiMeshRenderData* s : sets)
    {
        CHECK(s->instanceCount > 0u);
        CHECK(s->instanceCount <= 1024u);
        if (s->instanceCount == 1024u)
        {
            ++full;
        }
        else
        {
            ++partial;
        }
    }
    CHECK(full >= 1u);
    CHECK(partial >= 1u);

    // Far away: nothing is in range and nothing new is built (a hidden viewport never grows).
    const u64 builds = f.mgr->BuildCount();
    const Float3 far{2000.0f, 12.0f, 0.0f};
    sets = f.Extract(snapshot, &far);
    CHECK(sets.IsEmpty());
    CHECK(f.mgr->BuildCount() == builds);
    CHECK(f.mgr->BuiltSetCount() == 4u); // the sets stay cached for the return

    // Back in range: the cached sets return without a rebuild.
    sets = f.Extract(snapshot, &near);
    CHECK(sets.Size() == 4u);
    CHECK(f.mgr->BuildCount() == builds);
}

TEST_CASE("engine.vegetation: a version bump regrows only the touched chunks when a region says which")
{
    Fixture f(/*withSplat*/ false);
    f.mgr->SetBuildBudget(100);
    render::ExtractedScene snapshot{DefaultAllocator()};
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, nullptr);
    REQUIRE(sets.Size() == 4u);
    CHECK(f.mgr->BuildCount() == 4u);
    Array<u32> versions;
    Array<u64> keys;
    for (const render::MultiMeshRenderData* s : sets)
    {
        versions.PushBack(s->version);
        keys.PushBack(s->key);
    }

    // A sculpt inside chunk (0, 0) only: bump + region -> ONE rebuild, its version bumps, the
    // other three keep theirs.
    f.grid->BumpVersion();
    hf::HeightfieldRegion region;
    region.minX = 10;
    region.maxX = 20;
    region.minZ = 10;
    region.maxZ = 20;
    f.mgr->InvalidateRegion(region);
    sets = f.Extract(snapshot, nullptr);
    REQUIRE(sets.Size() == 4u);
    CHECK(f.mgr->BuildCount() == 5u);
    u32 bumped = 0;
    for (const render::MultiMeshRenderData* s : sets)
    {
        for (usize i = 0; i < keys.Size(); ++i)
        {
            if (keys[i] == s->key)
            {
                bumped += (s->version != versions[i]) ? 1 : 0;
            }
        }
    }
    CHECK(bumped == 1u);

    // A bump with no region notice regrows everything (the conservative fallback).
    f.grid->BumpVersion();
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuildCount() == 9u);

    // A splat bump behaves the same; a scatter-parameter change resets the whole layer.
    auto splat = MakeHalfSplat();
    f.resource->weights = splat.Get();
    sets = f.Extract(snapshot, nullptr); // a new splat identity: every chunk regrows
    CHECK(f.mgr->BuildCount() == 13u);
    f.Layer().density = 0.5f;
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuildCount() == 17u);
    for (const render::MultiMeshRenderData* s : sets)
    {
        CHECK(s->instanceCount == 2048u);
    }

    // Moving the terrain entity recomposes (versions bump) without a rescatter.
    f.scene.SetLocalPosition(f.terrain, Float3{100.0f, 0.0f, 0.0f});
    f.scene.UpdateTransforms();
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuildCount() == 17u);
    REQUIRE(sets.Size() == 4u);
    for (const render::MultiMeshRenderData* s : sets)
    {
        CHECK(s->worldCenter.x > 30.0f); // shifted by +100
        CHECK(s->transforms[0].m[3][0] > 30.0f);
    }
}

TEST_CASE("engine.vegetation: the build budget spreads a cold start over extractions")
{
    Fixture f(/*withSplat*/ false);
    f.mgr->SetBuildBudget(1);
    render::ExtractedScene snapshot{DefaultAllocator()};
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 1u);
    CHECK(f.mgr->BuildCount() == 1u);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 2u);
    sets = f.Extract(snapshot, nullptr);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);
    CHECK(f.mgr->BuildCount() == 4u);
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuildCount() == 4u); // warm: no more builds
}

TEST_CASE("engine.vegetation: no layer, a hidden layer, a layer off any terrain, or no mesh extracts nothing")
{
    scene::Scene bare{DefaultAllocator()};
    engine::terrain::AddTerrainSceneManagers(bare);
    engine::vegetation::AddVegetationSceneManagers(bare);
    auto* mgr = bare.GetSystem<VegetationLayerComponentManager>();
    REQUIRE(mgr != nullptr);
    bare.Start();
    render::ExtractedScene snapshot{DefaultAllocator()};
    mgr->ExtractRenderData(snapshot);
    CHECK(snapshot.IsEmpty());

    Fixture f(/*withSplat*/ false);
    f.mgr->SetBuildBudget(100);
    f.Layer().visible = false;
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());
    f.Layer().visible = true;
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);

    // An inactive entity is absent; so is a layer whose entity has no terrain above it.
    f.scene.SetActive(f.grass, false);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());
    f.scene.SetActive(f.grass, true);
    f.scene.SetParent(f.grass, scene::EntityHandle{});
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());
    f.scene.SetParent(f.grass, f.terrain);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);

    // No mesh: nothing to instance.
    f.Layer().mesh = nullptr;
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());

    // A removed layer drops its cache.
    f.Layer().mesh = f.mesh.Get();
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuiltSetCount() == 4u);
    f.mgr->Remove(f.grass);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());
    CHECK(f.mgr->BuiltSetCount() == 0u);
}

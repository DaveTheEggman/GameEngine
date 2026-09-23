// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// engine.vegetation - the component's reflection + wire (an array of layers), and the manager's
// contract: sets built only for chunks in range, one MultiMeshRenderData per (layer, chunk) with
// the fade prefix as its count, region-scoped regrow on a version bump, the build budget, and
// nothing extra without a layer.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

#include <initializer_list>

import foundation.core;
import foundation.scene;
import foundation.geometry;
import foundation.heightfield;
import foundation.terrain;
import foundation.terrain.resource;
import foundation.vegetation.resource;
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
using engine::vegetation::TerrainVegetationComponent;
using engine::vegetation::TerrainVegetationComponentManager;
using engine::vegetation::VegetationLayer;

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

    // A scene with a terrain entity carrying the vegetation component with one grass layer.
    struct Fixture
    {
        scene::Scene scene{DefaultAllocator()};
        RefPtr<hf::Heightfield> grid;
        RefPtr<tmodel::SplatWeights> splat;
        RefPtr<tmodel::TerrainResource> resource;
        RefPtr<geometry::StaticMesh> mesh;
        scene::EntityHandle terrain{};
        TerrainVegetationComponentManager* mgr = nullptr;

        explicit Fixture(bool withSplat = true)
        {
            engine::terrain::AddTerrainSceneManagers(scene);
            engine::vegetation::AddVegetationSceneManagers(scene);
            mgr = scene.GetSystem<TerrainVegetationComponentManager>();
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
            TerrainVegetationComponent& c = mgr->Add(terrain);
            VegetationLayer grass;
            grass.name = String(u8"Grass");
            grass.mesh = mesh.Get();
            grass.placement = withSplat ? veg::VegetationPlacement::Splat
                                        : veg::VegetationPlacement::Uniform;
            grass.splatLayer = 0;
            grass.density = 0.25f; // 1024 candidates per chunk
            grass.maxSlopeDegrees = 90.0f;
            grass.fadeStart = 40.0f;
            grass.fadeEnd = 80.0f;
            c.layers.PushBack(grass);
            scene.Start();
        }

        TerrainVegetationComponent& Component() { return *mgr->Get(terrain); }
        VegetationLayer& Layer(usize i = 0) { return Component().layers[i]; }

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

TEST_CASE("engine.vegetation: the component reflects (layers as a container of reflected layers) and round-trips its wire")
{
    engine::vegetation::RegisterVegetationComponentReflection();
    engine::vegetation::RegisterVegetationComponentReflection(); // idempotent
    const TypeInfo& type = TypeOf<TerrainVegetationComponent>();
    const PropertyInfo* layers = FindProperty(type, "layers");
    REQUIRE(layers != nullptr);
    REQUIRE(layers->type != nullptr);
    REQUIRE(layers->type->container != nullptr); // the list editor's contract
    CHECK(layers->type->container->elementType == &TypeOf<VegetationLayer>());
    CHECK(FindProperty(type, "visible") != nullptr);
    CHECK(FindProperty(type, "mask") != nullptr);
    const Variant* display = FindAttribute(type, "displayName");
    REQUIRE(display != nullptr);
    CHECK(*display->TryGet<String>() == String(u8"Terrain Vegetation"));
    const Variant* category = FindAttribute(type, "category");
    REQUIRE(category != nullptr);
    CHECK(*category->TryGet<String>() == String(u8"Terrain"));

    // A new layer (the inspector's add button default-constructs one) is MANUAL: it grows
    // nothing until painted or given a source.
    CHECK(VegetationLayer{}.placement == veg::VegetationPlacement::Scattered);
    CHECK(VegetationLayer{}.ToScatterLayer().placement == veg::VegetationPlacement::Scattered);
    const TypeInfo& layerType = TypeOf<VegetationLayer>();
    for (const char* name : {"name", "mesh", "material", "placement", "splatLayer",
                             "splatThreshold", "maskPlane", "density", "scaleRange",
                             "maxSlopeDegrees", "heightRange", "alignToNormal", "fadeStart",
                             "fadeEnd", "castShadows", "maxInstancesPerChunk", "visible"})
    {
        INFO(name);
        CHECK(FindProperty(layerType, name) != nullptr);
    }

    TerrainVegetationComponent authored;
    authored.visible = false;
    authored.mask.SetId(Guid{0x77u, 0x88u});
    VegetationLayer grass;
    grass.name = String(u8"Grass");
    grass.placement = veg::VegetationPlacement::Uniform;
    grass.splatLayer = 3;
    grass.splatThreshold = 0.5f;
    grass.density = 7.5f;
    grass.scaleRange = Float2{0.5f, 2.5f};
    grass.maxSlopeDegrees = 12.0f;
    grass.heightRange = Float2{-3.0f, 30.0f};
    grass.alignToNormal = true;
    grass.fadeStart = 10.0f;
    grass.fadeEnd = 20.0f;
    grass.castShadows = true;
    grass.maxInstancesPerChunk = 512;
    grass.visible = false;
    authored.layers.PushBack(grass);
    VegetationLayer rocks;
    rocks.name = String(u8"Rocks");
    rocks.density = 0.05f;
    rocks.castShadows = true;
    rocks.placement = veg::VegetationPlacement::Scattered;
    rocks.instances.PushBack(Float4x4::Translation(Float3{1.0f, 2.0f, 3.0f}));
    rocks.instances.PushBack(Float4x4::Translation(Float3{7.0f, 2.0f, 3.0f}));
    authored.layers.PushBack(rocks);

    MemoryStream buffer;
    {
        BinarySerializer writer(buffer, SerializeMode::Write);
        Serialize(writer, authored);
    }
    (void)buffer.Seek(0, SeekOrigin::Begin);
    TerrainVegetationComponent loaded;
    {
        BinarySerializer reader(buffer, SerializeMode::Read);
        Serialize(reader, loaded);
    }
    CHECK(!loaded.visible);
    CHECK(loaded.mask.id == Guid{0x77u, 0x88u});
    REQUIRE(loaded.layers.Size() == 2u);
    const VegetationLayer& g = loaded.layers[0];
    CHECK(g.name == String(u8"Grass"));
    CHECK(g.placement == veg::VegetationPlacement::Uniform);
    CHECK(g.splatLayer == 3u);
    CHECK(g.splatThreshold == 0.5f);
    CHECK(g.density == 7.5f);
    CHECK(g.scaleRange.y == 2.5f);
    CHECK(g.maxSlopeDegrees == 12.0f);
    CHECK(g.heightRange.x == -3.0f);
    CHECK(g.alignToNormal);
    CHECK(g.fadeStart == 10.0f);
    CHECK(g.fadeEnd == 20.0f);
    CHECK(g.castShadows);
    CHECK(g.maxInstancesPerChunk == 512u);
    CHECK(!g.visible);
    CHECK(loaded.layers[1].name == String(u8"Rocks"));
    CHECK(loaded.layers[1].density == 0.05f);
    REQUIRE(loaded.layers[1].instances.Size() == 2u); // the authored props ride the wire
    CHECK(loaded.layers[1].instances[1].m[3][0] == 7.0f);

    // ToScatterLayer mirrors the scatter fields.
    const veg::ScatterLayer layer = g.ToScatterLayer();
    CHECK(layer.density == 7.5f);
    CHECK(layer.castShadows);
    CHECK(veg::LayerScatterHash(layer) == veg::LayerScatterHash(grass.ToScatterLayer()));
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
        CHECK(render::EntityTag::Index(s->entityId) == f.terrain.index);
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
        CHECK(s->fadeStart == doctest::Approx(40.0f)); // the layer's window, for the per-instance dissolve
        CHECK(s->fadeEnd == doctest::Approx(80.0f));
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

TEST_CASE("engine.vegetation: two layers are two families of sets; a removed slot drops its sets")
{
    Fixture f(/*withSplat*/ false);
    f.mgr->SetBuildBudget(100);
    RefPtr<geometry::StaticMesh> rockMesh = geometry::Primitives::Cube(DefaultAllocator(), 1.5f);
    VegetationLayer rocks;
    rocks.name = String(u8"Rocks");
    rocks.mesh = rockMesh.Get();
    rocks.placement = veg::VegetationPlacement::Uniform;
    rocks.density = 0.01f; // ~41 per chunk
    rocks.maxSlopeDegrees = 90.0f;
    rocks.castShadows = true;
    f.Component().layers.PushBack(rocks);

    render::ExtractedScene snapshot{DefaultAllocator()};
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, nullptr);
    REQUIRE(sets.Size() == 8u); // 4 chunks x 2 layers
    CHECK(f.mgr->BuildCount() == 8u);
    u32 grassSets = 0;
    u32 rockSets = 0;
    for (const render::MultiMeshRenderData* s : sets)
    {
        if (s->mesh == f.mesh.Get())
        {
            ++grassSets;
            CHECK(!s->castShadows);
            CHECK(s->instanceCount == 1024u);
        }
        else
        {
            REQUIRE(s->mesh == rockMesh.Get());
            ++rockSets;
            CHECK(s->castShadows);
            CHECK(s->instanceCount == 41u);
        }
        // Every set's key is unique across layers and chunks.
        for (const render::MultiMeshRenderData* other : sets)
        {
            CHECK((other == s || other->key != s->key));
        }
    }
    CHECK(grassSets == 4u);
    CHECK(rockSets == 4u);

    // A hidden layer draws nothing but keeps its sets; unhiding costs no rebuild.
    f.Layer(1).visible = false;
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);
    CHECK(f.mgr->BuiltSetCount() == 8u);
    f.Layer(1).visible = true;
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 8u);
    CHECK(f.mgr->BuildCount() == 8u);

    // Removing the rock slot drops its sets on the next extraction.
    f.Component().layers.RemoveAt(1);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);
    CHECK(f.mgr->BuiltSetCount() == 4u);
}

TEST_CASE("engine.vegetation: no component, a hidden component, one off any terrain, or a layer without a mesh extracts nothing")
{
    scene::Scene bare{DefaultAllocator()};
    engine::terrain::AddTerrainSceneManagers(bare);
    engine::vegetation::AddVegetationSceneManagers(bare);
    auto* mgr = bare.GetSystem<TerrainVegetationComponentManager>();
    REQUIRE(mgr != nullptr);
    bare.Start();
    render::ExtractedScene snapshot{DefaultAllocator()};
    mgr->ExtractRenderData(snapshot);
    CHECK(snapshot.IsEmpty());

    Fixture f(/*withSplat*/ false);
    f.mgr->SetBuildBudget(100);
    f.Component().visible = false;
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());
    f.Component().visible = true;
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);

    // An inactive entity is absent.
    f.scene.SetActive(f.terrain, false);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());
    f.scene.SetActive(f.terrain, true);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);

    // The component on a CHILD of the terrain entity grows on the parent's terrain; on an
    // entity with no terrain above it, nothing.
    const scene::EntityHandle child = f.scene.CreateEntity(u8"dressing");
    f.scene.SetParent(child, f.terrain);
    TerrainVegetationComponent moved = f.Component();
    f.mgr->Remove(f.terrain);
    f.mgr->Add(child) = moved;
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);
    f.scene.SetParent(child, scene::EntityHandle{});
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());
    f.scene.SetParent(child, f.terrain);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);

    // No mesh: nothing to instance.
    f.mgr->Get(child)->layers[0].mesh = nullptr;
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());

    // A removed component drops its caches.
    f.mgr->Get(child)->layers[0].mesh = f.mesh.Get();
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuiltSetCount() == 4u);
    f.mgr->Remove(child);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());
    CHECK(f.mgr->BuiltSetCount() == 0u);
}

TEST_CASE("engine.vegetation: a Mask layer follows the component's painted plane; a mask edit regrows the touched chunks")
{
    Fixture f(/*withSplat*/ false);
    f.mgr->SetBuildBudget(100);
    // Plane 0 painted on the z < 0 half of the footprint.
    auto mask = MakeRef<veg::VegetationMask>(DefaultAllocator(), 64, 64, 1);
    for (i32 y = 0; y < 32; ++y)
    {
        for (i32 x = 0; x < 64; ++x)
        {
            mask->SetDensity(0, x, y, 255);
        }
    }
    f.Component().mask = mask.Get();
    f.Layer().placement = veg::VegetationPlacement::Mask;
    f.Layer().maskPlane = 0;

    render::ExtractedScene snapshot{DefaultAllocator()};
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, nullptr);
    REQUIRE(sets.Size() == 2u); // the two z < 0 chunks
    for (const render::MultiMeshRenderData* s : sets)
    {
        CHECK(s->worldCenter.z < 0.0f);
        CHECK(s->instanceCount == 1024u);
    }
    CHECK(f.mgr->BuildCount() == 4u);

    // A brush stroke over one chunk's footprint quarter, with the footprint notice: only that
    // chunk regrows (its instance count follows the new paint).
    for (i32 y = 32; y < 64; ++y)
    {
        for (i32 x = 32; x < 64; ++x)
        {
            mask->SetDensity(0, x, y, 255); // paint the (+x, +z) quadrant
        }
    }
    mask->BumpVersion();
    // The notice stays inside the chunk: a rect touching the shared boundary sample (u = 0.5)
    // would rightly regrow both neighbours.
    f.mgr->InvalidateFootprint(0.6f, 0.6f, 0.9f, 0.9f, kGrid);
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuildCount() == 5u);
    REQUIRE(sets.Size() == 3u);

    // A bump with no notice regrows everything; dropping the mask empties the Mask layer.
    mask->BumpVersion();
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuildCount() == 9u);
    CHECK(sets.Size() == 3u);
    f.Component().mask = nullptr;
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.IsEmpty());
}

TEST_CASE("engine.vegetation: a Scattered layer draws its authored instances bucketed per chunk; a change re-buckets")
{
    Fixture f(/*withSplat*/ false);
    f.mgr->SetBuildBudget(100);
    VegetationLayer& rocks = f.Layer();
    rocks.placement = veg::VegetationPlacement::Scattered;
    rocks.scaleRange = Float2{1.0f, 1.0f};
    // Three props in the (-x, -z) chunk, one in the (+x, +z) chunk, none elsewhere.
    rocks.instances.PushBack(Float4x4::Translation(Float3{-40.0f, 2.0f, -40.0f}));
    rocks.instances.PushBack(Float4x4::Translation(Float3{-10.0f, 2.0f, -50.0f}));
    rocks.instances.PushBack(Float4x4::Translation(Float3{-30.0f, 2.0f, -1.0f}));
    rocks.instances.PushBack(Float4x4::Translation(Float3{20.0f, 2.0f, 20.0f}));

    render::ExtractedScene snapshot{DefaultAllocator()};
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, nullptr);
    REQUIRE(sets.Size() == 2u);
    u32 three = 0, one = 0;
    for (const render::MultiMeshRenderData* s : sets)
    {
        if (s->instanceCount == 3u)
        {
            ++three;
            CHECK(s->worldCenter.x < 0.0f);
            CHECK(s->worldCenter.z < 0.0f);
            for (u32 i = 0; i < 3; ++i)
            {
                CHECK(s->transforms[i].m[3][0] < 0.0f);
            }
        }
        else if (s->instanceCount == 1u)
        {
            ++one;
            CHECK(s->transforms[0].m[3][0] == 20.0f);
        }
    }
    CHECK(three == 1u);
    CHECK(one == 1u);
    CHECK(f.mgr->BuildCount() == 4u);

    // Unchanged instances: no rebuild. A brush stroke (a new instance) re-buckets the layer.
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuildCount() == 4u);
    f.Layer().instances.PushBack(Float4x4::Translation(Float3{50.0f, 2.0f, -50.0f}));
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuildCount() == 8u);
    CHECK(sets.Size() == 3u);
    // Erasing back to the old content is another hash: another re-bucket, two sets again.
    f.Layer().instances.RemoveAt(4);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 2u);
    // The fade prefix applies like any set: a far origin thins the props.
    const Float3 far{2000.0f, 5.0f, 0.0f};
    sets = f.Extract(snapshot, &far);
    CHECK(sets.IsEmpty());
}

TEST_CASE("engine.vegetation: an invalidated set keeps drawing its old instances until its rebuild lands; props rebuild whole")
{
    Fixture f(/*withSplat*/ false);
    f.mgr->SetBuildBudget(100);
    render::ExtractedScene snapshot{DefaultAllocator()};
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, nullptr);
    REQUIRE(sets.Size() == 4u); // warm
    CHECK(f.mgr->BuildCount() == 4u);

    // A sculpt (every chunk dirty) under a budget of one: every frame still draws all four
    // sets - the three not yet rebuilt show their previous instances - while one rebuilds
    // per extraction, so nothing blinks under the brush.
    f.mgr->SetBuildBudget(1);
    f.grid->BumpVersion();
    for (u32 frame = 1; frame <= 4; ++frame)
    {
        sets = f.Extract(snapshot, nullptr);
        CHECK(sets.Size() == 4u);
        CHECK(f.mgr->BuildCount() == 4u + frame);
    }
    sets = f.Extract(snapshot, nullptr);
    CHECK(f.mgr->BuildCount() == 8u); // all caught up
    // A cold set (never built) still waits its turn: a fresh layer under the same budget.
    VegetationLayer flowers = f.Layer();
    flowers.name = String(u8"Flowers");
    f.Component().layers.PushBack(flowers);
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 5u); // the four grass sets + the one flower chunk built this frame

    // Authored props re-bucket outside the budget: a stroke lands whole in one extraction.
    f.Component().layers.RemoveAt(1);
    f.Layer().placement = veg::VegetationPlacement::Scattered;
    for (i32 i = 0; i < 4; ++i)
    {
        f.Layer().instances.PushBack(Float4x4::Translation(
            Float3{(i % 2 == 0) ? -30.0f : 30.0f, 2.0f, (i < 2) ? -30.0f : 30.0f}));
    }
    sets = f.Extract(snapshot, nullptr); // the placement change reset the layer: 4 rebuilds
    REQUIRE(sets.Size() == 4u);
    const u64 builds = f.mgr->BuildCount();
    f.Layer().instances.PushBack(Float4x4::Translation(Float3{-31.0f, 2.0f, -31.0f}));
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);
    CHECK(f.mgr->BuildCount() == builds + 4u); // every chunk re-bucketed this frame, budget 1
    u32 total = 0;
    for (const render::MultiMeshRenderData* s : sets)
    {
        total += s->instanceCount;
    }
    CHECK(total == 5u);
}

TEST_CASE("engine.vegetation: a freshly added layer with a mesh grows nothing until it is painted or given a source")
{
    Fixture f(/*withSplat*/ true); // the splat has palette 0 painted: a Splat default would grow
    f.mgr->SetBuildBudget(100);
    VegetationLayer fresh; // exactly what the inspector's add button makes, plus a mesh
    fresh.mesh = f.mesh.Get();
    f.Component().layers.PushBack(fresh);
    render::ExtractedScene snapshot{DefaultAllocator()};
    Array<const render::MultiMeshRenderData*> sets = f.Extract(snapshot, nullptr);
    for (const render::MultiMeshRenderData* s : sets)
    {
        CHECK(render::EntityTag::Index(s->entityId) == f.terrain.index);
    }
    CHECK(sets.Size() == 2u); // only the fixture's grass (the two painted chunks); nothing new
    // Painting one prop into it draws it; choosing Splat makes it grow like the grass.
    f.Component().layers[1].instances.PushBack(Float4x4::Translation(Float3{10.0f, 2.0f, 10.0f}));
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 3u);
    f.Component().layers[1].instances.Clear();
    f.Component().layers[1].placement = veg::VegetationPlacement::Splat;
    sets = f.Extract(snapshot, nullptr);
    CHECK(sets.Size() == 4u);
}

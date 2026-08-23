// engine.terrain (Phase A): the TerrainComponent's reflected surface + a scene serialize round-trip
// (the terrain reference + flags survive save/load through the component manager).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.scene;
import foundation.scene.resource; // SerializeScene
import foundation.resource;
import foundation.terrain.resource;
import foundation.heightfield;
import foundation.rhi;
import foundation.rhi.null;
import engine.terrain;

using namespace foundation::core;
using namespace engine::terrain;
namespace scene = foundation::scene;
namespace hf = foundation::heightfield;

TEST_CASE("engine.terrain: TerrainComponent reflects its authored fields")
{
    RegisterTerrainComponentReflection();
    const TypeInfo& type = TypeOf<TerrainComponent>();
    CHECK(FindProperty(type, "terrain") != nullptr);
    CHECK(FindProperty(type, "castShadows") != nullptr);
    CHECK(FindProperty(type, "visible") != nullptr);
}

TEST_CASE("engine.terrain: a TerrainComponent survives a scene serialize round-trip")
{
    RegisterTerrainComponentReflection();

    scene::Scene a{u8"terrain-wire"};
    a.AddSystem<TerrainComponentManager>();
    scene::EntityHandle e = a.CreateEntity(u8"terrain");
    {
        TerrainComponent& t = a.GetSystem<TerrainComponentManager>()->Add(e);
        t.terrain.SetId(Guid(42, 7));
        t.castShadows = false;
        t.visible = false;
    }

    MemoryStream stream;
    {
        BinarySerializer writer(stream, SerializeMode::Write);
        scene::SerializeScene(writer, a);
    }
    (void)stream.Seek(0, SeekOrigin::Begin);

    scene::Scene b{u8"terrain-wire2"};
    b.AddSystem<TerrainComponentManager>();
    {
        BinarySerializer reader(stream, SerializeMode::Read);
        scene::SerializeScene(reader, b);
    }

    scene::EntityHandle loaded = b.FindEntity(a.GetEntityId(e));
    REQUIRE(loaded.IsAssigned());
    TerrainComponent* t = b.GetSystem<TerrainComponentManager>()->Get(loaded);
    REQUIRE(t != nullptr);
    CHECK(t->terrain.id == Guid(42, 7));
    CHECK(t->castShadows == false);
    CHECK(t->visible == false);
}

TEST_CASE("engine.terrain: the GPU height-texture cache keys by heightfield + version")
{
    foundation::rhi::null::NullDevice device{DefaultAllocator()};
    RefPtr<hf::Heightfield> grid =
        MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
    RefPtr<hf::Heightfield> other =
        MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);

    TerrainHeightTextureCache cache;

    // First request creates + caches.
    foundation::rhi::TextureView* v1 = cache.GetOrCreate(device, *grid, 1);
    REQUIRE(v1 != nullptr);
    CHECK(cache.Size() == 1u);

    // Same heightfield + version -> cache hit, same view, no new entry.
    foundation::rhi::TextureView* v1again = cache.GetOrCreate(device, *grid, 1);
    CHECK(v1again == v1);
    CHECK(cache.Size() == 1u);

    // Same heightfield, bumped version (a sculpt/regen re-upload) -> rebuilt in place (one entry).
    foundation::rhi::TextureView* v2 = cache.GetOrCreate(device, *grid, 2);
    REQUIRE(v2 != nullptr);
    CHECK(cache.Size() == 1u);

    // A DIFFERENT heightfield -> a second cached texture (keyed by the heightfield itself, so two
    // terrains sharing one heightfield share one texture).
    foundation::rhi::TextureView* vb = cache.GetOrCreate(device, *other, 1);
    REQUIRE(vb != nullptr);
    CHECK(cache.Size() == 2u);

    // An empty grid yields no texture.
    RefPtr<hf::Heightfield> empty = MakeRef<hf::Heightfield>(DefaultAllocator());
    CHECK(cache.GetOrCreate(device, *empty, 1) == nullptr);

    cache.Clear(device);
    CHECK(cache.Size() == 0u);
}

TEST_CASE("engine.terrain: the cache keys by UID, never pointer (address-reuse aliasing, pass 14)")
{
    // The bind-group-cache rule's failure mode: heightfield A dies, a FRESH grid B lands on
    // (potentially) the same address at the same version - the cache must never serve A's
    // texture for B. Pointer keying cannot pass this test reliably; uid keying always does.
    foundation::rhi::null::NullDevice device{DefaultAllocator()};
    TerrainHeightTextureCache cache;

    u64 uidA = 0;
    {
        RefPtr<hf::Heightfield> a =
            MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
        uidA = a->uid;
        REQUIRE(cache.GetOrCreate(device, *a, a->Version()) != nullptr);
        CHECK(cache.Size() == 1u);
    } // A dies; its cache entry remains keyed by A's uid

    // Fresh grids get fresh uids (the identity that survives address reuse)...
    RefPtr<hf::Heightfield> b =
        MakeRef<hf::Heightfield>(DefaultAllocator(), 65, Float2{64.0f, 64.0f}, 0.0f, 10.0f);
    CHECK(b->uid != uidA);
    // ...so B at the SAME version gets its OWN texture entry, never A's stale one.
    foundation::rhi::TextureView* vb = cache.GetOrCreate(device, *b, b->Version());
    REQUIRE(vb != nullptr);
    CHECK(cache.Size() == 2u);

    cache.Clear(device);
}

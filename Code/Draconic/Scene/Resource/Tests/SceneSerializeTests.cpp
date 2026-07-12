// Phase 5 - whole-scene serialization round-trip: serialize a scene (entities +
// transform hierarchy + components) to bytes and deserialize into a fresh scene,
// preserving Guids, names, parent links, transforms, and component data.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.scene;
import draconic.scene.resource;

using namespace draconic::core;
using namespace draconic::scene;

namespace
{
    struct Health { f32 value = 100.0f; };
    void Serialize(ISerializer& ar, Health& h) { draconic::core::Serialize(ar, "value", h.value); }

    class HealthManager : public SerializableComponentManager<Health> {
    public:
        HealthManager() : SerializableComponentManager<Health>(u8"demo.Health") {}
    };

    bool Near(f32 a, f32 b) { return Abs(a - b) < 1e-4f; }
}

TEST_CASE("scene round-trips through SerializeScene (entities, hierarchy, transforms, components)")
{
    // --- author scene A ---
    Scene a(u8"level");
    HealthManager* mgrA = a.AddSystem<HealthManager>();
    EntityHandle root  = a.CreateEntity(u8"root");
    EntityHandle child = a.CreateEntity(u8"child");
    a.SetParent(child, root);
    a.SetLocalPosition(root, Float3{ 1, 2, 3 });
    a.SetLocalPosition(child, Float3{ 4, 0, 0 });
    mgrA->Add(root).value = 50.0f;
    mgrA->Add(child).value = 75.0f;
    a.SetActive(child, false);

    const Guid rootId  = a.GetEntityId(root);
    const Guid childId = a.GetEntityId(child);

    // --- serialize to memory ---
    MemoryStream stream;
    {
        BinarySerializer writer(stream, SerializeMode::Write);
        SerializeScene(writer, a);
    }
    (void)stream.Seek(0, SeekOrigin::Begin);

    // --- deserialize into a fresh scene B (with the same manager present) ---
    Scene b;
    HealthManager* mgrB = b.AddSystem<HealthManager>();
    {
        BinarySerializer reader(stream, SerializeMode::Read);
        SerializeScene(reader, b);
    }

    // --- verify ---
    CHECK(b.Name() == u8"level");
    CHECK(b.EntityCount() == 2);

    EntityHandle rootB  = b.FindEntity(rootId);     // Guids preserved
    EntityHandle childB = b.FindEntity(childId);
    REQUIRE(rootB.IsAssigned());
    REQUIRE(childB.IsAssigned());

    CHECK(b.GetEntityName(rootB) == u8"root");
    CHECK(b.GetEntityName(childB) == u8"child");
    CHECK(b.GetParent(childB) == rootB);            // hierarchy relinked
    CHECK(b.GetParent(rootB) == EntityHandle::Invalid());
    CHECK_FALSE(b.IsActive(childB));                 // active state preserved

    CHECK(Near(b.GetLocalTransform(rootB).position.x, 1.0f));
    CHECK(Near(b.GetLocalTransform(childB).position.x, 4.0f));

    REQUIRE(mgrB->Has(rootB));
    REQUIRE(mgrB->Has(childB));
    CHECK(Near(mgrB->Get(rootB)->value, 50.0f));    // component data preserved
    CHECK(Near(mgrB->Get(childB)->value, 75.0f));
    CHECK(mgrB->Count() == 2);

    // world transforms compose correctly after load
    b.UpdateTransforms();
    CHECK(Near(b.GetWorldPosition(childB).x, 5.0f)); // root(1) + child local(4)
    (void)mgrA;
}

TEST_CASE("empty scene round-trips")
{
    Scene a(u8"empty");
    MemoryStream stream;
    { BinarySerializer w(stream, SerializeMode::Write); SerializeScene(w, a); }
    (void)stream.Seek(0, SeekOrigin::Begin);

    Scene b;
    { BinarySerializer r(stream, SerializeMode::Read); SerializeScene(r, b); }
    CHECK(b.Name() == u8"empty");
    CHECK(b.EntityCount() == 0);
}

// Sibling ORDER round-trips: entities are written in TREE order (roots in list order,
// depth-first children), so load's create+relink sequence reproduces the reordered lists -
// the editor hierarchy is reorderable and saves must preserve it.
TEST_CASE("scene-serialize: sibling order round-trips after reorders")
{
    Scene scene(u8"ordered");
    EntityHandle a = scene.CreateEntity(u8"a");
    EntityHandle b = scene.CreateEntity(u8"b");
    EntityHandle c = scene.CreateEntity(u8"c");
    EntityHandle p = scene.CreateEntity(u8"p");
    scene.SetParent(a, p);
    scene.SetParent(b, p);
    scene.SetParent(c, p);
    scene.MoveBefore(c, a);          // children: c, a, b
    scene.MoveBefore(p, scene.GetFirstRoot());   // p to the front of the roots

    MemoryStream buffer;
    {
        BinarySerializer ar(buffer, SerializeMode::Write);
        SerializeScene(ar, scene);
    }

    Scene loaded;
    (void)buffer.Seek(0, SeekOrigin::Begin);
    {
        BinarySerializer ar(buffer, SerializeMode::Read);
        SerializeScene(ar, loaded);
    }

    // Roots: p first.
    EntityHandle lp = loaded.FindEntity(scene.GetEntityId(p));
    REQUIRE(lp.IsAssigned());
    CHECK(loaded.GetFirstRoot() == lp);

    // Children of p in reordered order: c, a, b.
    EntityHandle k0 = loaded.GetFirstChild(lp);
    REQUIRE(k0.IsAssigned());
    CHECK(loaded.GetEntityName(k0) == u8"c");
    EntityHandle k1 = loaded.GetNextSibling(k0);
    REQUIRE(k1.IsAssigned());
    CHECK(loaded.GetEntityName(k1) == u8"a");
    EntityHandle k2 = loaded.GetNextSibling(k1);
    REQUIRE(k2.IsAssigned());
    CHECK(loaded.GetEntityName(k2) == u8"b");
}

// Regression (user-hit, corrupted a real save): the scene's guid RNG is deterministic and a
// LOAD does not advance it, so entities created after a load reproduced loaded guids - and
// since editor commands route BY GUID, edits landed on the wrong entity. CreateEntity now
// re-rolls on collision; loading a save that already contains duplicates recovers by
// reassigning fresh ids (warning) instead of asserting.
TEST_CASE("scene-serialize: fresh guids never collide with loaded entities")
{
    // Session 1: create + save.
    MemoryStream blob;
    Guid firstId;
    {
        Scene scene;
        firstId = scene.GetEntityId(scene.CreateEntity(u8"First"));
        BinarySerializer ar(blob, SerializeMode::Write);
        SerializeScene(ar, scene);
        REQUIRE(ar.IsOk());
    }

    // Session 2 (fresh scene = fresh deterministic RNG): load, then create MORE entities.
    Scene loaded;
    REQUIRE(blob.Seek(0, SeekOrigin::Begin) == 0);
    {
        BinarySerializer ar(blob, SerializeMode::Read);
        SerializeScene(ar, loaded);
        REQUIRE(ar.IsOk());
    }
    CHECK(loaded.FindEntity(firstId).IsAssigned());

    // Without the re-roll these reproduced firstId (same RNG sequence).
    for (i32 i = 0; i < 4; ++i)
    {
        const Guid fresh = loaded.GetEntityId(loaded.CreateEntity(u8"Later"));
        CHECK(fresh != firstId);
        CHECK(fresh != Guid{});
    }
}

TEST_CASE("scene-serialize: a save with duplicate entity guids loads with recovery")
{
    // Forge a corrupt save: two entities sharing one guid (the pre-fix bug's output).
    MemoryStream blob;
    Guid shared;
    {
        Scene scene;
        shared = scene.GetEntityId(scene.CreateEntity(u8"Original"));
        (void)scene.CreateEntity(shared, u8"Impostor");   // explicit-guid create = the corruption
        BinarySerializer ar(blob, SerializeMode::Write);
        SerializeScene(ar, scene);
        REQUIRE(ar.IsOk());
    }

    Scene loaded;
    REQUIRE(blob.Seek(0, SeekOrigin::Begin) == 0);
    {
        BinarySerializer ar(blob, SerializeMode::Read);
        SerializeScene(ar, loaded);
        REQUIRE(ar.IsOk());
    }

    // Both entities exist, uniquely addressable; the shared guid resolves to its first holder.
    CHECK(loaded.EntityCount() == 2u);
    const EntityHandle first = loaded.FindEntity(shared);
    REQUIRE(first.IsAssigned());
    CHECK(loaded.GetEntityName(first) == StringView(u8"Original"));
}

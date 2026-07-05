// Phase 5 — whole-scene serialization round-trip: serialize a scene (entities +
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
    a.SetLocalPosition(root, Vector3{ 1, 2, 3 });
    a.SetLocalPosition(child, Vector3{ 4, 0, 0 });
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

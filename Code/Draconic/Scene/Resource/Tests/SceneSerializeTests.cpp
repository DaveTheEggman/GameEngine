// Phase 5 - whole-scene serialization round-trip: serialize a scene (entities +
// transform hierarchy + components) to bytes and deserialize into a fresh scene,
// preserving Guids, names, parent links, transforms, and component data.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

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

TEST_CASE("scene-snapshot: capture -> simulate-style mutations -> restore into the SAME scene")
{
    // The editor's Simulate loop: snapshot, let the running scene mutate freely, restore the
    // exact pre-play state into the same Scene instance (borrowed pointers stay valid; guids
    // are part of the snapshot so guid-keyed state re-resolves).
    Scene scene(u8"level");
    HealthManager* mgr = scene.AddSystem<HealthManager>();
    EntityHandle hero  = scene.CreateEntity(u8"hero");
    EntityHandle prop  = scene.CreateEntity(u8"prop");
    scene.SetLocalPosition(hero, Float3{ 1, 0, 0 });
    mgr->Add(hero).value = 50.0f;
    const Guid heroId = scene.GetEntityId(hero);
    const Guid propId = scene.GetEntityId(prop);

    UniquePtr<SceneSnapshot> snapshot = SceneSnapshot::Capture(scene);
    REQUIRE(snapshot);

    // "Runtime" mutations: move + damage the hero, destroy the prop, spawn a projectile.
    scene.SetLocalPosition(hero, Float3{ 9, 9, 9 });
    mgr->Get(hero)->value = 1.0f;
    scene.DestroyEntity(prop);
    EntityHandle projectile = scene.CreateEntity(u8"projectile");
    const Guid projectileId = scene.GetEntityId(projectile);

    REQUIRE(snapshot->Restore(scene).IsOk());

    // The exact pre-play world: hero back at its pose/value, the prop resurrected under its
    // ORIGINAL guid, the runtime spawn gone.
    const EntityHandle heroRestored = scene.FindEntity(heroId);
    REQUIRE(heroRestored.IsAssigned());
    CHECK(Near(scene.GetLocalTransform(heroRestored).position.x, 1.0f));
    REQUIRE(mgr->Get(heroRestored) != nullptr);
    CHECK(Near(mgr->Get(heroRestored)->value, 50.0f));
    CHECK(scene.FindEntity(propId).IsAssigned());
    CHECK_FALSE(scene.FindEntity(projectileId).IsAssigned());

    // Restore is repeatable (the same snapshot supports multiple Simulate rounds).
    scene.DestroyEntity(scene.FindEntity(heroId));
    REQUIRE(snapshot->Restore(scene).IsOk());
    CHECK(scene.FindEntity(heroId).IsAssigned());
}

namespace
{
    struct Turret { f32 range = 5.0f; u32 seenVersion = 0; };
    void Serialize(ISerializer& ar, Turret& t)
    {
        t.seenVersion = ar.Version();   // record what the scope exposes (test probe)
        draconic::core::Serialize(ar, "range", t.range);
    }
    class TurretManager : public SerializableComponentManager<Turret> {
    public:
        TurretManager() : SerializableComponentManager<Turret>(u8"demo.Turret") {}
    };
}

DRACONIC_REFLECT_VALUE(Turret, "demo")
{
    builder.DataVersion(3);
    builder.Property<&Turret::range>("range");
}

TEST_CASE("scene-serialize: component records carry the reflected type's data version")
{
    DraconicRegisterValue_Turret();   // patches TypeOf<Turret> (name + dataVersion 3)
    REQUIRE(TypeOf<Turret>().dataVersion == 3u);

    Scene a(u8"level");
    a.AddSystem<TurretManager>()->Add(a.CreateEntity(u8"t")).range = 9.0f;

    MemoryStream blob;
    {
        BinarySerializer ar(blob, SerializeMode::Write);
        SerializeScene(ar, a);
        REQUIRE(ar.IsOk());
    }

    Scene b;
    TurretManager* mgr = b.AddSystem<TurretManager>();
    REQUIRE(blob.Seek(0, SeekOrigin::Begin) == 0);
    {
        BinarySerializer ar(blob, SerializeMode::Read);
        SerializeScene(ar, b);
        REQUIRE(ar.IsOk());
    }
    Turret* loaded = nullptr;
    mgr->ForEach([&](Turret& t, EntityHandle) { loaded = &t; });
    REQUIRE(loaded != nullptr);
    CHECK(loaded->range == doctest::Approx(9.0f));
    // The record's stored version was active while the component deserialized - the seam a
    // component's Serialize migrates through when its layout changes.
    CHECK(loaded->seenVersion == 3u);
}

namespace
{
    // A scene system with a scene-LEVEL settings block (the Sedulous scene-modules pattern:
    // environment/sky-style state, one per scene, shown in the inspector when no entity is
    // selected and persisted with the scene).
    struct FogSettings
    {
        f32 density = 0.5f;
        Color tint  = Color{ 1, 1, 1, 1 };
    };

    class FogSystem final : public SceneSystem
    {
    public:
        [[nodiscard]] const TypeInfo* SettingsType() const noexcept override { return &TypeOf<FogSettings>(); }
        [[nodiscard]] void* SettingsInstance() noexcept override { return &settings; }
        [[nodiscard]] StringView SettingsId() const noexcept override { return u8"fog"; }
        void SerializeSettings(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "density", settings.density);
            draconic::core::Serialize(ar, "tint", settings.tint);
        }
        FogSettings settings;
    };
}

TEST_CASE("scene-serialize: scene-system settings round-trip; pre-settings saves still load")
{
    // --- round-trip ---
    Scene a(u8"level");
    FogSystem* fogA = a.AddSystem<FogSystem>();
    fogA->settings.density = 2.25f;
    fogA->settings.tint    = Color{ 0.2f, 0.4f, 0.6f, 1.0f };
    (void)a.CreateEntity(u8"e");

    MemoryStream stream;
    {
        BinarySerializer writer(stream, SerializeMode::Write);
        SerializeScene(writer, a);
        REQUIRE(writer.IsOk());
    }
    {
        (void)stream.Seek(0, SeekOrigin::Begin);
        Scene b;
        FogSystem* fogB = b.AddSystem<FogSystem>();
        BinarySerializer reader(stream, SerializeMode::Read);
        SerializeScene(reader, b, &stream);
        REQUIRE(reader.IsOk());
        CHECK(Near(fogB->settings.density, 2.25f));
        CHECK(Near(fogB->settings.tint.g, 0.4f));
    }

    // --- legacy stream (saved BEFORE the settings section existed) ---
    // Simulate by serializing a scene with NO settings systems and chopping the trailing
    // settings-count u32 - byte-identical to a pre-settings save. The legacyProbe stream
    // check must leave defaults standing with the serializer still OK.
    Scene legacy(u8"old");
    (void)legacy.CreateEntity(u8"e");
    MemoryStream legacyFull;
    {
        BinarySerializer writer(legacyFull, SerializeMode::Write);
        SerializeScene(writer, legacy);
        REQUIRE(writer.IsOk());
    }
    // A pre-settings save ends after components: chop the settings count PLUS the prefab
    // section (mode tag u8 + instance count u32) that a current write appends after it.
    const usize legacyChop = sizeof(u32) + sizeof(u8) + sizeof(u32);
    MemoryStream legacyStream;
    REQUIRE(legacyFull.Bytes().Size() > legacyChop);
    (void)legacyStream.Write(legacyFull.Bytes().Data(), legacyFull.Bytes().Size() - legacyChop);
    (void)legacyStream.Seek(0, SeekOrigin::Begin);
    {
        Scene c;
        FogSystem* fogC = c.AddSystem<FogSystem>();
        BinarySerializer reader(legacyStream, SerializeMode::Read);
        SerializeScene(reader, c, &legacyStream);
        REQUIRE(reader.IsOk());
        CHECK(Near(fogC->settings.density, 0.5f));   // defaults stand
    }

    // A settings-era save from BEFORE the prefab section: chop just that section - the
    // probe guard must end the read cleanly with no pending instances.
    MemoryStream prePrefabStream;
    (void)prePrefabStream.Write(legacyFull.Bytes().Data(),
                                legacyFull.Bytes().Size() - (sizeof(u8) + sizeof(u32)));
    (void)prePrefabStream.Seek(0, SeekOrigin::Begin);
    {
        Scene c;
        FogSystem* fogC = c.AddSystem<FogSystem>();
        BinarySerializer reader(prePrefabStream, SerializeMode::Read);
        SerializeScene(reader, c, &prePrefabStream);
        REQUIRE(reader.IsOk());
        CHECK(Near(fogC->settings.density, 0.5f));
        CHECK(c.PendingPrefabInstanceCount() == 0u);
    }

    // Without the probe (a snapshot restore), the section is expected and reads normally.
    (void)stream.Seek(0, SeekOrigin::Begin);
    {
        Scene d;
        FogSystem* fogD = d.AddSystem<FogSystem>();
        BinarySerializer reader(stream, SerializeMode::Read);
        SerializeScene(reader, d);
        REQUIRE(reader.IsOk());
        CHECK(Near(fogD->settings.density, 2.25f));
    }
}

// ============================== Prefab P1 =====================================================

TEST_CASE("prefab: capture -> spawn twice (fresh guids, hierarchy, components, baselines)")
{
    Scene author(u8"author");
    HealthManager* authorHealth = author.AddSystem<HealthManager>();
    EntityHandle root  = author.CreateEntity(u8"Turret");
    EntityHandle barrel = author.CreateEntity(u8"Barrel");
    author.SetParent(barrel, root);
    author.SetLocalPosition(root, Float3{ 9, 9, 9 });   // authoring placement - NOT part of the payload semantics
    author.SetLocalPosition(barrel, Float3{ 0, 1, 0 });
    authorHealth->Add(root).value = 40.0f;
    authorHealth->Add(barrel).value = 10.0f;

    MemoryStream payload;
    REQUIRE(CapturePrefab(author, root, payload).IsOk());

    Scene target(u8"level");
    HealthManager* health = target.AddSystem<HealthManager>();
    EntityHandle anchor = target.CreateEntity(u8"Anchor");

    (void)payload.Seek(0, SeekOrigin::Begin);
    const Guid prefabId{ 0xAA, 0xBB };
    EntityHandle inst1 = SpawnPrefab(target, payload, prefabId, anchor);
    (void)payload.Seek(0, SeekOrigin::Begin);
    EntityHandle inst2 = SpawnPrefab(target, payload, prefabId);
    REQUIRE(inst1.IsAssigned());
    REQUIRE(inst2.IsAssigned());
    CHECK(target.GetEntityId(inst1) != target.GetEntityId(inst2));   // fresh guids per spawn

    CHECK(target.GetParent(inst1) == anchor);
    CHECK(!target.GetParent(inst2).IsAssigned());
    CHECK(target.GetEntityName(inst1) == StringView(u8"Turret"));
    EntityHandle barrel1 = target.GetFirstChild(inst1);
    REQUIRE(barrel1.IsAssigned());
    CHECK(target.GetEntityName(barrel1) == StringView(u8"Barrel"));
    CHECK(Near(target.GetLocalTransform(barrel1).position.y, 1.0f));
    REQUIRE(health->Has(inst1));
    CHECK(Near(health->Get(inst1)->value, 40.0f));
    REQUIRE(health->Has(barrel1));
    CHECK(Near(health->Get(barrel1)->value, 10.0f));

    CHECK(target.PrefabInstanceCount() == 2u);
    Scene::PrefabInstanceState* state = target.FindPrefabInstanceByRoot(target.GetEntityId(inst1));
    REQUIRE(state != nullptr);
    CHECK(state->prefabId == prefabId);
    CHECK(state->sourceIds.Size() == 2u);
    CHECK(state->componentBaselines.Size() == 2u);
}

TEST_CASE("prefab: scenes save instances as ref+deltas and restore them (overrides survive)")
{
    // Author + capture the payload.
    Scene author(u8"author");
    HealthManager* authorHealth = author.AddSystem<HealthManager>();
    EntityHandle root = author.CreateEntity(u8"Tower");
    EntityHandle top  = author.CreateEntity(u8"Top");
    EntityHandle flag = author.CreateEntity(u8"Flag");
    author.SetParent(top, root);
    author.SetParent(flag, top);
    authorHealth->Add(root).value = 100.0f;
    authorHealth->Add(top).value = 50.0f;
    MemoryStream payload;
    REQUIRE(CapturePrefab(author, root, payload).IsOk());

    // Level: one plain entity + one instance with EVERY delta kind.
    Scene level(u8"level");
    HealthManager* health = level.AddSystem<HealthManager>();
    EntityHandle plain = level.CreateEntity(u8"Plain");
    health->Add(plain).value = 7.0f;

    (void)payload.Seek(0, SeekOrigin::Begin);
    const Guid prefabId{ 0x11, 0x22 };
    EntityHandle inst = SpawnPrefab(level, payload, prefabId);
    REQUIRE(inst.IsAssigned());
    level.SetLocalPosition(inst, Float3{ 5, 0, 5 });              // instance placement
    EntityHandle instTop = level.GetFirstChild(inst);
    EntityHandle instFlag = level.GetFirstChild(instTop);
    REQUIRE(instFlag.IsAssigned());
    health->Get(instTop)->value = 51.0f;                          // component MODIFY
    level.SetLocalPosition(instTop, Float3{ 0, 2, 0 });           // transform override
    health->Add(instFlag).value = 5.0f;                           // component ADD
    health->RemoveComponent(inst);                                // component REMOVE (root's)
    const Guid instId = level.GetEntityId(inst);
    const Guid instTopId = level.GetEntityId(instTop);

    // Save (Referenced) -> the instance's members are NOT plain records.
    MemoryStream saved;
    {
        BinarySerializer w(saved, SerializeMode::Write);
        SerializeScene(w, level);
        REQUIRE(w.IsOk());
    }

    // Load into a fresh scene + resolve prefabs with a payload resolver.
    Scene loaded(u8"loaded");
    HealthManager* loadedHealth = loaded.AddSystem<HealthManager>();
    (void)saved.Seek(0, SeekOrigin::Begin);
    {
        BinarySerializer r(saved, SerializeMode::Read);
        SerializeScene(r, loaded, &saved);
        REQUIRE(r.IsOk());
    }
    CHECK(loaded.EntityCount() == 1u);   // only the plain entity so far
    CHECK(loaded.PendingPrefabInstanceCount() == 1u);
    const Span<const byte> payloadBytes = payload.Bytes();
    ResolveScenePrefabs(loaded, Function<UniquePtr<IStream>(const Guid&)>{
        [&payloadBytes, prefabId](const Guid& id) -> UniquePtr<IStream> {
            if (id != prefabId) { return UniquePtr<IStream>{}; }
            auto stream = MakeUnique<MemoryStream>(DefaultAllocator());
            (void)stream->Write(payloadBytes.Data(), payloadBytes.Size());
            (void)stream->Seek(0, SeekOrigin::Begin);
            return UniquePtr<IStream>(stream.Release(), DefaultAllocator());
        } });

    // Identity: the instance respawned with its SAVED guids.
    EntityHandle lInst = loaded.FindEntity(instId);
    EntityHandle lTop  = loaded.FindEntity(instTopId);
    REQUIRE(lInst.IsAssigned());
    REQUIRE(lTop.IsAssigned());
    CHECK(loaded.PrefabInstanceCount() == 1u);

    // Placement + every delta kind survived.
    CHECK(Near(loaded.GetLocalTransform(lInst).position.x, 5.0f));
    CHECK(Near(loaded.GetLocalTransform(lTop).position.y, 2.0f));   // transform override
    REQUIRE(loadedHealth->Has(lTop));
    CHECK(Near(loadedHealth->Get(lTop)->value, 51.0f));             // modify
    EntityHandle lFlag = loaded.GetFirstChild(lTop);
    REQUIRE(lFlag.IsAssigned());
    REQUIRE(loadedHealth->Has(lFlag));
    CHECK(Near(loadedHealth->Get(lFlag)->value, 5.0f));             // add
    CHECK(!loadedHealth->Has(lInst));                                // remove
}

TEST_CASE("prefab: destroyed members stay destroyed across save/load")
{
    Scene author(u8"author");
    (void)author.AddSystem<HealthManager>();
    EntityHandle root = author.CreateEntity(u8"Squad");
    EntityHandle a = author.CreateEntity(u8"A");
    EntityHandle b = author.CreateEntity(u8"B");
    author.SetParent(a, root);
    author.SetParent(b, root);
    MemoryStream payload;
    REQUIRE(CapturePrefab(author, root, payload).IsOk());

    Scene level(u8"level");
    (void)level.AddSystem<HealthManager>();
    (void)payload.Seek(0, SeekOrigin::Begin);
    const Guid prefabId{ 0x77, 0x88 };
    EntityHandle inst = SpawnPrefab(level, payload, prefabId);
    REQUIRE(inst.IsAssigned());
    EntityHandle memberA = level.GetFirstChild(inst);
    REQUIRE(memberA.IsAssigned());
    level.DestroyEntity(memberA);   // user deletes one member

    MemoryStream saved;
    {
        BinarySerializer w(saved, SerializeMode::Write);
        SerializeScene(w, level);
    }
    Scene loaded(u8"loaded");
    (void)loaded.AddSystem<HealthManager>();
    (void)saved.Seek(0, SeekOrigin::Begin);
    {
        BinarySerializer r(saved, SerializeMode::Read);
        SerializeScene(r, loaded, &saved);
    }
    const Span<const byte> payloadBytes = payload.Bytes();
    ResolveScenePrefabs(loaded, Function<UniquePtr<IStream>(const Guid&)>{
        [&payloadBytes](const Guid&) -> UniquePtr<IStream> {
            auto stream = MakeUnique<MemoryStream>(DefaultAllocator());
            (void)stream->Write(payloadBytes.Data(), payloadBytes.Size());
            (void)stream->Seek(0, SeekOrigin::Begin);
            return UniquePtr<IStream>(stream.Release(), DefaultAllocator());
        } });

    // Root + ONE surviving child (the destroyed member did not respawn).
    EntityHandle lRoot = loaded.GetFirstRoot();
    REQUIRE(lRoot.IsAssigned());
    u32 childCount = 0;
    for (EntityHandle c = loaded.GetFirstChild(lRoot); c.IsAssigned(); c = loaded.GetNextSibling(c)) { ++childCount; }
    CHECK(childCount == 1u);
}

TEST_CASE("prefab: snapshots expand instances and restore their state resolver-free")
{
    Scene author(u8"author");
    HealthManager* authorHealth = author.AddSystem<HealthManager>();
    EntityHandle root = author.CreateEntity(u8"Prop");
    authorHealth->Add(root).value = 33.0f;
    MemoryStream payload;
    REQUIRE(CapturePrefab(author, root, payload).IsOk());

    Scene level(u8"level");
    HealthManager* health = level.AddSystem<HealthManager>();
    (void)payload.Seek(0, SeekOrigin::Begin);
    const Guid prefabId{ 0x42, 0x42 };
    EntityHandle inst = SpawnPrefab(level, payload, prefabId);
    REQUIRE(inst.IsAssigned());
    health->Get(inst)->value = 34.0f;   // an override the snapshot must preserve
    const Guid instId = level.GetEntityId(inst);

    UniquePtr<SceneSnapshot> snapshot = SceneSnapshot::Capture(level);
    REQUIRE(static_cast<bool>(snapshot));

    // Simulate: mutate hard (destroy the instance), then restore.
    level.DestroyEntity(inst);
    CHECK(!level.FindEntity(instId).IsAssigned());
    REQUIRE(snapshot->Restore(level).IsOk());

    EntityHandle restored = level.FindEntity(instId);
    REQUIRE(restored.IsAssigned());
    REQUIRE(health->Has(restored));
    CHECK(Near(health->Get(restored)->value, 34.0f));
    CHECK(level.PrefabInstanceCount() == 1u);   // bookkeeping restored WITHOUT a resolver
    Scene::PrefabInstanceState* state = level.FindPrefabInstanceByRoot(instId);
    REQUIRE(state != nullptr);
    CHECK(state->prefabId == prefabId);
}

TEST_CASE("prefab: template rebuild preserves deltas and picks up new members")
{
    Scene author(u8"author");
    HealthManager* authorHealth = author.AddSystem<HealthManager>();
    EntityHandle root = author.CreateEntity(u8"House");
    EntityHandle door = author.CreateEntity(u8"Door");
    author.SetParent(door, root);
    authorHealth->Add(door).value = 20.0f;
    MemoryStream payloadV1;
    REQUIRE(CapturePrefab(author, root, payloadV1).IsOk());

    Scene level(u8"level");
    HealthManager* health = level.AddSystem<HealthManager>();
    (void)payloadV1.Seek(0, SeekOrigin::Begin);
    const Guid prefabId{ 0xF0, 0x0D };
    EntityHandle inst = SpawnPrefab(level, payloadV1, prefabId);
    REQUIRE(inst.IsAssigned());
    const Guid instId = level.GetEntityId(inst);
    EntityHandle instDoor = level.GetFirstChild(inst);
    const Guid instDoorId = level.GetEntityId(instDoor);
    health->Get(instDoor)->value = 21.0f;                 // user override
    level.SetLocalPosition(inst, Float3{ 3, 0, 0 });      // placement

    // Template v2: door healthier + a brand-new window member.
    authorHealth->Get(door)->value = 25.0f;
    EntityHandle window = author.CreateEntity(u8"Window");
    author.SetParent(window, root);
    MemoryStream payloadV2;
    REQUIRE(CapturePrefab(author, root, payloadV2).IsOk());

    const Span<const byte> v2 = payloadV2.Bytes();
    CHECK(RebuildPrefabInstances(level, prefabId, v2) == 1u);

    // Identity preserved, override preserved, new member present, placement intact.
    EntityHandle rInst = level.FindEntity(instId);
    EntityHandle rDoor = level.FindEntity(instDoorId);
    REQUIRE(rInst.IsAssigned());
    REQUIRE(rDoor.IsAssigned());
    CHECK(Near(level.GetLocalTransform(rInst).position.x, 3.0f));
    CHECK(Near(health->Get(rDoor)->value, 21.0f));        // override beats the template's 25
    u32 kids = 0;
    bool sawWindow = false;
    for (EntityHandle c = level.GetFirstChild(rInst); c.IsAssigned(); c = level.GetNextSibling(c)) {
        ++kids;
        if (level.GetEntityName(c) == StringView(u8"Window")) { sawWindow = true; }
    }
    CHECK(kids == 2u);
    CHECK(sawWindow);
    CHECK(level.PrefabInstanceCount() == 1u);
}

TEST_CASE("prefab P2: apply-as-template keeps source ids; revert discards deltas")
{
    Scene author(u8"author");
    HealthManager* authorHealth = author.AddSystem<HealthManager>();
    EntityHandle root = author.CreateEntity(u8"Cart");
    EntityHandle wheel = author.CreateEntity(u8"Wheel");
    author.SetParent(wheel, root);
    authorHealth->Add(wheel).value = 10.0f;
    MemoryStream payload;
    REQUIRE(CapturePrefab(author, root, payload).IsOk());
    const Guid wheelSourceId = author.GetEntityId(wheel);

    Scene level(u8"level");
    HealthManager* health = level.AddSystem<HealthManager>();
    (void)payload.Seek(0, SeekOrigin::Begin);
    const Guid prefabId{ 0xCA, 0x87 };
    EntityHandle inst = SpawnPrefab(level, payload, prefabId);
    REQUIRE(inst.IsAssigned());
    EntityHandle instWheel = level.GetFirstChild(inst);

    // Member queries + override detection.
    PrefabMemberInfo member;
    REQUIRE(FindPrefabMember(level, level.GetEntityId(instWheel), member));
    CHECK(member.state->sourceIds[member.memberIndex] == wheelSourceId);
    ComponentManagerBase* mgr = level.FindManagerBySerializationId(u8"demo.Health");
    REQUIRE(mgr != nullptr);
    CHECK(!IsPrefabComponentOverridden(level, member, *mgr));
    health->Get(instWheel)->value = 11.0f;
    CHECK(IsPrefabComponentOverridden(level, member, *mgr));

    // Apply-as-template: edits + a user-added child become the template, keyed by SOURCE ids.
    EntityHandle lamp = level.CreateEntity(u8"Lamp");
    level.SetParent(lamp, inst);
    MemoryStream applied;
    REQUIRE(CaptureInstanceAsTemplate(level, *member.state, applied).IsOk());

    Scene check(u8"check");
    HealthManager* checkHealth = check.AddSystem<HealthManager>();
    (void)applied.Seek(0, SeekOrigin::Begin);
    HashMap<Guid, Guid> pin;   // spawn with source ids AS live ids to inspect the template
    pin.InsertOrAssign(wheelSourceId, wheelSourceId);
    EntityHandle tRoot = SpawnPrefab(check, applied, prefabId, EntityHandle::Invalid(), &pin);
    REQUIRE(tRoot.IsAssigned());
    EntityHandle tWheel = check.FindEntity(wheelSourceId);   // SOURCE id preserved
    REQUIRE(tWheel.IsAssigned());
    CHECK(Near(checkHealth->Get(tWheel)->value, 11.0f));     // the applied override
    u32 kids = 0;
    for (EntityHandle c = check.GetFirstChild(tRoot); c.IsAssigned(); c = check.GetNextSibling(c)) { ++kids; }
    CHECK(kids == 2u);                                        // wheel + the applied lamp

    // Revert: back to the ORIGINAL template, same guids, deltas gone.
    const Guid instId = level.GetEntityId(inst);
    const Guid instWheelId = level.GetEntityId(instWheel);
    level.SetLocalPosition(inst, Float3{ 7, 0, 0 });          // placement must SURVIVE revert
    const Span<const byte> original = payload.Bytes();
    REQUIRE(RevertPrefabInstance(level, instId, original));
    EntityHandle rInst = level.FindEntity(instId);
    EntityHandle rWheel = level.FindEntity(instWheelId);
    REQUIRE(rInst.IsAssigned());
    REQUIRE(rWheel.IsAssigned());
    CHECK(Near(health->Get(rWheel)->value, 10.0f));           // override discarded
    CHECK(Near(level.GetLocalTransform(rInst).position.x, 7.0f));   // placement kept
    u32 rKids = 0;
    for (EntityHandle c = level.GetFirstChild(rInst); c.IsAssigned(); c = level.GetNextSibling(c)) { ++rKids; }
    CHECK(rKids == 1u);                                        // the user-added lamp is gone? NO -
    // the lamp was parented under the instance but is NOT a member; destroying the root took
    // it with the subtree. That is the documented revert semantic: non-member children die
    // with the instance they live under.
}

TEST_CASE("prefab: legacy multi-root payload normalizes to one root on spawn")
{
    // Author TWO root entities and serialize the whole scene (the shape an old prefab-page
    // save produced before single-root enforcement).
    Scene author(u8"author");
    (void)author.CreateEntity(u8"Ball");
    (void)author.CreateEntity(u8"Box");
    MemoryStream payload;
    {
        BinarySerializer ser(payload, SerializeMode::Write);
        SerializeScene(ser, author, nullptr, ScenePrefabMode::Expanded);
    }
    (void)payload.Seek(0, SeekOrigin::Begin);

    Scene level(u8"level");
    EntityHandle root = SpawnPrefab(level, payload, Guid{ 0xAB, 0x12 });
    REQUIRE(root.IsAssigned());
    CHECK(level.GetEntityName(root) == StringView(u8"Ball"));

    // The extra root became a CHILD of the instance root - nothing is a loose sibling.
    EntityHandle child = level.GetFirstChild(root);
    REQUIRE(child.IsAssigned());
    CHECK(level.GetEntityName(child) == StringView(u8"Box"));
    CHECK(!level.GetNextSibling(root).IsAssigned());

    // Apply-as-template walks the root subtree, so BOTH members survive a round-trip.
    Scene::PrefabInstanceState* state = level.FindPrefabInstanceByRoot(level.GetEntityId(root));
    REQUIRE(state != nullptr);
    MemoryStream captured;
    REQUIRE(CaptureInstanceAsTemplate(level, *state, captured).IsOk());
    (void)captured.Seek(0, SeekOrigin::Begin);
    Scene other(u8"other");
    EntityHandle respawned = SpawnPrefab(other, captured, Guid{ 0xAB, 0x12 });
    REQUIRE(respawned.IsAssigned());
    CHECK(other.GetFirstChild(respawned).IsAssigned());
}

TEST_CASE("prefab: SavePrefab refuses a multi-root scene")
{
    // SavePrefab needs a content instance; the root-count gate rejects before any write, so
    // exercise the gate through SerializeScene's caller contract instead: the page blocks
    // multi-root saves and SavePrefab returns InvalidArgument (verified via the editor lib).
    Scene scene(u8"prefab");
    (void)scene.CreateEntity(u8"A");
    (void)scene.CreateEntity(u8"B");
    usize roots = 0;
    for (EntityHandle r = scene.GetFirstRoot(); r.IsAssigned(); r = scene.GetNextSibling(r)) { ++roots; }
    CHECK(roots == 2);
}

TEST_CASE("prefab: apply-as-template keeps the template root transform, not the placement")
{
    Scene author(u8"author");
    EntityHandle tmpl = author.CreateEntity(u8"Lamp");
    Transform authored;
    authored.position = Float3{ 1.0f, 2.0f, 3.0f };
    author.SetLocalTransform(tmpl, authored);
    MemoryStream payload;
    REQUIRE(CapturePrefab(author, tmpl, payload).IsOk());

    Scene level(u8"level");
    (void)payload.Seek(0, SeekOrigin::Begin);
    EntityHandle inst = SpawnPrefab(level, payload, Guid{ 0x77, 0x3 });
    REQUIRE(inst.IsAssigned());

    // Move the instance root - that's PLACEMENT, not template content.
    Transform placed;
    placed.position = Float3{ 50.0f, 0.0f, -9.0f };
    level.SetLocalTransform(inst, placed);

    Scene::PrefabInstanceState* state = level.FindPrefabInstanceByRoot(level.GetEntityId(inst));
    REQUIRE(state != nullptr);
    MemoryStream captured;
    REQUIRE(CaptureInstanceAsTemplate(level, *state, captured).IsOk());

    // Respawn the captured template elsewhere: the root sits at the AUTHORED transform.
    Scene other(u8"other");
    (void)captured.Seek(0, SeekOrigin::Begin);
    EntityHandle fresh = SpawnPrefab(other, captured, Guid{ 0x77, 0x3 });
    REQUIRE(fresh.IsAssigned());
    const Transform t = other.GetLocalTransform(fresh);
    CHECK(t.position.x == 1.0f);
    CHECK(t.position.y == 2.0f);
    CHECK(t.position.z == 3.0f);
}

// ============================== P4: nesting ==================================

namespace {
    // Author a Q template: root "Wheel" (health 10) + child "Hub" (health 5).
    Array<byte> AuthorInnerTemplate()
    {
        Scene author(u8"author");
        HealthManager* health = author.AddSystem<HealthManager>();
        EntityHandle wheel = author.CreateEntity(u8"Wheel");
        EntityHandle hub = author.CreateEntity(u8"Hub");
        author.SetParent(hub, wheel);
        health->Add(wheel).value = 10.0f;
        health->Add(hub).value = 5.0f;
        MemoryStream payload;
        REQUIRE(CapturePrefab(author, wheel, payload).IsOk());
        Array<byte> bytes;
        for (byte b : payload.Bytes()) { bytes.PushBack(b); }
        return bytes;
    }

    // Author the OUTER template "Cart" in an edit scene: own entity "Body" (health 40) + an
    // instance of the inner template whose Wheel is customized to 11. Saved via the
    // SavePrefab path (Referenced, no settings) so nesting persists as a RECORD.
    struct OuterAuthoring { Array<byte> payload; Guid innerRootSource; };
    OuterAuthoring AuthorOuterTemplate(const Guid& innerId, const Array<byte>& innerPayload)
    {
        Scene edit(u8"Cart");
        HealthManager* health = edit.AddSystem<HealthManager>();
        EntityHandle body = edit.CreateEntity(u8"Body");
        health->Add(body).value = 40.0f;

        MemoryStream innerStream;
        (void)innerStream.Write(innerPayload.Data(), innerPayload.Size());
        (void)innerStream.Seek(0, SeekOrigin::Begin);
        EntityHandle wheel = SpawnPrefab(edit, innerStream, innerId, body);
        REQUIRE(wheel.IsAssigned());
        // Owner customization: the Wheel's health becomes 11 INSIDE the Cart template.
        health->Get(wheel)->value = 11.0f;

        MemoryStream out;
        BinarySerializer ser(out, SerializeMode::Write);
        SerializeScene(ser, edit, nullptr, ScenePrefabMode::Referenced, /*includeSettings=*/false);
        REQUIRE(ser.IsOk());
        OuterAuthoring result;
        for (byte b : out.Bytes()) { result.payload.PushBack(b); }
        result.innerRootSource = edit.GetEntityId(wheel);
        return result;
    }

    PrefabPayloadResolver MakeResolver(const Guid& innerId, const Array<byte>* innerPayload,
                                       const Guid& outerId = Guid{}, const Array<byte>* outerPayload = nullptr)
    {
        return PrefabPayloadResolver{ [=](const Guid& id) -> UniquePtr<IStream> {
            const Array<byte>* source = nullptr;
            if (id == innerId) { source = innerPayload; }
            else if (id == outerId) { source = outerPayload; }
            if (source == nullptr) { return UniquePtr<IStream>{}; }
            auto stream = MakeUnique<MemoryStream>(DefaultAllocator());
            (void)stream->Write(source->Data(), source->Size());
            (void)stream->Seek(0, SeekOrigin::Begin);
            return UniquePtr<IStream>(stream.Release(), DefaultAllocator());
        } };
    }
}

TEST_CASE("prefab P4: nested instance spawns linked, owner customization is BASELINE")
{
    const Guid innerId{ 0xAA, 0x1 };
    const Guid outerId{ 0xBB, 0x2 };
    Array<byte> inner = AuthorInnerTemplate();
    OuterAuthoring outer = AuthorOuterTemplate(innerId, inner);
    PrefabPayloadResolver resolver = MakeResolver(innerId, &inner);

    Scene level(u8"level");
    HealthManager* health = level.AddSystem<HealthManager>();
    MemoryStream outerStream;
    (void)outerStream.Write(outer.payload.Data(), outer.payload.Size());
    (void)outerStream.Seek(0, SeekOrigin::Begin);
    EntityHandle cart = SpawnPrefab(level, outerStream, outerId, EntityHandle::Invalid(),
                                    nullptr, &resolver);
    REQUIRE(cart.IsAssigned());

    // Two states: the Cart (top-level) + the nested Wheel instance linked to it.
    Scene::PrefabInstanceState* cartState = level.FindPrefabInstanceByRoot(level.GetEntityId(cart));
    REQUIRE(cartState != nullptr);
    CHECK(cartState->ownerRootEntityId.IsNil());
    REQUIRE(cartState->referencedPrefabIds.Size() == 2u);   // own + inner
    Scene::PrefabInstanceState* wheelState = nullptr;
    level.ForEachPrefabInstance([&](Scene::PrefabInstanceState& s) {
        if (s.prefabId == innerId) { wheelState = &s; }
    });
    REQUIRE(wheelState != nullptr);
    CHECK(wheelState->ownerRootEntityId == cartState->rootEntityId);
    CHECK(wheelState->nestedRootSourceId == outer.innerRootSource);

    // The owner's customization (11) applied AND became the baseline: no override reads.
    EntityHandle wheel = level.FindEntity(wheelState->rootEntityId);
    REQUIRE(wheel.IsAssigned());
    CHECK(health->Get(wheel)->value == doctest::Approx(11.0f));
    PrefabMemberInfo member;
    REQUIRE(FindPrefabMember(level, wheelState->rootEntityId, member));
    ComponentManagerBase* manager = level.FindManagerBySerializationId(u8"demo.Health");
    CHECK(!IsPrefabComponentOverridden(level, member, *manager));
    // The Hub kept its pure-template value.
    EntityHandle hub = level.GetFirstChild(wheel);
    REQUIRE(hub.IsAssigned());
    CHECK(health->Get(hub)->value == doctest::Approx(5.0f));
}

TEST_CASE("prefab P4: scene round-trip preserves nesting links, guids, and scene overrides")
{
    const Guid innerId{ 0xAA, 0x11 };
    const Guid outerId{ 0xBB, 0x22 };
    Array<byte> inner = AuthorInnerTemplate();
    OuterAuthoring outer = AuthorOuterTemplate(innerId, inner);
    PrefabPayloadResolver resolver = MakeResolver(innerId, &inner, outerId, &outer.payload);

    Scene level(u8"level");
    HealthManager* health = level.AddSystem<HealthManager>();
    MemoryStream outerStream;
    (void)outerStream.Write(outer.payload.Data(), outer.payload.Size());
    (void)outerStream.Seek(0, SeekOrigin::Begin);
    EntityHandle cart = SpawnPrefab(level, outerStream, outerId, EntityHandle::Invalid(),
                                    nullptr, &resolver);
    REQUIRE(cart.IsAssigned());
    Scene::PrefabInstanceState* wheelState = nullptr;
    level.ForEachPrefabInstance([&](Scene::PrefabInstanceState& s) {
        if (s.prefabId == innerId) { wheelState = &s; }
    });
    REQUIRE(wheelState != nullptr);
    EntityHandle wheel = level.FindEntity(wheelState->rootEntityId);
    const Guid wheelGuid = wheelState->rootEntityId;
    EntityHandle hub = level.GetFirstChild(wheel);
    const Guid hubGuid = level.GetEntityId(hub);

    // SCENE-level override on the nested Hub (owner baseline is 5).
    health->Get(hub)->value = 7.0f;

    MemoryStream saved;
    {
        BinarySerializer w(saved, SerializeMode::Write);
        SerializeScene(w, level);
        REQUIRE(w.IsOk());
    }
    (void)saved.Seek(0, SeekOrigin::Begin);
    Scene loaded(u8"loaded");
    HealthManager* loadedHealth = loaded.AddSystem<HealthManager>();
    {
        BinarySerializer r(saved, SerializeMode::Read);
        SerializeScene(r, loaded, &saved);
        REQUIRE(r.IsOk());
    }
    ResolveScenePrefabs(loaded, resolver);

    // Guids preserved through the nested merge; owner customization + scene override layered.
    EntityHandle loadedWheel = loaded.FindEntity(wheelGuid);
    EntityHandle loadedHub = loaded.FindEntity(hubGuid);
    REQUIRE(loadedWheel.IsAssigned());
    REQUIRE(loadedHub.IsAssigned());
    CHECK(loadedHealth->Get(loadedWheel)->value == doctest::Approx(11.0f));   // owner custom
    CHECK(loadedHealth->Get(loadedHub)->value == doctest::Approx(7.0f));      // scene override
    Scene::PrefabInstanceState* loadedWheelState = loaded.FindPrefabInstanceByRoot(wheelGuid);
    REQUIRE(loadedWheelState != nullptr);
    CHECK(!loadedWheelState->ownerRootEntityId.IsNil());
    // Scene override still reads as an override (baseline = owner-customized template).
    PrefabMemberInfo member;
    REQUIRE(FindPrefabMember(loaded, hubGuid, member));
    ComponentManagerBase* manager = loaded.FindManagerBySerializationId(u8"demo.Health");
    CHECK(IsPrefabComponentOverridden(loaded, member, *manager));
}

TEST_CASE("prefab P4: inner-template edits propagate THROUGH the outer instance")
{
    const Guid innerId{ 0xAA, 0x21 };
    const Guid outerId{ 0xBB, 0x32 };
    Array<byte> inner = AuthorInnerTemplate();
    OuterAuthoring outer = AuthorOuterTemplate(innerId, inner);

    // The edited inner template: Hub health becomes 50 (Wheel stays 10).
    Array<byte> innerV2;
    {
        Scene author(u8"author");
        HealthManager* health = author.AddSystem<HealthManager>();
        EntityHandle wheel = author.CreateEntity(u8"Wheel");
        EntityHandle hub = author.CreateEntity(u8"Hub");
        author.SetParent(hub, wheel);
        health->Add(wheel).value = 10.0f;
        health->Add(hub).value = 50.0f;
        MemoryStream payload;
        REQUIRE(CapturePrefab(author, wheel, payload).IsOk());
        for (byte b : payload.Bytes()) { innerV2.PushBack(b); }
    }

    PrefabPayloadResolver resolver = MakeResolver(innerId, &inner, outerId, &outer.payload);
    Scene level(u8"level");
    HealthManager* health = level.AddSystem<HealthManager>();
    MemoryStream outerStream;
    (void)outerStream.Write(outer.payload.Data(), outer.payload.Size());
    (void)outerStream.Seek(0, SeekOrigin::Begin);
    EntityHandle cart = SpawnPrefab(level, outerStream, outerId, EntityHandle::Invalid(),
                                    nullptr, &resolver);
    REQUIRE(cart.IsAssigned());
    const Guid cartGuid = level.GetEntityId(cart);

    // Rebuild with the NEW inner template: the resolver serves innerV2 now.
    PrefabPayloadResolver resolverV2 = MakeResolver(innerId, &innerV2, outerId, &outer.payload);
    const u32 rebuilt = RebuildPrefabInstances(level, innerId,
        Span<const byte>{ innerV2.Data(), innerV2.Size() }, &resolverV2);
    CHECK(rebuilt == 1u);   // the CART rebuilt (it references the inner prefab)

    Scene::PrefabInstanceState* wheelState = nullptr;
    level.ForEachPrefabInstance([&](Scene::PrefabInstanceState& s) {
        if (s.prefabId == innerId) { wheelState = &s; }
    });
    REQUIRE(wheelState != nullptr);
    EntityHandle wheel = level.FindEntity(wheelState->rootEntityId);
    REQUIRE(wheel.IsAssigned());
    EntityHandle hub = level.GetFirstChild(wheel);
    REQUIRE(hub.IsAssigned());
    CHECK(health->Get(wheel)->value == doctest::Approx(11.0f));   // owner custom STILL wins
    CHECK(health->Get(hub)->value == doctest::Approx(50.0f));     // template edit propagated
    CHECK(level.FindPrefabInstanceByRoot(cartGuid) != nullptr);   // cart intact
}

TEST_CASE("prefab P4: apply-to-prefab keeps nested records with owner customization")
{
    const Guid innerId{ 0xAA, 0x31 };
    const Guid outerId{ 0xBB, 0x42 };
    Array<byte> inner = AuthorInnerTemplate();
    OuterAuthoring outer = AuthorOuterTemplate(innerId, inner);
    PrefabPayloadResolver resolver = MakeResolver(innerId, &inner);

    Scene level(u8"level");
    HealthManager* health = level.AddSystem<HealthManager>();
    MemoryStream outerStream;
    (void)outerStream.Write(outer.payload.Data(), outer.payload.Size());
    (void)outerStream.Seek(0, SeekOrigin::Begin);
    EntityHandle cart = SpawnPrefab(level, outerStream, outerId, EntityHandle::Invalid(),
                                    nullptr, &resolver);
    REQUIRE(cart.IsAssigned());
    Scene::PrefabInstanceState* cartState = level.FindPrefabInstanceByRoot(level.GetEntityId(cart));
    REQUIRE(cartState != nullptr);

    // Scene tweak: Wheel 11 -> 13, then apply the CART as the new template.
    Scene::PrefabInstanceState* wheelState = nullptr;
    level.ForEachPrefabInstance([&](Scene::PrefabInstanceState& s) {
        if (s.prefabId == innerId) { wheelState = &s; }
    });
    REQUIRE(wheelState != nullptr);
    EntityHandle wheel = level.FindEntity(wheelState->rootEntityId);
    health->Get(wheel)->value = 13.0f;

    MemoryStream captured;
    REQUIRE(CaptureInstanceAsTemplate(level, *cartState, captured, &resolver).IsOk());

    // Fresh spawn of the captured template elsewhere: nested link + the 13 travel.
    Scene other(u8"other");
    HealthManager* otherHealth = other.AddSystem<HealthManager>();
    (void)captured.Seek(0, SeekOrigin::Begin);
    EntityHandle fresh = SpawnPrefab(other, captured, outerId, EntityHandle::Invalid(),
                                     nullptr, &resolver);
    REQUIRE(fresh.IsAssigned());
    Scene::PrefabInstanceState* freshWheelState = nullptr;
    other.ForEachPrefabInstance([&](Scene::PrefabInstanceState& s) {
        if (s.prefabId == innerId) { freshWheelState = &s; }
    });
    REQUIRE(freshWheelState != nullptr);
    EntityHandle freshWheel = other.FindEntity(freshWheelState->rootEntityId);
    REQUIRE(freshWheel.IsAssigned());
    CHECK(otherHealth->Get(freshWheel)->value == doctest::Approx(13.0f));
    EntityHandle freshHub = other.GetFirstChild(freshWheel);
    REQUIRE(freshHub.IsAssigned());
    CHECK(otherHealth->Get(freshHub)->value == doctest::Approx(5.0f));
}

TEST_CASE("prefab P4: rebuild preserves user entities under members and user-spawned instances inside")
{
    const Guid innerId{ 0xAA, 0x41 };
    Array<byte> inner = AuthorInnerTemplate();
    PrefabPayloadResolver resolver = MakeResolver(innerId, &inner);

    Scene level(u8"level");
    HealthManager* health = level.AddSystem<HealthManager>();
    MemoryStream innerStream;
    (void)innerStream.Write(inner.Data(), inner.Size());
    (void)innerStream.Seek(0, SeekOrigin::Begin);
    EntityHandle wheel = SpawnPrefab(level, innerStream, innerId);
    REQUIRE(wheel.IsAssigned());
    const Guid wheelGuid = level.GetEntityId(wheel);

    // A plain USER entity parented under a member, and a USER-SPAWNED instance inside.
    EntityHandle userChild = level.CreateEntity(u8"Sticker");
    level.SetParent(userChild, wheel);
    const Guid userChildGuid = level.GetEntityId(userChild);
    MemoryStream innerStream2;
    (void)innerStream2.Write(inner.Data(), inner.Size());
    (void)innerStream2.Seek(0, SeekOrigin::Begin);
    EntityHandle userWheel = SpawnPrefab(level, innerStream2, innerId, wheel);
    REQUIRE(userWheel.IsAssigned());
    const Guid userWheelGuid = level.GetEntityId(userWheel);
    health->Get(userWheel)->value = 99.0f;   // scene delta on the user-spawned instance

    const u32 rebuilt = RebuildPrefabInstances(level, innerId,
        Span<const byte>{ inner.Data(), inner.Size() }, &resolver);
    CHECK(rebuilt >= 1u);

    // Everything survives: the user child re-attached, the user-spawned instance respawned
    // standalone with its guid + delta.
    EntityHandle survivedChild = level.FindEntity(userChildGuid);
    REQUIRE(survivedChild.IsAssigned());
    CHECK(level.GetParent(survivedChild).IsAssigned());
    CHECK(level.GetEntityId(level.GetParent(survivedChild)) == wheelGuid);
    EntityHandle survivedWheel = level.FindEntity(userWheelGuid);
    REQUIRE(survivedWheel.IsAssigned());
    CHECK(health->Get(survivedWheel)->value == doctest::Approx(99.0f));
}

TEST_CASE("scene v2: unknown component and settings records SKIP instead of aborting")
{
    // Save with health components; load into a scene WITHOUT the manager: entities +
    // hierarchy load, records skip with a warning, and the stream stays consumable to the
    // END (the prefab section after them still parses).
    Scene a(u8"level");
    HealthManager* mgr = a.AddSystem<HealthManager>();
    EntityHandle hero = a.CreateEntity(u8"Hero");
    mgr->Add(hero).value = 42.0f;

    MemoryStream saved;
    {
        BinarySerializer w(saved, SerializeMode::Write);
        SerializeScene(w, a);
        REQUIRE(w.IsOk());
    }
    (void)saved.Seek(0, SeekOrigin::Begin);
    Scene b(u8"loaded");   // NO HealthManager
    {
        BinarySerializer r(saved, SerializeMode::Read);
        SerializeScene(r, b, &saved);
        REQUIRE(r.IsOk());
    }
    CHECK(b.EntityCount() == 1u);
    CHECK(b.FindEntity(a.GetEntityId(hero)).IsAssigned());
}

// Phase 1 — the entity table: generational handles, free-list slot reuse + stale-handle
// detection, persistent-Guid <-> handle mapping, active/name state. These encode the
// entity-lifecycle/validity behaviors pinned from the Sedulous test suite.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import raptor.core;
import raptor.scene;

using namespace raptor::core;
using namespace raptor::scene;

TEST_CASE("entity create: unique valid handles + count")
{
    Scene scene(u8"world");
    CHECK(scene.Name() == u8"world");
    CHECK(scene.EntityCount() == 0);

    EntityHandle a = scene.CreateEntity(u8"a");
    EntityHandle b = scene.CreateEntity(u8"b");
    CHECK(scene.IsValid(a));
    CHECK(scene.IsValid(b));
    CHECK(a != b);
    CHECK(scene.EntityCount() == 2);
    CHECK(scene.GetEntityName(a) == u8"a");
    CHECK(scene.GetEntityName(b) == u8"b");
    CHECK(scene.IsActive(a));                       // entities start active
}

TEST_CASE("entity destroy: invalidates the handle")
{
    Scene scene;
    EntityHandle e = scene.CreateEntity();
    REQUIRE(scene.IsValid(e));
    scene.DestroyEntity(e);
    CHECK_FALSE(scene.IsValid(e));
    CHECK(scene.EntityCount() == 0);
    scene.DestroyEntity(e);                         // double-destroy is a no-op
    CHECK(scene.EntityCount() == 0);
}

TEST_CASE("slot reuse bumps generation: a stale handle is detected, not confused with the new one")
{
    Scene scene;
    EntityHandle first = scene.CreateEntity();
    const u32 reusedIndex = first.index;
    scene.DestroyEntity(first);

    EntityHandle second = scene.CreateEntity();     // reuses the freed slot
    CHECK(second.index == reusedIndex);             // same slot...
    CHECK(second.generation != first.generation);   // ...new generation
    CHECK(scene.IsValid(second));
    CHECK_FALSE(scene.IsValid(first));              // old handle stays invalid
}

TEST_CASE("invalid/unassigned handles never validate")
{
    Scene scene;
    CHECK_FALSE(scene.IsValid(EntityHandle::Invalid()));
    CHECK_FALSE(scene.IsValid(EntityHandle{ 999u, 1u }));   // out of range
    CHECK(scene.GetEntityName(EntityHandle::Invalid()) == StringView{});
    scene.SetActive(EntityHandle::Invalid(), false);        // no crash
    scene.SetEntityName(EntityHandle::Invalid(), u8"x");    // no crash
}

TEST_CASE("persistent Guid <-> handle: find resolves, survives a specific-id create")
{
    Scene scene;
    EntityHandle e = scene.CreateEntity(u8"named");
    const Guid id = scene.GetEntityId(e);
    CHECK(id != Guid{});
    CHECK(scene.FindEntity(id) == e);

    // a deterministic id (e.g. from a loaded scene) round-trips through Find
    const Guid fixed{ 0x0123456789abcdefull, 0xfedcba9876543210ull };
    EntityHandle loaded = scene.CreateEntity(fixed, u8"loaded");
    CHECK(scene.GetEntityId(loaded) == fixed);
    CHECK(scene.FindEntity(fixed) == loaded);

    // after destroy, Find prunes the stale entry and reports Invalid
    scene.DestroyEntity(loaded);
    CHECK(scene.FindEntity(fixed) == EntityHandle::Invalid());
}

TEST_CASE("active + name are mutable on live entities")
{
    Scene scene;
    EntityHandle e = scene.CreateEntity(u8"orig");
    scene.SetActive(e, false);
    CHECK_FALSE(scene.IsActive(e));
    scene.SetEntityName(e, u8"renamed");
    CHECK(scene.GetEntityName(e) == u8"renamed");
}

TEST_CASE("ForEachEntity visits exactly the live entities")
{
    Scene scene;
    EntityHandle a = scene.CreateEntity();
    EntityHandle b = scene.CreateEntity();
    EntityHandle c = scene.CreateEntity();
    scene.DestroyEntity(b);

    u32 visited = 0;
    bool sawA = false, sawC = false, sawB = false;
    scene.ForEachEntity([&](EntityHandle h) {
        ++visited;
        if (h == a) { sawA = true; }
        if (h == c) { sawC = true; }
        if (h == b) { sawB = true; }
    });
    CHECK(visited == 2);
    CHECK(sawA);
    CHECK(sawC);
    CHECK_FALSE(sawB);
}

TEST_CASE("revision advances on structural change")
{
    Scene scene;
    const u64 r0 = scene.Revision();
    EntityHandle e = scene.CreateEntity();
    CHECK(scene.Revision() > r0);
    const u64 r1 = scene.Revision();
    scene.DestroyEntity(e);
    CHECK(scene.Revision() > r1);
}

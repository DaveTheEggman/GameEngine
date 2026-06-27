// Phase 3a — the value-pool component manager (sparse set): add/get/has/remove by
// entity, dense contiguous iteration, swap-remove keeping the pack dense, generation
// staleness, the one-per-entity invariant, and manager-driven deferred lifecycle.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import raptor.core;
import raptor.scene;

using namespace raptor::core;
using namespace raptor::scene;

namespace
{
    // A plain value component.
    struct Health {
        f32 value = 100.0f;
    };

    // A concrete manager that records lifecycle calls (manager-driven hooks).
    class HealthManager : public ComponentManager<Health> {
    public:
        int created = 0, initialized = 0, destroyed = 0;
    protected:
        void OnComponentCreated(Health&, EntityHandle) override { ++created; }
        void OnComponentInitialized(Health&, EntityHandle) override { ++initialized; }
        void OnComponentDestroyed(Health&, EntityHandle) override { ++destroyed; }
    };

    constexpr EntityHandle E(u32 i, u32 g = 1) { return EntityHandle{ i, g }; }
}

TEST_CASE("add/get/has/remove a component by entity")
{
    HealthManager mgr;
    CHECK_FALSE(mgr.Has(E(0)));
    Health& h = mgr.Add(E(0));
    h.value = 42.0f;
    CHECK(mgr.Has(E(0)));
    CHECK(mgr.Count() == 1);
    CHECK(mgr.Get(E(0))->value == doctest::Approx(42.0f));
    CHECK(mgr.Get(E(1)) == nullptr);                 // different entity, no component

    mgr.RemoveComponent(E(0));
    CHECK_FALSE(mgr.Has(E(0)));
    CHECK(mgr.Get(E(0)) == nullptr);
    CHECK(mgr.Count() == 0);
}

TEST_CASE("dense storage stays packed across a middle remove (swap-with-last)")
{
    HealthManager mgr;
    mgr.Add(E(0)).value = 10.0f;
    mgr.Add(E(1)).value = 20.0f;
    mgr.Add(E(2)).value = 30.0f;
    CHECK(mgr.Dense().Size() == 3);

    mgr.RemoveComponent(E(1));                        // remove the middle one
    CHECK(mgr.Count() == 2);
    CHECK(mgr.Dense().Size() == 2);                  // still contiguous, no hole
    // remaining components are still reachable by their entity (regardless of order)
    CHECK(mgr.Get(E(0))->value == doctest::Approx(10.0f));
    CHECK(mgr.Get(E(2))->value == doctest::Approx(30.0f));
    CHECK(mgr.Get(E(1)) == nullptr);

    // every dense slot has a live owner with a resolvable component
    u32 seen = 0;
    mgr.ForEach([&](Health& c, EntityHandle owner) {
        ++seen;
        CHECK(mgr.Get(owner) == &c);                 // owner resolves back to this slot
    });
    CHECK(seen == 2);
}

TEST_CASE("generational staleness: a reused entity slot does not resolve an old handle")
{
    HealthManager mgr;
    mgr.Add(E(5, /*gen*/1)).value = 1.0f;
    mgr.RemoveComponent(E(5, 1));
    // slot index 5 reused by a new entity (generation 2) with its own component
    mgr.Add(E(5, 2)).value = 2.0f;

    CHECK(mgr.Get(E(5, 2))->value == doctest::Approx(2.0f));   // current occupant
    CHECK(mgr.Get(E(5, 1)) == nullptr);                        // stale handle: absent
    CHECK_FALSE(mgr.Has(E(5, 1)));
}

TEST_CASE("manager-driven lifecycle: create immediately, init deferred, destroy on remove")
{
    HealthManager mgr;
    mgr.Add(E(0));
    mgr.Add(E(1));
    CHECK(mgr.created == 2);
    CHECK(mgr.initialized == 0);                     // not yet — deferred

    mgr.InitializePendingComponents();
    CHECK(mgr.initialized == 2);

    // a component added then removed before init is never initialized
    mgr.Add(E(2));
    mgr.RemoveComponent(E(2));
    mgr.InitializePendingComponents();
    CHECK(mgr.initialized == 2);                     // still 2
    CHECK(mgr.destroyed == 1);

    // OnEntityDestroyed (the base hook the Scene calls) removes the component
    mgr.OnEntityDestroyed(E(0));
    CHECK_FALSE(mgr.Has(E(0)));
    CHECK(mgr.destroyed == 2);
}

TEST_CASE("component type id is stable + distinct per component type")
{
    HealthManager mgr;
    struct Mana { f32 v = 0; };
    ComponentManager<Mana> manaMgr;
    CHECK(mgr.ComponentType() == &TypeOf<Health>());
    CHECK(mgr.ComponentType() != manaMgr.ComponentType());
}

// Unit tests for the draconic.ui Core mechanics: Event multicast + Property<T>.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;

using namespace draconic::ui;
namespace core = draconic::core;

namespace
{
    struct MockOwner : IPropertyOwner
    {
        int count = 0;
        InvalidationKind lastKind = InvalidationKind::Visual;
        void OnPropertyChanged(InvalidationKind kind) override { ++count; lastKind = kind; }
    };
}

TEST_CASE("ui.event: multicast add + invoke in order")
{
    Event<void(int)> e;
    int sum = 0, calls = 0;
    e.Add(core::Function<void(int)>{ [&](int v) { sum += v; ++calls; } });
    e.Add(core::Function<void(int)>{ [&](int v) { sum += v * 10; ++calls; } });
    CHECK(e.Count() == 2u);
    e(3);
    CHECK(calls == 2);
    CHECK(sum == 33);
    e.Clear();
    CHECK(e.Count() == 0u);
}

TEST_CASE("ui.property: value + Changed fires only on real change")
{
    Property<int> p{ 5 };
    CHECK(p.Value() == 5);

    int fired = 0, last = 0;
    p.Changed.Add(core::Function<void(int)>{ [&](int v) { ++fired; last = v; } });
    p.SetValue(5);                 // same value -> no fire
    CHECK(fired == 0);
    p.SetValue(9);
    CHECK(fired == 1);
    CHECK(last == 9);
    CHECK(p.Value() == 9);
}

TEST_CASE("ui.property: SetSilent + owner invalidation")
{
    MockOwner owner;
    Property<core::f32> p{ 1.0f };
    p.SetOwner(&owner, InvalidationKind::Visual);

    p.SetSilent(2.0f);             // no fire, no invalidate
    CHECK(owner.count == 0);
    CHECK(p.Value() == 2.0f);

    p.SetValue(3.0f);
    CHECK(owner.count == 1);
    CHECK(owner.lastKind == InvalidationKind::Visual);
}

TEST_CASE("ui.property: one-way + two-way binding with loop guard")
{
    Property<int> a{ 1 };
    Property<int> b{ 0 };
    a.BindTo(b);
    a.SetValue(7);
    CHECK(b.Value() == 7);

    Property<int> x{ 0 };
    Property<int> y{ 0 };
    x.BindTwoWay(y);
    x.SetValue(4);
    CHECK(y.Value() == 4);
    y.SetValue(9);
    CHECK(x.Value() == 9);         // loop guard stops infinite recursion
}
